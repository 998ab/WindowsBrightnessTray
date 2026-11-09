#include "tray.h"
#include <shellapi.h>
#include <algorithm>

TrayControl* TrayControl::instance_=nullptr;
bool TrayControl::Start(HWND owner,UINT wheelMessage) {
    owner_=owner;message_=wheelMessage;instance_=this;
    hook_=SetWindowsHookExW(WH_MOUSE_LL,Hook,GetModuleHandleW(nullptr),0);return hook_!=nullptr;
}
void TrayControl::Stop() {
    if(hook_){UnhookWindowsHookEx(hook_);hook_=nullptr;}
    if(toast_){DestroyWindow(toast_);toast_=nullptr;surface_.Clear();}
    if(instance_==this)instance_=nullptr;
}
void TrayControl::InvalidatePosition(){hovered_=false;SetRectEmpty(&icon_);remainder_=0;}
void TrayControl::Hover() {
    NOTIFYICONIDENTIFIER identifier{sizeof(identifier),owner_,1,{}};
    RECT rect{};
    if(SUCCEEDED(Shell_NotifyIconGetRect(&identifier,&rect))){icon_=rect;hovered_=true;}
    else InvalidatePosition();
}
LRESULT CALLBACK TrayControl::Hook(int code,WPARAM wp,LPARAM lp) {
    auto self=instance_;
    if(code==HC_ACTION&&self) {
        auto input=reinterpret_cast<MSLLHOOKSTRUCT*>(lp);
        if(wp==WM_MOUSEMOVE&&!PtInRect(&self->icon_,input->pt)){self->hovered_=false;self->remainder_=0;}
        if(wp==WM_MOUSEWHEEL&&self->hovered_&&PtInRect(&self->icon_,input->pt)) {
            int delta=static_cast<short>(HIWORD(input->mouseData));
            PostMessageW(self->owner_,self->message_,static_cast<WPARAM>(static_cast<INT_PTR>(delta)),(GetAsyncKeyState(VK_CONTROL)&0x8000)?1:0);
            return 1;
        }
    }
    return CallNextHookEx(nullptr,code,wp,lp);
}
int TrayControl::TakeWheel(int delta,bool fine,int step) {
    remainder_+=delta;int detents=remainder_/WHEEL_DELTA;remainder_%=WHEEL_DELTA;
    return detents*(fine?1:std::clamp(step,1,10));
}
void TrayControl::Toast(std::wstring title,std::wstring value) {
    title_=std::move(title);value_=std::move(value);
    if(!toast_) {
        WNDCLASSW wc{};wc.lpfnWndProc=ToastProc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"BrightnessTray.OSD";
        wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassW(&wc);
        toast_=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE|WS_EX_TOPMOST,wc.lpszClassName,L"BrightnessTray",WS_POPUP,
            icon_.left,icon_.top-100,280,82,owner_,nullptr,wc.hInstance,this);
        surface_.Init(toast_);
    }
    ui::Frame(toast_);MONITORINFO info{sizeof(info)};
    GetMonitorInfoW(MonitorFromRect(&icon_,MONITOR_DEFAULTTONEAREST),&info);
    int w=surface_.S(280),h=surface_.S(84);
    int x=std::clamp(icon_.right-w,info.rcWork.left,std::max(info.rcWork.left,info.rcWork.right-w));
    int y=std::clamp(icon_.top-h-surface_.S(12),info.rcWork.top,std::max(info.rcWork.top,info.rcWork.bottom-h));
    SetWindowPos(toast_,HWND_TOPMOST,x,y,w,h,SWP_SHOWWINDOW|SWP_NOACTIVATE);
    InvalidateRect(toast_,nullptr,FALSE);SetTimer(toast_,1,1200,nullptr);
}
LRESULT CALLBACK TrayControl::ToastProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
    auto self=reinterpret_cast<TrayControl*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
    if(msg==WM_NCCREATE){self=static_cast<TrayControl*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
    if(!self)return DefWindowProcW(hwnd,msg,wp,lp);
    if(msg==WM_MOUSEACTIVATE)return MA_NOACTIVATE;
    if(msg==WM_ERASEBKGND)return 1;
    if(msg==WM_TIMER){KillTimer(hwnd,1);ShowWindow(hwnd,SW_HIDE);return 0;}
    if(msg==WM_PAINT) {
        PAINTSTRUCT ps;HDC dc=BeginPaint(hwnd,&ps);RECT r;GetClientRect(hwnd,&r);ui::Fill(dc,r,ui::colors.card);
        RECT title{self->surface_.S(18),self->surface_.S(10),r.right-self->surface_.S(18),self->surface_.S(30)};
        RECT value{title.left,self->surface_.S(34),title.right,r.bottom-self->surface_.S(10)};
        if(self->surface_.body){ui::Text(dc,self->title_,title,self->surface_.caption,ui::colors.muted,DT_SINGLELINE|DT_END_ELLIPSIS);
            ui::Text(dc,self->value_,value,self->surface_.number,ui::colors.text,DT_SINGLELINE|DT_END_ELLIPSIS);}
        EndPaint(hwnd,&ps);return 0;
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}
