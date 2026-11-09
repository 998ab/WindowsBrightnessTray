#include "../src/display_names.h"
#include <iostream>
#include <cstdlib>

void Check(bool ok){if(!ok){std::cerr<<"Name resolution failed\n";std::exit(1);}}
int main() {
    using namespace display_names;
    Names names{{L"\\\\.\\DISPLAY1",{L"DELL U2723QE"}},{L"\\\\.\\DISPLAY2",{L"LG ULTRAGEAR"}}};
    Check(Label(names,L"\\\\.\\DISPLAY1",L"Generic PnP Monitor",0,1)==L"DELL U2723QE · Display 1");
    Check(Label(names,L"\\\\.\\DISPLAY2",L"Generic PnP Monitor",0,1)==L"LG ULTRAGEAR · Display 2");
    Check(Label({},L"\\\\.\\DISPLAY3",L"  Acer\r\n  XV272  ",0,1)==L"Acer XV272 · Display 3");
    Check(Label({},L"\\\\.\\DISPLAY4",L"\u0423\u043d\u0438\u0432\u0435\u0440\u0441\u0430\u043b\u044c\u043d\u044b\u0439 \u043c\u043e\u043d\u0438\u0442\u043e\u0440 PnP",0,1)==L"Monitor · Display 4");
    names[L"\\\\.\\DISPLAY1"]={L"DELL U2723QE",L""};
    Check(Label(names,L"\\\\.\\DISPLAY1",L"Generic PnP Monitor",0,1)==L"Monitor · Display 1");
    Check(Label(names,L"\\\\.\\DISPLAY2",L"Generic PnP Monitor",1,2)==L"Monitor · Display 2.2");
    std::cout<<"PASS: EDID names, source association, generic fallback, whitespace, clone ambiguity\n";
}
