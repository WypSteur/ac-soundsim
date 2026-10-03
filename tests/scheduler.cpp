#include "soundsim/audio_scheduler.hpp"
#include <mmsystem.h>
#include <chrono>
#include <iostream>
#include <thread>

using namespace soundsim;
void check(bool ok,const char* text) { if(!ok) throw std::runtime_error(text); }
double clockSeconds() {
    static const auto origin=std::chrono::steady_clock::now();
    return std::chrono::duration<double>(std::chrono::steady_clock::now()-origin).count();
}
int main(int argc,char** argv) {
    try {
        if (argc==3 && std::string(argv[1])=="--probe") {
            const std::string policy=argv[2];
            check(policy=="legacy" || policy=="precise","probe policy");
            // Simulate Windows 11 background/inaudible timer throttling in THIS
            // disposable probe only. No game/runtime MMF, audio or global edits.
            PROCESS_POWER_THROTTLING_STATE power{};
            power.Version=PROCESS_POWER_THROTTLING_CURRENT_VERSION;
            power.ControlMask=power.StateMask=0x4; // IGNORE_TIMER_RESOLUTION
            const bool ignored=SetProcessInformation(GetCurrentProcess(),ProcessPowerThrottling,&power,sizeof(power))!=0;
            timeBeginPeriod(1);
            HANDLE stop=CreateEventW(nullptr,TRUE,FALSE,nullptr);
            check(stop!=nullptr,"probe stop event");
            AudioWakeTimer timer; AudioThreadTask task;
            const bool mmcss=policy=="precise" && task.setActive(true);
            constexpr double period=1.0/150; double next=clockSeconds(),start=next;
            std::uint64_t blocks=0,missed=0; double maximumLate=0;
            while(clockSeconds()-start<4) {
                const auto begin=clockSeconds();
                while(clockSeconds()-begin<.0048) {} // bounded synthetic ~4.8ms CPU work
                ++blocks; next+=period; const auto after=clockSeconds();
                maximumLate=std::max(maximumLate,std::max(0.0,after-next));
                if(policy=="legacy") {
                    if(after>next+period) {
                        missed+=static_cast<int>(std::min(150.0,std::floor((after-next)/period)));
                        next=after+period;
                    }
                    if(next>clockSeconds()) std::this_thread::sleep_for(std::chrono::duration<double>(next-clockSeconds()));
                } else {
                    missed+=recoverAudioDeadline(next,after,period).missed;
                    timer.wait(next-clockSeconds(),stop);
                }
            }
            std::cout<<"policy="<<policy<<" ignore_timer_resolution="<<ignored<<" mmcss="<<mmcss
                     <<" rendered_blocks="<<blocks<<" missed="<<missed<<" max_late_ms="<<maximumLate*1000
                     <<" elapsed_s="<<clockSeconds()-start<<'\n';
            CloseHandle(stop); timeEndPeriod(1); return 0;
        }
        check(argc==1,"usage: scheduler-test [--probe legacy|precise]");
        constexpr double p=1.0/150;
        double next=p;
        check(recoverAudioDeadline(next,p,p).missed==0 && next==p,"on-time deadline");
        check(recoverAudioDeadline(next,1.9*p,p).missed==0 && next==p,"sub-block grace");
        auto recovery=recoverAudioDeadline(next,4.2*p,p);
        check(recovery.missed==3 && !recovery.largeGap && std::abs(next-4*p)<1e-12,"late wake lost time grid");
        check(recoverAudioDeadline(next,4.7*p,p).missed==0,"recovery adds extra idle period");
        recovery=recoverAudioDeadline(next,10,p);
        check(recovery.largeGap && recovery.missed==150 && next>10,"long suspend unbounded catch-up");
        bool rejected=false;
        try { recoverAudioDeadline(next,10,0); } catch(const std::invalid_argument&) { rejected=true; }
        check(rejected,"invalid period");
        AudioWakeTimer timer;
        HANDLE stop=CreateEventW(nullptr,TRUE,TRUE,nullptr);
        check(stop!=nullptr,"test stop event");
        const double before=clockSeconds();
        check(!timer.wait(.1,stop) && clockSeconds()-before<.050,"stop interrupting wait");
        ResetEvent(stop); check(timer.wait(.001,stop),"timer wake");
        CloseHandle(stop);
        std::cout<<"PASS audio time-grid recovery, bounded suspend, high-res timer and stop interruption\n";
        return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
