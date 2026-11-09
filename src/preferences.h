#pragma once
#include "transition.h"

struct Preferences {
    int transitionMs=transition::DefaultMs;
    int wheelStep=2;
    int hardwareStep=2;
    int responseMode=1;
    void SetResponse(int mode) {
        responseMode=std::clamp(mode,0,2);
        transitionMs=responseMode==0?0:600;
        hardwareStep=responseMode==0?0:responseMode==1?5:1;
    }
    void AdoptLegacyResponse(){SetResponse(transitionMs==0?0:transitionMs<=400?1:2);}
    void Normalize() {
        transitionMs=transition::Normalize(transitionMs);
        if(wheelStep!=1&&wheelStep!=2&&wheelStep!=5)wheelStep=2;
        if(hardwareStep!=0&&hardwareStep!=1&&hardwareStep!=2&&hardwareStep!=5)hardwareStep=2;
    }
};
