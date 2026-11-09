#pragma once
#include <algorithm>
#include <cstdint>
#include <array>

struct Calibration {
    int low = 0, middle = 50, high = 100;
    int quarter = -1, threeQuarter = -1;
    bool Matched() const { return quarter >= 0 && threeQuarter >= 0; }
    std::array<int,5> Nodes() const { return {low,quarter<0?(low+middle+1)/2:quarter,middle,threeQuarter<0?(middle+high+1)/2:threeQuarter,high}; }
    bool Valid() const {
        if(!(0 <= low && low < middle && middle < high && high <= 100))return false;
        return (quarter==-1 && threeQuarter==-1) || (low<quarter && quarter<middle && middle<threeQuarter && threeQuarter<high);
    }
    static Calibration FromNodes(const std::array<int,5>& nodes) { return {nodes[0],nodes[2],nodes[4],nodes[1],nodes[3]}; }
};
inline int ToHardwarePercent(int logical, const Calibration& c) {
    logical = std::clamp(logical, 0, 100);
    if(c.Matched()) {
        auto nodes=c.Nodes();int segment=std::min(3,logical/25),offset=logical-segment*25;
        return nodes[segment]+(offset*(nodes[segment+1]-nodes[segment])+12)/25;
    }
    return logical <= 50 ? c.low + (logical * (c.middle - c.low) + 25) / 50
        : c.middle + ((logical - 50) * (c.high - c.middle) + 25) / 50;
}
inline int ToLogicalPercent(int hardware, const Calibration& c) {
    if(c.Matched()) {
        auto nodes=c.Nodes();hardware=std::clamp(hardware,c.low,c.high);int segment=0;
        while(segment<3 && hardware>nodes[segment+1])++segment;
        int range=nodes[segment+1]-nodes[segment];
        return segment*25+((hardware-nodes[segment])*25+range/2)/range;
    }
    return std::clamp(hardware <= c.middle
        ? ((hardware - c.low) * 50 + (c.middle - c.low) / 2) / (c.middle - c.low)
        : 50 + ((hardware - c.middle) * 50 + (c.high - c.middle) / 2) / (c.high - c.middle), 0, 100);
}
inline std::uint32_t ToRaw(int percent, std::uint32_t low, std::uint32_t high) {
    return low + static_cast<std::uint32_t>((static_cast<std::uint64_t>(high - low) * std::clamp(percent, 0, 100) + 50) / 100);
}
inline int FromRaw(std::uint32_t raw, std::uint32_t low, std::uint32_t high) {
    return high > low ? static_cast<int>((static_cast<std::uint64_t>(std::clamp(raw, low, high) - low) * 100 + (high - low) / 2) / (high - low)) : 0;
}
