#pragma once
#include <windows.h>
#include <thread>
#include <atomic>
#include <mutex>
#include <algorithm>

// A coalesced UI message is paced by an active-only high-resolution timer.
// No drawing is performed on the helper thread; idle means an unsignaled wait.
class FrameClock {
public:
    bool Start(HWND owner,UINT message,unsigned refreshHz){
        std::lock_guard<std::mutex> lock(gate_);
        if(active_)return true;
        if(!timer_){
            stop_=CreateEventW(nullptr,TRUE,FALSE,nullptr);
            timer_=CreateWaitableTimerExW(nullptr,nullptr,0x2,TIMER_MODIFY_STATE|SYNCHRONIZE);
            if(!timer_)timer_=CreateWaitableTimerW(nullptr,FALSE,nullptr);
            if(!stop_||!timer_){if(stop_)CloseHandle(stop_);if(timer_)CloseHandle(timer_);stop_=timer_=nullptr;return false;}
            thread_=std::thread([this]{Run();});
        }
        owner_=owner;message_=message;interval_=10000000LL/std::clamp(refreshHz,30u,240u);active_=true;pending_=false;Arm();return true;
    }
    void Acknowledge(){pending_=false;}
    void Stop(){std::lock_guard<std::mutex> lock(gate_);active_=false;if(timer_)CancelWaitableTimer(timer_);}
    void Close(){Stop();if(stop_)SetEvent(stop_);if(thread_.joinable())thread_.join();if(timer_)CloseHandle(timer_);if(stop_)CloseHandle(stop_);timer_=stop_=nullptr;}
    ~FrameClock(){Close();}
private:
    void Arm(){LARGE_INTEGER due;due.QuadPart=-interval_;SetWaitableTimer(timer_,&due,0,nullptr,nullptr,FALSE);}
    void Run(){HANDLE handles[]={stop_,timer_};for(;;){DWORD result=WaitForMultipleObjects(2,handles,FALSE,INFINITE);if(result!=WAIT_OBJECT_0+1)return;
        std::lock_guard<std::mutex> lock(gate_);if(!active_)continue;
        if(!pending_.exchange(true)&&!PostMessageW(owner_,message_,0,0))pending_=false;Arm();
    }}
    HANDLE stop_{},timer_{};HWND owner_{};UINT message_{};LONGLONG interval_=166667;
    std::thread thread_;std::mutex gate_;std::atomic<bool> pending_{false};bool active_=false;
};
