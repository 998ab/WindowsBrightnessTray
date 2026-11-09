#include "localization.h"
#include "version.h"
#include <windows.h>
#include <shellapi.h>
#include <commctrl.h>
#include <algorithm>
#include <map>
#include <string>
#include <vector>
#include <array>
#include <cmath>
#include "calibration.h"
#include "tray.h"
#include "preferences.h"
#include "brightness_trace.h"

#pragma comment(linker,"/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
namespace {
constexpr UINT Result = WM_APP+1, Tray = WM_APP+2, Wheel=WM_APP+4;
constexpr int Sync=10, Refresh=11, Exit=12, SliderBase=1000, CalibrateBase=2000, ResetBase=3000;
constexpr int BrightnessPage=20,SettingsPage=21,SpeedPresetBase=23,WheelPresetBase=27,MatchSettings=30,LanguageEn=31,LanguageRu=32,OptionsBase=4000;
constexpr int WheelPresets[]={1,2,5};
const wchar_t* ResponseLabels[]={L"Instant",L"Quick",L"Smoother"};
HWND window{}, list{};
ui::Surface surface;
MonitorWorker worker;
CalibrationWindow calibration;
HICON trayIcon{},appIcon{};
constexpr UINT_PTR TrayAnimationTimer=2;
constexpr size_t TrayFrameCount=48;
std::array<HICON,TrayFrameCount> trayFrames{};
std::map<std::wstring,unsigned long long> animatedWrites;
std::map<std::wstring,int> requestedLevels;
int trayDirection=1;
bool trayAnimating=false;
bool trayCoasting=false;
size_t trayFrame=0;
double trayPosition=0,coastFrom=0,coastTarget=0,coastDuration=0;
ULONGLONG trayAnimationTick=0,coastStarted=0;
TrayControl trayControl;
size_t activeMonitor=0;
UINT taskbarCreated{};
std::wstring settings, status=tr(L"Detecting monitors…");
std::vector<Monitor> monitors;
std::map<std::wstring,unsigned long long> latestWrites;
unsigned long long scanTag{};
bool scanning=true, syncing=false, demo=false, tracing=false;
bool settingsPage=false;
Preferences preferences;
int scrollOffset=0;
void TrayIcon(DWORD operation);
void Layout();
void ClearTrayFrames(){for(auto& icon:trayFrames){if(icon)DestroyIcon(icon);icon=nullptr;}}
void PrepareTrayFrames(){if(trayFrames[1])return;for(size_t i=1;i<TrayFrameCount;++i)trayFrames[i]=ui::CreateTrayIcon(true,i*6.283185307179586/TrayFrameCount);}
void AdvanceTrayAnimation(ULONGLONG now) {
    if(!trayAnimating)return;
    if(trayCoasting) {
        double t=std::clamp((now-coastStarted)/coastDuration,0.0,1.0);
        trayPosition=coastFrom+(coastTarget-coastFrom)*(2*t-t*t);
        if(t>=1){trayAnimating=false;trayCoasting=false;KillTimer(window,TrayAnimationTimer);}
    }else trayPosition+=trayDirection*static_cast<double>(now-trayAnimationTick)/40.0;
    trayAnimationTick=now;
    auto frame=std::llround(trayPosition)%static_cast<long long>(TrayFrameCount);
    trayFrame=static_cast<size_t>(frame<0?frame+TrayFrameCount:frame);
    if(!trayAnimating)trayPosition=static_cast<double>(trayFrame);
}
void SetTrayDirection(int direction) {
    if(direction==trayDirection)return;
    auto now=GetTickCount64();AdvanceTrayAnimation(now);
    trayDirection=direction;trayCoasting=false;trayAnimationTick=now;
}
void UpdateTrayAnimation() {
    bool busy=!latestWrites.empty()||!animatedWrites.empty();
    auto now=GetTickCount64();
    if(busy) {
        if(trayAnimating&&!trayCoasting)return;
        AdvanceTrayAnimation(now);PrepareTrayFrames();
        trayAnimating=true;trayCoasting=false;trayAnimationTick=now;
        SetTimer(window,TrayAnimationTimer,40,nullptr);
    }else {
        if(!trayAnimating||trayCoasting)return;
        AdvanceTrayAnimation(now);coastFrom=trayPosition;
        constexpr double symmetry=TrayFrameCount/3.0;
        coastTarget=trayDirection>0?(std::floor(coastFrom/symmetry)+1)*symmetry
            :(std::ceil(coastFrom/symmetry)-1)*symmetry;
        // Leave enough angular distance for visible deceleration. If already
        // almost aligned, finish at the following equivalent 120-degree pose.
        if(trayDirection*(coastTarget-coastFrom)<3)coastTarget+=trayDirection*symmetry;
        coastDuration=2*std::abs(coastTarget-coastFrom)*40;
        coastStarted=now;trayCoasting=true;
    }
    TrayIcon(NIM_MODIFY);
}

std::wstring ResponseText(){return tr(ResponseLabels[preferences.responseMode]);}
void Mark(HWND button,bool selected) {
    if(selected)SetPropW(button,L"Accent",reinterpret_cast<HANDLE>(1));else RemovePropW(button,L"Accent");
    InvalidateRect(button,nullptr,FALSE);
}
void UpdatePreferencesControls() {
    for(int i=0;i<3;++i)Mark(GetDlgItem(window,SpeedPresetBase+i),preferences.responseMode==i);
    for(int i=0;i<3;++i)Mark(GetDlgItem(window,WheelPresetBase+i),preferences.wheelStep==WheelPresets[i]);
    Mark(GetDlgItem(window,LanguageEn),!i18n::useRussian.load());Mark(GetDlgItem(window,LanguageRu),i18n::useRussian.load());
    InvalidateRect(window,nullptr,FALSE);
}
void SavePreferences() {
    preferences.Normalize();worker.SetTransitionDuration(preferences.transitionMs);
    worker.SetHardwareStep(preferences.hardwareStep);
    bool saved=WritePrivateProfileStringW(L"General",L"TransitionMs",std::to_wstring(preferences.transitionMs).c_str(),settings.c_str())!=FALSE;
    saved=(WritePrivateProfileStringW(L"General",L"ResponseMode",std::to_wstring(preferences.responseMode).c_str(),settings.c_str())!=FALSE)&&saved;
    saved=(WritePrivateProfileStringW(L"General",L"WheelStep",std::to_wstring(preferences.wheelStep).c_str(),settings.c_str())!=FALSE)&&saved;
    saved=(WritePrivateProfileStringW(L"General",L"HardwareStep",std::to_wstring(preferences.hardwareStep).c_str(),settings.c_str())!=FALSE)&&saved;
    saved=(WritePrivateProfileStringW(L"General",L"Language",i18n::Language(),settings.c_str())!=FALSE)&&saved;
    UpdatePreferencesControls();TrayIcon(NIM_MODIFY);
    status=saved?tr(L"Settings saved. Changes are already active."):tr(L"Changes are active, but settings could not be saved.");
    InvalidateRect(window,nullptr,FALSE);
}
void SetPage(bool value) {
    if(calibration.Handle())return;
    settingsPage=value;
    Mark(GetDlgItem(window,BrightnessPage),!value);Mark(GetDlgItem(window,SettingsPage),value);
    ShowWindow(list,value?SW_HIDE:SW_SHOW);ShowWindow(GetDlgItem(window,Sync),value?SW_HIDE:SW_SHOW);
    ShowWindow(GetDlgItem(window,Refresh),value?SW_HIDE:SW_SHOW);
    for(int id=SpeedPresetBase;id<=LanguageRu;++id)ShowWindow(GetDlgItem(window,id),value?SW_SHOW:SW_HIDE);
    Layout();
    if(GetActiveWindow()==window)SetFocus(GetDlgItem(window,value?SettingsPage:BrightnessPage));
}

RECT Box(int x,int y,int width,int height) { return {surface.S(x),surface.S(y),surface.S(x+width),surface.S(y+height)}; }
int Width(HWND hwnd) { RECT r; GetClientRect(hwnd,&r); return MulDiv(r.right,96,static_cast<int>(surface.dpi)); }
int Height(HWND hwnd) { RECT r; GetClientRect(hwnd,&r); return MulDiv(r.bottom,96,static_cast<int>(surface.dpi)); }
void Status(std::wstring value) { status=std::move(value); InvalidateRect(window,nullptr,FALSE); }
int HardwarePercent(const Monitor& m) { return FromRaw(m.current,m.minimum,m.maximum); }
int LogicalPercent(const Monitor& m) { return ToLogicalPercent(HardwarePercent(m),m.calibration); }
bool Save(const Monitor& m, Calibration c) {
    if (!c.Valid()) return false;
    std::wstring section=L"Min="+std::to_wstring(c.low);
    section.push_back(L'\0'); section+=L"Mid="+std::to_wstring(c.middle);
    section.push_back(L'\0'); section+=L"Max="+std::to_wstring(c.high);
    section.push_back(L'\0'); section+=L"Point25="+std::to_wstring(c.quarter);
    section.push_back(L'\0'); section+=L"Point75="+std::to_wstring(c.threeQuarter);
    section.push_back(L'\0'); section.push_back(L'\0');
    return WritePrivateProfileSectionW(m.key.c_str(),section.c_str(),settings.c_str())!=FALSE;
}
bool SaveGroup(const std::vector<std::wstring>& keys,const std::vector<Calibration>& curves) {
    if(keys.size()!=curves.size())return false;
    WritePrivateProfileStringW(nullptr,nullptr,nullptr,settings.c_str());
    std::wstring temporary=settings+L".pending-"+std::to_wstring(GetCurrentProcessId());
    if(GetFileAttributesW(settings.c_str())!=INVALID_FILE_ATTRIBUTES && !CopyFileW(settings.c_str(),temporary.c_str(),FALSE))return false;
    for(size_t i=0;i<keys.size();++i) {
        const auto& c=curves[i];if(!c.Valid()||!c.Matched()){DeleteFileW(temporary.c_str());return false;}
        std::wstring data;
        for(auto pair:std::vector<std::pair<std::wstring,int>>{{L"Min",c.low},{L"Mid",c.middle},{L"Max",c.high},{L"Point25",c.quarter},{L"Point75",c.threeQuarter}}) {
            data+=pair.first+L"="+std::to_wstring(pair.second);data.push_back(L'\0');
        }
        data.push_back(L'\0');
        if(!WritePrivateProfileSectionW(keys[i].c_str(),data.c_str(),temporary.c_str())){DeleteFileW(temporary.c_str());return false;}
    }
    WritePrivateProfileStringW(nullptr,nullptr,nullptr,temporary.c_str());
    if(!MoveFileExW(temporary.c_str(),settings.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){DeleteFileW(temporary.c_str());return false;}
    WritePrivateProfileStringW(nullptr,nullptr,nullptr,settings.c_str());return true;
}
void Load(Monitor& m) {
    auto& c=m.calibration;
    c.low=static_cast<int>(GetPrivateProfileIntW(m.key.c_str(),L"Min",0,settings.c_str()));
    c.high=static_cast<int>(GetPrivateProfileIntW(m.key.c_str(),L"Max",100,settings.c_str()));
    c.middle=static_cast<int>(GetPrivateProfileIntW(m.key.c_str(),L"Mid",(c.low+c.high)/2,settings.c_str()));
    c.quarter=static_cast<int>(GetPrivateProfileIntW(m.key.c_str(),L"Point25",-1,settings.c_str()));
    c.threeQuarter=static_cast<int>(GetPrivateProfileIntW(m.key.c_str(),L"Point75",-1,settings.c_str()));
    if(!c.Valid()) c={};
}
Command WriteCommand(const Monitor& m,int logical) {
    requestedLevels[m.key]=logical;
    auto tag=NextTag(); latestWrites[m.key]=tag;
    DWORD raw=ToRaw(ToHardwarePercent(logical,m.calibration),m.minimum,m.maximum);
    brightness_trace::Log("input",tag,logical,raw,preferences.transitionMs);
    return {Operation::Write,m.key,raw,tag,true,logical,m.calibration};
}
void LayoutRows() {
    if(!list) return;
    int w=Width(list), h=Height(list), total=static_cast<int>(monitors.size())*112;
    scrollOffset=std::clamp(scrollOffset,0,std::max(0,total-h));
    SCROLLINFO si{sizeof(si),SIF_RANGE|SIF_PAGE|SIF_POS,0,std::max(0,total-1),static_cast<UINT>(h),scrollOffset,0};
    SetScrollInfo(list,SB_VERT,&si,TRUE);
    for(size_t i=0;i<monitors.size();++i) {
        int y=static_cast<int>(i)*112-scrollOffset;
        ui::Place(GetDlgItem(list,SliderBase+static_cast<int>(i)),surface,30,y+43,w-128,36);
        ui::Place(GetDlgItem(list,OptionsBase+static_cast<int>(i)),surface,w-84,y+12,54,28);
    }
    InvalidateRect(list,nullptr,FALSE);
}
void RebuildRows() {
    requestedLevels.clear();
    std::vector<HWND> children;
    for(HWND c=GetWindow(list,GW_CHILD);c;c=GetWindow(c,GW_HWNDNEXT)) children.push_back(c);
    for(HWND c:children) DestroyWindow(c);
    for(size_t i=0;i<monitors.size();++i) {
        HWND slider=ui::Control(list,surface,TRACKBAR_CLASSW,tr(L"Brightness: ")+monitors[i].name,TBS_NOTICKS|WS_TABSTOP,SliderBase+static_cast<int>(i),true);
        SendMessageW(slider,TBM_SETRANGE,TRUE,MAKELPARAM(0,100));
        SendMessageW(slider,TBM_SETPOS,TRUE,LogicalPercent(monitors[i]));
        ui::Button(list,surface,tr(L"More"),OptionsBase+static_cast<int>(i),false,true);
        for(int id:{SliderBase,OptionsBase}) EnableWindow(GetDlgItem(list,id+static_cast<int>(i)),monitors[i].supported&&!scanning);
    }
    LayoutRows();
}
void Layout() {
    if(!surface.body || !list) return;
    int w=Width(window),h=Height(window);
    int tabWidth=(w-56)/2;
    ui::Place(GetDlgItem(window,BrightnessPage),surface,24,100,tabWidth,34);
    ui::Place(GetDlgItem(window,SettingsPage),surface,32+tabWidth,100,tabWidth,34);
    ui::Place(GetDlgItem(window,Sync),surface,24,154,w-48,58);
    ui::Place(list,surface,12,254,w-24,std::max(0,h-334));
    int presetWidth=(w-96)/3;
    for(int i=0;i<3;++i)ui::Place(GetDlgItem(window,SpeedPresetBase+i),surface,40+i*(presetWidth+8),214,presetWidth,36);
    for(int i=0;i<3;++i)ui::Place(GetDlgItem(window,WheelPresetBase+i),surface,w-204+i*52,352,48,32);
    ui::Place(GetDlgItem(window,MatchSettings),surface,w-174,448,132,34);
    ui::Place(GetDlgItem(window,LanguageEn),surface,w-242,524,98,30);
    ui::Place(GetDlgItem(window,LanguageRu),surface,w-136,524,98,30);
    ui::Place(GetDlgItem(window,Refresh),surface,w-234,h-54,108,32);
    ui::Place(GetDlgItem(window,Exit),surface,w-114,h-54,90,32);
    LayoutRows(); InvalidateRect(window,nullptr,FALSE);
}
void Scan() {
    calibration.Cancel(); scanning=true; scanTag=NextTag(); latestWrites.clear();
    animatedWrites.clear();UpdateTrayAnimation();
    worker.Queue({Operation::Scan,L"",0,scanTag});
    EnableWindow(GetDlgItem(window,Refresh),FALSE);
    for(HWND c=GetWindow(list,GW_CHILD);c;c=GetWindow(c,GW_HWNDNEXT)) EnableWindow(c,FALSE);
    Status(tr(L"Detecting monitors…"));
}
void Apply(size_t source) {
    if(scanning || calibration.Handle() || source>=monitors.size()) return;
    activeMonitor=source;
    int value=static_cast<int>(SendDlgItemMessageW(list,SliderBase+static_cast<int>(source),TBM_GETPOS,0,0));
    auto previous=requestedLevels.find(monitors[source].key);
    int prior=previous==requestedLevels.end()?LogicalPercent(monitors[source]):previous->second;
    if(value!=prior)SetTrayDirection(value>prior?1:-1);
    std::vector<Command> batch;
    for(size_t i=0;i<monitors.size();++i) if(monitors[i].supported&&(syncing||i==source)) {
        SendDlgItemMessageW(list,SliderBase+static_cast<int>(i),TBM_SETPOS,TRUE,value);
        batch.push_back(WriteCommand(monitors[i],value));
    }
    worker.QueueBatch(std::move(batch));
    UpdateTrayAnimation();
    TrayIcon(NIM_MODIFY);
    InvalidateRect(list,nullptr,FALSE); Status(tr(L"Applying brightness…"));
}
void TrayValue(int value,bool relative) {
    if(scanning||calibration.Handle()) {trayControl.Toast(tr(L"Brightness"),scanning?tr(L"Detecting monitors…"):tr(L"Finish calibration first"));return;}
    if(activeMonitor>=monitors.size()||!monitors[activeMonitor].supported) {
        auto it=std::find_if(monitors.begin(),monitors.end(),[](const Monitor& m){return m.supported;});
        if(it==monitors.end())return;activeMonitor=static_cast<size_t>(it-monitors.begin());
    }
    int id=SliderBase+static_cast<int>(activeMonitor);
    int current=static_cast<int>(SendDlgItemMessageW(list,id,TBM_GETPOS,0,0));
    int next=std::clamp(relative?current+value:value,0,100);
    SendDlgItemMessageW(list,id,TBM_SETPOS,TRUE,next);Apply(activeMonitor);
    trayControl.Toast(syncing?tr(L"All monitors"):monitors[activeMonitor].name,std::to_wstring(next)+L"%");
}
void DrawMonitor(HDC dc,int x,int y) {
    ui::Round(dc,Box(x,y,26,18),ui::colors.card,ui::colors.accent,surface.S(4));
    ui::Fill(dc,Box(x+11,y+19,4,3),ui::colors.accent); ui::Fill(dc,Box(x+6,y+23,14,2),ui::colors.accent);
}
void PaintList(HDC dc) {
    RECT rect; GetClientRect(list,&rect); ui::Fill(dc,rect,ui::colors.background);
    int w=Width(list),h=Height(list);
    for(size_t i=0;i<monitors.size();++i) {
        int y=static_cast<int>(i)*112-scrollOffset;
        if(y+104<0 || y>h) continue;
        const auto& m=monitors[i];
        ui::Round(dc,Box(12,y,w-24,104),ui::colors.card,ui::colors.border,surface.S(10));
        DrawMonitor(dc,30,y+20);
        ui::Text(dc,m.name,Box(70,y+17,w-168,24),surface.body,ui::colors.text,DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
        int value=static_cast<int>(SendDlgItemMessageW(list,SliderBase+static_cast<int>(i),TBM_GETPOS,0,0));
        ui::Text(dc,m.supported ? std::to_wstring(value)+L"%" : L"—",Box(w-85,y+43,58,38),surface.number,ui::colors.text,DT_RIGHT|DT_SINGLELINE);
        const auto& c=m.calibration;
        std::wstring range=m.supported ? (c.Matched()?tr(L"Matched"):tr(L"Not matched")) : tr(L"DDC/CI unavailable");
        ui::Text(dc,range,Box(30,y+82,w-60,18),surface.caption,ui::colors.muted,DT_SINGLELINE|DT_END_ELLIPSIS);
    }
    if(monitors.empty()) {
        ui::Text(dc,scanning ? tr(L"Looking for your monitors…") : tr(L"No monitors found"),Box(24,28,w-48,40),surface.heading,ui::colors.text);
        ui::Text(dc,tr(L"Enable DDC/CI in your monitor menu\nand click Refresh."),Box(24,78,w-48,70),surface.body,ui::colors.muted);
    }
}
void PaintMain(HDC dc) {
    RECT rect; GetClientRect(window,&rect); ui::Fill(dc,rect,ui::colors.background);
    int w=Width(window),h=Height(window);
    ui::Text(dc,settingsPage?tr(L"Settings"):tr(L"Brightness"),Box(24,20,w-48,42),surface.title,ui::colors.text);
    std::wstring subtitle=settingsPage?tr(L"Choose how brightness responds."):tr(L"Response: ")+ResponseText()+tr(L" · Wheel ±")+std::to_wstring(preferences.wheelStep)+L"%";
    if(demo)subtitle=tr(L"Demo · physical monitors are not changed");
    if(tracing)subtitle+=tr(L" · Diagnostics");
    ui::Text(dc,subtitle,Box(24,66,w-48,22),surface.body,ui::colors.muted);
    if(settingsPage) {
        ui::Round(dc,Box(24,154,w-48,156),ui::colors.card,ui::colors.border,surface.S(10));
        ui::Text(dc,tr(L"Brightness response"),Box(40,172,w-80,28),surface.heading,ui::colors.text);
        const wchar_t* hints[]={tr(L"Apply the final brightness without intermediate steps."),tr(L"Respond quickly to slider movements."),tr(L"Smaller steps; large changes take longer.\nBacklight steps may still be visible.")};
        ui::Text(dc,hints[preferences.responseMode],Box(40,264,w-80,36),surface.caption,ui::colors.muted);
        ui::Round(dc,Box(24,322,w-48,86),ui::colors.card,ui::colors.border,surface.S(10));
        ui::Text(dc,tr(L"Tray mouse wheel"),Box(40,338,w-256,26),surface.heading,ui::colors.text);
        ui::Text(dc,tr(L"Brightness step. Ctrl: 1%."),Box(40,371,w-256,22),surface.caption,ui::colors.muted);
        ui::Round(dc,Box(24,420,w-48,88),ui::colors.card,ui::colors.border,surface.S(10));
        ui::Text(dc,tr(L"Match displays"),Box(40,436,w-230,26),surface.heading,ui::colors.text);
        ui::Text(dc,tr(L"Common range and brightness"),Box(40,470,w-230,22),surface.caption,ui::colors.muted);
        ui::Text(dc,tr(L"Language"),Box(40,529,w-300,24),surface.body,ui::colors.text);
    }else ui::Text(dc,tr(L"MONITORS  ·  ")+std::to_wstring(monitors.size()),Box(26,230,w-52,20),surface.caption,ui::colors.muted);
    ui::Text(dc,status,Box(24,h-76,w-48,20),surface.caption,ui::colors.muted,DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
    ui::Text(dc,settingsPage?std::wstring(tr(L"Saved automatically")):std::wstring(L"BrightnessTray ")+BRIGHTNESSTRAY_VERSION_W,Box(24,h-46,w-170,24),surface.caption,ui::colors.muted);
}
void BufferedPaint(HWND hwnd,void (*paint)(HDC)) {
    PAINTSTRUCT ps; HDC dc=BeginPaint(hwnd,&ps); RECT r; GetClientRect(hwnd,&r);
    if(surface.body&&r.right>0&&r.bottom>0) {
        HDC buffer=CreateCompatibleDC(dc); HBITMAP bitmap=CreateCompatibleBitmap(dc,r.right,r.bottom);
        auto old=SelectObject(buffer,bitmap); paint(buffer); BitBlt(dc,0,0,r.right,r.bottom,buffer,0,0,SRCCOPY);
        SelectObject(buffer,old); DeleteObject(bitmap); DeleteDC(buffer);
    }
    EndPaint(hwnd,&ps);
}
void TrayIcon(DWORD operation) {
    NOTIFYICONDATAW icon{};icon.cbSize=sizeof(icon);icon.hWnd=window;icon.uID=1;
    icon.uFlags=NIF_MESSAGE|NIF_ICON|NIF_TIP;icon.uCallbackMessage=Tray;
    icon.hIcon=trayFrame&&trayFrames[trayFrame]?trayFrames[trayFrame]:trayIcon;
    std::wstring tip=tr(L"BrightnessTray · wheel ±")+std::to_wstring(preferences.wheelStep)+L"% · Ctrl ±1%";
    if(activeMonitor<monitors.size()) {
        int value=static_cast<int>(SendDlgItemMessageW(list,SliderBase+static_cast<int>(activeMonitor),TBM_GETPOS,0,0));
        tip=std::to_wstring(value)+L"% · "+(syncing?tr(L"All monitors"):monitors[activeMonitor].name)+tr(L"\nWheel ±")+std::to_wstring(preferences.wheelStep)+L"% · Ctrl ±1%";
    }
    wcsncpy_s(icon.szTip,tip.c_str(),_TRUNCATE);Shell_NotifyIconW(operation,&icon);
}
void Show() {
    if(calibration.Handle()) { SetForegroundWindow(calibration.Handle()); return; }
    POINT cursor;GetCursorPos(&cursor);MONITORINFO info{sizeof(info)};
    GetMonitorInfoW(MonitorFromPoint(cursor,MONITOR_DEFAULTTONEAREST),&info);
    RECT r;GetWindowRect(window,&r);
    SetWindowPos(window,HWND_TOP,std::max(info.rcWork.left,info.rcWork.right-(r.right-r.left)-12),
        std::max(info.rcWork.top,info.rcWork.bottom-(r.bottom-r.top)-12),0,0,SWP_NOSIZE|SWP_SHOWWINDOW);
    SetForegroundWindow(window);
}
void HandleEvents() {
    for(const auto& event:worker.Events()) {
        if(event.operation==Operation::Write) {
            if(!event.final)animatedWrites[event.key]=event.tag;
            else {auto it=animatedWrites.find(event.key);if(it!=animatedWrites.end()&&it->second<=event.tag)animatedWrites.erase(it);}
        }
        calibration.OnEvent(event);
        if(event.operation==Operation::Scan) {
            if(event.tag!=scanTag) continue;
            animatedWrites.clear();
            monitors=event.monitors;for(auto& m:monitors)Load(m);
            scanning=false;EnableWindow(GetDlgItem(window,Refresh),TRUE);RebuildRows();
            if(activeMonitor>=monitors.size())activeMonitor=0;TrayIcon(NIM_MODIFY);
            Status(monitors.empty() ? tr(L"Connect an external monitor and refresh the list.") : tr(L"Ready. Brightness changes only when you adjust it."));
        } else {
            auto it=std::find_if(monitors.begin(),monitors.end(),[&](const Monitor& m){return m.key==event.key;});
            if(event.operation==Operation::Write&&event.ok&&calibration.Handle()&&latestWrites.empty()&&it!=monitors.end()&&event.raw!=it->current)
                SetTrayDirection(event.raw>it->current?1:-1);
            if(it!=monitors.end()&&event.ok) it->current=event.raw;
            if(event.operation==Operation::Write&&!event.final)continue;
            auto pending=latestWrites.find(event.key);
            const bool tracked=pending!=latestWrites.end();
            const bool matching=event.operation==Operation::Write&&tracked&&pending->second==event.tag;
            if(matching)latestWrites.erase(pending);
            if(!calibration.Handle()&&!scanning) {
                if(matching) {
                    if(!event.ok)Status(tr(L"The monitor did not respond. Try again or refresh the list."));
                    else if(latestWrites.empty())Status(tr(L"Brightness applied."));
                }
                if(event.operation==Operation::Write && !event.ok && !tracked)
                    Status(tr(L"The monitor did not respond. Refresh and check its level."));
                if(event.ok&&it!=monitors.end()) {
                    size_t index=static_cast<size_t>(it-monitors.begin());
                    if(!matching&&!latestWrites.count(event.key)) SendDlgItemMessageW(list,SliderBase+static_cast<int>(index),TBM_SETPOS,TRUE,LogicalPercent(*it));
                    InvalidateRect(list,nullptr,FALSE);
                    TrayIcon(NIM_MODIFY);
                }
            }
        }
    }
    UpdateTrayAnimation();
}
LRESULT CALLBACK ListProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
    switch(msg) {
    case WM_PAINT:BufferedPaint(hwnd,PaintList);return 0;
    case WM_ERASEBKGND:return 1;
    case WM_SIZE:LayoutRows();return 0;
    case WM_COMMAND:case WM_NOTIFY:case WM_HSCROLL:case WM_DRAWITEM:
        return SendMessageW(window,msg,wp,lp);
    case WM_CTLCOLORSTATIC:case WM_CTLCOLORBTN:return ui::ColorControl(msg,wp,lp);
    case WM_MOUSEWHEEL:scrollOffset-=GET_WHEEL_DELTA_WPARAM(wp)/WHEEL_DELTA*48;LayoutRows();return 0;
    case WM_VSCROLL: {
        SCROLLINFO si{sizeof(si),SIF_ALL};GetScrollInfo(hwnd,SB_VERT,&si);
        int command=LOWORD(wp);
        if(command==SB_THUMBTRACK)scrollOffset=si.nTrackPos;
        if(command==SB_LINEUP)scrollOffset-=24;
        if(command==SB_LINEDOWN)scrollOffset+=24;
        if(command==SB_PAGEUP)scrollOffset-=static_cast<int>(si.nPage);
        if(command==SB_PAGEDOWN)scrollOffset+=static_cast<int>(si.nPage);
        LayoutRows();return 0;
    }
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}
LRESULT CALLBACK WindowProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
    if(taskbarCreated && msg==taskbarCreated){TrayIcon(NIM_ADD);trayControl.InvalidatePosition();return 0;}
    switch(msg) {
    case WM_CLOSE:ShowWindow(hwnd,SW_HIDE);return 0;
    case WM_PAINT:BufferedPaint(hwnd,PaintMain);return 0;
    case WM_ERASEBKGND:return 1;
    case WM_SIZE:Layout();return 0;
    case WM_GETMINMAXINFO:reinterpret_cast<MINMAXINFO*>(lp)->ptMinTrackSize={surface.S(490),surface.S(680)};return 0;
    case WM_HSCROLL: {
        if(lp){int id=GetDlgCtrlID(reinterpret_cast<HWND>(lp));
            if(id>=SliderBase&&id<CalibrateBase)Apply(static_cast<size_t>(id-SliderBase));}
        return 0;
    }
    case WM_COMMAND: {
        int id=LOWORD(wp);
        if(id==IDCANCEL){ShowWindow(hwnd,SW_HIDE);return 0;}
        if(id==BrightnessPage||id==SettingsPage){SetPage(id==SettingsPage);return 0;}
        if(id==LanguageEn||id==LanguageRu) {
            i18n::SetLanguage(id==LanguageEn?L"en":L"ru");
            SetDlgItemTextW(window,Sync,syncing?tr(L"Monitor synchronization: on"):tr(L"Monitor synchronization: off"));
            for(auto item:std::vector<std::pair<int,const wchar_t*>>{{Refresh,L"Refresh"},{Exit,L"Exit"},{BrightnessPage,L"Brightness"},{SettingsPage,L"Settings"},{MatchSettings,L"Configure"},{LanguageEn,L"English"},{LanguageRu,L"Russian"}})
                SetDlgItemTextW(window,item.first,tr(item.second));
            for(int i=0;i<3;++i)SetDlgItemTextW(window,SpeedPresetBase+i,tr(ResponseLabels[i]));
            SetWindowTextW(list,tr(L"Monitors"));SavePreferences();Scan();return 0;
        }
        if(id>=SpeedPresetBase&&id<SpeedPresetBase+3){preferences.SetResponse(id-SpeedPresetBase);SavePreferences();return 0;}
        if(id>=WheelPresetBase&&id<WheelPresetBase+3){preferences.wheelStep=WheelPresets[id-WheelPresetBase];SavePreferences();return 0;}
        if(id==MatchSettings) {
            size_t reference=activeMonitor;
            if(reference>=monitors.size()||!monitors[reference].supported){reference=0;while(reference<monitors.size()&&!monitors[reference].supported)++reference;}
            if(reference<monitors.size())SendMessageW(hwnd,WM_COMMAND,CalibrateBase+reference,0);
            else Status(tr(L"Connect at least two DDC/CI monitors to match brightness."));return 0;
        }
        if(id>=OptionsBase&&id<OptionsBase+static_cast<int>(monitors.size())) {
            int index=id-OptionsBase;activeMonitor=static_cast<size_t>(index);RECT r;GetWindowRect(GetDlgItem(list,id),&r);
            HMENU menu=CreatePopupMenu();AppendMenuW(menu,MF_STRING,1,tr(L"Match displays · use as reference"));
            AppendMenuW(menu,MF_STRING,2,tr(L"Reset this display calibration"));
            int action=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTALIGN,r.right,r.bottom,0,hwnd,nullptr);DestroyMenu(menu);
            if(action)SendMessageW(hwnd,WM_COMMAND,(action==1?CalibrateBase:ResetBase)+index,0);return 0;
        }
        if(id==Sync) {
            syncing=!syncing;WritePrivateProfileStringW(L"General",L"Sync",syncing?L"1":L"0",settings.c_str());
            SetDlgItemTextW(window,Sync,syncing?tr(L"Monitor synchronization: on"):tr(L"Monitor synchronization: off"));
            InvalidateRect(GetDlgItem(window,Sync),nullptr,FALSE);
        }
        if(id==Refresh)Scan();
        if(id==Exit){calibration.Cancel();DestroyWindow(hwnd);}
        if(id>=CalibrateBase&&id<ResetBase&&!scanning) {
            size_t index=static_cast<size_t>(id-CalibrateBase);
            if(index<monitors.size()&&monitors[index].supported) {
                std::vector<Monitor> selected;std::vector<std::wstring> keys;size_t reference=0;
                for(size_t i=0;i<monitors.size();++i)if(monitors[i].supported){if(i==index)reference=selected.size();selected.push_back(monitors[i]);keys.push_back(monitors[i].key);}
                if(selected.size()<2){Status(tr(L"Connect at least two DDC/CI monitors to match brightness."));return 0;}
                calibration.Open(window,selected,reference,worker,[keys](const std::vector<Calibration>& curves){
                    if(!SaveGroup(keys,curves))return false;
                    for(size_t i=0;i<keys.size();++i)for(auto& m:monitors)if(m.key==keys[i])m.calibration=curves[i];
                    RebuildRows();Status(tr(L"Matched curves saved for all displays: 0 / 25 / 50 / 75 / 100%."));return true;
                });
            }
        }
        if(id>=ResetBase&&id<ResetBase+1000&&!scanning) {
            size_t index=static_cast<size_t>(id-ResetBase);
            if(index<monitors.size()&&Save(monitors[index],{})) {
                monitors[index].calibration={};RebuildRows();Status(tr(L"Calibration reset. Monitor brightness was not changed."));
            }
        }
        return 0;
    }
    case WM_NOTIFY:return ui::SliderDraw(reinterpret_cast<NMHDR*>(lp),surface);
    case WM_DRAWITEM: {
        auto& item=*reinterpret_cast<DRAWITEMSTRUCT*>(lp);
        if(item.CtlID==Sync) {
            ui::Fill(item.hDC,item.rcItem,ui::colors.background);
            RECT r=item.rcItem;InflateRect(&r,-1,-1);ui::Round(item.hDC,r,ui::colors.card,ui::colors.border,surface.S(10));
            int w=MulDiv(r.right-r.left,96,static_cast<int>(surface.dpi));
            auto local=[&](int x,int y,int width,int height){return RECT{r.left+surface.S(x),r.top+surface.S(y),r.left+surface.S(x+width),r.top+surface.S(y+height)};};
            ui::Text(item.hDC,tr(L"Synchronization"),local(16,9,w-92,22),surface.body,ui::colors.text);
            ui::Text(item.hDC,tr(L"One level for all monitors"),local(16,32,w-92,18),surface.caption,ui::colors.muted);
            ui::Round(item.hDC,local(w-64,20,40,20),syncing?ui::colors.accent:ui::colors.track,syncing?ui::colors.accent:ui::colors.track,surface.S(20));
            ui::Round(item.hDC,local(w-61+(syncing?20:0),23,14,14),syncing?ui::colors.onAccent:ui::colors.card,syncing?ui::colors.onAccent:ui::colors.card,surface.S(14));
            if(item.itemState&ODS_FOCUS){InflateRect(&r,-4,-4);DrawFocusRect(item.hDC,&r);}return TRUE;
        }
        if(ui::DrawButton(item,surface))return TRUE;
        break;
    }
    case WM_CTLCOLORSTATIC:case WM_CTLCOLORBTN:return ui::ColorControl(msg,wp,lp);
    case Result:HandleEvents();return 0;
    case Wheel:{int change=trayControl.TakeWheel(static_cast<int>(static_cast<INT_PTR>(wp)),lp!=0,preferences.wheelStep);if(change)TrayValue(change,true);return 0;}
    case WM_MOUSEWHEEL:{if(settingsPage)return 0;int change=trayControl.TakeWheel(GET_WHEEL_DELTA_WPARAM(wp),(GET_KEYSTATE_WPARAM(wp)&MK_CONTROL)!=0,preferences.wheelStep);if(change)TrayValue(change,true);return 0;}
    case WM_CONTEXTMENU:SendMessageW(hwnd,Tray,0,WM_RBUTTONUP);return 0;
    case WM_DISPLAYCHANGE:trayControl.InvalidatePosition();calibration.Cancel();SetTimer(hwnd,1,1500,nullptr);return 0;
    case WM_POWERBROADCAST:if(wp==PBT_APMRESUMEAUTOMATIC){calibration.Cancel();SetTimer(hwnd,1,1500,nullptr);}return TRUE;
    case WM_TIMER:
        if(wp==TrayAnimationTimer){if(trayAnimating){AdvanceTrayAnimation(GetTickCount64());TrayIcon(NIM_MODIFY);}return 0;}
        if(wp==1){KillTimer(hwnd,1);Scan();}return 0;
    case WM_SETTINGCHANGE:case WM_THEMECHANGED:
        ui::RefreshTheme();ui::Frame(hwnd);InvalidateRect(hwnd,nullptr,TRUE);InvalidateRect(list,nullptr,TRUE);
        {ClearTrayFrames();if(trayAnimating||trayFrame)PrepareTrayFrames();HICON previous=trayIcon;trayIcon=ui::CreateTrayIcon();TrayIcon(NIM_MODIFY);if(previous)DestroyIcon(previous);
        previous=appIcon;appIcon=ui::CreateTrayIcon(false);SendMessageW(hwnd,WM_SETICON,ICON_SMALL,reinterpret_cast<LPARAM>(appIcon));SendMessageW(hwnd,WM_SETICON,ICON_BIG,reinterpret_cast<LPARAM>(appIcon));if(previous)DestroyIcon(previous);}
        trayControl.InvalidatePosition();
        RedrawWindow(hwnd,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN);
        if(calibration.Handle()){ui::Frame(calibration.Handle());RedrawWindow(calibration.Handle(),nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN);}return 0;
    case WM_DPICHANGED: {
        surface.Init(hwnd);RebuildRows();
        for(int id:{Sync,Refresh,Exit,BrightnessPage,SettingsPage,MatchSettings,LanguageEn,LanguageRu})SendDlgItemMessageW(hwnd,id,WM_SETFONT,reinterpret_cast<WPARAM>(surface.body),TRUE);
        for(int id=SpeedPresetBase;id<WheelPresetBase+3;++id)SendDlgItemMessageW(hwnd,id,WM_SETFONT,reinterpret_cast<WPARAM>(surface.body),TRUE);
        RECT r=*reinterpret_cast<RECT*>(lp);SetWindowPos(hwnd,nullptr,r.left,r.top,r.right-r.left,r.bottom-r.top,SWP_NOZORDER);Layout();return 0;
    }
    case Tray:
        if(lp==WM_MOUSEMOVE)trayControl.Hover();
        if(lp==WM_LBUTTONUP){if(calibration.Handle())Show();else if(IsWindowVisible(hwnd))ShowWindow(hwnd,SW_HIDE);else Show();}
        if(lp==WM_RBUTTONUP) {
            trayControl.Hover();HMENU menu=CreatePopupMenu();AppendMenuW(menu,MF_STRING,1,tr(L"Open"));
            UINT available=scanning||calibration.Handle()?MF_GRAYED:MF_ENABLED;
            AppendMenuW(menu,MF_STRING|available,10,(tr(L"Brighter  +")+std::to_wstring(preferences.wheelStep)+L"%").c_str());AppendMenuW(menu,MF_STRING|available,11,(tr(L"Dimmer  −")+std::to_wstring(preferences.wheelStep)+L"%").c_str());
            HMENU levels=CreatePopupMenu();for(int p:{0,25,50,75,100})AppendMenuW(levels,MF_STRING|available,200+p,(std::to_wstring(p)+L"%").c_str());
            AppendMenuW(menu,MF_POPUP,reinterpret_cast<UINT_PTR>(levels),tr(L"Set brightness"));
            AppendMenuW(menu,MF_STRING|(syncing?MF_CHECKED:0),12,tr(L"Synchronization"));
            AppendMenuW(menu,MF_STRING,13,tr(L"Settings"));
            if(!syncing)for(size_t i=0;i<monitors.size();++i)if(monitors[i].supported)
                AppendMenuW(menu,MF_STRING|(i==activeMonitor?MF_CHECKED:0),1000+i,monitors[i].name.c_str());
            AppendMenuW(menu,MF_SEPARATOR,0,nullptr);AppendMenuW(menu,MF_STRING,2,tr(L"Exit"));
            POINT p;GetCursorPos(&p);SetForegroundWindow(hwnd);
            int command=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,p.x,p.y,0,hwnd,nullptr);DestroyMenu(menu);
            if(command==1)Show();
            if(command==2){calibration.Cancel();DestroyWindow(hwnd);}
            if(command==10)TrayValue(preferences.wheelStep,true);if(command==11)TrayValue(-preferences.wheelStep,true);
            if(command==13){Show();SetPage(true);}
            if(command>=200&&command<=300)TrayValue(command-200,false);
            if(command==12)SendMessageW(hwnd,WM_COMMAND,Sync,0);
            if(command>=1000&&command<1000+static_cast<int>(monitors.size())){activeMonitor=static_cast<size_t>(command-1000);TrayIcon(NIM_MODIFY);}
            PostMessageW(hwnd,WM_NULL,0,0);
        }return 0;
    case WM_DESTROY:KillTimer(hwnd,TrayAnimationTimer);calibration.Cancel();trayControl.Stop();TrayIcon(NIM_DELETE);ClearTrayFrames();PostQuitMessage(0);return 0;
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR arguments,int) {
    wchar_t executable[MAX_PATH]{};GetModuleFileNameW(nullptr,executable,MAX_PATH);
    std::wstring executableLower=executable;std::transform(executableLower.begin(),executableLower.end(),executableLower.begin(),towlower);
    demo=std::wstring(arguments).find(L"--demo")!=std::wstring::npos || executableLower.find(L"brightnesstray.demo.exe")!=std::wstring::npos;
    if(std::wstring(arguments).find(L"--light")!=std::wstring::npos)ui::themeOverride=1;
    if(std::wstring(arguments).find(L"--dark")!=std::wstring::npos)ui::themeOverride=2;
    HANDLE singleton=CreateMutexW(nullptr,FALSE,demo?L"Local\\BrightnessTray.Native.Demo":L"Local\\BrightnessTray.Native.Singleton");
    if(!singleton)return 1;
    if(GetLastError()==ERROR_ALREADY_EXISTS){
        HWND existing=FindWindowW(L"BrightnessTray.Native",demo?L"BrightnessTray · Demo":L"BrightnessTray");
        if(existing){HWND popup=GetLastActivePopup(existing);SetWindowPos(existing,nullptr,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_SHOWWINDOW);SetForegroundWindow(popup);}
        CloseHandle(singleton);return 0;
    }
    tracing=std::wstring(arguments).find(L"--trace")!=std::wstring::npos;
    if(tracing){std::wstring path=executable;path=path.substr(0,path.find_last_of(L"\\/"))+L"\\brightness-trace.csv";brightness_trace::Start(path.c_str());}
    wchar_t folder[MAX_PATH]{};
    if(!GetEnvironmentVariableW(L"LOCALAPPDATA",folder,MAX_PATH)){CloseHandle(singleton);return 1;}
    settings=std::wstring(folder)+L"\\BrightnessTray";CreateDirectoryW(settings.c_str(),nullptr);
    settings+=demo?L"\\settings-demo.ini":L"\\settings.ini";
    wchar_t language[16]{};GetPrivateProfileStringW(L"General",L"Language",L"auto",language,16,settings.c_str());
    std::wstring selectedLanguage=language;
    if(std::wstring(arguments).find(L"--lang=en")!=std::wstring::npos)selectedLanguage=L"en";
    if(std::wstring(arguments).find(L"--lang=ru")!=std::wstring::npos)selectedLanguage=L"ru";
    i18n::Initialize(selectedLanguage);status=tr(L"Detecting monitors…");
    syncing=GetPrivateProfileIntW(L"General",L"Sync",0,settings.c_str())!=0;
    preferences.transitionMs=static_cast<int>(GetPrivateProfileIntW(L"General",L"TransitionMs",transition::DefaultMs,settings.c_str()));
    preferences.wheelStep=static_cast<int>(GetPrivateProfileIntW(L"General",L"WheelStep",2,settings.c_str()));preferences.Normalize();
    preferences.hardwareStep=static_cast<int>(GetPrivateProfileIntW(L"General",L"HardwareStep",2,settings.c_str()));preferences.Normalize();
    int response=static_cast<int>(GetPrivateProfileIntW(L"General",L"ResponseMode",99,settings.c_str()));
    if(response>=0&&response<=2)preferences.SetResponse(response);else preferences.AdoptLegacyResponse();
    worker.SetTransitionDuration(preferences.transitionMs);
    worker.SetHardwareStep(preferences.hardwareStep);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_BAR_CLASSES};InitCommonControlsEx(&controls);ui::RefreshTheme();
    WNDCLASSW wc{};wc.lpfnWndProc=WindowProc;wc.hInstance=instance;wc.lpszClassName=L"BrightnessTray.Native";
    wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassW(&wc);
    wc.lpfnWndProc=ListProc;wc.lpszClassName=L"BrightnessTray.List";RegisterClassW(&wc);
    taskbarCreated=RegisterWindowMessageW(L"TaskbarCreated");
    window=CreateWindowExW(WS_EX_APPWINDOW|WS_EX_CONTROLPARENT,L"BrightnessTray.Native",demo?L"BrightnessTray · Demo":L"BrightnessTray",
        WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,530,640,nullptr,nullptr,instance,nullptr);
    if(!window){CloseHandle(singleton);return 1;}
    surface.Init(window);ui::Frame(window);trayIcon=ui::CreateTrayIcon();appIcon=ui::CreateTrayIcon(false);
    SendMessageW(window,WM_SETICON,ICON_SMALL,reinterpret_cast<LPARAM>(appIcon));SendMessageW(window,WM_SETICON,ICON_BIG,reinterpret_cast<LPARAM>(appIcon));
    ui::Button(window,surface,syncing?tr(L"Monitor synchronization: on"):tr(L"Monitor synchronization: off"),Sync);
    ui::Button(window,surface,tr(L"Refresh"),Refresh);ui::Button(window,surface,tr(L"Exit"),Exit);
    ui::Button(window,surface,tr(L"Brightness"),BrightnessPage);ui::Button(window,surface,tr(L"Settings"),SettingsPage);
    for(int i=0;i<3;++i)ui::Button(window,surface,tr(ResponseLabels[i]),SpeedPresetBase+i,false,true);
    for(int i=0;i<3;++i)ui::Button(window,surface,std::to_wstring(WheelPresets[i])+L"%",WheelPresetBase+i,false,true);
    ui::Button(window,surface,tr(L"Configure"),MatchSettings,false,true);
    ui::Button(window,surface,tr(L"English"),LanguageEn);ui::Button(window,surface,tr(L"Russian"),LanguageRu);
    list=CreateWindowExW(WS_EX_CONTROLPARENT,L"BrightnessTray.List",tr(L"Monitors"),WS_CHILD|WS_VISIBLE|WS_VSCROLL|WS_CLIPCHILDREN,
        0,0,1,1,window,nullptr,instance,nullptr);
    RECT r{0,0,surface.S(520),surface.S(640)};AdjustWindowRectExForDpi(&r,static_cast<DWORD>(GetWindowLongPtrW(window,GWL_STYLE)),FALSE,
        static_cast<DWORD>(GetWindowLongPtrW(window,GWL_EXSTYLE)),surface.dpi);
    SetWindowPos(window,nullptr,0,0,r.right-r.left,r.bottom-r.top,SWP_NOMOVE|SWP_NOZORDER);
    UpdatePreferencesControls();SetPage(false);Layout();TrayIcon(NIM_ADD);bool wheelInstalled=trayControl.Start(window,Wheel);worker.Start(window,Result,demo);Scan();Show();
    if(!wheelInstalled)Status(tr(L"Mouse wheel unavailable. Use the tray menu to adjust brightness."));
    MSG message{};
    while(GetMessageW(&message,nullptr,0,0)>0) {
        if(message.message==WM_KEYDOWN&&message.wParam==VK_ESCAPE) {
            if(calibration.Handle())calibration.Cancel();else ShowWindow(window,SW_HIDE);
            continue;
        }
        HWND dialog=calibration.Handle()?calibration.Handle():window;
        if(!IsDialogMessageW(dialog,&message)){TranslateMessage(&message);DispatchMessageW(&message);}
    }
    worker.Stop();brightness_trace::Stop();DestroyIcon(trayIcon);DestroyIcon(appIcon);surface.Clear();
    DeleteObject(ui::backgroundBrush);DeleteObject(ui::cardBrush);CloseHandle(singleton);return 0;
}


