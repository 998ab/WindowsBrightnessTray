#include "../src/tray.h"
#include <iostream>
#include <cstdlib>

void Check(bool value,const char* message){if(!value){std::cerr<<message<<"\n";std::exit(1);}}
// Exercise the actual hook callback with local synthetic structs. No SendInput,
// global hook installation, real mouse input or display brightness is used.
struct TrayControlTestAccess {
    static void Run() {
        HWND sink=CreateWindowExW(0,L"STATIC",L"tray-test",0,0,0,0,0,HWND_MESSAGE,nullptr,GetModuleHandleW(nullptr),nullptr);
        Check(sink!=nullptr,"Could not create message-only sink");
        TrayControl tray;tray.owner_=sink;tray.message_=WM_APP+41;tray.icon_={100,100,124,124};
        TrayControl::instance_=&tray;tray.hovered_=true;
        MSLLHOOKSTRUCT input{};input.pt={112,112};input.mouseData=static_cast<DWORD>(static_cast<unsigned short>(120))<<16;
        Check(TrayControl::Hook(HC_ACTION,WM_MOUSEWHEEL,reinterpret_cast<LPARAM>(&input))==1,"Wheel over icon was not consumed");
        MSG event{};Check(PeekMessageW(&event,sink,tray.message_,tray.message_,PM_REMOVE)!=FALSE,"Wheel event was not posted");
        Check(static_cast<int>(static_cast<INT_PTR>(event.wParam))==120,"Positive wheel delta corrupted");
        input.mouseData=static_cast<DWORD>(static_cast<unsigned short>(-120))<<16;
        TrayControl::Hook(HC_ACTION,WM_MOUSEWHEEL,reinterpret_cast<LPARAM>(&input));
        Check(PeekMessageW(&event,sink,tray.message_,tray.message_,PM_REMOVE)!=FALSE,"Negative wheel event was lost");
        Check(static_cast<int>(static_cast<INT_PTR>(event.wParam))==-120,"Negative wheel delta corrupted");
        input.pt={200,200};TrayControl::Hook(HC_ACTION,WM_MOUSEMOVE,reinterpret_cast<LPARAM>(&input));
        Check(!tray.hovered_,"Leaving icon did not disable interception");
        input.pt={112,112};TrayControl::Hook(HC_ACTION,WM_MOUSEWHEEL,reinterpret_cast<LPARAM>(&input));
        Check(!PeekMessageW(&event,sink,tray.message_,tray.message_,PM_REMOVE),"Wheel intercepted without a fresh tray hover");
        Check(tray.TakeWheel(60,false)==0&&tray.TakeWheel(60,false)==2,"High-resolution positive wheel accumulation failed");
        Check(tray.TakeWheel(-60,false)==0&&tray.TakeWheel(-60,false)==-2,"Negative wheel accumulation failed");
        Check(tray.TakeWheel(120,true)==1&&tray.TakeWheel(-120,true)==-1,"Fine adjustment failed");
        Check(tray.TakeWheel(120,false,5)==5&&tray.TakeWheel(-120,false,1)==-1,"Configurable wheel step failed");
        Check(tray.TakeWheel(120,true,5)==1,"Ctrl did not preserve precise adjustment");
        tray.Stop();DestroyWindow(sink);
    }
};
int main(){TrayControlTestAccess::Run();std::cout<<"PASS: tray hook hit testing, signed deltas, leave handling, fine/high-resolution wheel\n";}
