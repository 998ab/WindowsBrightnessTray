#pragma once
#include <windows.h>
#include <cwchar>

// Used only by the UI regression executable. Simulate a small desktop without
// changing the user's display mode or the production application's API calls.
inline BOOL UiTestGetMonitorInfoW(HMONITOR monitor,LPMONITORINFO info) {
    BOOL result=GetMonitorInfoW(monitor,info);
    if(result&&std::wcsstr(GetCommandLineW(),L"--small-screen")) {
        info->rcMonitor.right=info->rcMonitor.left+1024;
        info->rcMonitor.bottom=info->rcMonitor.top+768;
        info->rcWork=info->rcMonitor;info->rcWork.bottom-=40;
    }
    return result;
}
#define GetMonitorInfoW UiTestGetMonitorInfoW
