#pragma once
#include <algorithm>
#include <cstdint>

// Follow acknowledged values at their measured cadence. Begin marks a new
// request, so time spent idle is never mistaken for device response latency.
class ConfirmedMotion {
public:
    static constexpr std::uint64_t DefaultDuration=80;
    void Begin(std::uint64_t now){lastAck_=now;measuring_=true;}
    bool Confirm(double value,std::uint64_t now,bool animate=true){
        value=std::clamp(value,0.0,100.0);
        if(!initialized_||!animate){
            from_=target_=value;started_=now;duration_=0;initialized_=true;
            measuring_=false;return false;
        }
        if(value==target_)return Active(now);
        const double current=Value(now);
        duration_=measuring_&&now>lastAck_?now-lastAck_:DefaultDuration;
        lastAck_=now;measuring_=true;from_=current;target_=value;started_=now;
        return Active(now);
    }
    double Value(std::uint64_t now)const{
        if(!initialized_)return 0;
        if(!duration_||(now>=started_&&now-started_>=duration_))return target_;
        const double t=now>started_?static_cast<double>(now-started_)/duration_:0;
        return from_+(target_-from_)*t;
    }
    bool Active(std::uint64_t now)const{return initialized_&&from_!=target_&&now>=started_&&now-started_<duration_;}
private:
    double from_=0,target_=0;
    std::uint64_t started_=0,duration_=0,lastAck_=0;
    bool initialized_=false,measuring_=false;
};
