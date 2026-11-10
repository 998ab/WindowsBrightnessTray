#pragma once
#include "ui.h"

// No mouse tracking/polling timer. The hook only posts wheel events while the
// pointer is over our notification icon; all DDC/CI stays outside the hook.
class TrayControl {
public:
    bool Start(HWND owner,UINT wheelMessage);
    void Stop();
    bool Hover();
    void InvalidatePosition();
    int TakeWheel(int delta,bool fine,int step=2);
    void Toast(std::wstring title,std::wstring value);
    ~TrayControl(){Stop();}
private:
    friend struct TrayControlTestAccess;
    static LRESULT CALLBACK Hook(int,WPARAM,LPARAM);
    static LRESULT CALLBACK ToastProc(HWND,UINT,WPARAM,LPARAM);
    static TrayControl* instance_;
    HWND owner_{},toast_{};
    HHOOK hook_{};
    UINT message_{};
    RECT icon_{};
    bool hovered_=false;
    int remainder_=0;
    ui::Surface surface_;
    std::wstring title_,value_;
};
