// Exercise production window/layout/focus handlers without starting the worker,
// tray hook, or hardware I/O. The application entry point is not invoked.
#include "../src/main.cpp"
#include <iostream>
#include <stdexcept>
bool quietUi=false;
void CheckUi(bool value,const char* message){if(!value)throw std::runtime_error(message);if(!quietUi)std::cout<<"PASS: "<<message<<std::endl;}
RECT InParent(HWND child){RECT r{};GetWindowRect(child,&r);MapWindowPoints(nullptr,GetParent(child),reinterpret_cast<POINT*>(&r),2);return r;}
bool Within(HWND child){auto r=InParent(child);RECT viewport{};GetClientRect(GetParent(child),&viewport);return r.top>=0&&r.bottom<=viewport.bottom&&r.left>=0&&r.right<=viewport.right;}
void CaptureUi(HWND target,const wchar_t* name){
    RedrawWindow(target,nullptr,nullptr,RDW_INVALIDATE|RDW_UPDATENOW|RDW_ALLCHILDREN);
    RECT outer;GetWindowRect(target,&outer);RECT r{0,0,outer.right-outer.left,outer.bottom-outer.top};HDC dc=GetWindowDC(target),buffer=CreateCompatibleDC(dc);BITMAPINFO info{};info.bmiHeader={sizeof(BITMAPINFOHEADER),r.right,-r.bottom,1,32,BI_RGB};void* bits=nullptr;auto bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&bits,nullptr,0);auto old=SelectObject(buffer,bitmap);PrintWindow(target,buffer,2);
    wchar_t exe[MAX_PATH]{};GetModuleFileNameW(nullptr,exe,MAX_PATH);std::wstring path=exe;path=path.substr(0,path.find_last_of(L"\\/"))+L"\\"+name;
    BITMAPFILEHEADER header{};header.bfType=0x4d42;header.bfOffBits=sizeof(header)+sizeof(BITMAPINFOHEADER);header.bfSize=header.bfOffBits+r.right*r.bottom*4;DWORD written;
    HANDLE out=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,0,nullptr);WriteFile(out,&header,sizeof(header),&written,nullptr);WriteFile(out,&info.bmiHeader,sizeof(info.bmiHeader),&written,nullptr);WriteFile(out,bits,r.right*r.bottom*4,&written,nullptr);CloseHandle(out);SelectObject(buffer,old);DeleteObject(bitmap);DeleteDC(buffer);ReleaseDC(target,dc);
}
void SmallSettings(){
    SetPage(true);layoutBusy=true;SetWindowPos(window,nullptr,100,100,surface.S(320),surface.S(244),SWP_NOZORDER|SWP_NOACTIVATE);layoutBusy=false;
    ui::Place(settingsPane,surface,0,52,320,192);settingsOffset=0;LayoutSettings();
}
void WaitUi(const std::function<bool()>& ready){
    auto deadline=GetTickCount64()+5000;
    do{HandleEvents();if(ready())return;Sleep(5);}while(GetTickCount64()<deadline);
    throw std::runtime_error("backend did not settle in UI regression");
}
void CheckButtonLabel(HWND button,int horizontalPadding=16){
    if(!button||!IsWindowVisible(button))return;
    wchar_t text[256]{};GetWindowTextW(button,text,256);RECT r{};GetClientRect(button,&r);
    HDC dc=GetDC(button);auto old=SelectObject(dc,reinterpret_cast<HFONT>(SendMessageW(button,WM_GETFONT,0,0)));
    RECT measured{};DrawTextW(dc,text,-1,&measured,DT_CALCRECT|DT_SINGLELINE|DT_NOPREFIX);
    SelectObject(dc,old);ReleaseDC(button,dc);
    UINT dpi=surface.dpi;
    if(r.right<measured.right+MulDiv(horizontalPadding,static_cast<int>(dpi),96)||r.bottom<measured.bottom+MulDiv(6,static_cast<int>(dpi),96))
        throw std::runtime_error("localized button label is clipped at current DPI");
    if(!Within(button))throw std::runtime_error("visible control extends beyond its viewport");
}
void CheckCompactLayouts(){
    frameClock.Stop();framesRunning=false;quietUi=true;
    for(const wchar_t* language:{L"en",L"ru"})for(int theme:{1,2})for(UINT dpi:{96u,120u,144u,192u}){
        ui::themeOverride=theme;ui::RefreshTheme();
        SendMessageW(window,WM_COMMAND,wcscmp(language,L"ru")==0?LanguageRu:LanguageEn,0);
        RECT requested{100,100,100+MulDiv(320,static_cast<int>(dpi),96),100+MulDiv(348,static_cast<int>(dpi),96)};
        SendMessageW(window,WM_DPICHANGED,MAKEWPARAM(dpi,dpi),reinterpret_cast<LPARAM>(&requested));
        syncing=true;SetPage(false);Status(tr(L"Brightness applied."));
        SendMessageW(Control(SharedSlider),TBM_SETPOS,TRUE,50);SendMessageW(window,WM_HSCROLL,TB_THUMBPOSITION,reinterpret_cast<LPARAM>(Control(SharedSlider)));WaitUi([]{return latestWrites.empty();});
        CheckUi(Width(window)==320&&Height(window)==250,"two-monitor linked popup is 320 by 250 DIPs at each locale/theme/DPI");
        if(dpi==96||dpi==192){std::wstring name=L"compact-main-"+std::wstring(language)+L"-"+std::to_wstring(dpi)+L"-"+std::to_wstring(theme)+L".bmp";CaptureUi(window,name.c_str());}
        Status(tr(L"The monitor did not respond. Try again or refresh the list."));
        HDC dc=GetDC(window);auto old=SelectObject(dc,surface.caption);RECT error{0,0,surface.S(288),0};
        DrawTextW(dc,status.c_str(),-1,&error,DT_CALCRECT|DT_WORDBREAK|DT_NOPREFIX);SelectObject(dc,old);ReleaseDC(window,dc);
        CheckUi(surface.S(ErrorHeight()-12)>=error.bottom,"error text gets its measured height without overlapping controls");
        SetPage(true);MONITORINFO work{sizeof(work)};GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&work);
        int available=MulDiv(work.rcWork.bottom-work.rcWork.top,96,static_cast<int>(dpi))-24;
        CheckUi(Width(window)==320&&Height(window)==std::min(348,available),"compact settings fit their content or the available work area at each DPI");
        for(int id:{SpeedPresetBase,SpeedPresetBase+1,SpeedPresetBase+2,WheelPresetBase,WheelPresetBase+1,WheelPresetBase+2,MatchSettings,CalibrationToggle,Refresh}){SetFocus(Control(id));CheckButtonLabel(Control(id));}
        SetFocus(Control(LanguageSelect));CheckButtonLabel(Control(LanguageSelect),32);
        settingsOffset=0;LayoutSettings();
        for(const wchar_t* hint:{tr(L"No intermediate steps."),tr(L"Fast response to brightness changes."),tr(L"Smaller steps for a gentler transition.")}){
            dc=GetDC(window);old=SelectObject(dc,surface.caption);RECT measured{};DrawTextW(dc,hint,-1,&measured,DT_CALCRECT|DT_SINGLELINE|DT_NOPREFIX);SelectObject(dc,old);ReleaseDC(window,dc);
            CheckUi(measured.right<=surface.S(288)&&measured.bottom<=surface.S(18),"all response hints fit without clipping");
        }
        if(dpi==96||dpi==192){std::wstring name=L"compact-settings-"+std::wstring(language)+L"-"+std::to_wstring(dpi)+L"-"+std::to_wstring(theme)+L".bmp";CaptureUi(window,name.c_str());}
        SetPage(false);SendMessageW(window,WM_COMMAND,CalibrateBase,0);
        WaitUi([]{return calibration.Handle()&&IsWindowEnabled(GetDlgItem(calibration.Handle(),102));});
        RECT calRect{100,100,100+MulDiv(420,static_cast<int>(dpi),96),100+MulDiv(520,static_cast<int>(dpi),96)};
        SendMessageW(calibration.Handle(),WM_DPICHANGED,MAKEWPARAM(dpi,dpi),reinterpret_cast<LPARAM>(&calRect));
        for(int step=0;step<7;++step){
            auto cal=calibration.Handle();
            SendMessageW(cal,WM_VSCROLL,SB_BOTTOM,0);for(int id:{101,102,103})CheckButtonLabel(GetDlgItem(cal,id));
            SendMessageW(cal,WM_VSCROLL,SB_TOP,0);CheckButtonLabel(GetDlgItem(cal,104));
            RECT outer{};GetWindowRect(cal,&outer);
            CheckUi(outer.right-outer.left<=surface.S(420),"calibration keeps its compact width");
            if(dpi==96)CheckUi(outer.bottom-outer.top<=surface.S(560),"calibration uses measured content instead of a 640-DIP fixed height");
            if(dpi==96||dpi==192){std::wstring name=L"compact-cal-"+std::wstring(language)+L"-"+std::to_wstring(dpi)+L"-"+std::to_wstring(theme)+L"-"+std::to_wstring(step)+L".bmp";CaptureUi(cal,name.c_str());}
            HWND rows=FindWindowExW(cal,nullptr,L"BrightnessTray.MatchRows",nullptr);
            RECT rowBounds=InParent(rows);
            for(int rowHeight:{160,40}){
                SetWindowPos(rows,nullptr,0,0,rowBounds.right-rowBounds.left,surface.S(rowHeight),SWP_NOMOVE|SWP_NOZORDER);
                RECT viewport{};GetClientRect(rows,&viewport);
                auto sliderBounds=InParent(GetDlgItem(rows,200));
                CheckUi(sliderBounds.left>=0&&sliderBounds.right<=viewport.right,"calibration slider width follows scrollbar visibility");
            }
            SetWindowPos(rows,nullptr,0,0,rowBounds.right-rowBounds.left,rowBounds.bottom-rowBounds.top,SWP_NOMOVE|SWP_NOZORDER);
            SendMessageW(cal,WM_SIZE,0,0);
            for(int id:{200,201}){HWND slider=GetDlgItem(rows,id);if(IsWindowVisible(slider)&&IsWindowEnabled(slider)){SetFocus(slider);if(!Within(slider)){auto bounds=InParent(slider);RECT viewport{};GetClientRect(rows,&viewport);std::wcerr<<language<<L" theme="<<theme<<L" DPI="<<dpi<<L" step="<<step<<L" slider="<<id<<L" bounds="<<bounds.left<<L","<<bounds.top<<L","<<bounds.right<<L","<<bounds.bottom<<L" viewport="<<viewport.right<<L","<<viewport.bottom<<std::endl;CaptureUi(cal,L"failed-calibration-focus.bmp");}CheckUi(Within(slider),"keyboard focus reveals calibration sliders in a constrained viewport");}}
            if(step<6){SendMessageW(cal,WM_COMMAND,102,0);WaitUi([]{return IsWindowEnabled(GetDlgItem(calibration.Handle(),102));});}
        }
        calibration.Cancel();WaitUi([]{return latestWrites.empty();});
    }
    quietUi=false;CheckUi(true,"compact layout matrix passed: English/Russian, light/dark, 100/125/150/200% DPI, all wizard steps");
    auto saved=monitors;monitors.clear();syncing=true;RebuildRows();SetPage(false);
    CheckUi(Height(list)>=78,"linked empty state keeps room for its complete refresh hint");
    CaptureUi(window,L"compact-empty.bmp");monitors=saved;RebuildRows();
    MONITORINFO screen{sizeof(screen)};GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&screen);
    auto stacked=monitors;stacked[0].bounds=screen.rcMonitor;stacked[1].bounds=screen.rcMonitor;
    OffsetRect(&stacked[1].bounds,0,screen.rcMonitor.bottom-screen.rcMonitor.top);
    calibration.Open(window,stacked,0,worker,[](const std::vector<Calibration>&){return false;});
    WaitUi([]{return calibration.Handle()&&IsWindowEnabled(GetDlgItem(calibration.Handle(),102));});
    RECT requested{screen.rcWork.left+24,screen.rcWork.top+24,screen.rcWork.left+24+surface.S(420),screen.rcWork.top+24+surface.S(520)};
    SendMessageW(calibration.Handle(),WM_DPICHANGED,MAKEWPARAM(192,192),reinterpret_cast<LPARAM>(&requested));
    RECT wizard{};GetWindowRect(calibration.Handle(),&wizard);
    int strip=std::min(surface.S(240),static_cast<int>(screen.rcMonitor.bottom-screen.rcMonitor.top)/4);
    CheckUi(wizard.bottom<=screen.rcMonitor.bottom-strip-surface.S(12),"high-DPI wizard stays above the comparison strip on stacked displays");
    for(int id:{102,103}){SetFocus(GetDlgItem(calibration.Handle(),id));CheckButtonLabel(GetDlgItem(calibration.Handle(),id));}
    CaptureUi(calibration.Handle(),L"compact-stacked-200.bmp");calibration.Cancel();
}
int main(){int result=0;try{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);INITCOMMONCONTROLSEX common{sizeof(common),ICC_BAR_CLASSES};InitCommonControlsEx(&common);ui::themeOverride=1;ui::RefreshTheme();i18n::Initialize(L"ru");
    HINSTANCE instance=GetModuleHandleW(nullptr);WNDCLASSW wc{};wc.hInstance=instance;wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.lpfnWndProc=WindowProc;wc.lpszClassName=L"BrightnessTray.Native";RegisterClassW(&wc);wc.lpfnWndProc=SettingsProc;wc.lpszClassName=L"BrightnessTray.Settings";RegisterClassW(&wc);wc.lpfnWndProc=ListProc;wc.lpszClassName=L"BrightnessTray.List";RegisterClassW(&wc);
    window=CreateWindowExW(WS_EX_CONTROLPARENT,L"BrightnessTray.Native",L"UI layout regression",WS_POPUP|WS_CLIPCHILDREN,100,100,352,420,nullptr,nullptr,instance,nullptr);CheckUi(window!=nullptr,"layout test owns its window");surface.Init(window);CreateControls(instance);UpdatePreferencesControls();
    scanning=false;syncing=false;for(int i=0;i<4;++i){Monitor m{};m.name=L"Monitor "+std::to_wstring(i+1);m.key=std::to_wstring(i);m.maximum=100;m.current=50;m.supported=true;monitors.push_back(m);}RebuildRows();SetPage(false);ShowWindow(window,SW_SHOWNOACTIVATE);
    CheckUi(Width(window)<=320&&Height(window)<=320,"four-monitor popup stays within a compact 320-DIP footprint");
    SetPage(true);CheckUi(Height(window)<=348,"settings fit inside 348 DIPs without shrinking the text");SetPage(false);
    HWND fourth=GetDlgItem(list,SliderBase+3);CheckUi(!Within(fourth),"fourth monitor begins outside viewport");SetFocus(fourth);CheckUi(GetFocus()==fourth&&Within(fourth),"focusing fourth monitor scrolls slider into view");
    HWND first=GetDlgItem(list,SliderBase);SetFocus(first);CheckUi(Within(first),"reverse focus scrolls first monitor into view");
    // Run the actual dialog keyboard dispatcher (the same path as the app loop).
    HWND lastOptions=GetDlgItem(list,OptionsBase+2);SetFocus(lastOptions);MSG key{};key.hwnd=lastOptions;key.message=WM_KEYDOWN;key.wParam=VK_TAB;
    IsDialogMessageW(window,&key);CheckUi(GetFocus()==fourth&&Within(fourth),"Tab to fourth monitor reveals focused slider");CaptureUi(window,L"native-four-monitor-focus.bmp");
    SmallSettings();CheckUi(!Within(Control(Refresh)),"short settings viewport initially clips lower controls");
    SetFocus(Control(MatchSettings));key.hwnd=Control(MatchSettings);IsDialogMessageW(window,&key);CheckUi(GetFocus()==Control(CalibrationToggle)&&Within(Control(CalibrationToggle)),"Tab reveals the calibration mode control");
    key.hwnd=Control(CalibrationToggle);IsDialogMessageW(window,&key);CheckUi(GetFocus()==Control(LanguageSelect)&&Within(Control(LanguageSelect)),"Tab to language scrolls inside short settings viewport");
    SendMessageW(window,WM_CHANGEUISTATE,MAKEWPARAM(UIS_CLEAR,UISF_HIDEFOCUS),0);CaptureUi(window,L"native-short-language-focus.bmp");
    SetFocus(Control(Refresh));CheckUi(Within(Control(Refresh)),"focused refresh scrolls inside short settings viewport");CaptureUi(window,L"native-short-refresh.bmp");
    SetFocus(Control(SpeedPresetBase));CheckUi(Within(Control(SpeedPresetBase)),"reverse settings focus returns to top");
    SmallSettings();settingsWheelRemainder=0;for(int i=0;i<4;++i)SendMessageW(settingsPane,WM_MOUSEWHEEL,MAKEWPARAM(0,static_cast<WORD>(-30)),0);CheckUi(settingsOffset==48,"settings accumulates four partial wheel deltas");
    SetPage(false);scrollOffset=0;listWheelRemainder=0;LayoutRows();for(int i=0;i<4;++i)SendMessageW(list,WM_MOUSEWHEEL,MAKEWPARAM(0,static_cast<WORD>(-30)),0);CheckUi(scrollOffset==48,"monitor list accumulates partial wheel deltas");SmallSettings();
    SendMessageW(settingsPane,WM_VSCROLL,SB_BOTTOM,0);CheckUi(Within(Control(Refresh)),"settings scrollbar reaches refresh");
    // Compare the actual owner-draw output with/without ODS_FOCUS.
    HWND language=Control(LanguageSelect);RECT r;GetClientRect(language,&r);HDC dc=GetDC(language),buffer=CreateCompatibleDC(dc);auto bitmap=CreateCompatibleBitmap(dc,r.right,r.bottom);auto old=SelectObject(buffer,bitmap);DRAWITEMSTRUCT item{};item.CtlType=ODT_BUTTON;item.CtlID=LanguageSelect;item.hwndItem=language;item.hDC=buffer;item.rcItem=r;
    SendMessageW(window,WM_DRAWITEM,LanguageSelect,reinterpret_cast<LPARAM>(&item));int x=surface.S(12),y=r.bottom-surface.S(3);COLORREF before=GetPixel(buffer,x,y);item.itemState=ODS_FOCUS;SendMessageW(window,WM_DRAWITEM,LanguageSelect,reinterpret_cast<LPARAM>(&item));CheckUi(before!=GetPixel(buffer,x,y)&&GetPixel(buffer,x,y)==ui::colors.accent,"language keyboard focus has visible underline pixels");
    SendMessageW(window,WM_CHANGEUISTATE,MAKEWPARAM(UIS_SET,UISF_HIDEFOCUS),0);SendMessageW(window,WM_DRAWITEM,LanguageSelect,reinterpret_cast<LPARAM>(&item));CheckUi(GetPixel(buffer,x,y)==before,"mouse focus does not force keyboard underline");SelectObject(buffer,old);DeleteObject(bitmap);DeleteDC(buffer);ReleaseDC(language,dc);
    layoutBusy=true;SetWindowPos(window,nullptr,100,100,surface.S(320),surface.S(348),SWP_NOZORDER|SWP_NOACTIVATE);layoutBusy=false;ui::Place(settingsPane,surface,0,52,320,296);LayoutSettings();CheckUi(settingsOffset==0&&Within(Control(Refresh)),"expanded settings restores full content without scroll");
    // A real demo lane succeeds while a disconnected lane fails in the same batch.
    // Failure must survive regardless of which monitor is reported last.
    worker.SetTransitionDuration(0);worker.Start(nullptr,0,true);Scan();WaitUi([]{return !scanning;});
    monitors[0].key=L"Disconnected";syncing=true;RebuildRows();SetPage(false);
    SendDlgItemMessageW(list,SliderBase,TBM_SETPOS,TRUE,30);Apply(0);
    WaitUi([]{return latestWrites.empty();});
    CheckUi(StatusIsError(),"partial linked write failure survives a later success");
    monitors[0].key=L"Demo_0";
    monitors[1].key=L"Disconnected";RebuildRows();
    SendDlgItemMessageW(list,SliderBase,TBM_SETPOS,TRUE,35);Apply(0);WaitUi([]{return latestWrites.empty();});
    CheckUi(StatusIsError(),"partial linked failure is reported in the reverse completion order");
    monitors[1].key=L"Demo_1";RebuildRows();
    SendDlgItemMessageW(list,SliderBase,TBM_SETPOS,TRUE,45);Apply(0);WaitUi([]{return latestWrites.empty();});
    CheckUi(!StatusIsError()&&monitors[0].current==45&&monitors[1].current==45,"successful linked retry clears the prior operation failure");
    worker.Queue({Operation::Write,L"Demo_0",22,NextTag()});
    WaitUi([]{return monitors[0].current==22;});
    CheckUi(SendMessageW(Control(SharedSlider),TBM_GETPOS,0,0)==22,"shared target follows an untracked calibration restoration");
    wchar_t executable[MAX_PATH]{};GetModuleFileNameW(nullptr,executable,MAX_PATH);settings=executable;
    settings=settings.substr(0,settings.find_last_of(L"\\/"))+L"\\reset-regression-"+std::to_wstring(GetCurrentProcessId())+L".ini";
    syncing=false;worker.SetTransitionDuration(600);monitors[0].calibration={0,20,60,10,40};RebuildRows();
    SendDlgItemMessageW(list,SliderBase,TBM_SETPOS,TRUE,75);Apply(0);
    SendMessageW(window,WM_COMMAND,ResetBase,0);
    CheckUi(SendDlgItemMessageW(list,SliderBase,TBM_GETPOS,0,0)==40,"reset translates a pending target into the new calibration");
    WaitUi([]{return latestWrites.empty();});
    CheckUi(monitors[0].current==40&&SendDlgItemMessageW(list,SliderBase,TBM_GETPOS,0,0)==40,"reset preserves the hardware destination and finishes with a matching thumb");
    worker.SetTransitionDuration(0);preferences.wheelStep=2;
    frameClock.Stop();framesRunning=false;sunMotion=SunMotion{};trayAnimating=false;
    SendMessageW(window,Tray,0,WM_MOUSEMOVE);
    CheckUi(!sunMotion.Active(),"tray hover does not start sun rotation");
    SendDlgItemMessageW(list,SliderBase,TBM_SETPOS,TRUE,40);Apply(0);WaitUi([]{return latestWrites.empty();});
    CheckUi(!sunMotion.Active(),"slider adjustment without wheel does not rotate the tray icon");
    auto slider=GetDlgItem(list,SliderBase);SendMessageW(slider,TBM_SETPOS,TRUE,40);
    SendMessageW(slider,WM_MOUSEWHEEL,MAKEWPARAM(0,WHEEL_DELTA),0);
    CheckUi(SendMessageW(slider,TBM_GETPOS,0,0)==42,"slider wheel up raises brightness by the configured tray step");
    WaitUi([]{return latestWrites.empty();});
    SendMessageW(slider,WM_MOUSEWHEEL,MAKEWPARAM(MK_CONTROL,static_cast<WORD>(-WHEEL_DELTA)),0);
    CheckUi(SendMessageW(slider,TBM_GETPOS,0,0)==41,"slider wheel down lowers brightness with Ctrl precision");
    WaitUi([]{return latestWrites.empty();});
    for(int i=0;i<4;++i)SendMessageW(slider,WM_MOUSEWHEEL,MAKEWPARAM(0,30),0);
    CheckUi(SendMessageW(slider,TBM_GETPOS,0,0)==43,"slider accumulates partial positive wheel deltas");
    WaitUi([]{return latestWrites.empty();});
    SendMessageW(window,WM_POWERBROADCAST,PBT_APMSUSPEND,0);
    SendMessageW(window,WM_POWERBROADCAST,PBT_APMRESUMEAUTOMATIC,0);
    SendMessageW(window,WM_TIMER,1,0);
    WaitUi([]{return !scanning&&latestWrites.empty();});
    CheckUi(monitors[0].current==43,"resume restores the brightness target after monitor state is reset");
    monitors[0].calibration={0,20,60,10,40};CheckUi(Save(monitors[0],monitors[0].calibration),"matched curve fixture is saved");RebuildRows();
    SendMessageW(window,WM_COMMAND,43,0); // Toggle saved calibration off.
    SendDlgItemMessageW(list,SliderBase,TBM_SETPOS,TRUE,100);Apply(0);WaitUi([]{return latestWrites.empty();});
    CheckUi(monitors[0].current==100&&monitors[0].calibration.Matched(),"disabled calibration exposes hardware maximum without deleting the curve");
    SendMessageW(window,WM_COMMAND,43,0);
    SendDlgItemMessageW(list,SliderBase,TBM_SETPOS,TRUE,100);Apply(0);WaitUi([]{return latestWrites.empty();});
    CheckUi(monitors[0].current==60,"reenabled calibration restores the saved matched maximum");
    CheckUi(GetPrivateProfileIntW(L"General",L"CalibrationEnabled",0,settings.c_str())==1,"calibration enable state is persisted");
    worker.SetTransitionDuration(600);SendMessageW(window,WM_COMMAND,CalibrationToggle,0);
    SendDlgItemMessageW(list,SliderBase,TBM_SETPOS,TRUE,100);Apply(0);
    SendMessageW(window,WM_COMMAND,CalibrationToggle,0);SendMessageW(window,WM_COMMAND,CalibrationToggle,0);
    CheckUi(requestedLevels[monitors[0].key]==100&&requestedRawLevels[monitors[0].key]==100,"two mode switches preserve a pending target outside the saved curve");
    RememberBrightness();CheckUi(recoveryTargets[monitors[0].key]==100,"sleep remembers the exact pending hardware target across mode changes");
    EndRecovery();WaitUi([]{return latestWrites.empty();});SendMessageW(window,WM_COMMAND,CalibrationToggle,0);worker.SetTransitionDuration(0);
    SendMessageW(window,WM_POWERBROADCAST,PBT_APMSUSPEND,0);
    SendMessageW(window,WM_POWERBROADCAST,PBT_APMRESUMEAUTOMATIC,0);
    recoveryTargets[L"MissingMonitor"]=70;
    for(unsigned attempt=1;attempt<=5;++attempt){
        SendMessageW(window,WM_TIMER,1,0);WaitUi([]{return !scanning&&recoveryWrites.empty();});
        CheckUi(attempt==5?!recovering:recovering,"resume retries unavailable monitors with a bounded retry count");
    }
    EndRecovery();
    SendMessageW(window,WM_POWERBROADCAST,PBT_APMSUSPEND,0);
    SendMessageW(window,WM_POWERBROADCAST,PBT_APMRESUMEAUTOMATIC,0);
    SendDlgItemMessageW(list,SliderBase,TBM_SETPOS,TRUE,50);Apply(0);WaitUi([]{return latestWrites.empty();});
    CheckUi(!recovering&&recoveryTargets.empty()&&monitors[0].current==20,"new user input cancels delayed restoration");
    RememberBrightness();SendDlgItemMessageW(list,SliderBase,TBM_SETPOS,TRUE,75);Apply(0);
    CheckUi(recoveryTargets.empty(),"input before the wake notification discards the older sleep snapshot");
    WaitUi([]{return latestWrites.empty();});
    SendMessageW(window,WM_COMMAND,CalibrateBase,0);
    WaitUi([]{return calibration.Handle()&&IsWindowEnabled(GetDlgItem(calibration.Handle(),102));});
    SendMessageW(calibration.Handle(),WM_COMMAND,102,0);
    WaitUi([]{return IsWindowEnabled(GetDlgItem(calibration.Handle(),102));});
    auto rows=FindWindowExW(calibration.Handle(),nullptr,L"BrightnessTray.MatchRows",nullptr);
    auto sample=GetDlgItem(rows,200);SendMessageW(sample,TBM_SETPOS,TRUE,100);SendMessageW(calibration.Handle(),WM_HSCROLL,TB_THUMBPOSITION,reinterpret_cast<LPARAM>(sample));
    WaitUi([]{return IsWindowEnabled(GetDlgItem(calibration.Handle(),102));});
    SendMessageW(window,WM_POWERBROADCAST,PBT_APMSUSPEND,0);
    CheckUi(!calibration.Handle()&&recoveryTargets[L"Demo_0"]==40,"sleep during calibration remembers original brightness instead of the sample");
    SendMessageW(window,WM_POWERBROADCAST,PBT_APMRESUMEAUTOMATIC,0);SendMessageW(window,WM_TIMER,1,0);
    WaitUi([]{return !scanning&&!recovering&&latestWrites.empty();});
    CheckUi(monitors[0].current==40,"wake restores the original pre-calibration brightness");
    struct DisplayNotice{GUID PowerSetting;DWORD DataLength,state;} notice{DisplayPowerSetting,sizeof(DWORD),0};
    SendMessageW(window,WM_POWERBROADCAST,PBT_POWERSETTINGCHANGE,reinterpret_cast<LPARAM>(&notice));
    worker.Queue({Operation::Write,L"Demo_0",5,NextTag()});WaitUi([]{return monitors[0].current==5;});
    notice.state=1;SendMessageW(window,WM_POWERBROADCAST,PBT_POWERSETTINGCHANGE,reinterpret_cast<LPARAM>(&notice));
    SendMessageW(window,WM_TIMER,1,0);WaitUi([]{return !scanning&&!recovering&&latestWrites.empty();});
    CheckUi(monitors[0].current==40,"display-off and display-on notifications restore brightness without system sleep");
    worker.SetTransitionDuration(600);SendDlgItemMessageW(list,SliderBase,TBM_SETPOS,TRUE,100);Apply(0);
    SendMessageW(window,WM_COMMAND,CalibrateBase,0);
    SendMessageW(window,WM_POWERBROADCAST,PBT_APMSUSPEND,0);
    CheckUi(recoveryTargets[L"Demo_0"]==60,"sleep before wizard reads finish retains an unfinished user target");
    SendMessageW(window,WM_POWERBROADCAST,PBT_APMRESUMEAUTOMATIC,0);SendMessageW(window,WM_TIMER,1,0);
    WaitUi([]{return !scanning&&!recovering&&latestWrites.empty();});
    CheckUi(monitors[0].current==60,"resume completes the target interrupted by wizard initialization");
    CheckCompactLayouts();
}catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<std::endl;result=1;}
calibration.Cancel();
worker.Stop();
if(!settings.empty())DeleteFileW(settings.c_str());
if(window)DestroyWindow(window);popupPaintBuffer.Clear();listPaintBuffer.Clear();settingsPaintBuffer.Clear();surface.Clear();if(ui::backgroundBrush)DeleteObject(ui::backgroundBrush);if(ui::cardBrush)DeleteObject(ui::cardBrush);return result;}
