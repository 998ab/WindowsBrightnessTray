#include <windows.h>
#include <commctrl.h>
#include <string>
#include <iostream>
#include <fstream>
#include <stdexcept>
void Check(bool value,const char* message){if(!value)throw std::runtime_error(message);std::cout<<"PASS: "<<message<<std::endl;}
void Command(HWND window,int id){SendMessageW(window,WM_COMMAND,id,0);Sleep(100);}
bool OwnedBy(HWND window,DWORD pid){DWORD owner=0;return window&&GetWindowThreadProcessId(window,&owner)&&owner==pid;}
struct WindowSearch {DWORD pid;const wchar_t* className;HWND found=nullptr;};
BOOL CALLBACK FindOwned(HWND window,LPARAM parameter){
 auto& search=*reinterpret_cast<WindowSearch*>(parameter);wchar_t name[128]{};
 if(OwnedBy(window,search.pid)&&GetClassNameW(window,name,128)&&wcscmp(name,search.className)==0){search.found=window;return FALSE;}return TRUE;
}
HWND FindOwnedWindow(DWORD pid,const wchar_t* className){WindowSearch search{pid,className};EnumWindows(FindOwned,reinterpret_cast<LPARAM>(&search));return search.found;}
int decoyCommands=0;
LRESULT CALLBACK DecoyProc(HWND window,UINT message,WPARAM wp,LPARAM lp){if(message==WM_COMMAND||message==WM_CLOSE){++decoyCommands;return 0;}return DefWindowProcW(window,message,wp,lp);}
void Capture(HWND window,const std::wstring& path){
 RECT r;GetWindowRect(window,&r);int w=r.right-r.left,h=r.bottom-r.top;HDC dc=GetDC(window),buffer=CreateCompatibleDC(dc);
 BITMAPINFO format{};format.bmiHeader={sizeof(BITMAPINFOHEADER),w,-h,1,32,BI_RGB};void* bits=nullptr;
 HBITMAP bitmap=CreateDIBSection(dc,&format,DIB_RGB_COLORS,&bits,nullptr,0);auto old=SelectObject(buffer,bitmap);PrintWindow(window,buffer,2);
 BITMAPFILEHEADER header{};header.bfType=0x4d42;header.bfOffBits=sizeof(header)+sizeof(BITMAPINFOHEADER);header.bfSize=header.bfOffBits+w*h*4;
 HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,0,nullptr);DWORD written;WriteFile(file,&header,sizeof(header),&written,nullptr);WriteFile(file,&format.bmiHeader,sizeof(BITMAPINFOHEADER),&written,nullptr);WriteFile(file,bits,w*h*4,&written,nullptr);CloseHandle(file);
 SelectObject(buffer,old);DeleteObject(bitmap);DeleteDC(buffer);ReleaseDC(window,dc);
}
int wmain(int argc,wchar_t** argv){
 SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
 if(argc<2)return 2;std::wstring exe=argv[1],directory=exe.substr(0,exe.find_last_of(L"\\/"));
 std::wstring profile=directory+L"\\smoke-settings.ini";
 WritePrivateProfileStringW(L"General",L"Sync",L"1",profile.c_str());WritePrivateProfileStringW(L"General",L"Language",L"ru",profile.c_str());WritePrivateProfileStringW(L"General",L"ResponseMode",L"1",profile.c_str());WritePrivateProfileStringW(L"General",L"WheelStep",L"2",profile.c_str());
 std::wstring command=L"\""+exe+L"\" --demo --settings=\""+profile+L"\"";
 bool exerciseFailure=argc>2&&std::wstring(argv[2])==L"--exercise-failure-cleanup",injectedFailure=false;
 if(argc>2&&!exerciseFailure)command+=L" "+std::wstring(argv[2]);
 STARTUPINFOW start{sizeof(start)};start.dwFlags=STARTF_USESHOWWINDOW;start.wShowWindow=SW_HIDE;PROCESS_INFORMATION process{};
 WNDCLASSW decoyClass{};decoyClass.lpfnWndProc=DecoyProc;decoyClass.hInstance=GetModuleHandleW(nullptr);decoyClass.lpszClassName=L"BrightnessTray.Native";
 if(!RegisterClassW(&decoyClass))return 3;
 HWND decoy=CreateWindowExW(0,decoyClass.lpszClassName,L"BrightnessTray · Demo",0,0,0,10,10,nullptr,nullptr,decoyClass.hInstance,nullptr);
 if(!decoy)return 3;
 if(!CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,FALSE,0,nullptr,nullptr,&start,&process)){DestroyWindow(decoy);return 3;}
 HWND window=nullptr;int result=0;
 try{
  Check(FindOwnedWindow(GetCurrentProcessId(),L"BrightnessTray.Native")==decoy,"same-class decoy exists in another process");
  for(int i=0;i<60;++i){Sleep(100);window=FindOwnedWindow(process.dwProcessId,L"BrightnessTray.Native");if(window||WaitForSingleObject(process.hProcess,0)==WAIT_OBJECT_0)break;}
  Check(window!=nullptr,"demo window exists");DWORD pid=0;GetWindowThreadProcessId(window,&pid);Check(pid==process.dwProcessId,"test owns demo instance");Sleep(600);
  if(exerciseFailure){injectedFailure=true;throw std::runtime_error("injected check failure");}
  Check((GetWindowLongW(window,GWL_STYLE)&WS_CAPTION)==0,"no popup caption");
  auto style=GetWindowLongW(window,GWL_EXSTYLE);Check((style&WS_EX_TOOLWINDOW)&&!(style&WS_EX_APPWINDOW),"no taskbar button");
  HWND slider=GetDlgItem(window,40);Check(IsWindowVisible(slider)!=FALSE,"single shared slider visible");Capture(window,directory+L"\\native-brightness.bmp");
  SendMessageW(slider,TBM_SETPOS,TRUE,25);SendMessageW(window,WM_HSCROLL,TB_THUMBTRACK,reinterpret_cast<LPARAM>(slider));Sleep(100);
  int actual=static_cast<int>(reinterpret_cast<INT_PTR>(GetPropW(slider,L"ActualLevel")))-1;Check(actual>=25&&actual<=55,"confirmed progress separate from target");
  Sleep(1800);actual=static_cast<int>(reinterpret_cast<INT_PTR>(GetPropW(slider,L"ActualLevel")))-1;Check(actual==25,"confirmed progress reaches target");
  SendMessageW(window,WM_APP+4,WHEEL_DELTA,0);Sleep(1200);
  Check(static_cast<int>(SendMessageW(slider,TBM_GETPOS,0,0))==27,"wheel adjusts brightness without opening another window");
  Check(FindOwnedWindow(process.dwProcessId,L"BrightnessTray.OSD")==nullptr,"wheel does not create brightness OSD");
  Command(window,10);Check(!IsWindowVisible(slider),"unlinked hides shared slider");Capture(window,directory+L"\\native-independent.bmp");
  Command(window,21);Check(IsWindowVisible(GetDlgItem(GetDlgItem(window,42),41))!=FALSE,"settings language selector");Capture(window,directory+L"\\native-settings.bmp");Command(window,31);Command(window,32);
  Command(window,30);Sleep(500);HWND cal=FindOwnedWindow(process.dwProcessId,L"BrightnessTray.Calibration");Check(cal!=nullptr,"calibration separate window");Check(!IsWindowVisible(window),"popup hidden for calibration");
  RECT calBounds;GetWindowRect(cal,&calBounds);Check(calBounds.right-calBounds.left==MulDiv(440,static_cast<int>(GetDpiForWindow(cal)),96),"calibration is compact at current DPI");
  Capture(cal,directory+L"\\native-calibration.bmp");Command(cal,102);Sleep(200);Capture(cal,directory+L"\\native-calibration-step.bmp");
  SendMessageW(cal,WM_CLOSE,0,0);Sleep(500);Check(IsWindowVisible(window)!=FALSE,"cancel restores popup");
  SendMessageW(window,WM_ACTIVATE,WA_INACTIVE,0);Check(!IsWindowVisible(window),"deactivation dismisses popup");
 }catch(const std::exception& error){if(injectedFailure)std::cout<<"PASS: injected failure exercises cleanup\n";else{std::cerr<<"FAIL: "<<error.what()<<std::endl;result=1;}}
 // Resolve ownership again on failure too; a stale/reused HWND is never trusted.
 window=FindOwnedWindow(process.dwProcessId,L"BrightnessTray.Native");
 if(OwnedBy(window,process.dwProcessId))SendMessageW(window,WM_COMMAND,12,0);
 if(WaitForSingleObject(process.hProcess,5000)!=WAIT_OBJECT_0){TerminateProcess(process.hProcess,1);WaitForSingleObject(process.hProcess,5000);std::cerr<<"FAIL: child required forced cleanup\n";result=1;}
 if(!IsWindow(decoy)||decoyCommands){std::cerr<<"FAIL: smoke touched another process window\n";result=1;}else std::cout<<"PASS: same-class foreign window untouched during test and cleanup\n";
 DestroyWindow(decoy);CloseHandle(process.hThread);CloseHandle(process.hProcess);return result;
}
