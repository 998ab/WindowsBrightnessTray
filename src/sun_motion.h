#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

// Same timing as the interactive concept: input extends a 650ms pulse,
// 110 degrees/second, with a linear 280ms velocity fade. No pose snapping.
class SunMotion {
public:
    void Trigger(std::uint64_t now,int direction,std::uint64_t duration=650){
        Advance(now);last_=now;until_=now+duration;direction_=direction<0?-1:1;active_=true;
    }
    bool Advance(std::uint64_t now){
        if(!active_)return false;
        const auto end=std::min(now,until_);
        const auto fadeStart=until_>280?until_-280:0;
        double distance=0;
        if(end>last_){
            const auto flatEnd=std::min(end,fadeStart);
            if(flatEnd>last_)distance=static_cast<double>(flatEnd-last_);
            const auto start=std::max(last_,fadeStart);
            if(end>start){double a=static_cast<double>(until_-start),b=static_cast<double>(until_-end);distance+=(a*a-b*b)/(2*280.0);}
            angle_=std::fmod(angle_+direction_*distance*0.11,360.0);
        }
        last_=end;if(now>=until_)active_=false;return active_;
    }
    double Angle()const{return angle_;}
    bool Active()const{return active_;}
private:
    std::uint64_t last_{},until_{};
    int direction_=1;bool active_=false;double angle_=0;
};
