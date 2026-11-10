#pragma once
#include <windows.h>
#include <cstdio>
#include <string>
namespace render_profile {
inline FILE* output=nullptr;
inline LARGE_INTEGER Frequency(){LARGE_INTEGER f;QueryPerformanceFrequency(&f);return f;}
inline LONGLONG Stamp(){LARGE_INTEGER t;QueryPerformanceCounter(&t);return t.QuadPart;}
inline ULONGLONG NowMilliseconds(){static auto f=Frequency();auto t=Stamp();return static_cast<ULONGLONG>((t/f.QuadPart)*1000+(t%f.QuadPart)*1000/f.QuadPart);}
inline double Milliseconds(LONGLONG ticks){static auto f=Frequency();return 1000.0*ticks/f.QuadPart;}
inline void Record(const char* label,LONGLONG ticks){if(output)fprintf(output,"%s,%.4f\n",label,Milliseconds(ticks));}
class Scope {const char* label_;LONGLONG start_;public:explicit Scope(const char* label):label_(label),start_(output?Stamp():0){}~Scope(){if(start_)Record(label_,Stamp()-start_);}};
inline void Open(const std::wstring& path){_wfopen_s(&output,path.c_str(),L"w");if(output)fprintf(output,"event,milliseconds\n");}
inline void Close(){if(output){fclose(output);output=nullptr;}}
}
