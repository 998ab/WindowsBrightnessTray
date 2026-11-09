#pragma once
#include "brightness.h"
#include <cmath>

// Continuous logical space preserves matched calibration anchors during a fade.
// Endpoint corrections preserve the exact confirmed/raw target despite integer
// rounding, or a starting brightness outside the configured calibration range.
struct FadePath {
    std::uint32_t start, target, minimum, maximum;
    Calibration calibration;
    int logical;
    double progress=0;
    double Curve(double position) const {
        if(!calibration.Matched())return position<=50
            ?calibration.low+position*(calibration.middle-calibration.low)/50
            :calibration.middle+(position-50)*(calibration.high-calibration.middle)/50;
        auto n=calibration.Nodes();int segment=std::clamp(static_cast<int>(position/25),0,3);
        return n[segment]+(position-segment*25)*(n[segment+1]-n[segment])/25;
    }
    double Inverse(double hardware) const {
        if(!calibration.Matched())return hardware<=calibration.middle
            ?(hardware-calibration.low)*50/(calibration.middle-calibration.low)
            :50+(hardware-calibration.middle)*50/(calibration.high-calibration.middle);
        auto n=calibration.Nodes();int segment=0;while(segment<3&&hardware>n[segment+1])++segment;
        return segment*25+(hardware-n[segment])*25/(n[segment+1]-n[segment]);
    }
    double Coordinate(double raw) const {
        if(maximum<=minimum)return raw;
        double percent=(raw-minimum)*100/(maximum-minimum);
        return logical>=0&&calibration.Valid()?Inverse(percent):percent;
    }
    std::uint32_t Raw(double coordinate) const {
        double percent=logical>=0&&calibration.Valid()?Curve(coordinate):coordinate;
        double raw=minimum+(maximum-minimum)*percent/100;
        return static_cast<std::uint32_t>(std::llround(std::clamp(raw,
            static_cast<double>(minimum),static_cast<double>(maximum))));
    }
    std::uint32_t At(double fraction) const {
        fraction=std::clamp(fraction,0.0,1.0);
        if(fraction<=0)return start;if(fraction>=1)return target;
        double raw=start+(static_cast<double>(target)-start)*fraction;
        if(logical>=0&&maximum>minimum&&calibration.Valid()) {
            double range=static_cast<double>(maximum)-minimum;
            double origin=Inverse((static_cast<double>(start)-minimum)*100/range);
            auto mapped=[&](double value){return minimum+range*Curve(value)/100;};
            raw=mapped(origin+(logical-origin)*fraction)
                +(start-mapped(origin))*(1-fraction)+(target-mapped(logical))*fraction;
        }
        return static_cast<std::uint32_t>(std::llround(std::clamp(raw,
            static_cast<double>(std::min(start,target)),static_cast<double>(std::max(start,target)))));
    }
    std::uint32_t MaxStep() const {
        return static_cast<std::uint32_t>(std::max<std::uint64_t>(1,(static_cast<std::uint64_t>(maximum)-minimum+49)/50));
    }
};
