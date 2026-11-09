#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

namespace calibration_layout {
struct Rect { int left,top,right,bottom; };
enum class Edge { Left,Right,Top,Bottom };
struct Placement { size_t monitor;Edge edge; };
struct Link { Edge a,b;double distance,overlap,centers; };
inline Link Between(const Rect& a,const Rect& b) {
    double dx=std::max({0,a.left-b.right,b.left-a.right});
    double dy=std::max({0,a.top-b.bottom,b.top-a.bottom});
    double x=(b.left/2.0+b.right/2.0)-(a.left/2.0+a.right/2.0);
    double y=(b.top/2.0+b.bottom/2.0)-(a.top/2.0+a.bottom/2.0);
    int vertical=std::max(0,std::min(a.bottom,b.bottom)-std::max(a.top,b.top));
    int horizontal=std::max(0,std::min(a.right,b.right)-std::max(a.left,b.left));
    bool side=vertical>0&&dx>0 ? true : horizontal>0&&dy>0 ? false
        :vertical>0&&horizontal==0 ? true :horizontal>0&&vertical==0 ? false :std::abs(x)>=std::abs(y);
    return {side?(x>=0?Edge::Right:Edge::Left):(y>=0?Edge::Bottom:Edge::Top),
        side?(x>=0?Edge::Left:Edge::Right):(y>=0?Edge::Top:Edge::Bottom),
        dx*dx+dy*dy,static_cast<double>(side?vertical:horizontal),x*x+y*y};
}
// Connect neighboring rectangles, rather than placing every sample at the
// same desktop-relative edge or pairing distant screens across an intervening one.
inline std::vector<Placement> Plan(const std::vector<Rect>& screens,size_t reference) {
    std::vector<Placement> result;if(screens.size()<2)return result;
    std::vector<bool> connected(screens.size(),false);connected[std::min(reference,screens.size()-1)]=true;
    auto add=[&](size_t monitor,Edge edge){if(std::none_of(result.begin(),result.end(),[&](const Placement& p){return p.monitor==monitor&&p.edge==edge;}))result.push_back({monitor,edge});};
    for(size_t count=1;count<screens.size();++count) {
        size_t from=0,to=0;Link best{Edge::Left,Edge::Right,std::numeric_limits<double>::infinity(),0,0};
        for(size_t a=0;a<screens.size();++a)if(connected[a])for(size_t b=0;b<screens.size();++b)if(!connected[b]) {
            auto link=Between(screens[a],screens[b]);
            if(link.distance<best.distance||(link.distance==best.distance&&(link.overlap>best.overlap||(link.overlap==best.overlap&&link.centers<best.centers)))){best=link;from=a;to=b;}
        }
        add(from,best.a);add(to,best.b);connected[to]=true;
    }
    return result;
}
}
