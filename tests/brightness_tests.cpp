#include "../src/brightness.h"
#include "../src/fade.h"
#include "../src/preferences.h"
#include <cstdlib>
#include <iostream>
#include <limits>
#include <random>
#include <array>

void Check(bool condition) { if(!condition) { std::cerr << "FAILED\n"; std::exit(1); } }
int main() {
    int curves=0;
    // Cover every valid three-point curve, including one-percent segments.
    for(int low=0;low<99;++low)for(int middle=low+1;middle<100;++middle)for(int high=middle+1;high<=100;++high) {
        Calibration c{low,middle,high};Check(c.Valid());
        Check(ToHardwarePercent(0,c)==low);Check(ToHardwarePercent(50,c)==middle);Check(ToHardwarePercent(100,c)==high);
        Check(ToLogicalPercent(low,c)==0);Check(ToLogicalPercent(middle,c)==50);Check(ToLogicalPercent(high,c)==100);
        int last=low;
        for(int p=0;p<=100;++p) {
            int hardware=ToHardwarePercent(p,c);Check(hardware>=last&&hardware>=low&&hardware<=high);last=hardware;
            Check(ToLogicalPercent(hardware,c)>=0&&ToLogicalPercent(hardware,c)<=100);
        }
        ++curves;
    }
    Check(!Calibration{20,20,80}.Valid());Check(!Calibration{0,60,50}.Valid());Check(!Calibration{-1,50,100}.Valid());
    for(auto maximum:{100U,255U,65535U,std::numeric_limits<std::uint32_t>::max()}) {
        for(int p=0;p<=100;++p) Check(std::abs(FromRaw(ToRaw(p,0,maximum),0,maximum)-p)<=1);
        Check(ToRaw(0,10,maximum)==10);Check(ToRaw(100,10,maximum)==maximum);
    }
    Check(ToHardwarePercent(50,Calibration{10,35,80})==35);
    Check(ToHardwarePercent(25,Calibration{10,35,80})==23);
    Check(ToHardwarePercent(75,Calibration{10,35,80})==58);
    Check(FromRaw(75,25,125)==50);
    for(auto curve:{Calibration{10,35,80},Calibration::FromNodes({8,15,28,50,72})}){
        auto nodes=curve.Matched()?curve.Nodes():std::array<int,5>{10,10,35,80,80};
        const int segments=curve.Matched()?4:2;
        for(int segment=0;segment<segments;++segment){
            double low=curve.Matched()?nodes[segment]:(segment==0?curve.low:curve.middle);
            double high=curve.Matched()?nodes[segment+1]:(segment==0?curve.middle:curve.high);
            const double span=curve.Matched()?25:50;
            for(int step=0;step<=100;++step)Check(std::abs(ToLogicalContinuous(low+(high-low)*step/100,curve)-(segment*span+span*step/100))<0.000001);
        }
        Check(ToLogicalContinuous(-10,curve)==0&&ToLogicalContinuous(110,curve)==100);
    }
    std::mt19937 random(42);
    for(int test=0;test<10000;++test) {
        std::array<int,5> nodes;
        do {for(auto& p:nodes)p=static_cast<int>(random()%101);std::sort(nodes.begin(),nodes.end());}
        while(std::adjacent_find(nodes.begin(),nodes.end())!=nodes.end());
        auto c=Calibration::FromNodes(nodes);Check(c.Valid()&&c.Matched());
        for(int i=0;i<5;++i){Check(ToHardwarePercent(i*25,c)==nodes[i]);Check(ToLogicalPercent(nodes[i],c)==i*25);}
        int last=nodes[0];
        for(int p=0;p<=100;++p){int next=ToHardwarePercent(p,c);Check(next>=last&&next>=c.low&&next<=c.high);last=next;}
        Check(ToLogicalPercent(-20,c)==0);Check(ToLogicalPercent(120,c)==100);
    }
    Check(!Calibration{0,50,100,50,75}.Valid());Check(!Calibration{0,50,100,-1,75}.Valid());
    auto curved=Calibration::FromNodes({8,15,28,50,72});Check(ToHardwarePercent(50,curved)==28);Check(ToHardwarePercent(75,curved)==50);
    for(int test=0;test<2000;++test) {
        std::array<int,5> nodes;
        do {for(auto& p:nodes)p=static_cast<int>(random()%101);std::sort(nodes.begin(),nodes.end());}
        while(std::adjacent_find(nodes.begin(),nodes.end())!=nodes.end());
        auto c=Calibration::FromNodes(nodes);int logical=static_cast<int>(random()%101);
        unsigned start=random()%101,target=static_cast<unsigned>(ToHardwarePercent(logical,c));
        FadePath path{start,target,0,100,c,logical};Check(path.At(0)==start&&path.At(1)==target);
        auto last=start;
        for(int frame=1;frame<=100;++frame) {
            auto next=path.At(frame/100.0);Check(next>=std::min(start,target)&&next<=std::max(start,target));
            Check(target>=start?next>=last:next<=last);last=next;
        }
    }
    FadePath wide{0,std::numeric_limits<std::uint32_t>::max(),0,std::numeric_limits<std::uint32_t>::max(),{},-1};
    Check(wide.At(0)==0&&wide.At(1)==std::numeric_limits<std::uint32_t>::max());Check(wide.MaxStep()>0);
    Check(transition::Duration(0,1)==0);Check(transition::Duration(200,1)==200);
    Check(transition::Duration(200,0.02)<transition::Duration(200,1));
    Check(transition::Duration(200,1)<transition::Duration(1500,1));
    Check(transition::Progress(0,200)==0&&transition::Progress(200,200)==1&&transition::Progress(100,200)==0.5);
    Preferences preferences;Check(preferences.transitionMs==200&&preferences.wheelStep==2&&preferences.hardwareStep==2);
    preferences.hardwareStep=99;preferences.Normalize();Check(preferences.hardwareStep==2);
    preferences.hardwareStep=0;preferences.Normalize();Check(preferences.hardwareStep==0);
    for(int mode=0;mode<3;++mode){preferences.SetResponse(mode);Check(preferences.responseMode==mode);Check(preferences.transitionMs==(mode==0?0:600));Check(preferences.hardwareStep==(mode==0?0:mode==1?5:1));}
    preferences.transitionMs=0;preferences.AdoptLegacyResponse();Check(preferences.responseMode==0);
    preferences.transitionMs=250;preferences.AdoptLegacyResponse();Check(preferences.responseMode==1);
    preferences.transitionMs=1500;preferences.AdoptLegacyResponse();Check(preferences.responseMode==2&&preferences.hardwareStep==1);
    preferences.transitionMs=99999;preferences.wheelStep=0;preferences.Normalize();Check(preferences.transitionMs==2000&&preferences.wheelStep==2);
    preferences.transitionMs=0;preferences.wheelStep=5;preferences.Normalize();Check(preferences.transitionMs==0&&preferences.wheelStep==5);
    std::cout << "PASS: " << curves << " calibration curves, endpoints, monotonicity and raw conversions\n";
    std::cout << "PASS: 10000 five-point curves, exact matching anchors and legacy compatibility\n";
    std::cout << "PASS: 2000 calibrated fade paths, monotonicity, out-of-range starts, exact/raw endpoints\n";
    std::cout << "PASS: transition timing, short/large changes, instant mode and preference validation\n";
}
