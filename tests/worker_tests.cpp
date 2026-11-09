#include "../src/monitors.h"
#include <chrono>
#include <iostream>
#include <cstdlib>
#include <map>

void Check(bool value,const char* message){if(!value){std::cerr<<message<<"\n";std::exit(1);}}
std::map<unsigned long long,Event> Wait(MonitorWorker& worker,std::vector<unsigned long long> tags) {
    auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(8);std::map<unsigned long long,Event> found;
    for(;;) {
        for(auto& event:worker.Events())found[event.tag]=std::move(event);
        if(std::all_of(tags.begin(),tags.end(),[&](auto tag){return found.count(tag)!=0;}))return found;
        Check(std::chrono::steady_clock::now()<deadline,"Timed out waiting for backend");
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}
int main() {
    MonitorWorker worker;worker.Start(nullptr,0,true,120);worker.Queue({Operation::Scan,L"",0,1});
    auto scan=Wait(worker,{1});Check(scan[1].monitors.size()==2,"Demo scan failed");
    worker.QueueBatch({{Operation::Write,L"Demo_0",10,2},{Operation::Write,L"Demo_1",20,3}});
    auto writes=Wait(worker,{2,3});Check(writes[2].ok&&writes[3].ok,"Parallel write failed");
    Check(std::max(writes[2].started,writes[3].started)<std::min(writes[2].finished,writes[3].finished),"Writes did not overlap: backend is serial");
    LARGE_INTEGER frequency;QueryPerformanceFrequency(&frequency);
    double skew=std::abs(static_cast<double>(writes[2].started-writes[3].started))*1000.0/frequency.QuadPart;
    std::cout<<"PASS: independent monitor writes overlap; start skew "<<skew<<" ms\n";
    for(unsigned i=0;i<100;++i)worker.Queue({Operation::Write,L"Demo_0",i,10+i});
    worker.Queue({Operation::Read,L"Demo_0",0,200});auto newest=Wait(worker,{200});Check(newest[200].raw==99,"Latest write was lost");
    worker.Queue({Operation::Write,L"Demo_0",34,201});worker.Queue({Operation::Read,L"Demo_0",0,202});
    worker.Queue({Operation::Write,L"Demo_0",56,203});worker.Queue({Operation::Read,L"Demo_0",0,204});
    auto barriers=Wait(worker,{202,204});Check(barriers[202].raw==34&&barriers[204].raw==56,"Writes crossed read barriers");
    worker.Queue({Operation::Write,L"Demo_0",88,205});worker.Queue({Operation::Scan,L"",0,206});worker.Queue({Operation::Read,L"Demo_0",0,207});
    auto rescan=Wait(worker,{207});Check(rescan[207].raw==55,"Old write crossed scan barrier");
    worker.QueueBatch({{Operation::Write,L"Demo_0",55,208},{Operation::Write,L"Demo_1",72,209}});worker.Stop();
    auto drained=worker.Events();Check(drained.size()==2&&drained[0].ok&&drained[1].ok,"Stop did not drain restoration batch");
    std::cout<<"PASS: latest-value coalescing, read/scan barriers, shutdown drain\n";
    MonitorWorker smooth;smooth.Start(nullptr,0,true,15);smooth.Queue({Operation::Scan,L"",0,300});Wait(smooth,{300});
    smooth.QueueBatch({{Operation::Write,L"Demo_0",35,301,true,35,{}},{Operation::Write,L"Demo_1",52,302,true,52,{}}});
    std::map<std::wstring,DWORD> previous{{L"Demo_0",55},{L"Demo_1",72}};
    std::map<unsigned long long,Event> final,firstFrames;int frames=0;
    auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(8);
    while(final.size()<2) {
        for(auto& event:smooth.Events()) {
            Check(event.ok,"Fade write failed");
            auto last=previous[event.key];Check((event.raw>last?event.raw-last:last-event.raw)<=2,"Fade step exceeded native 2 percent");
            Check(event.raw<=last,"Downward fade reversed");previous[event.key]=event.raw;++frames;
            if(!firstFrames.count(event.tag))firstFrames[event.tag]=event;
            if(event.final)final[event.tag]=event;
        }
        Check(std::chrono::steady_clock::now()<deadline,"Fade did not settle");std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    Check(frames>4&&final[301].raw==35&&final[302].raw==52,"Fade did not reach exact endpoints");
    Check(!firstFrames[301].final&&!firstFrames[302].final,"Intermediate frame marked complete");
    Check(std::max(firstFrames[301].started,firstFrames[302].started)<std::min(firstFrames[301].finished,firstFrames[302].finished),"Smooth frame serialized monitors");
    smooth.Queue({Operation::Write,L"Demo_0",5,310,true});auto initial=Wait(smooth,{310});
    Check(!initial[310].final,"Long fade was an abrupt write");DWORD last=initial[310].raw;
    smooth.Queue({Operation::Write,L"Demo_0",70,311,true});smooth.Queue({Operation::Read,L"Demo_0",0,312});
    bool arrived=false,read=false;deadline=std::chrono::steady_clock::now()+std::chrono::seconds(8);
    while(!read) {
        for(auto& event:smooth.Events()) {
            Check(event.ok,"Retarget failed");
            if(event.operation==Operation::Write){Check((event.raw>last?event.raw-last:last-event.raw)<=2,"Retarget jumped from an unconfirmed position");last=event.raw;}
            if(event.tag==311&&event.final){Check(event.raw==70,"Retarget missed exact goal");arrived=true;}
            if(event.tag==312){Check(arrived&&event.raw==70,"Read crossed an unfinished fade");read=true;}
        }
        Check(std::chrono::steady_clock::now()<deadline,"Retarget timed out");std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    smooth.Queue({Operation::Write,L"Demo_0",0,313,true});Wait(smooth,{313});
    smooth.Queue({Operation::Write,L"Demo_0",55,314});smooth.Queue({Operation::Read,L"Demo_0",0,315});
    auto restored=Wait(smooth,{315});Check(restored[315].raw==55,"Immediate restoration did not cancel fade");
    smooth.Queue({Operation::Write,L"Demo_0",0,317,true});Wait(smooth,{317});
    smooth.Queue({Operation::Scan,L"",0,318});smooth.Queue({Operation::Read,L"Demo_0",0,319});
    auto topology=Wait(smooth,{319});Check(topology[319].raw==55,"Fade survived topology change");
    smooth.Queue({Operation::Write,L"Demo_0",70,316,true});smooth.Stop();
    bool drainedGoal=false;for(auto& event:smooth.Events())if(event.tag==316&&event.final&&event.ok&&event.raw==70)drainedGoal=true;
    Check(drainedGoal,"Shutdown lost latest smooth target");
    std::cout<<"PASS: parallel smooth frames, bounded steps, retargeting, exact arrival, read barrier and immediate restoration\n";
    MonitorWorker timed;timed.SetTransitionDuration(0);timed.Start(nullptr,0,true,15);timed.Queue({Operation::Scan,L"",0,400});Wait(timed,{400});
    timed.QueueBatch({{Operation::Write,L"Demo_0",10,401,true},{Operation::Write,L"Demo_1",10,402,true}});
    auto instant=Wait(timed,{401,402});Check(instant[401].final&&instant[402].final&&instant[401].raw==10&&instant[402].raw==10,"Instant mode animated instead of applying immediately");
    timed.SetTransitionDuration(200);timed.Queue({Operation::Write,L"Demo_0",90,403,true});
    bool fastDone=false,largerStep=false;last=10;int timedFrames=0;deadline=std::chrono::steady_clock::now()+std::chrono::seconds(8);
    while(!fastDone){for(auto& event:timed.Events()){Check(event.ok,"Timed write failed");Check(event.raw>=last&&event.raw<=90,"Timed transition reversed or overshot");largerStep=largerStep||(event.raw-last>2);last=event.raw;++timedFrames;if(event.final){Check(event.raw==90,"Timed mode missed endpoint");fastDone=true;}}
        Check(std::chrono::steady_clock::now()<deadline,"Timed transition did not finish");std::this_thread::sleep_for(std::chrono::milliseconds(5));}
    Check(largerStep&&timedFrames<30,"Fast mode retained old slow step cap");
    timed.SetTransitionDuration(1500);timed.Queue({Operation::Write,L"Demo_0",0,404,true});auto running=Wait(timed,{404});Check(!running[404].final,"Soft mode skipped transition");
    timed.SetTransitionDuration(0);timed.Queue({Operation::Read,L"Demo_0",0,405});auto switched=Wait(timed,{405});Check(switched[405].raw==0,"Changing to instant did not affect active transition");
    timed.Stop();std::cout<<"PASS: instant, fast timed transition, no old cap, live speed update\n";
    MonitorWorker repeated;repeated.SetTransitionDuration(600);repeated.Start(nullptr,0,true,50);
    repeated.Queue({Operation::Scan,L"",0,500});Wait(repeated,{500});repeated.Queue({Operation::Write,L"Demo_0",20,501});Wait(repeated,{501});
    std::atomic<bool> feeding{true};std::thread input([&]{unsigned long long tag=510;while(feeding.load()){repeated.Queue({Operation::Write,L"Demo_0",90,tag++,true,90,{}});std::this_thread::sleep_for(std::chrono::milliseconds(90));}});
    bool duplicateArrived=false;std::vector<DWORD> trace;
    auto until=std::chrono::steady_clock::now()+std::chrono::milliseconds(800);
    while(std::chrono::steady_clock::now()<until){for(auto& event:repeated.Events())if(event.operation==Operation::Write){trace.push_back(event.raw);if(event.ok&&event.final&&event.raw==90)duplicateArrived=true;}std::this_thread::sleep_for(std::chrono::milliseconds(5));}
    feeding.store(false);input.join();repeated.Stop();
    std::cout<<"Repeated-target trace:";for(auto raw:trace)std::cout<<' '<<raw;std::cout<<"\n";
    Check(duplicateArrived,"Repeated slider notifications kept restarting/decelerating the transition");
    std::cout<<"PASS: repeated input does not restart the motion or prolong arrival\n";
    MonitorWorker dragging;dragging.SetTransitionDuration(600);dragging.Start(nullptr,0,true,50);
    dragging.Queue({Operation::Scan,L"",0,600});Wait(dragging,{600});
    dragging.Queue({Operation::Write,L"Demo_0",10,601});Wait(dragging,{601});
    std::thread gesture([&]{for(int target=30;target<=90;target+=10){dragging.Queue({Operation::Write,L"Demo_0",static_cast<DWORD>(target),static_cast<unsigned long long>(target+600),true,target,{}});std::this_thread::sleep_for(std::chrono::milliseconds(90));}});
    last=10;bool dragArrived=false;deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
    while(!dragArrived){for(auto& event:dragging.Events())if(event.operation==Operation::Write){Check(event.ok&&event.raw>=last&&event.raw<=90,"Continuous dragging reversed or overshot");last=event.raw;if(event.tag==690&&event.final)dragArrived=true;}
        Check(std::chrono::steady_clock::now()<deadline,"Moving slider did not settle");std::this_thread::sleep_for(std::chrono::milliseconds(5));}
    gesture.join();
    dragging.Queue({Operation::Write,L"Demo_0",0,700,true,0,{}});auto down=Wait(dragging,{700});last=down[700].raw;
    dragging.Queue({Operation::Write,L"Demo_0",100,701,true,100,{}});bool reversed=false,firstReverse=true;
    deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
    while(!reversed){for(auto& event:dragging.Events())if(event.tag==701){Check(event.ok&&(firstReverse||event.raw>=last)&&event.raw<=100,"Direction change retained opposing velocity");firstReverse=false;last=event.raw;if(event.final)reversed=true;}
        Check(std::chrono::steady_clock::now()<deadline,"Reversed slider did not settle");std::this_thread::sleep_for(std::chrono::milliseconds(5));}
    dragging.Stop();std::cout<<"PASS: changing slider targets remain monotonic, settle, and reverse without recoil\n";
    // Real capture: a two-monitor DDC frame can take ~125 ms. A timed
    // transition must still obey its native step cap across steep profiles.
    MonitorWorker limited;limited.SetTransitionDuration(600);limited.SetHardwareStep(2);limited.Start(nullptr,0,true,125);
    limited.Queue({Operation::Scan,L"",0,800});Wait(limited,{800});
    auto a=Calibration::FromNodes({25,40,55,70,100});auto b=Calibration::FromNodes({26,48,78,86,100});
    limited.QueueBatch({{Operation::Write,L"Demo_0",25,801,true,0,a},{Operation::Write,L"Demo_1",26,802,true,0,b}});
    previous={{L"Demo_0",55},{L"Demo_1",72}};final.clear();firstFrames.clear();
    deadline=std::chrono::steady_clock::now()+std::chrono::seconds(8);
    while(final.size()<2){for(auto& event:limited.Events()) {
        auto prior=previous[event.key];Check(event.ok&&event.raw<=prior&&prior-event.raw<=2,"Slow DDC exceeded hardware step cap");
        previous[event.key]=event.raw;if(!firstFrames.count(event.tag))firstFrames[event.tag]=event;if(event.final)final[event.tag]=event;
    }Check(std::chrono::steady_clock::now()<deadline,"Limited slow DDC transition did not settle");std::this_thread::sleep_for(std::chrono::milliseconds(5));}
    Check(final[801].raw==25&&final[802].raw==26,"Limited transition missed calibrated endpoints");
    Check(std::max(firstFrames[801].started,firstFrames[802].started)<std::min(firstFrames[801].finished,firstFrames[802].finished),"Step cap serialized monitor calls");
    limited.Queue({Operation::Write,L"Demo_0",100,803,true,100,a});Wait(limited,{803});
    limited.SetHardwareStep(0);limited.Queue({Operation::Read,L"Demo_0",0,804});auto uncapped=Wait(limited,{804});Check(uncapped[804].raw==100,"Live step setting did not reach endpoint");
    limited.SetHardwareStep(1);limited.SetTransitionDuration(0);limited.Queue({Operation::Write,L"Demo_0",0,805,true});auto bypass=Wait(limited,{805});Check(bypass[805].raw==0&&bypass[805].final,"Instant mode retained step cap");
    limited.Stop();std::cout<<"PASS: native 2% cap with 125 ms DDC, steep calibrated curves, parallel calls, live setting and instant bypass\n";
}
