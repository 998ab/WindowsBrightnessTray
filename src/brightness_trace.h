#pragma once
#include <windows.h>
#include <cstdio>
#include <mutex>
#include <share.h>

// Opt-in, bounded local trace. No screen content or monitor identifiers.
namespace brightness_trace {
inline std::mutex mutex;
inline FILE* file=nullptr;
inline unsigned lines=0;
inline ULONGLONG origin=0;
inline void Start(const wchar_t* path) {
    std::lock_guard<std::mutex> lock(mutex);
    if(file)return;
    file=_wfsopen(path,L"w",_SH_DENYNO);if(!file)return;
    origin=GetTickCount64();
    std::fputs("ms,event,tag,logical,raw,duration_ms,ok\n",file);std::fflush(file);
}
inline void Log(const char* event,unsigned long long tag,int logical,DWORD raw,double duration=0,bool ok=true) {
    std::lock_guard<std::mutex> lock(mutex);
    if(!file||lines>=20000)return;
    std::fprintf(file,"%llu,%s,%llu,%d,%lu,%.3f,%d\n",GetTickCount64()-origin,event,tag,logical,raw,duration,ok?1:0);
    ++lines;std::fflush(file);
}
inline void Stop(){std::lock_guard<std::mutex> lock(mutex);if(file){std::fclose(file);file=nullptr;}}
}
