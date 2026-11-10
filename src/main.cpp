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
#include <functional>
#include "calibration.h"
#include "tray.h"
#include "tray_position.h"
#include "sun_motion.h"
#include "render_profile.h"
#include "frame_clock.h"
#include "preferences.h"
#include "brightness_trace.h"

#pragma comment(linker,"/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
namespace {
constexpr UINT Result = WM_APP+1, Tray = WM_APP+2, Wheel=WM_APP+4, CheckPopupFocus=WM_APP+5, AnimationFrame=WM_APP+6, RevealFocus=WM_APP+7;
constexpr int Sync=10, Refresh=11, Exit=12, SliderBase=1000, CalibrateBase=2000, ResetBase=3000;
constexpr int BrightnessPage=20,SettingsPage=21,SpeedPresetBase=23,WheelPresetBase=27,MatchSettings=30,LanguageEn=31,LanguageRu=32,OptionsBase=4000, SharedSlider=40, LanguageSelect=41;
constexpr int WheelPresets[]={1,2,5};
const wchar_t* ResponseLabels[]={L"Instant",L"Quick",L"Smoother"};
HWND window{}, list{},settingsPane{};
HWND Control(int id){HWND child=GetDlgItem(window,id);return child?child:GetDlgItem(settingsPane,id);}
ui::Surface surface;
ui::BackBuffer popupPaintBuffer,listPaintBuffer,settingsPaintBuffer;
FrameClock frameClock;
bool framesRunning=false;

MonitorWorker worker;
CalibrationWindow calibration;
HICON trayIcon{},appIcon{};
constexpr UINT_PTR TrayAnimationTimer=2;
constexpr double TrayFrameCount=48; // Angular units, not cached images.
std::map<std::wstring,int> requestedLevels;
bool trayAnimating=false;
double renderedTrayPosition=-1;
int renderedTrayPixels=0;
double trayPosition=0;
SunMotion sunMotion;
LONGLONG previousFrameStamp=0;
TrayControl trayControl;
size_t activeMonitor=0;
UINT taskbarCreated{};
std::wstring settings, status=tr(L"Detecting monitors…");
std::vector<Monitor> monitors;
std::map<std::wstring,unsigned long long> latestWrites;
unsigned long long scanTag{};
bool scanning=true, syncing=false, demo=false, tracing=false, inspection=false;
bool settingsPage=false;
Preferences preferences;
int scrollOffset=0,settingsOffset=0;
int settingsWheelRemainder=0,listWheelRemainder=0;
void ScrollWheel(int delta,int& remainder,int& offset){remainder+=delta;int steps=remainder/WHEEL_DELTA;remainder-=steps*WHEEL_DELTA;offset-=steps*48;}
constexpr int SettingsContentHeight=380;
void LayoutSettings();
void LayoutRows();
bool popupMenu=false,languageDropped=false,layoutBusy=false,dismissedOverTray=false;
ULONGLONG popupDismissedAt=0;
RECT popupAnchor{},popupTaskbar{},popupOverflow{};bool popupAnchorValid=false;
void PositionPopup(bool show=false);
void UpdateActualLevels();
int RowHeight(){return syncing?76:112;}
size_t FirstSupported(){for(size_t i=0;i<monitors.size();++i)if(monitors[i].supported)return i;return monitors.size();}
void TrayIcon(DWORD operation);
void Layout();
void AdvanceTrayAnimation(ULONGLONG now){
    trayAnimating=sunMotion.Advance(now);trayPosition=sunMotion.Angle()*TrayFrameCount/360.0;

}
bool EnsureFrames(){
    if(framesRunning)return true;
    MONITORINFOEXW monitor{};monitor.cbSize=sizeof(monitor);GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&monitor);
    DEVMODEW mode{};mode.dmSize=sizeof(mode);unsigned hz=60;if(EnumDisplaySettingsW(monitor.szDevice,ENUM_CURRENT_SETTINGS,&mode)&&mode.dmDisplayFrequency>1)hz=mode.dmDisplayFrequency;
    previousFrameStamp=render_profile::Stamp();framesRunning=frameClock.Start(window,AnimationFrame,hz);return framesRunning;
}
void AnimateTray(int direction,ULONGLONG duration=650){
    BOOL enabled=TRUE;SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION,0,&enabled,0);if(!enabled)return;
    sunMotion.Trigger(render_profile::NowMilliseconds(),direction,duration);trayPosition=sunMotion.Angle()*TrayFrameCount/360.0;trayAnimating=EnsureFrames();
}

std::wstring ResponseText(){return tr(ResponseLabels[preferences.responseMode]);}
void Mark(HWND button,bool selected) {
    if(selected)SetPropW(button,L"Accent",reinterpret_cast<HANDLE>(1));else RemovePropW(button,L"Accent");
    InvalidateRect(button,nullptr,FALSE);
}
void UpdatePreferencesControls() {
    for(int i=0;i<3;++i)Mark(Control(SpeedPresetBase+i),preferences.responseMode==i);
    for(int i=0;i<3;++i)Mark(Control(WheelPresetBase+i),preferences.wheelStep==WheelPresets[i]);
    SetWindowTextW(Control(LanguageSelect),i18n::useRussian.load()?L"Русский":L"English");InvalidateRect(Control(LanguageSelect),nullptr,FALSE);
    InvalidateRect(window,nullptr,FALSE);
    InvalidateRect(settingsPane,nullptr,FALSE);
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
    if(!saved)trayControl.Toast(tr(L"Settings"),tr(L"Changes are active, but settings could not be saved."));
    status=saved?tr(L"Settings saved. Changes are already active."):tr(L"Changes are active, but settings could not be saved.");
    InvalidateRect(window,nullptr,FALSE);
}
void SetPage(bool value) {
    if(calibration.Handle())return;
    settingsPage=value;
    ShowWindow(settingsPane,value?SW_SHOW:SW_HIDE);
    ShowWindow(list,value?SW_HIDE:SW_SHOW);
    ShowWindow(Control(Sync),value?SW_HIDE:SW_SHOW);
    ShowWindow(Control(SharedSlider),!value&&syncing?SW_SHOW:SW_HIDE);
    ShowWindow(Control(SettingsPage),value?SW_HIDE:SW_SHOW);
    ShowWindow(Control(BrightnessPage),value?SW_SHOW:SW_HIDE);
    for(int id:{SpeedPresetBase,SpeedPresetBase+1,SpeedPresetBase+2,WheelPresetBase,WheelPresetBase+1,WheelPresetBase+2,MatchSettings,LanguageSelect,Refresh})ShowWindow(Control(id),value?SW_SHOW:SW_HIDE);
    ShowWindow(Control(Exit),SW_HIDE);
    Layout();if(!value)UpdateActualLevels();
    if(GetActiveWindow()==window)SetFocus(Control(value?BrightnessPage:SettingsPage));
}

RECT Box(int x,int y,int width,int height) { return {surface.S(x),surface.S(y),surface.S(x+width),surface.S(y+height)}; }
int Width(HWND hwnd) { RECT r; GetClientRect(hwnd,&r); return MulDiv(r.right,96,static_cast<int>(surface.dpi)); }
int Height(HWND hwnd) { RECT r; GetClientRect(hwnd,&r); return MulDiv(r.bottom,96,static_cast<int>(surface.dpi)); }
void Status(std::wstring value) { status=std::move(value); InvalidateRect(window,nullptr,FALSE); }
int HardwarePercent(const Monitor& m) { return FromRaw(m.current,m.minimum,m.maximum); }
int LogicalPercent(const Monitor& m) { return ToLogicalPercent(HardwarePercent(m),m.calibration); }
double VisualPercent(const Monitor& m){double hardware=m.maximum>m.minimum?100.0*(std::clamp(m.current,m.minimum,m.maximum)-m.minimum)/(m.maximum-m.minimum):0;return ToLogicalContinuous(hardware,m.calibration);}
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
LRESULT CALLBACK FocusReveal(HWND child,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR){
    if(message==WM_SETFOCUS)SendMessageW(window,RevealFocus,0,reinterpret_cast<LPARAM>(child));
    if(message==WM_NCDESTROY)RemoveWindowSubclass(child,FocusReveal,89);
    return DefSubclassProc(child,message,wp,lp);
}
void LayoutSettings(){
    if(!settingsPane)return;
    int h=Height(settingsPane);settingsOffset=std::clamp(settingsOffset,0,std::max(0,SettingsContentHeight-h));
    SCROLLINFO info{sizeof(info),SIF_RANGE|SIF_PAGE|SIF_POS,0,SettingsContentHeight-1,static_cast<UINT>(h),settingsOffset,0};
    SetScrollInfo(settingsPane,SB_VERT,&info,TRUE);
    int w=Width(settingsPane),presetWidth=(w-52)/3;
    for(int i=0;i<3;++i)ui::Place(Control(SpeedPresetBase+i),surface,20+i*(presetWidth+6),44-settingsOffset,presetWidth,34);
    for(int i=0;i<3;++i)ui::Place(Control(WheelPresetBase+i),surface,20+i*(presetWidth+6),166-settingsOffset,presetWidth,32);
    ui::Place(Control(MatchSettings),surface,w-126,234-settingsOffset,106,32);
    ui::Place(Control(LanguageSelect),surface,w-146,282-settingsOffset,126,32);
    ui::Place(Control(Refresh),surface,20,328-settingsOffset,w-40,30);
    InvalidateRect(settingsPane,nullptr,FALSE);
}
void RevealControl(HWND child){
    HWND parent=GetParent(child);if(parent!=list&&parent!=settingsPane)return;
    RECT r;GetWindowRect(child,&r);MapWindowPoints(nullptr,parent,reinterpret_cast<POINT*>(&r),2);
    int top=MulDiv(r.top,96,static_cast<int>(surface.dpi)),bottom=MulDiv(r.bottom,96,static_cast<int>(surface.dpi));
    int h=Height(parent),delta=top<8?top-8:bottom>h-8?bottom-h+8:0;
    if(!delta)return;
    if(parent==settingsPane){settingsOffset+=delta;LayoutSettings();}
    else{scrollOffset+=delta;LayoutRows();}
}
void LayoutRows() {
    if(!list) return;
    int w=Width(list), h=Height(list), total=static_cast<int>(monitors.size())*RowHeight();
    scrollOffset=std::clamp(scrollOffset,0,std::max(0,total-h));
    SCROLLINFO si{sizeof(si),SIF_RANGE|SIF_PAGE|SIF_POS,0,std::max(0,total-1),static_cast<UINT>(h),scrollOffset,0};
    SetScrollInfo(list,SB_VERT,&si,TRUE);
    for(size_t i=0;i<monitors.size();++i) {
        int y=static_cast<int>(i)*RowHeight()-scrollOffset;
        ui::Place(GetDlgItem(list,SliderBase+static_cast<int>(i)),surface,16,y+58,w-32,32);
        ShowWindow(GetDlgItem(list,SliderBase+static_cast<int>(i)),syncing?SW_HIDE:SW_SHOW);
        ui::Place(GetDlgItem(list,OptionsBase+static_cast<int>(i)),surface,w-40,y+34,28,24);
    }
    InvalidateRect(list,nullptr,FALSE);
}
void RebuildRows() {
    std::vector<HWND> children;
    for(HWND c=GetWindow(list,GW_CHILD);c;c=GetWindow(c,GW_HWNDNEXT)) children.push_back(c);
    for(HWND c:children) DestroyWindow(c);
    for(size_t i=0;i<monitors.size();++i) {
        HWND slider=ui::Control(list,surface,TRACKBAR_CLASSW,tr(L"Brightness: ")+monitors[i].name,TBS_NOTICKS|WS_TABSTOP,SliderBase+static_cast<int>(i),false);
        SetWindowSubclass(slider,FocusReveal,89,0);
        SendMessageW(slider,TBM_SETRANGE,TRUE,MAKELPARAM(0,100));
        SendMessageW(slider,TBM_SETPOS,TRUE,requestedLevels.count(monitors[i].key)?requestedLevels[monitors[i].key]:LogicalPercent(monitors[i]));
        ui::Button(list,surface,L"…",OptionsBase+static_cast<int>(i),false,true);
        SetWindowSubclass(GetDlgItem(list,OptionsBase+static_cast<int>(i)),FocusReveal,89,0);
        for(int id:{SliderBase,OptionsBase}) EnableWindow(GetDlgItem(list,id+static_cast<int>(i)),monitors[i].supported&&!scanning);
    }
    const auto first=FirstSupported();
    SendMessageW(Control(SharedSlider),TBM_SETPOS,TRUE,first<monitors.size()?SendDlgItemMessageW(list,SliderBase+static_cast<int>(first),TBM_GETPOS,0,0):0);
    EnableWindow(Control(SharedSlider),first<monitors.size()&&!scanning);
    EnableWindow(Control(Sync),std::count_if(monitors.begin(),monitors.end(),[](const Monitor& m){return m.supported;})>=2&&!scanning);
    EnableWindow(Control(MatchSettings),std::count_if(monitors.begin(),monitors.end(),[](const Monitor& m){return m.supported;})>=2&&!scanning);
    if(first<monitors.size())ui::SetSliderActual(Control(SharedSlider),VisualPercent(monitors[first]),false);
    UpdateActualLevels();Layout();
}
void Layout() {
    if(!surface.body || !list) return;
    if(layoutBusy)return;
    layoutBusy=true;
    MONITORINFO area{sizeof(area)};GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&area);
    int available=MulDiv(area.rcWork.bottom-area.rcWork.top,96,static_cast<int>(surface.dpi))-24;
    int rows=std::max(1,static_cast<int>(monitors.size()))*RowHeight();
    int height=std::min(settingsPage?444:72+std::min(rows,336)+(syncing?106:0)+54,available);
    SetWindowPos(window,nullptr,0,0,surface.S(352),surface.S(height),SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
    int w=Width(window),h=Height(window);
    ui::Place(Control(BrightnessPage),surface,16,16,32,32);
    ui::Place(Control(SettingsPage),surface,w-52,16,32,32);
    ui::Place(Control(Sync),surface,w-92,16,32,32);
    int listHeight=std::max(0,h-72-(syncing?106:0)-54);
    ui::Place(list,surface,4,64,w-8,listHeight);
    ui::Place(Control(SharedSlider),surface,20,64+listHeight+38,w-40,32);
    ui::Place(settingsPane,surface,0,64,w,std::max(0,h-64));
    LayoutSettings();
    LayoutRows();InvalidateRect(window,nullptr,FALSE);
    layoutBusy=false;
    PositionPopup();
}

void Scan() {
    calibration.Cancel(); scanning=true; scanTag=NextTag(); latestWrites.clear();requestedLevels.clear();
    worker.Queue({Operation::Scan,L"",0,scanTag});
    EnableWindow(Control(Refresh),FALSE);
    for(HWND c=GetWindow(list,GW_CHILD);c;c=GetWindow(c,GW_HWNDNEXT)) EnableWindow(c,FALSE);
    Status(tr(L"Detecting monitors…"));
}
void Apply(size_t source) {
    if(scanning || calibration.Handle() || source>=monitors.size()) return;
    activeMonitor=source;
    int value=static_cast<int>(SendDlgItemMessageW(list,SliderBase+static_cast<int>(source),TBM_GETPOS,0,0));
    auto previous=requestedLevels.find(monitors[source].key);
    int prior=previous==requestedLevels.end()?LogicalPercent(monitors[source]):previous->second;
    if(value!=prior)AnimateTray(value>prior?1:-1);
    std::vector<Command> batch;
    for(size_t i=0;i<monitors.size();++i) if(monitors[i].supported&&(syncing||i==source)) {
        if(!latestWrites.count(monitors[i].key)){
            ui::BeginSliderActual(GetDlgItem(list,SliderBase+static_cast<int>(i)));
            if(i==FirstSupported())ui::BeginSliderActual(Control(SharedSlider));
        }
        SendDlgItemMessageW(list,SliderBase+static_cast<int>(i),TBM_SETPOS,TRUE,value);
        batch.push_back(WriteCommand(monitors[i],value));
    }
    worker.QueueBatch(std::move(batch));
    TrayIcon(NIM_MODIFY);
    if(syncing)SendMessageW(Control(SharedSlider),TBM_SETPOS,TRUE,value);
    UpdateActualLevels();InvalidateRect(list,nullptr,FALSE);InvalidateRect(window,nullptr,FALSE);
}
void TrayValue(int value,bool relative) {
    if(scanning||calibration.Handle())return;
    if(activeMonitor>=monitors.size()||!monitors[activeMonitor].supported) {
        auto it=std::find_if(monitors.begin(),monitors.end(),[](const Monitor& m){return m.supported;});
        if(it==monitors.end())return;activeMonitor=static_cast<size_t>(it-monitors.begin());
    }
    int id=SliderBase+static_cast<int>(activeMonitor);
    int current=static_cast<int>(SendDlgItemMessageW(list,id,TBM_GETPOS,0,0));
    int next=std::clamp(relative?current+value:value,0,100);
    SendDlgItemMessageW(list,id,TBM_SETPOS,TRUE,next);Apply(activeMonitor);
}
bool SharedLevelsMatch(){int value=-1;for(size_t i=0;i<monitors.size();++i)if(monitors[i].supported){int next=static_cast<int>(SendDlgItemMessageW(list,SliderBase+static_cast<int>(i),TBM_GETPOS,0,0));if(value>=0&&value!=next)return false;value=next;}return true;}
void DrawMonitor(HDC dc,int x,int y) {
    ui::DrawGlyph(dc,Box(x-3,y-3,32,32),ui::Glyph::Monitor,ui::colors.muted);
}
void PaintList(HDC dc) {
    RECT rect;GetClientRect(list,&rect);ui::Fill(dc,rect,ui::colors.background);
    int w=Width(list),h=Height(list);
    for(size_t i=0;i<monitors.size();++i){
        int y=static_cast<int>(i)*RowHeight()-scrollOffset;if(y+RowHeight()<0||y>h)continue;
        const auto& m=monitors[i];DrawMonitor(dc,16,y+14);
        ui::Text(dc,m.name,Box(54,y+10,w-150,22),surface.body,ui::colors.text,DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
        ui::Text(dc,m.supported?std::to_wstring(LogicalPercent(m))+L"%":L"—",Box(w-94,y+10,70,24),surface.heading,ui::colors.text,DT_RIGHT|DT_SINGLELINE);
        ui::Text(dc,m.supported?(m.calibration.Matched()?tr(L"Matched"):tr(L"Not matched")):tr(L"DDC/CI unavailable"),Box(54,y+36,w-152,20),surface.caption,ui::colors.muted,DT_SINGLELINE|DT_END_ELLIPSIS);
        if(m.supported)ui::Text(dc,L"DDC "+std::to_wstring(HardwarePercent(m))+L"%",Box(w-118,y+38,70,18),surface.caption,ui::colors.muted,DT_RIGHT|DT_SINGLELINE);
        ui::Fill(dc,Box(16,y+RowHeight()-1,w-32,1),ui::colors.border);
    }
    if(monitors.empty()){
        ui::Text(dc,scanning?tr(L"Looking for your monitors…"):tr(L"No monitors found"),Box(16,6,w-32,28),surface.body,ui::colors.text);
        ui::Text(dc,tr(L"Enable DDC/CI in your monitor menu\nand click Refresh."),Box(16,36,w-32,58),surface.caption,ui::colors.muted);
    }
}
void PaintSettings(HDC dc){
    RECT r;GetClientRect(settingsPane,&r);ui::Fill(dc,r,ui::colors.background);int w=Width(settingsPane);
    auto box=[&](int x,int y,int width,int height){return Box(x,y-settingsOffset,width,height);};
    ui::Text(dc,tr(L"Brightness response"),box(20,12,w-40,22),surface.body,ui::colors.text);
    const wchar_t* hints[]={tr(L"Apply the final brightness without intermediate steps."),tr(L"Respond quickly to slider movements."),tr(L"Smaller steps; large changes take longer.\nBacklight steps may still be visible.")};
    ui::Text(dc,hints[preferences.responseMode],box(20,88,w-40,46),surface.caption,ui::colors.muted);
    ui::Text(dc,tr(L"Tray mouse wheel"),box(20,138,w-40,22),surface.body,ui::colors.text);
    ui::Text(dc,tr(L"Brightness step. Ctrl: 1%."),box(20,206,w-40,20),surface.caption,ui::colors.muted);
    ui::Text(dc,tr(L"Match displays"),box(20,240,w-152,22),surface.body,ui::colors.text);
    ui::Text(dc,tr(L"Language"),box(20,288,w-180,24),surface.body,ui::colors.text);
}
bool StatusIsError(){return status==tr(L"Changes are active, but settings could not be saved.")||status==tr(L"The monitor did not respond. Try again or refresh the list.")||status==tr(L"The monitor did not respond. Refresh and check its level.");}
void PaintMain(HDC dc) {
    RECT rect;GetClientRect(window,&rect);ui::Fill(dc,rect,ui::colors.background);
    int w=Width(window),h=Height(window);
    if(!settingsPage){
        ui::DrawSun(dc,Box(20,20,24,24),trayPosition*6.283185307179586/TrayFrameCount);
        RECT clip{};GetClipBox(dc,&clip);RECT sun=Box(16,16,32,32);
        if(clip.left>=sun.left&&clip.top>=sun.top&&clip.right<=sun.right&&clip.bottom<=sun.bottom)return;
    }
    ui::Text(dc,settingsPage?tr(L"Settings"):tr(L"Brightness"),Box(56,20,w-154,28),surface.heading,ui::colors.text);
    if(!settingsPage){
        if(syncing){RECT r;GetWindowRect(list,&r);int y=64+MulDiv(r.bottom-r.top,96,static_cast<int>(surface.dpi));
            ui::Text(dc,SharedLevelsMatch()?tr(L"Shared brightness"):tr(L"Choose a shared level"),Box(20,y+12,w-92,24),surface.body,ui::colors.text);
            auto first=FirstSupported();int target=static_cast<int>(SendMessageW(Control(SharedSlider),TBM_GETPOS,0,0));
            ui::Text(dc,first<monitors.size()&&SharedLevelsMatch()?std::to_wstring(target)+L"%":L"—",Box(w-78,y+12,58,24),surface.heading,ui::colors.text,DT_RIGHT|DT_SINGLELINE);
            ui::Text(dc,tr(L"One level for all monitors"),Box(20,y+78,w-40,20),surface.caption,ui::colors.muted);
        }
        ui::Text(dc,tr(L"Wheel ±")+std::to_wstring(preferences.wheelStep)+L"% · Ctrl ±1%",Box(20,h-30,w-40,20),surface.caption,ui::colors.muted);
    }
    if(StatusIsError()&&!settingsPage)ui::Text(dc,status,Box(20,h-64,w-40,34),surface.caption,ui::colors.text);
}
void UpdateActualLevels(){
    BOOL enabled=TRUE;SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION,0,&enabled,0);bool smooth=enabled&&preferences.responseMode!=0&&!calibration.Handle();
    bool progress=false;for(size_t i=0;i<monitors.size();++i)progress=ui::SetSliderActual(GetDlgItem(list,SliderBase+static_cast<int>(i)),VisualPercent(monitors[i]),smooth)||progress;
    auto first=FirstSupported();progress=ui::SetSliderActual(Control(SharedSlider),first<monitors.size()?VisualPercent(monitors[first]):0,smooth)||progress;
    if(progress&&IsWindowVisible(window)&&!settingsPage)EnsureFrames();
    InvalidateRect(list,nullptr,FALSE);InvalidateRect(window,nullptr,FALSE);
}

void BufferedPaint(HWND hwnd,void (*paint)(HDC)) {
    render_profile::Scope measured("paint");
    PAINTSTRUCT ps; HDC dc=BeginPaint(hwnd,&ps); RECT r; GetClientRect(hwnd,&r);
    if(surface.body&&r.right>0&&r.bottom>0) {
        HDC buffer=(hwnd==window?popupPaintBuffer:hwnd==settingsPane?settingsPaintBuffer:listPaintBuffer).Begin(dc,r.right,r.bottom);
        if(buffer){int saved=SaveDC(buffer);IntersectClipRect(buffer,ps.rcPaint.left,ps.rcPaint.top,ps.rcPaint.right,ps.rcPaint.bottom);paint(buffer);RestoreDC(buffer,saved);
            BitBlt(dc,ps.rcPaint.left,ps.rcPaint.top,ps.rcPaint.right-ps.rcPaint.left,ps.rcPaint.bottom-ps.rcPaint.top,buffer,ps.rcPaint.left,ps.rcPaint.top,SRCCOPY);}
    }
    EndPaint(hwnd,&ps);
}
void TrayIcon(DWORD operation) {
    NOTIFYICONDATAW icon{};icon.cbSize=sizeof(icon);icon.hWnd=window;icon.uID=1;
    icon.uFlags=NIF_MESSAGE|NIF_ICON|NIF_TIP;icon.uCallbackMessage=Tray;
    int pixels=GetSystemMetricsForDpi(SM_CXSMICON,surface.dpi);
    if(operation!=NIM_DELETE && (!trayIcon || renderedTrayPosition!=trayPosition || renderedTrayPixels!=pixels)) {
        HICON next=ui::CreateTrayIcon(true,trayPosition*6.283185307179586/TrayFrameCount,pixels);
        if(next){HICON previous=trayIcon;trayIcon=next;renderedTrayPosition=trayPosition;renderedTrayPixels=pixels;if(previous)DestroyIcon(previous);}
    }
    icon.hIcon=trayIcon;
    std::wstring tip=tr(L"BrightnessTray · wheel ±")+std::to_wstring(preferences.wheelStep)+L"% · Ctrl ±1%";
    if(activeMonitor<monitors.size()) {
        int value=static_cast<int>(SendDlgItemMessageW(list,SliderBase+static_cast<int>(activeMonitor),TBM_GETPOS,0,0));
        tip=std::to_wstring(value)+L"% · "+(syncing?tr(L"All monitors"):monitors[activeMonitor].name)+tr(L"\nWheel ±")+std::to_wstring(preferences.wheelStep)+L"% · Ctrl ±1%";
    }
    wcsncpy_s(icon.szTip,tip.c_str(),_TRUNCATE);{render_profile::Scope measured("shell_notify");Shell_NotifyIconW(operation,&icon);}
}
void PositionPopup(bool show){
    if(!window||layoutBusy)return;
    if(!popupAnchorValid)popupAnchorValid=tray_position::Resolve(window,popupAnchor,popupTaskbar,&popupOverflow);
    MONITORINFO info{sizeof(info)};GetMonitorInfoW(MonitorFromRect(&popupTaskbar,MONITOR_DEFAULTTONEAREST),&info);
    RECT r;GetWindowRect(window,&r);int w=r.right-r.left,h=r.bottom-r.top;
    POINT position=tray_position::Place(info.rcWork,popupAnchor,popupTaskbar,w,h,surface.S(10),&popupOverflow);int x=position.x,y=position.y;
    SetWindowPos(window,HWND_TOPMOST,x,y,w,h,SWP_NOACTIVATE|(show?SWP_SHOWWINDOW:0));
}
void Show(){
    if(calibration.Handle()){SetForegroundWindow(calibration.Handle());return;}
    popupAnchorValid=tray_position::Resolve(window,popupAnchor,popupTaskbar,&popupOverflow);SetPage(false);Layout();PositionPopup(true);UpdateActualLevels();SetForegroundWindow(window);SetFocus(Control(SettingsPage));
}
void HandleEvents() {
    for(const auto& event:worker.Events()) {
        calibration.OnEvent(event);
        if(event.operation==Operation::Scan) {
            if(event.tag!=scanTag) continue;
            monitors=event.monitors;for(auto& m:monitors)Load(m);
            scanning=false;EnableWindow(Control(Refresh),TRUE);RebuildRows();
            if(activeMonitor>=monitors.size())activeMonitor=0;TrayIcon(NIM_MODIFY);
            Status(monitors.empty() ? tr(L"Connect an external monitor and refresh the list.") : tr(L"Ready. Brightness changes only when you adjust it."));
        } else {
            auto it=std::find_if(monitors.begin(),monitors.end(),[&](const Monitor& m){return m.key==event.key;});
            if(event.operation==Operation::Write&&event.ok&&calibration.Handle()&&latestWrites.empty()&&it!=monitors.end()&&event.raw!=it->current)
                AnimateTray(event.raw>it->current?1:-1);
            if(it!=monitors.end()&&event.ok) it->current=event.raw;
            UpdateActualLevels();
            if(event.operation==Operation::Write&&!event.final)continue;
            auto pending=latestWrites.find(event.key);
            const bool tracked=pending!=latestWrites.end();
            const bool matching=event.operation==Operation::Write&&tracked&&pending->second==event.tag;
            if(matching){latestWrites.erase(pending);requestedLevels.erase(event.key);if(!event.ok&&it!=monitors.end()){auto index=static_cast<size_t>(it-monitors.begin());SendDlgItemMessageW(list,SliderBase+static_cast<int>(index),TBM_SETPOS,TRUE,LogicalPercent(*it));if(syncing)SendMessageW(Control(SharedSlider),TBM_SETPOS,TRUE,LogicalPercent(*it));}}
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

}
LRESULT CALLBACK SettingsProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
    switch(msg){
    case WM_PAINT:BufferedPaint(hwnd,PaintSettings);return 0;
    case WM_ERASEBKGND:return 1;
    case WM_SIZE:LayoutSettings();return 0;
    case WM_COMMAND:case WM_NOTIFY:case WM_DRAWITEM:case WM_MEASUREITEM:return SendMessageW(window,msg,wp,lp);
    case WM_CTLCOLORSTATIC:case WM_CTLCOLORBTN:return ui::ColorControl(msg,wp,lp);
    case WM_MOUSEWHEEL:ScrollWheel(GET_WHEEL_DELTA_WPARAM(wp),settingsWheelRemainder,settingsOffset);LayoutSettings();return 0;
    case WM_VSCROLL:{
        SCROLLINFO info{sizeof(info),SIF_ALL};GetScrollInfo(hwnd,SB_VERT,&info);
        switch(LOWORD(wp)){
        case SB_THUMBTRACK:settingsOffset=info.nTrackPos;break;
        case SB_TOP:settingsOffset=0;break;
        case SB_BOTTOM:settingsOffset=SettingsContentHeight;break;
        case SB_LINEUP:settingsOffset-=24;break;case SB_LINEDOWN:settingsOffset+=24;break;
        case SB_PAGEUP:settingsOffset-=static_cast<int>(info.nPage);break;case SB_PAGEDOWN:settingsOffset+=static_cast<int>(info.nPage);break;
        }LayoutSettings();return 0;
    }
    }return DefWindowProcW(hwnd,msg,wp,lp);
}
LRESULT CALLBACK ListProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
    switch(msg) {
    case WM_PAINT:BufferedPaint(hwnd,PaintList);return 0;
    case WM_ERASEBKGND:return 1;
    case WM_SIZE:LayoutRows();return 0;
    case WM_COMMAND:case WM_NOTIFY:case WM_HSCROLL:case WM_DRAWITEM:
        return SendMessageW(window,msg,wp,lp);
    case WM_CTLCOLORSTATIC:case WM_CTLCOLORBTN:return ui::ColorControl(msg,wp,lp);
    case WM_MOUSEWHEEL:ScrollWheel(GET_WHEEL_DELTA_WPARAM(wp),listWheelRemainder,scrollOffset);LayoutRows();return 0;
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
    if(taskbarCreated && msg==taskbarCreated){popupAnchorValid=false;TrayIcon(NIM_ADD);trayControl.InvalidatePosition();return 0;}
    switch(msg) {
    case WM_CLOSE:ShowWindow(hwnd,SW_HIDE);return 0;
    case WM_PAINT:BufferedPaint(hwnd,PaintMain);return 0;
    case WM_ERASEBKGND:return 1;
    case WM_SIZE:Layout();return 0;
    case RevealFocus:RevealControl(reinterpret_cast<HWND>(lp));return 0;
    case WM_ACTIVATE:
        if(LOWORD(wp)==WA_INACTIVE&&!popupMenu&&!languageDropped&&!calibration.Handle()){
            NOTIFYICONIDENTIFIER id{sizeof(id),hwnd,1,{}};RECT r{};POINT cursor;GetCursorPos(&cursor);
            dismissedOverTray=SUCCEEDED(Shell_NotifyIconGetRect(&id,&r))&&PtInRect(&r,cursor);popupDismissedAt=GetTickCount64();ShowWindow(hwnd,SW_HIDE);
        }return 0;
    case CheckPopupFocus:{HWND focus=GetForegroundWindow();if(!popupMenu&&!calibration.Handle()&&focus!=hwnd&&!IsChild(hwnd,focus))ShowWindow(hwnd,SW_HIDE);return 0;}
    case WM_HSCROLL: {
        if(lp){int id=GetDlgCtrlID(reinterpret_cast<HWND>(lp));
            if(id==SharedSlider){auto first=FirstSupported();if(first<monitors.size()){SendDlgItemMessageW(list,SliderBase+static_cast<int>(first),TBM_SETPOS,TRUE,SendMessageW(Control(SharedSlider),TBM_GETPOS,0,0));Apply(first);}return 0;}
            if(id>=SliderBase&&id<CalibrateBase)Apply(static_cast<size_t>(id-SliderBase));}
        return 0;
    }
    case WM_COMMAND: {
        int id=LOWORD(wp);
        if(id==IDCANCEL){ShowWindow(hwnd,SW_HIDE);return 0;}
        if(id==BrightnessPage||id==SettingsPage){SetPage(id==SettingsPage);return 0;}
        if(id==LanguageSelect){
            RECT r;GetWindowRect(Control(LanguageSelect),&r);popupMenu=true;
            HMENU menu=CreatePopupMenu();AppendMenuW(menu,MF_STRING|(i18n::useRussian.load()?MF_CHECKED:0),LanguageRu,L"Русский");AppendMenuW(menu,MF_STRING|(!i18n::useRussian.load()?MF_CHECKED:0),LanguageEn,L"English");
            int chosen=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTALIGN,r.right,r.bottom,0,hwnd,nullptr);DestroyMenu(menu);popupMenu=false;PostMessageW(hwnd,CheckPopupFocus,0,0);
            if(chosen)SendMessageW(hwnd,WM_COMMAND,chosen,0);InvalidateRect(Control(LanguageSelect),nullptr,FALSE);return 0;
        }
        if(id==LanguageEn||id==LanguageRu) {
            i18n::SetLanguage(id==LanguageEn?L"en":L"ru");
            SetWindowTextW(Control(Sync),syncing?tr(L"Monitor synchronization: on"):tr(L"Monitor synchronization: off"));
            for(auto item:std::vector<std::pair<int,const wchar_t*>>{{Refresh,L"Refresh"},{Exit,L"Exit"},{BrightnessPage,L"Brightness"},{SettingsPage,L"Settings"},{MatchSettings,L"Configure"},{LanguageEn,L"English"},{LanguageRu,L"Russian"}})
                SetWindowTextW(Control(item.first),tr(item.second));
            for(int i=0;i<3;++i)SetWindowTextW(Control(SpeedPresetBase+i),tr(ResponseLabels[i]));
            SetWindowTextW(list,tr(L"Monitors"));SavePreferences();RebuildRows();Layout();return 0;
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
            popupMenu=true;HMENU menu=CreatePopupMenu();AppendMenuW(menu,MF_STRING,1,tr(L"Match displays · use as reference"));
            AppendMenuW(menu,MF_STRING,2,tr(L"Reset this display calibration"));
            int action=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTALIGN,r.right,r.bottom,0,hwnd,nullptr);DestroyMenu(menu);popupMenu=false;PostMessageW(hwnd,CheckPopupFocus,0,0);
            if(action)SendMessageW(hwnd,WM_COMMAND,(action==1?CalibrateBase:ResetBase)+index,0);return 0;
        }
        if(id==Sync) {
            syncing=!syncing;if(!WritePrivateProfileStringW(L"General",L"Sync",syncing?L"1":L"0",settings.c_str()))trayControl.Toast(tr(L"Settings"),tr(L"Changes are active, but settings could not be saved."));
            SetWindowTextW(Control(Sync),syncing?tr(L"Monitor synchronization: on"):tr(L"Monitor synchronization: off"));
            InvalidateRect(Control(Sync),nullptr,FALSE);scrollOffset=0;auto first=FirstSupported();if(first<monitors.size())SendMessageW(Control(SharedSlider),TBM_SETPOS,TRUE,SendDlgItemMessageW(list,SliderBase+static_cast<int>(first),TBM_GETPOS,0,0));SetPage(settingsPage);
        }
        if(id==Refresh)Scan();
        if(id==Exit){calibration.Cancel();DestroyWindow(hwnd);}
        if(id>=CalibrateBase&&id<ResetBase&&!scanning) {
            size_t index=static_cast<size_t>(id-CalibrateBase);
            if(index<monitors.size()&&monitors[index].supported) {
                std::vector<Monitor> selected;std::vector<std::wstring> keys;size_t reference=0;
                for(size_t i=0;i<monitors.size();++i)if(monitors[i].supported){if(i==index)reference=selected.size();selected.push_back(monitors[i]);keys.push_back(monitors[i].key);}
                if(selected.size()<2){Status(tr(L"Connect at least two DDC/CI monitors to match brightness."));return 0;}
                ShowWindow(window,SW_HIDE);calibration.Open(window,selected,reference,worker,[keys](const std::vector<Calibration>& curves){
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
    case WM_MEASUREITEM:if(wp==LanguageSelect){reinterpret_cast<MEASUREITEMSTRUCT*>(lp)->itemHeight=surface.S(22);return TRUE;}break;
    case WM_CTLCOLORLISTBOX:SetTextColor(reinterpret_cast<HDC>(wp),ui::colors.text);SetBkColor(reinterpret_cast<HDC>(wp),ui::colors.background);return reinterpret_cast<LRESULT>(ui::backgroundBrush);
    case WM_DRAWITEM: {
        auto& item=*reinterpret_cast<DRAWITEMSTRUCT*>(lp);
        if(item.CtlID==LanguageSelect){
            ui::Fill(item.hDC,item.rcItem,ui::colors.background);RECT r=item.rcItem;r.left+=surface.S(8);r.right-=surface.S(24);
            ui::Text(item.hDC,i18n::useRussian.load()?L"Русский":L"English",r,surface.body,ui::colors.text,DT_SINGLELINE|DT_VCENTER);
            RECT arrow{item.rcItem.right-surface.S(24),item.rcItem.top+surface.S(6),item.rcItem.right-surface.S(4),item.rcItem.bottom-surface.S(6)};ui::DrawGlyph(item.hDC,arrow,ui::Glyph::Chevron,ui::colors.muted);
            if((item.itemState&ODS_FOCUS)&&!(SendMessageW(item.hwndItem,WM_QUERYUISTATE,0,0)&UISF_HIDEFOCUS)){
                RECT line=item.rcItem;line.left+=surface.S(8);line.right-=surface.S(8);line.top=line.bottom-surface.S(3);line.bottom=line.top+surface.S(1);ui::Fill(item.hDC,line,ui::colors.accent);
            }return TRUE;
        }
        if(item.CtlID==Sync||item.CtlID==SettingsPage||item.CtlID==BrightnessPage){
            ui::Fill(item.hDC,item.rcItem,ui::colors.background);
            RECT r=item.rcItem;InflateRect(&r,-1,-1);
            if((item.CtlID==Sync&&syncing)||(item.itemState&ODS_SELECTED))ui::Round(item.hDC,r,ui::colors.hover,ui::colors.hover,surface.S(7));
            ui::DrawGlyph(item.hDC,r,item.CtlID==Sync?ui::Glyph::Link:item.CtlID==SettingsPage?ui::Glyph::Settings:ui::Glyph::Back,item.CtlID==Sync&&syncing?ui::colors.accent:ui::colors.text);
            if((item.itemState&ODS_FOCUS)&&!(SendMessageW(hwnd,WM_QUERYUISTATE,0,0)&UISF_HIDEFOCUS)){InflateRect(&r,-3,-3);DrawFocusRect(item.hDC,&r);}return TRUE;
        }
        if(ui::DrawButton(item,surface))return TRUE;
        break;
    }
    case WM_CTLCOLORSTATIC:case WM_CTLCOLORBTN:return ui::ColorControl(msg,wp,lp);
    case Result:HandleEvents();return 0;
    case Wheel:{int change=trayControl.TakeWheel(static_cast<int>(static_cast<INT_PTR>(wp)),lp!=0,preferences.wheelStep);if(change)TrayValue(change,true);return 0;}
    case WM_MOUSEWHEEL:{if(settingsPage)return SendMessageW(settingsPane,msg,wp,lp);int change=trayControl.TakeWheel(GET_WHEEL_DELTA_WPARAM(wp),(GET_KEYSTATE_WPARAM(wp)&MK_CONTROL)!=0,preferences.wheelStep);if(change)TrayValue(change,true);return 0;}
    case WM_CONTEXTMENU:SendMessageW(hwnd,Tray,0,WM_RBUTTONUP);return 0;
    case WM_DISPLAYCHANGE:popupAnchorValid=false;trayControl.InvalidatePosition();calibration.Cancel();SetTimer(hwnd,1,1500,nullptr);return 0;
    case WM_POWERBROADCAST:if(wp==PBT_APMRESUMEAUTOMATIC){calibration.Cancel();SetTimer(hwnd,1,1500,nullptr);}return TRUE;
    case AnimationFrame:{
        frameClock.Acknowledge();if(!framesRunning)return 0;
        auto stamp=render_profile::Stamp();if(previousFrameStamp)render_profile::Record("frame_interval",stamp-previousFrameStamp);previousFrameStamp=stamp;
        if(trayAnimating){AdvanceTrayAnimation(render_profile::NowMilliseconds());TrayIcon(NIM_MODIFY);RECT header=Box(16,16,32,32);InvalidateRect(hwnd,&header,FALSE);}
        bool progress=false;if(IsWindowVisible(hwnd)&&!settingsPage){
            if(syncing)progress=ui::AdvanceSliderActual(Control(SharedSlider));
            else for(size_t i=0;i<monitors.size();++i)progress=ui::AdvanceSliderActual(GetDlgItem(list,SliderBase+static_cast<int>(i)))||progress;
        }
        if(!trayAnimating&&!progress){frameClock.Stop();framesRunning=false;previousFrameStamp=0;}
        return 0;
    }
    case WM_TIMER:

        if(wp==1){KillTimer(hwnd,1);Scan();}return 0;
    case WM_SETTINGCHANGE:case WM_THEMECHANGED:
        ui::RefreshTheme();ui::Frame(hwnd);InvalidateRect(hwnd,nullptr,TRUE);InvalidateRect(list,nullptr,TRUE);
        {renderedTrayPosition=-1;TrayIcon(NIM_MODIFY);HICON previous=nullptr;
        previous=appIcon;appIcon=ui::CreateTrayIcon(false);SendMessageW(hwnd,WM_SETICON,ICON_SMALL,reinterpret_cast<LPARAM>(appIcon));SendMessageW(hwnd,WM_SETICON,ICON_BIG,reinterpret_cast<LPARAM>(appIcon));if(previous)DestroyIcon(previous);}
        trayControl.InvalidatePosition();
        RedrawWindow(hwnd,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN);
        if(calibration.Handle()){ui::Frame(calibration.Handle());RedrawWindow(calibration.Handle(),nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN);}return 0;
    case WM_DPICHANGED: {
        surface.Init(hwnd);RebuildRows();
        for(int id:{Sync,Refresh,Exit,BrightnessPage,SettingsPage,MatchSettings,LanguageSelect,SharedSlider})SendMessageW(Control(id),WM_SETFONT,reinterpret_cast<WPARAM>(surface.body),TRUE);
        for(int id=SpeedPresetBase;id<WheelPresetBase+3;++id)SendMessageW(Control(id),WM_SETFONT,reinterpret_cast<WPARAM>(surface.body),TRUE);
        RECT r=*reinterpret_cast<RECT*>(lp);SetWindowPos(hwnd,nullptr,r.left,r.top,r.right-r.left,r.bottom-r.top,SWP_NOZORDER);Layout();return 0;
    }
    case Tray:
        if(lp==WM_MOUSEMOVE&&trayControl.Hover()&&!trayAnimating)AnimateTray(1,280);
        if(lp==WM_LBUTTONUP){bool toggleClosed=dismissedOverTray&&GetTickCount64()-popupDismissedAt<GetDoubleClickTime();dismissedOverTray=false;if(calibration.Handle())Show();else if(IsWindowVisible(hwnd)||toggleClosed)ShowWindow(hwnd,SW_HIDE);else Show();}
        if(lp==WM_RBUTTONUP) {
            trayControl.Hover();popupMenu=true;HMENU menu=CreatePopupMenu();AppendMenuW(menu,MF_STRING,1,tr(L"Open"));
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
            int command=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,p.x,p.y,0,hwnd,nullptr);DestroyMenu(menu);popupMenu=false;PostMessageW(hwnd,CheckPopupFocus,0,0);
            if(command==1)Show();
            if(command==2){calibration.Cancel();DestroyWindow(hwnd);}
            if(command==10)TrayValue(preferences.wheelStep,true);if(command==11)TrayValue(-preferences.wheelStep,true);
            if(command==13){Show();SetPage(true);}
            if(command>=200&&command<=300)TrayValue(command-200,false);
            if(command==12)SendMessageW(hwnd,WM_COMMAND,Sync,0);
            if(command>=1000&&command<1000+static_cast<int>(monitors.size())){activeMonitor=static_cast<size_t>(command-1000);TrayIcon(NIM_MODIFY);}
            PostMessageW(hwnd,WM_NULL,0,0);
        }return 0;
    case WM_DESTROY:frameClock.Close();calibration.Cancel();trayControl.Stop();TrayIcon(NIM_DELETE);PostQuitMessage(0);return 0;
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}
}
void CreateControls(HINSTANCE instance){
    settingsPane=CreateWindowExW(WS_EX_CONTROLPARENT,L"BrightnessTray.Settings",tr(L"Settings"),WS_CHILD|WS_VSCROLL|WS_CLIPCHILDREN,0,0,1,1,window,reinterpret_cast<HMENU>(42),instance,nullptr);
    ui::Button(window,surface,syncing?tr(L"Monitor synchronization: on"):tr(L"Monitor synchronization: off"),Sync);
    ui::Button(window,surface,tr(L"Refresh"),Refresh);ui::Button(window,surface,tr(L"Exit"),Exit);
    ui::Button(window,surface,tr(L"Brightness"),BrightnessPage);ui::Button(window,surface,tr(L"Settings"),SettingsPage);
    for(int i=0;i<3;++i)ui::Button(window,surface,tr(ResponseLabels[i]),SpeedPresetBase+i,false,true);
    for(int i=0;i<3;++i)ui::Button(window,surface,std::to_wstring(WheelPresets[i])+L"%",WheelPresetBase+i,false,true);
    ui::Button(window,surface,tr(L"Configure"),MatchSettings,false,true);
    ui::Button(window,surface,tr(L"Language"),LanguageSelect);
    ui::Control(window,surface,TRACKBAR_CLASSW,tr(L"Shared brightness"),TBS_NOTICKS|WS_TABSTOP,SharedSlider);
    SendMessageW(Control(SharedSlider),TBM_SETRANGE,TRUE,MAKELPARAM(0,100));
    for(int id:{SpeedPresetBase,SpeedPresetBase+1,SpeedPresetBase+2,WheelPresetBase,WheelPresetBase+1,WheelPresetBase+2,MatchSettings,LanguageSelect,Refresh}){
        HWND child=Control(id);SetParent(child,settingsPane);SetWindowPos(child,HWND_BOTTOM,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);SetWindowSubclass(child,FocusReveal,89,0);
    }
    list=CreateWindowExW(WS_EX_CONTROLPARENT,L"BrightnessTray.List",tr(L"Monitors"),WS_CHILD|WS_VISIBLE|WS_VSCROLL|WS_CLIPCHILDREN,
        0,0,1,1,window,nullptr,instance,nullptr);
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR arguments,int) {
    wchar_t executable[MAX_PATH]{};GetModuleFileNameW(nullptr,executable,MAX_PATH);
    std::wstring executableLower=executable;std::transform(executableLower.begin(),executableLower.end(),executableLower.begin(),towlower);
    inspection=executableLower.find(L"brightnesstray.inspection.exe")!=std::wstring::npos;
    demo=inspection||std::wstring(arguments).find(L"--demo")!=std::wstring::npos || executableLower.find(L"brightnesstray.demo.exe")!=std::wstring::npos;
    if(std::wstring(arguments).find(L"--light")!=std::wstring::npos)ui::themeOverride=1;
    if(std::wstring(arguments).find(L"--dark")!=std::wstring::npos)ui::themeOverride=2;
    std::wstring mutexName=inspection?L"Local\\BrightnessTray.Native.Inspection":demo?L"Local\\BrightnessTray.Native.Demo":L"Local\\BrightnessTray.Native.Singleton";
    if(demo&&std::wstring(arguments).find(L"--settings=")!=std::wstring::npos)mutexName+=L".Test."+std::to_wstring(std::hash<std::wstring>{}(arguments));
    HANDLE singleton=CreateMutexW(nullptr,FALSE,mutexName.c_str());
    if(!singleton)return 1;
    if(GetLastError()==ERROR_ALREADY_EXISTS){
        HWND existing=FindWindowW(L"BrightnessTray.Native",inspection?L"BrightnessTray · Inspection":demo?L"BrightnessTray · Demo":L"BrightnessTray");
        if(existing){HWND popup=GetLastActivePopup(existing);SetWindowPos(existing,nullptr,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_SHOWWINDOW);SetForegroundWindow(popup);}
        CloseHandle(singleton);return 0;
    }
    if(std::wstring(arguments).find(L"--perf")!=std::wstring::npos){std::wstring path=executable;path=path.substr(0,path.find_last_of(L"\\/"))+L"\\render-perf.csv";render_profile::Open(path);}
    tracing=std::wstring(arguments).find(L"--trace")!=std::wstring::npos;
    if(tracing){std::wstring path=executable;path=path.substr(0,path.find_last_of(L"\\/"))+L"\\brightness-trace.csv";brightness_trace::Start(path.c_str());}
    wchar_t folder[MAX_PATH]{};
    if(!GetEnvironmentVariableW(L"LOCALAPPDATA",folder,MAX_PATH)){CloseHandle(singleton);return 1;}
    settings=std::wstring(folder)+L"\\BrightnessTray";CreateDirectoryW(settings.c_str(),nullptr);
    settings+=demo?L"\\settings-demo.ini":L"\\settings.ini";
    if(demo){std::wstring args=arguments;auto pos=args.find(L"--settings=");if(pos!=std::wstring::npos){auto end=args.find(L" --",pos);settings=args.substr(pos+11,end==std::wstring::npos?end:end-pos-11);if(settings.size()>1&&settings.front()==L'"'&&settings.back()==L'"')settings=settings.substr(1,settings.size()-2);}}
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
    wc.lpfnWndProc=SettingsProc;wc.lpszClassName=L"BrightnessTray.Settings";RegisterClassW(&wc);
    wc.lpfnWndProc=ListProc;wc.lpszClassName=L"BrightnessTray.List";RegisterClassW(&wc);
    taskbarCreated=RegisterWindowMessageW(L"TaskbarCreated");
    window=CreateWindowExW((inspection?WS_EX_APPWINDOW:WS_EX_TOOLWINDOW)|WS_EX_TOPMOST|WS_EX_CONTROLPARENT,L"BrightnessTray.Native",inspection?L"BrightnessTray · Inspection":demo?L"BrightnessTray · Demo":L"BrightnessTray",
        WS_POPUP|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,352,420,nullptr,nullptr,instance,nullptr);
    if(!window){CloseHandle(singleton);return 1;}
    surface.Init(window);ui::Frame(window);SendMessageW(window,WM_CHANGEUISTATE,MAKEWPARAM(UIS_SET,UISF_HIDEFOCUS),0);trayIcon=ui::CreateTrayIcon();appIcon=ui::CreateTrayIcon(false);
    SendMessageW(window,WM_SETICON,ICON_SMALL,reinterpret_cast<LPARAM>(appIcon));SendMessageW(window,WM_SETICON,ICON_BIG,reinterpret_cast<LPARAM>(appIcon));
    CreateControls(instance);
    RECT r{0,0,surface.S(352),surface.S(420)};AdjustWindowRectExForDpi(&r,static_cast<DWORD>(GetWindowLongPtrW(window,GWL_STYLE)),FALSE,
        static_cast<DWORD>(GetWindowLongPtrW(window,GWL_EXSTYLE)),surface.dpi);
    SetWindowPos(window,nullptr,0,0,r.right-r.left,r.bottom-r.top,SWP_NOMOVE|SWP_NOZORDER);
    UpdatePreferencesControls();SetPage(false);Layout();TrayIcon(NIM_ADD);bool wheelInstalled=trayControl.Start(window,Wheel);worker.Start(window,Result,demo);Scan();Show();
    if(!wheelInstalled)Status(tr(L"Mouse wheel unavailable. Use the tray menu to adjust brightness."));
    MSG message{};
    while(GetMessageW(&message,nullptr,0,0)>0) {
        if(message.message==WM_KEYDOWN&&message.wParam==VK_ESCAPE&&!languageDropped) {
            if(calibration.Handle())calibration.Cancel();else ShowWindow(window,SW_HIDE);
            continue;
        }
        HWND dialog=calibration.Handle()?calibration.Handle():window;
        if(!IsDialogMessageW(dialog,&message)){TranslateMessage(&message);DispatchMessageW(&message);}
    }
    worker.Stop();brightness_trace::Stop();render_profile::Close();DestroyIcon(trayIcon);DestroyIcon(appIcon);popupPaintBuffer.Clear();listPaintBuffer.Clear();settingsPaintBuffer.Clear();surface.Clear();
    DeleteObject(ui::backgroundBrush);DeleteObject(ui::cardBrush);CloseHandle(singleton);return 0;
}


