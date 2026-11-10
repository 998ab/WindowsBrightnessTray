// Exercise production window/layout/focus handlers without starting the worker,
// tray hook, or hardware I/O. The application entry point is not invoked.
#include "../src/main.cpp"
#include <iostream>
#include <stdexcept>
void CheckUi(bool value,const char* message){if(!value)throw std::runtime_error(message);std::cout<<"PASS: "<<message<<std::endl;}
RECT InParent(HWND child){RECT r{};GetWindowRect(child,&r);MapWindowPoints(nullptr,GetParent(child),reinterpret_cast<POINT*>(&r),2);return r;}
bool Within(HWND child){auto r=InParent(child);RECT viewport{};GetClientRect(GetParent(child),&viewport);return r.top>=0&&r.bottom<=viewport.bottom&&r.left>=0&&r.right<=viewport.right;}
void CaptureUi(HWND target,const wchar_t* name){
    RECT r;GetClientRect(target,&r);HDC dc=GetDC(target),buffer=CreateCompatibleDC(dc);BITMAPINFO info{};info.bmiHeader={sizeof(BITMAPINFOHEADER),r.right,-r.bottom,1,32,BI_RGB};void* bits=nullptr;auto bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&bits,nullptr,0);auto old=SelectObject(buffer,bitmap);PrintWindow(target,buffer,2);
    wchar_t exe[MAX_PATH]{};GetModuleFileNameW(nullptr,exe,MAX_PATH);std::wstring path=exe;path=path.substr(0,path.find_last_of(L"\\/"))+L"\\"+name;
    BITMAPFILEHEADER header{};header.bfType=0x4d42;header.bfOffBits=sizeof(header)+sizeof(BITMAPINFOHEADER);header.bfSize=header.bfOffBits+r.right*r.bottom*4;DWORD written;
    HANDLE out=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,0,nullptr);WriteFile(out,&header,sizeof(header),&written,nullptr);WriteFile(out,&info.bmiHeader,sizeof(info.bmiHeader),&written,nullptr);WriteFile(out,bits,r.right*r.bottom*4,&written,nullptr);CloseHandle(out);SelectObject(buffer,old);DeleteObject(bitmap);DeleteDC(buffer);ReleaseDC(target,dc);
}
void SmallSettings(){
    SetPage(true);layoutBusy=true;SetWindowPos(window,nullptr,100,100,surface.S(352),surface.S(312),SWP_NOZORDER|SWP_NOACTIVATE);layoutBusy=false;
    ui::Place(settingsPane,surface,0,64,352,248);settingsOffset=0;LayoutSettings();
}
int main(){int result=0;try{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);INITCOMMONCONTROLSEX common{sizeof(common),ICC_BAR_CLASSES};InitCommonControlsEx(&common);ui::themeOverride=1;ui::RefreshTheme();i18n::Initialize(L"ru");
    HINSTANCE instance=GetModuleHandleW(nullptr);WNDCLASSW wc{};wc.hInstance=instance;wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.lpfnWndProc=WindowProc;wc.lpszClassName=L"BrightnessTray.Native";RegisterClassW(&wc);wc.lpfnWndProc=SettingsProc;wc.lpszClassName=L"BrightnessTray.Settings";RegisterClassW(&wc);wc.lpfnWndProc=ListProc;wc.lpszClassName=L"BrightnessTray.List";RegisterClassW(&wc);
    window=CreateWindowExW(WS_EX_CONTROLPARENT,L"BrightnessTray.Native",L"UI layout regression",WS_POPUP|WS_CLIPCHILDREN,100,100,352,420,nullptr,nullptr,instance,nullptr);CheckUi(window!=nullptr,"layout test owns its window");surface.Init(window);CreateControls(instance);UpdatePreferencesControls();
    scanning=false;syncing=false;for(int i=0;i<4;++i){Monitor m{};m.name=L"Monitor "+std::to_wstring(i+1);m.key=std::to_wstring(i);m.maximum=100;m.current=50;m.supported=true;monitors.push_back(m);}RebuildRows();SetPage(false);ShowWindow(window,SW_SHOWNOACTIVATE);
    HWND fourth=GetDlgItem(list,SliderBase+3);CheckUi(!Within(fourth),"fourth monitor begins outside viewport");SetFocus(fourth);CheckUi(GetFocus()==fourth&&Within(fourth),"focusing fourth monitor scrolls slider into view");
    HWND first=GetDlgItem(list,SliderBase);SetFocus(first);CheckUi(Within(first),"reverse focus scrolls first monitor into view");
    // Run the actual dialog keyboard dispatcher (the same path as the app loop).
    HWND lastOptions=GetDlgItem(list,OptionsBase+2);SetFocus(lastOptions);MSG key{};key.hwnd=lastOptions;key.message=WM_KEYDOWN;key.wParam=VK_TAB;
    IsDialogMessageW(window,&key);CheckUi(GetFocus()==fourth&&Within(fourth),"Tab to fourth monitor reveals focused slider");CaptureUi(window,L"native-four-monitor-focus.bmp");
    SmallSettings();CheckUi(!Within(Control(Refresh)),"short settings viewport initially clips lower controls");
    SetFocus(Control(MatchSettings));key.hwnd=Control(MatchSettings);IsDialogMessageW(window,&key);CheckUi(GetFocus()==Control(LanguageSelect)&&Within(Control(LanguageSelect)),"Tab to language scrolls inside short settings viewport");
    SendMessageW(window,WM_CHANGEUISTATE,MAKEWPARAM(UIS_CLEAR,UISF_HIDEFOCUS),0);CaptureUi(window,L"native-short-language-focus.bmp");
    SetFocus(Control(Refresh));CheckUi(Within(Control(Refresh)),"focused refresh scrolls inside short settings viewport");CaptureUi(window,L"native-short-refresh.bmp");
    SetFocus(Control(SpeedPresetBase));CheckUi(Within(Control(SpeedPresetBase)),"reverse settings focus returns to top");
    SmallSettings();settingsWheelRemainder=0;for(int i=0;i<4;++i)SendMessageW(settingsPane,WM_MOUSEWHEEL,MAKEWPARAM(0,static_cast<WORD>(-30)),0);CheckUi(settingsOffset==48,"settings accumulates four partial wheel deltas");
    scrollOffset=0;listWheelRemainder=0;LayoutRows();for(int i=0;i<4;++i)SendMessageW(list,WM_MOUSEWHEEL,MAKEWPARAM(0,static_cast<WORD>(-30)),0);CheckUi(scrollOffset==48,"monitor list accumulates partial wheel deltas");
    SendMessageW(settingsPane,WM_VSCROLL,SB_BOTTOM,0);CheckUi(Within(Control(Refresh)),"settings scrollbar reaches refresh");
    // Compare the actual owner-draw output with/without ODS_FOCUS.
    HWND language=Control(LanguageSelect);RECT r;GetClientRect(language,&r);HDC dc=GetDC(language),buffer=CreateCompatibleDC(dc);auto bitmap=CreateCompatibleBitmap(dc,r.right,r.bottom);auto old=SelectObject(buffer,bitmap);DRAWITEMSTRUCT item{};item.CtlType=ODT_BUTTON;item.CtlID=LanguageSelect;item.hwndItem=language;item.hDC=buffer;item.rcItem=r;
    SendMessageW(window,WM_DRAWITEM,LanguageSelect,reinterpret_cast<LPARAM>(&item));int x=surface.S(12),y=r.bottom-surface.S(3);COLORREF before=GetPixel(buffer,x,y);item.itemState=ODS_FOCUS;SendMessageW(window,WM_DRAWITEM,LanguageSelect,reinterpret_cast<LPARAM>(&item));CheckUi(before!=GetPixel(buffer,x,y)&&GetPixel(buffer,x,y)==ui::colors.accent,"language keyboard focus has visible underline pixels");
    SendMessageW(window,WM_CHANGEUISTATE,MAKEWPARAM(UIS_SET,UISF_HIDEFOCUS),0);SendMessageW(window,WM_DRAWITEM,LanguageSelect,reinterpret_cast<LPARAM>(&item));CheckUi(GetPixel(buffer,x,y)==before,"mouse focus does not force keyboard underline");SelectObject(buffer,old);DeleteObject(bitmap);DeleteDC(buffer);ReleaseDC(language,dc);
    layoutBusy=true;SetWindowPos(window,nullptr,100,100,surface.S(352),surface.S(444),SWP_NOZORDER|SWP_NOACTIVATE);layoutBusy=false;ui::Place(settingsPane,surface,0,64,352,380);LayoutSettings();CheckUi(settingsOffset==0&&Within(Control(Refresh)),"expanded settings restores full content without scroll");
}catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<std::endl;result=1;}
if(window)DestroyWindow(window);popupPaintBuffer.Clear();listPaintBuffer.Clear();settingsPaintBuffer.Clear();surface.Clear();if(ui::backgroundBrush)DeleteObject(ui::backgroundBrush);if(ui::cardBrush)DeleteObject(ui::cardBrush);return result;}
