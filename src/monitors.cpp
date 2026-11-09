#include "monitors.h"
#include "display_names.h"
#include "fade.h"
#include "transition.h"
#include "brightness_trace.h"
#include <map>
#include <physicalmonitorenumerationapi.h>
#include <highlevelmonitorconfigurationapi.h>
#include <lowlevelmonitorconfigurationapi.h>
#include <chrono>
#include <memory>

namespace {
bool Read(Monitor& m) {
    if (!m.lowLevel && GetMonitorBrightness(m.handle, &m.minimum, &m.current, &m.maximum))
        return m.maximum > m.minimum && m.current >= m.minimum && m.current <= m.maximum;
    m.lowLevel = true; m.minimum = 0;
    return GetVCPFeatureAndVCPFeatureReply(m.handle, 0x10, nullptr, &m.current, &m.maximum)
        && m.maximum > 0 && m.current <= m.maximum;
}
struct Enumeration { std::vector<Monitor>& monitors; const display_names::Names& names; };
BOOL CALLBACK Enumerate(HMONITOR display, HDC, LPRECT, LPARAM data) {
    auto& context=*reinterpret_cast<Enumeration*>(data);
    auto& monitors = context.monitors;
    DWORD count = 0;
    if (!GetNumberOfPhysicalMonitorsFromHMONITOR(display, &count) || !count) return TRUE;
    std::vector<PHYSICAL_MONITOR> physical(count);
    if (!GetPhysicalMonitorsFromHMONITOR(display, count, physical.data())) return TRUE;
    MONITORINFOEXW info{}; info.cbSize = sizeof(info); GetMonitorInfoW(display, &info);
    for (DWORD i = 0; i < count; ++i) {
        Monitor m; m.handle = physical[i].hPhysicalMonitor; m.bounds = info.rcMonitor;
        m.name = physical[i].szPhysicalMonitorDescription;
        m.key = std::wstring(info.szDevice) + L"_" + m.name + L"_" + std::to_wstring(i);
        std::replace(m.key.begin(), m.key.end(), L'[', L'_');
        std::replace(m.key.begin(), m.key.end(), L']', L'_');
        // Display labels are independent of the historical settings key.
        m.name=display_names::Label(context.names,info.szDevice,m.name,i,count);
        m.supported = Read(m); monitors.push_back(m);
    }
    return TRUE;
}
struct Batch {
    std::mutex mutex;
    std::condition_variable wake;
    bool go = false;
    size_t remaining = 0;
    std::vector<Event> events;
};
struct Job { Command command; size_t index; std::shared_ptr<Batch> batch; };
class Lane {
public:
    Monitor monitor;
    Lane(Monitor source, bool demo, unsigned delay) : monitor(std::move(source)), demo_(demo), delay_(delay) {
        if(monitor.supported) thread_=std::thread(&Lane::Run,this);
    }
    ~Lane() {
        {std::lock_guard<std::mutex> lock(mutex_);stop_=true;}wake_.notify_one();
        if(thread_.joinable())thread_.join();
        if(monitor.handle)DestroyPhysicalMonitor(monitor.handle);
    }
    void Push(Job job) {
        {std::lock_guard<std::mutex> lock(mutex_);jobs_.push_back(std::move(job));}wake_.notify_one();
    }
private:
    bool demo_,stop_=false;
    unsigned delay_;
    std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<Job> jobs_;
    std::thread thread_;
    void Run() {
        for(;;) {
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait(lock,[&]{return stop_||!jobs_.empty();});
            if(jobs_.empty()&&stop_)return;
            auto job=std::move(jobs_.front());jobs_.pop_front();lock.unlock();
            auto batch=job.batch;
            {std::unique_lock<std::mutex> start(batch->mutex);batch->wake.wait(start,[&]{return batch->go;});}
            Event result{job.command.operation,job.command.key,job.command.raw,job.command.tag,false,{}};
            LARGE_INTEGER time;QueryPerformanceCounter(&time);result.started=time.QuadPart;
            if(demo_&&delay_)std::this_thread::sleep_for(std::chrono::milliseconds(delay_));
            if(job.command.operation==Operation::Read) {
                result.ok=demo_||Read(monitor);result.raw=monitor.current;
            } else {
                result.raw=std::clamp(job.command.raw,monitor.minimum,monitor.maximum);
                result.ok=demo_||(monitor.lowLevel?SetVCPFeature(monitor.handle,0x10,result.raw):SetMonitorBrightness(monitor.handle,result.raw))!=FALSE;
                if(result.ok)monitor.current=result.raw;
            }
            QueryPerformanceCounter(&time);result.finished=time.QuadPart;
            if(job.command.operation==Operation::Write) {
                LARGE_INTEGER frequency;QueryPerformanceFrequency(&frequency);
                brightness_trace::Log("write",result.tag,job.command.logical,result.raw,
                    (result.finished-result.started)*1000.0/frequency.QuadPart,result.ok);
            }
            {std::lock_guard<std::mutex> done(batch->mutex);batch->events[job.index]=std::move(result);--batch->remaining;}
            batch->wake.notify_all();
        }
    }
};
bool Writes(const std::vector<Command>& commands) {
    return !commands.empty()&&std::all_of(commands.begin(),commands.end(),[](const Command& c){return c.operation==Operation::Write;});
}
}
void MonitorWorker::Start(HWND target,UINT message,bool demo,unsigned demoDelayMs) {
    target_=target;message_=message;demo_=demo;demoDelayMs_=demoDelayMs;
    thread_=std::thread(&MonitorWorker::Run,this);
}
void MonitorWorker::Queue(Command command) { QueueBatch({std::move(command)}); }
void MonitorWorker::SetTransitionDuration(int milliseconds) {
    transitionMs_.store(transition::Normalize(milliseconds));wake_.notify_one();
}
void MonitorWorker::SetHardwareStep(int percent) {
    hardwareStep_.store(percent==1||percent==2||percent==5?percent:0);wake_.notify_one();
}
void MonitorWorker::QueueBatch(std::vector<Command> commands) {
    if(commands.empty())return;
    std::lock_guard<std::mutex> lock(mutex_);
    if(stopping_)return;
    // Replace stale writes as a group, without crossing read/scan barriers.
    if(!commands_.empty()&&Writes(commands)&&Writes(commands_.back())) {
        auto& pending=commands_.back();
        for(auto& command:commands) {
            auto it=std::find_if(pending.begin(),pending.end(),[&](const Command& c){return c.key==command.key;});
            if(it==pending.end())pending.push_back(std::move(command));else *it=std::move(command);
        }
    } else commands_.push_back(std::move(commands));
    wake_.notify_one();
}
std::deque<Event> MonitorWorker::Events() {
    std::lock_guard<std::mutex> lock(mutex_);
    auto events=std::move(events_);events_.clear();return events;
}
void MonitorWorker::Stop() {
    {std::lock_guard<std::mutex> lock(mutex_);stopping_=true;}wake_.notify_one();
    if(thread_.joinable())thread_.join();
}
void MonitorWorker::Run() {
    using Clock=std::chrono::steady_clock;
    constexpr auto frameInterval=std::chrono::milliseconds(40);
    std::vector<std::unique_ptr<Lane>> lanes;
    struct Fade {
        Command goal;Lane* lane;FadePath path;
        int durationMs=-1;
        double position=0,velocity=0;
    };
    std::map<std::wstring,Fade> fades;
    auto nextFrame=Clock::now(),previousFrame=nextFrame-frameInterval;
    auto laneFor=[&](const std::wstring& key)->Lane* {
        auto it=std::find_if(lanes.begin(),lanes.end(),[&](const auto& lane){return lane->monitor.key==key&&lane->monitor.supported;});
        return it==lanes.end()?nullptr:it->get();
    };
    auto publish=[&](std::vector<Event> results) {
        if(results.empty())return;
        {std::lock_guard<std::mutex> lock(mutex_);for(auto& result:results)events_.push_back(std::move(result));}
        if(target_)PostMessageW(target_,message_,0,0);
    };
    auto execute=[&](const std::vector<Command>& commands) {
        auto batch=std::make_shared<Batch>();batch->events.resize(commands.size());
        std::vector<std::pair<Lane*,Job>> jobs;
        for(size_t i=0;i<commands.size();++i) {
            const auto& command=commands[i];
            batch->events[i]={command.operation,command.key,command.raw,command.tag,false,{}};
            if(auto lane=laneFor(command.key))jobs.push_back({lane,{command,i,batch}});
        }
        batch->remaining=jobs.size();
        for(auto& job:jobs)job.first->Push(std::move(job.second));
        std::unique_lock<std::mutex> start(batch->mutex);batch->go=true;batch->wake.notify_all();
        batch->wake.wait(start,[&]{return batch->remaining==0;});
        return std::move(batch->events);
    };
    auto finishFades=[&] {
        std::vector<Command> goals;for(auto& entry:fades)goals.push_back(entry.second.goal);
        fades.clear();if(!goals.empty())publish(execute(goals));
    };
    auto animate=[&] {
        auto now=Clock::now();
        int configured=transitionMs_.load();
        if(configured==0){finishFades();return;}
        for(auto& entry:fades)if(entry.second.durationMs!=configured) {
            auto& fade=entry.second;fade.durationMs=configured;
        }
        double elapsed=std::clamp(std::chrono::duration<double>(now-previousFrame).count(),0.016,0.250);
        previousFrame=now;nextFrame=now+frameInterval;
        double alpha=1-std::exp(-elapsed/0.110);
        std::map<std::wstring,double> proposals;
        std::map<std::wstring,double> origins;
        int hardwareStep=hardwareStep_.load();
        for(auto& entry:fades) {
            double progress=entry.second.path.progress;
            auto& fade=entry.second;
            origins[entry.first]=fade.position;
            if(fade.durationMs>=0) {
                // Exact critically damped motion. Retargeting preserves both
                // fractional position and velocity, rather than restarting ease-in.
                double goal=fade.path.Coordinate(fade.goal.raw);
                double omega=8.0/std::max(0.05,fade.durationMs/1000.0);
                double delta=fade.position-goal;
                if(fade.velocity*delta>0)fade.velocity=0;
                double temp=(fade.velocity+omega*delta)*elapsed;
                double decay=std::exp(-omega*elapsed);
                double position=goal+(delta+temp)*decay;
                fade.velocity=(fade.velocity-omega*temp)*decay;
                fade.position=std::clamp(position,std::min(fade.position,goal),std::max(fade.position,goal));
            }
            proposals[entry.first]=fade.durationMs<0?(progress>=0.995?1:progress+(1-progress)*alpha)
                :progress;
        }
        // Limit actual native changes, not just logical interpolation: steep
        // calibration segments and slow DDC calls otherwise create large steps.
        // A shared scale preserves coordinated movement across the whole group.
        auto fits=[&](double scale) {
            for(const auto& entry:fades) {
                const auto& fade=entry.second;
                if(fade.durationMs>=0&&!hardwareStep)continue;
                double progress=fade.path.progress+(proposals.at(entry.first)-fade.path.progress)*scale;
                double origin=origins.at(entry.first);
                auto raw=fade.durationMs<0?fade.path.At(progress):fade.path.Raw(origin+(fade.position-origin)*scale);
                auto current=fade.lane->monitor.current;
                auto delta=raw>current?raw-current:current-raw;
                auto range=static_cast<std::uint64_t>(fade.path.maximum)-fade.path.minimum;
                auto maximum=fade.durationMs<0?fade.path.MaxStep():std::max<std::uint64_t>(1,(range*hardwareStep+99)/100);
                if(delta>maximum)return false;
            }
            return true;
        };
        double scale=1;
        if(!fits(scale)) {
            double low=0,high=1;
            for(int i=0;i<16;++i){double middle=(low+high)/2;if(fits(middle))low=middle;else high=middle;}
            scale=low;
        }
        std::vector<Command> writes;std::vector<Event> completed;
        std::map<std::wstring,bool> final;
        std::vector<std::wstring> remove;
        for(auto& entry:fades) {
            auto& fade=entry.second;
            fade.path.progress+=(proposals.at(entry.first)-fade.path.progress)*scale;
            if(fade.durationMs>=0&&scale<1){double origin=origins.at(entry.first);fade.position=origin+(fade.position-origin)*scale;fade.velocity*=scale;}
            auto raw=fade.durationMs<0?fade.path.At(fade.path.progress):fade.path.Raw(fade.position);
            final[entry.first]=raw==fade.goal.raw;
            if(raw==fade.lane->monitor.current) {
                if(final[entry.first]) {
                    Event event{Operation::Write,entry.first,raw,fade.goal.tag,true,{}};
                    LARGE_INTEGER time;QueryPerformanceCounter(&time);event.started=event.finished=time.QuadPart;
                    completed.push_back(std::move(event));remove.push_back(entry.first);
                }
                continue;
            }
            Command command=fade.goal;command.raw=raw;command.smooth=false;writes.push_back(std::move(command));
        }
        if(!writes.empty()) {
            auto results=execute(writes);
            for(auto& event:results) {
                event.final=!event.ok||final.at(event.key);
                if(event.final)remove.push_back(event.key);
                completed.push_back(std::move(event));
            }
        }
        for(const auto& key:remove)fades.erase(key);
        publish(std::move(completed));
    };
    for(;;) {
        std::vector<Command> commands;bool stop=false;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            for(;;) {
                stop=stopping_;
                bool readBarrier=!commands_.empty()&&commands_.front().front().operation==Operation::Read&&!fades.empty();
                if(!commands_.empty()&&(!readBarrier||stop)) {
                    if(!stop&&fades.empty()&&Writes(commands_.front()))
                        wake_.wait_for(lock,std::chrono::milliseconds(8),[&]{return stopping_;});
                    stop=stopping_;commands=std::move(commands_.front());commands_.pop_front();break;
                }
                if(stop)break;
                if(!fades.empty()) {
                    if(Clock::now()>=nextFrame)break;
                    wake_.wait_until(lock,nextFrame);
                }else wake_.wait(lock,[&]{return stopping_||!commands_.empty();});
            }
        }
        if(commands.empty()) {
            if(stop){finishFades();break;}
            animate();continue;
        }
        if(commands.front().operation==Operation::Scan) {
            // Any in-flight frame is complete. A topology change cancels the
            // remaining animation before the old physical handles are released.
            fades.clear();lanes.clear();std::vector<Monitor> found;
            if(demo_) {
                std::vector<RECT> bounds;
                EnumDisplayMonitors(nullptr,nullptr,[](HMONITOR h,HDC,LPRECT,LPARAM data)->BOOL{
                    MONITORINFO info{sizeof(info)};GetMonitorInfoW(h,&info);
                    reinterpret_cast<std::vector<RECT>*>(data)->push_back(info.rcMonitor);return TRUE;
                },reinterpret_cast<LPARAM>(&bounds));
                if(bounds.empty())bounds.push_back({0,0,1920,1080});
                for(int i=0;i<2;++i) {
                    Monitor m;m.name=i?L"Dell UltraSharp · 27″":L"LG UltraFine · 32″";
                    m.key=L"Demo_"+std::to_wstring(i);m.bounds=bounds[static_cast<size_t>(i)%bounds.size()];
                    m.maximum=100;m.current=i?72:55;m.supported=true;found.push_back(m);
                }
            }else {
                auto names=display_names::Query();Enumeration context{found,names};
                EnumDisplayMonitors(nullptr,nullptr,Enumerate,reinterpret_cast<LPARAM>(&context));
            }
            Event event{Operation::Scan,L"",0,commands.front().tag,true,{}};
            for(auto& m:found) {
                auto copy=m;copy.handle=nullptr;event.monitors.push_back(copy);
                lanes.push_back(std::make_unique<Lane>(std::move(m),demo_,demoDelayMs_));
            }
            publish({std::move(event)});
        }else if(commands.front().operation==Operation::Read) {
            // Normally this barrier waits for the gentle arrival at the goal.
            // During shutdown finish immediately rather than extending exit.
            if(stop)finishFades();
            publish(execute(commands));
        }else {
            std::vector<Command> direct;std::vector<Event> unavailable;
            bool hadFades=!fades.empty();
            int configured=transitionMs_.load();
            for(auto& command:commands) {
                if(!command.smooth||stop||configured==0) {
                    fades.erase(command.key);direct.push_back(std::move(command));continue;
                }
                auto lane=laneFor(command.key);
                if(!lane){unavailable.push_back({command.operation,command.key,command.raw,command.tag,false,{}});continue;}
                auto& m=lane->monitor;command.raw=std::clamp(command.raw,m.minimum,m.maximum);
                auto existing=fades.find(command.key);
                if(existing!=fades.end()&&configured>=0) {
                    auto& fade=existing->second;
                    // Same mapping for all ordinary slider commands. A mapping
                    // change (e.g. calibration preview) starts from confirmed raw.
                    const auto& old=fade.goal.calibration;const auto& cal=command.calibration;
                    bool same=(fade.goal.logical>=0)==(command.logical>=0)&&old.Nodes()==cal.Nodes();
                    if(same){fade.goal=command;fade.path.target=command.raw;fade.path.logical=command.logical;continue;}
                }
                FadePath path{m.current,command.raw,m.minimum,m.maximum,command.calibration,command.logical,0};
                // Retarget from the most recently confirmed hardware value.
                Fade fade{command,lane,path,configured};fade.position=path.Coordinate(m.current);
                fades.insert_or_assign(command.key,std::move(fade));
            }
            if(!direct.empty())publish(execute(direct));
            publish(std::move(unavailable));
            if(!fades.empty()) {
                if(!hadFades){nextFrame=Clock::now();previousFrame=nextFrame-frameInterval;}
                if(!stop&&Clock::now()>=nextFrame)animate();
            }
        }
    }
    lanes.clear();
}

