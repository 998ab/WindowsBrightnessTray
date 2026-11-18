// Exercise the real read transaction while replacing only the two DDC API calls.
// Include SDK declarations before the macros so imported declarations stay intact.
#include "../src/monitors.h"
#include <highlevelmonitorconfigurationapi.h>
#include <lowlevelmonitorconfigurationapi.h>
#include <iostream>

namespace read_test {
struct Reply { BOOL ok; DWORD minimum, current, maximum; };
Reply high{}, low{};
int highCalls=0, lowCalls=0;
BOOL High(HANDLE,LPDWORD minimum,LPDWORD current,LPDWORD maximum) {
    ++highCalls;*minimum=high.minimum;*current=high.current;*maximum=high.maximum;return high.ok;
}
BOOL Low(HANDLE,BYTE,LPMC_VCP_CODE_TYPE,LPDWORD current,LPDWORD maximum) {
    ++lowCalls;*current=low.current;*maximum=low.maximum;return low.ok;
}
}
#define GetMonitorBrightness read_test::High
#define GetVCPFeatureAndVCPFeatureReply read_test::Low
#include "../src/monitors.cpp"
#undef GetMonitorBrightness
#undef GetVCPFeatureAndVCPFeatureReply

int main() {
    int failures=0;
    auto check=[&](bool value,const char* message) {
        std::cout<<(value?"PASS: ":"FAIL: ")<<message<<'\n';if(!value)++failures;
    };
    auto original=[] {
        Monitor monitor;monitor.minimum=10;monitor.current=50;monitor.maximum=100;
        monitor.supported=true;return monitor;
    };
    auto unchanged=[](const Monitor& monitor,const Monitor& before) {
        return monitor.minimum==before.minimum&&monitor.current==before.current
            &&monitor.maximum==before.maximum&&monitor.lowLevel==before.lowLevel
            &&monitor.supported==before.supported;
    };
    {
        auto monitor=original();
        read_test::high={TRUE,5,40,120};read_test::lowCalls=0;
        check(Read(monitor)&&read_test::lowCalls==0&&!monitor.lowLevel
            &&monitor.minimum==5&&monitor.current==40&&monitor.maximum==120,
            "valid high-level read commits values without changing backend");
    }
    {
        auto monitor=original(),before=monitor;
        read_test::high={FALSE,70,80,90};read_test::low={FALSE,0,900,1000};
        check(!Read(monitor),"both failed APIs report a failed read");
        check(unchanged(monitor,before),"failed fallback preserves confirmed current, range, and backend");
    }
    {
        auto monitor=original();
        read_test::high={TRUE,10,300,100};read_test::low={TRUE,0,30,80};
        read_test::lowCalls=0;
        check(Read(monitor),"invalid high-level payload permits valid low-level fallback");
        check(read_test::lowCalls==1&&monitor.lowLevel&&monitor.minimum==0
            &&monitor.current==30&&monitor.maximum==80,"valid fallback commits its complete range and backend");
    }
    {
        auto monitor=original(),before=monitor;
        read_test::high={FALSE,0,0,0};read_test::low={TRUE,0,101,100};
        check(!Read(monitor),"out-of-range low-level payload reports failure");
        check(unchanged(monitor,before),"invalid low-level payload cannot replace confirmed state");
    }
    {
        auto monitor=original();monitor.lowLevel=true;monitor.minimum=0;auto before=monitor;
        read_test::low={FALSE,0,999,0};read_test::highCalls=0;
        check(!Read(monitor)&&read_test::highCalls==0,"low-level monitor retains its API selection during a failed read");
        check(unchanged(monitor,before),"failed low-level refresh preserves confirmed values");
    }
    return failures?1:0;
}
