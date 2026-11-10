#include "../src/tray.h"
#include "../src/tray_position.h"
#include "../src/sun_motion.h"
#include "../src/frame_clock.h"
#include "../src/confirmed_motion.h"
#include <iostream>
#include <cstdlib>
#include <cmath>
#include <algorithm>

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
void CheckSunResources() {
    HICON warmup=ui::CreateTrayIcon();Check(warmup!=nullptr,"Sun icon creation failed");DestroyIcon(warmup);
    DWORD gdi=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
    DWORD user=GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS);
    for(int i=0;i<1000;++i) {
        constexpr int sizes[]={16,20,24,32};int extent=sizes[i%4];
        HICON icon=ui::CreateTrayIcon(true,i*0.031,extent);
        Check(icon!=nullptr,"Rotated sun icon creation failed");
        ICONINFO info{};Check(GetIconInfo(icon,&info)!=FALSE,"Sun icon bitmap unavailable");
        BITMAP bitmap{};GetObjectW(info.hbmColor,sizeof(bitmap),&bitmap);
        Check(bitmap.bmWidth==extent&&bitmap.bmHeight==extent,"Unexpected sun icon size");
        DeleteObject(info.hbmColor);DeleteObject(info.hbmMask);DestroyIcon(icon);
    }
    Check(GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)==gdi,"Sun rendering leaked GDI objects");
    Check(GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS)==user,"Sun rendering leaked icon handles");
}
void CheckSmallTrayRays(){
    constexpr double pi=3.14159265358979323846;
    for(int extent:{16,20,24,32})for(int frame=0;frame<16;++frame){
        double angle=frame*pi/64;HICON icon=ui::CreateTrayIcon(true,angle,extent);Check(icon!=nullptr,"Tray visibility icon creation failed");
        ICONINFO info{};Check(GetIconInfo(icon,&info)!=FALSE,"Could not inspect tray visibility");
        BITMAPINFO format{};format.bmiHeader={sizeof(BITMAPINFOHEADER),extent,-extent,1,32,BI_RGB};DWORD pixels[32*32]{};HDC dc=GetDC(nullptr);
        Check(GetDIBits(dc,info.hbmColor,0,extent,pixels,&format,DIB_RGB_COLORS)!=0,"Could not read tray alpha");
        for(int ray=0;ray<8;++ray){double a=angle+ray*pi/4,c=std::cos(a),s=std::sin(a);int peak=0;
            for(int y=0;y<extent;++y)for(int x=0;x<extent;++x){double px=(x+0.5)*16/extent-8,py=(y+0.5)*16/extent-8;double along=px*c+py*s,across=std::abs(-px*s+py*c);if(along>=4.2&&along<=7.2&&across<=1)peak=std::max(peak,static_cast<int>(pixels[y*extent+x]>>24));}
            Check(peak>=170,"A tray sun ray disappears at a small DPI size or angle");
        }
        for(int i=0;i<extent;++i)Check((pixels[i]>>24)==0&&(pixels[(extent-1)*extent+i]>>24)==0&&(pixels[i*extent]>>24)==0&&(pixels[i*extent+extent-1]>>24)==0,"Tray sun touches the icon edge");
        DeleteObject(info.hbmColor);DeleteObject(info.hbmMask);ReleaseDC(nullptr,dc);DestroyIcon(icon);
    }
}
void CheckAntialiasing(){
    HDC dc=CreateCompatibleDC(nullptr);BITMAPINFO format{};format.bmiHeader={sizeof(BITMAPINFOHEADER),48,-48,1,32,BI_RGB};void* bits=nullptr;
    HBITMAP bitmap=CreateDIBSection(dc,&format,DIB_RGB_COLORS,&bits,nullptr,0);Check(bitmap&&bits,"Could not create rendering surface");auto old=SelectObject(dc,bitmap);
    ui::Fill(dc,{0,0,48,48},RGB(0,0,0));ui::Round(dc,{5,5,43,43},RGB(255,255,255),RGB(255,255,255),38);
    GdiFlush();int intermediate=0;auto pixels=static_cast<DWORD*>(bits);for(int i=0;i<48*48;++i){int red=(pixels[i]>>16)&255;if(red>0&&red<255)++intermediate;}
    Check(intermediate>20,"Rounded geometry has no antialiased edge coverage");
    SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);
}
void CheckPopupPlacement(){
    RECT work{0,0,1920,1040},bar{0,1040,1920,1080},pinned{1880,1044,1900,1064},hidden{1760,840,1776,856};
    Check(tray_position::InTaskbar(pinned,bar),"Pinned tray icon not recognized");Check(!tray_position::InTaskbar(hidden,bar),"Overflow icon confused with pinned icon");
    auto anchor=tray_position::OverflowAnchor({1700,1040,1920,1080},bar,32);
    auto normal=tray_position::Place(work,pinned,bar,352,384,10),overflow=tray_position::Place(work,anchor,bar,352,384,10);
    Check(normal.y==646&&overflow.y==normal.y,"Overflow popup is not aligned to the taskbar edge");
    Check(overflow.x==1380,"Overflow popup does not align with notification-area anchor");
    // Actual desktop geometry: Shell_NotifyIconGetRect returned the chevron
    // even though the visible overflow flyout occupied 1514,918 .. 1748,1032.
    RECT flyout{1514,918,1748,1032};auto above=tray_position::Place({0,0,1920,1032},flyout,{0,1032,1920,1080},352,384,10,&flyout);
    Check(above.x==1396&&above.y==524,"Real Windows 11 overflow geometry placed incorrectly");
    RECT bounds{above.x,above.y,above.x+352,above.y+384},intersection{};Check(!IntersectRect(&intersection,&bounds,&flyout),"Popup overlaps the hidden-icons flyout");
    auto top=tray_position::Place({0,40,1920,1080},{1700,4,1720,24},{0,0,1920,40},352,384,10);Check(top.y==50,"Top taskbar placement failed");
    auto left=tray_position::Place({-1860,0,0,1080},{-1920,900,-1860,932},{-1920,0,-1860,1080},352,384,10);Check(left.x==-1850&&left.y==548,"Left taskbar or negative monitor placement failed");
    auto right=tray_position::Place({0,0,1860,1080},{1860,900,1920,932},{1860,0,1920,1080},352,384,10);Check(right.x==1498,"Right taskbar placement failed");
    auto small=tray_position::Place({-800,-600,0,0},{-20,0,0,32},{-800,0,0,32},900,700,10);Check(small.x==-800&&small.y==-600,"Small work area clamping failed");
}
void CheckSunMotion(){
    SunMotion pulse;pulse.Trigger(1000,1);Check(pulse.Advance(1100),"Input animation did not start immediately");Check(std::abs(pulse.Angle()-11)<0.001,"Prototype sun speed changed");
    pulse.Advance(1370);Check(std::abs(pulse.Angle()-40.7)<0.001,"Prototype cruise period changed");
    Check(!pulse.Advance(1650),"Prototype pulse did not stop after its fade");Check(std::abs(pulse.Angle()-56.1)<0.001,"Animation snapped to a fixed pose");double resting=pulse.Angle();pulse.Advance(9000);Check(pulse.Angle()==resting,"Idle animation kept changing");
    SunMotion repeat;repeat.Trigger(1000,1);repeat.Advance(1400);double before=repeat.Angle();repeat.Trigger(1400,-1);Check(repeat.Angle()==before,"Direction reversal jumped the sun pose");Check(repeat.Advance(1700),"Repeated input did not extend animation");Check(repeat.Angle()<before,"Sun failed to reverse");Check(!repeat.Advance(2050),"Retargeted animation failed to become idle");
    SunMotion hover;hover.Trigger(1000,1,280);Check(!hover.Advance(1280),"Hover pulse failed to stop");Check(std::abs(hover.Angle()-15.4)<0.001,"Hover does not match prototype fade");
}
void CheckFrameClock(){
    HWND sink=CreateWindowExW(0,L"STATIC",L"frame-clock-test",0,0,0,0,0,HWND_MESSAGE,nullptr,GetModuleHandleW(nullptr),nullptr);Check(sink!=nullptr,"Frame clock sink unavailable");
    constexpr UINT frame=WM_APP+78;FrameClock clock;Check(clock.Start(sink,frame,120),"Frame clock failed to start");
    // A continuous stream of input must not postpone the first frame. An
    // unresponsive UI must receive at most one outstanding frame message.
    for(int i=0;i<80;++i){Check(clock.Start(sink,frame,120),"Frame clock retrigger failed");Sleep(1);}
    MSG event{};int count=0;while(PeekMessageW(&event,sink,frame,frame,PM_REMOVE))++count;Check(count==1,"Frames were starved by input or flooded the UI queue");
    clock.Acknowledge();bool received=false;for(int i=0;i<100&&!received;++i){Sleep(2);received=PeekMessageW(&event,sink,frame,frame,PM_REMOVE)!=FALSE;}Check(received,"Frame pacing did not resume after acknowledgement");
    clock.Stop();while(PeekMessageW(&event,sink,frame,frame,PM_REMOVE)){}Sleep(40);Check(!PeekMessageW(&event,sink,frame,frame,PM_REMOVE),"Frame clock continued posting while idle");clock.Close();DestroyWindow(sink);
}
void CheckBufferResources(){
    HDC screen=GetDC(nullptr);DWORD before=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
    {ui::BackBuffer buffer;for(int i=0;i<500;++i){HDC dc=buffer.Begin(screen,64+(i%3),48);Check(dc!=nullptr,"Reusable paint buffer allocation failed");ui::Fill(dc,{0,0,64,48},RGB(20,20,20));}}
    Check(GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)==before,"Reusable paint buffers leaked GDI resources");ReleaseDC(nullptr,screen);
}
void CheckConfirmedMotion(){
    ConfirmedMotion level;Check(!level.Confirm(20,1000),"Initial acknowledged level must appear immediately");
    level.Begin(1000);Check(level.Confirm(30,1100),"New acknowledgement did not start visual interpolation");
    Check(std::abs(level.Value(1150)-25)<0.000001,"Acknowledged progress is not fractional");
    Check(!level.Active(1200)&&level.Value(1200)==30,"Displayed progress did not reach acknowledged level");
    level.Begin(1200);level.Confirm(60,1300);double before=level.Value(1320);level.Confirm(35,1320);
    Check(level.Value(1320)==before,"Reversed progress jumped at acknowledgement");
    for(int i=0;i<=20;++i)Check(level.Value(1320+i)>=35&&level.Value(1320+i)<=before,"Displayed progress overshot acknowledgement");
    level.Begin(1400);level.Confirm(70,1440);level.Confirm(70,1460);
    Check(!level.Active(1480)&&level.Value(1480)==70,"Duplicate acknowledgement restarted interpolation");
    Check(!level.Confirm(10,1500,false)&&level.Value(1500)==10,"Instant/reduced-motion mode still interpolates");
    level.Confirm(-50,1600,false);Check(level.Value(1600)==0,"Displayed progress below range");level.Confirm(200,1700,false);Check(level.Value(1700)==100,"Displayed progress above range");
    // More than the old 16-sample capacity; test slow DDC as well as fast demo replies.
    for(std::uint64_t cadence:{40ULL,125ULL,250ULL,1000ULL}){
        ConfirmedMotion stream;stream.Confirm(0,1000);stream.Begin(1000);double previous=0;
        for(std::uint64_t now=1001;now<=1000+40*cadence;++now){
            if((now-1000)%cadence==0)stream.Confirm(static_cast<double>((now-1000)/cadence)*2,now);
            double current=stream.Value(now);
            if(now>1000+cadence)Check(std::abs(current-previous-2.0/cadence)<0.000001,"Regular acknowledgements changed line speed at a sample join");
            Check(current<=((now-1000)/cadence)*2,"Progress extrapolated beyond acknowledged brightness");previous=current;
        }
        auto settled=1000+41*cadence;Check(!stream.Active(settled)&&stream.Value(settled)==80,"Adaptive stream failed to settle");
        auto idle=settled+60000;stream.Begin(idle);stream.Confirm(90,idle+40);
        Check(stream.Value(idle+40)==80,"First response after idle jumped");
        Check(!stream.Active(idle+80)&&stream.Value(idle+80)==90,"Idle time was mistaken for response latency");
    }
    ConfirmedMotion changing;changing.Confirm(0,1000);changing.Begin(1000);changing.Confirm(2,1125);changing.Confirm(4,1250);
    changing.Confirm(8,1500);double previous=changing.Value(1500);
    for(std::uint64_t now=1501;now<=2000;++now){
        if(now==1750)changing.Confirm(12,now);if(now==2000)changing.Confirm(16,now);
        double current=changing.Value(now);Check(std::abs(current-previous-4.0/250)<0.000001,"Changed device cadence causes repeated pauses");previous=current;
    }
    changing.Confirm(5,2100,false);changing.Confirm(6,2200);
    Check(!changing.Active(2280)&&changing.Value(2280)==6,"Reduced-motion reset retained old cadence");
    std::cout<<"PASS: confirmed line uniform at 40/125/250/1000 ms, cadence changes, idle, reversal and no prediction\n";
}
void CheckDescendingProgressRendering(){
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_BAR_CLASSES};InitCommonControlsEx(&controls);
    HWND parent=CreateWindowExW(0,L"STATIC",L"progress-render-test",0,0,0,400,100,HWND_MESSAGE,nullptr,GetModuleHandleW(nullptr),nullptr);Check(parent!=nullptr,"Progress render parent failed");
    ui::Surface surface;surface.Init(parent);auto previous=ui::colors;ui::colors={RGB(20,20,20),RGB(30,30,30),RGB(240,240,240),RGB(150,150,150),RGB(60,60,60),RGB(30,160,230),RGB(0,0,0),RGB(40,40,40),RGB(80,80,80)};
    HWND slider=ui::Control(parent,surface,TRACKBAR_CLASSW,L"test",TBS_NOTICKS,42);Check(slider!=nullptr,"Progress render slider failed");MoveWindow(slider,0,0,320,40,FALSE);SendMessageW(slider,TBM_SETRANGE,TRUE,MAKELPARAM(0,100));SendMessageW(slider,TBM_SETPOS,TRUE,20);ui::SetSliderActual(slider,80,false);
    HDC dc=CreateCompatibleDC(nullptr);BITMAPINFO format{};format.bmiHeader={sizeof(BITMAPINFOHEADER),320,-40,1,32,BI_RGB};void* bits=nullptr;HBITMAP bitmap=CreateDIBSection(dc,&format,DIB_RGB_COLORS,&bits,nullptr,0);Check(bitmap!=nullptr,"Progress render surface failed");auto old=SelectObject(dc,bitmap);
    SendMessageW(slider,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(dc),PRF_CLIENT);RECT thumb{},channel{};SendMessageW(slider,TBM_GETTHUMBRECT,0,reinterpret_cast<LPARAM>(&thumb));SendMessageW(slider,TBM_GETCHANNELRECT,0,reinterpret_cast<LPARAM>(&channel));
    int sampleX=channel.left+(channel.right-channel.left)*60/100,sampleY=(thumb.top+thumb.bottom)/2;Check(GetPixel(dc,sampleX,sampleY)==ui::colors.accent,"Blue confirmed line snapped down to the thumb before the monitor changed");
    SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);DestroyWindow(parent);ui::colors=previous;
}
int main(){TrayControlTestAccess::Run();CheckAntialiasing();CheckPopupPlacement();CheckSunMotion();CheckConfirmedMotion();CheckDescendingProgressRendering();CheckFrameClock();CheckBufferResources();CheckSmallTrayRays();CheckSunResources();std::cout<<"PASS: tray wheel handling and antialiased geometry and 1000 DPI-sized sun frames without resource leaks\n";}
