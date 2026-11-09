#include "../src/calibration_layout.h"
#include <cstdlib>
#include <iostream>
using namespace calibration_layout;
void Check(bool value){if(!value){std::cerr<<"Calibration placement failed\n";std::exit(1);}}
bool Has(const std::vector<Placement>& p,size_t monitor,Edge edge){return std::any_of(p.begin(),p.end(),[&](const Placement& v){return v.monitor==monitor&&v.edge==edge;});}
int main(){
    auto p=Plan({{0,0,1920,1080},{1920,0,3840,1080}},0);
    Check(p.size()==2&&Has(p,0,Edge::Right)&&Has(p,1,Edge::Left));
    p=Plan({{-2560,-300,0,1140},{0,0,1920,1080}},1);
    Check(p.size()==2&&Has(p,0,Edge::Right)&&Has(p,1,Edge::Left));
    p=Plan({{0,-1080,1920,0},{0,0,1920,1080}},1);
    Check(p.size()==2&&Has(p,0,Edge::Bottom)&&Has(p,1,Edge::Top));
    p=Plan({{0,0,1920,1080},{1920,0,3840,1080},{3840,0,5760,1080}},2);
    Check(p.size()==4&&Has(p,0,Edge::Right)&&Has(p,1,Edge::Left)&&Has(p,1,Edge::Right)&&Has(p,2,Edge::Left));
    p=Plan({{0,0,1920,1080},{1920,-1080,3840,0},{1920,0,3840,1080}},0);
    Check(p.size()==4&&Has(p,0,Edge::Right)&&Has(p,2,Edge::Left)&&Has(p,1,Edge::Bottom)&&Has(p,2,Edge::Top));
    p=Plan({{0,0,1920,1080},{2000,1200,3920,2280}},0);
    Check(p.size()==2&&Has(p,0,Edge::Right)&&Has(p,1,Edge::Left));
    p=Plan({{0,0,1080,1920},{1080,200,3000,1280}},0);
    Check(p.size()==2&&Has(p,0,Edge::Right)&&Has(p,1,Edge::Left));
    p=Plan({{0,0,1920,1080},{0,0,1920,1080}},99);Check(p.size()==2);
    Check(Plan({},0).empty()&&Plan({{0,0,1920,1080}},0).empty());
    std::cout<<"PASS: facing edges, negative coordinates, offsets, portrait, stacked, three-screen chain, L-shape, gaps and cloned demo\n";
}
