#pragma once
#include <algorithm>
#include <cmath>

namespace transition {
constexpr int DefaultMs=200,MaximumMs=2000;
inline int Normalize(int milliseconds){return std::clamp(milliseconds,0,MaximumMs);}
inline double Duration(int milliseconds,double distance) {
    milliseconds=Normalize(milliseconds);
    if(!milliseconds)return 0;
    return std::min<double>(milliseconds,std::max(100.0,milliseconds*std::sqrt(std::clamp(distance,0.0,1.0))));
}
inline double Progress(double elapsedMs,double durationMs) {
    if(durationMs<=0)return 1;
    double t=std::clamp(elapsedMs/durationMs,0.0,1.0);
    return t*t*(3-2*t);
}
}
