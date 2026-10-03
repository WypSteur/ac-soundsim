#include "soundsim/csp_stream.hpp"
#include "soundsim/ipc.hpp"
#include "soundsim/audit_frame.hpp"
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <thread>

using namespace soundsim;
namespace {
void check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
std::string unique(const char* suffix) {
    return "AcTools.ACSoundSim.Test." + std::to_string(GetCurrentProcessId()) + "." + std::to_string(GetTickCount64()) + "." + suffix;
}
ipc::State target() {
    ipc::State s;
    s.commit=2; s.generation=1; s.flags=ipc::active|ipc::player;
    s.rpm=3000; s.throttle=.3f; s.look.z=1; s.up.y=1;
    std::strcpy(s.carID,"ks_toyota_gt86");
    return s;
}
void unit() {
    auto s=target();
    check(ipc::valid(s),"valid packet rejected");
    const auto decoded=ipc::decode(s);
    check(decoded.sequence==2 && decoded.rpm==3000 && decoded.active && decoded.playerCar,"RuntimeCarStateV1 decode");
    ipc::StateTracker tracker;
    check(tracker.mode(0)==ipc::Mode::waiting,"initial mode");
    tracker.accept(s,0);
    check(tracker.mode(.1)==ipc::Mode::running,"running mode");
    check(!tracker.accept(s,.2),"duplicate refreshed heartbeat");
    check(tracker.mode(.251)==ipc::Mode::stale,"stale threshold");
    s.commit=8; s.flags|=ipc::paused; tracker.accept(s,.3);
    check(tracker.mode(.3)==ipc::Mode::paused && tracker.dropped()==2,"paused/dropped");
    s.commit=10; s.resetCounter=1; tracker.accept(s,.4);
    check(tracker.resets()==2,"reset detection");
    s.commit=12; s.generation=2; tracker.accept(s,.5);
    check(tracker.resets()==3,"generation detection");
    s.rpm=std::numeric_limits<float>::quiet_NaN();
    check(!ipc::valid(s),"NaN accepted");
    s=target(); std::memset(s.carID,'x',64); check(!ipc::valid(s),"unterminated ID");
    s=target(); s.size=191; check(!ipc::valid(s),"wrong size");
    s=target(); s.version=2; check(!ipc::valid(s),"wrong version");
    s=target(); s.rpm=12001; check(!ipc::valid(s),"RPM bound");
    s=target(); s.commit=3; check(!ipc::valid(s),"odd commit");
    s=target(); s.carIndex=1; tracker.accept(s,1); check(tracker.mode(1)==ipc::Mode::wrongCar,"non-player rejected");

    ipc::Mapping writer, reader;
    const auto name=unique("state"); writer.create(name,sizeof(s));
    check(reader.open(name,sizeof(s),false),"read-only view open");
    check(!ipc::snapshot(reader.data(),s),"uninitialized packet");
    ipc::publish(writer.data(),target());
    check(ipc::snapshot(reader.data(),s) && s.commit==2 && s.rpm==3000,"real MMF snapshot");
    InterlockedExchange(static_cast<volatile LONG*>(writer.data()),3);
    check(!ipc::snapshot(reader.data(),s),"torn packet accepted");
    InterlockedExchange(static_cast<volatile LONG*>(writer.data()),2);

    const auto audioName=unique("audio");
    ipc::CspStream stream(audioName);
    ipc::Mapping audio;
    check(audio.open(audioName,ipc::CspStream::kSize,false),"audio view");
    auto* h=static_cast<const ipc::CspAudioHeader*>(audio.data());
    check(h->sampleRate==44100 && h->channels==1 && h->fmodFormat==5 && h->decodeSamples==1764 && h->decodeBytes==7056,"CSP header ABI");
    std::array<float,294> block{};
    std::uint64_t total=0;
    for(int b=0;b<450;++b) {
        for(int i=0;i<294;++i) block[i]=static_cast<float>((total+i)%1000)/1000;
        stream.push(block.data(),294); total+=294;
        check(h->publishedBytes==static_cast<std::int64_t>(total*4),"published byte counter");
        const auto* ring=reinterpret_cast<const float*>(static_cast<const char*>(audio.data())+64);
        check(ring[(total-1)%ipc::CspStream::kCapacity]==block[293],"wrapped ring data");
    }
    stream.stop(); check(h->publishedBytes==0,"stop sentinel");
    std::cout<<"PASS IPC ABI, validation, torn reads, heartbeat, CSP header/ring/wrap\n";
}
void integration(const std::string& executable,bool cadenceProbe=false) {
    const auto stateName=unique("live-state"), statusName=unique("live-status");
    ipc::Mapping state, status, audio, auditMap;
    state.create(stateName,sizeof(ipc::State));
    const auto testLogs=std::filesystem::current_path()/"artifacts"/"ipc-tests"/statusName;
    std::string command="\""+executable+"\" --seconds 20 --audit --logs \""+testLogs.string()+"\" --state-name "+stateName+" --status-name "+statusName;
    STARTUPINFOA startup{}; startup.cb=sizeof(startup);
    PROCESS_INFORMATION process{};
    check(CreateProcessA(nullptr,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process)!=0,"runtime process start");
    CloseHandle(process.hThread);
    // Always stop only our uniquely named runtime, including on test failure.
    struct Cleanup {
        HANDLE process; std::string stopName;
        ~Cleanup() {
            HANDLE stop=OpenEventA(EVENT_MODIFY_STATE,FALSE,("Local\\"+stopName+".Stop").c_str());
            if(stop) {SetEvent(stop);CloseHandle(stop);}
            WaitForSingleObject(process,25000); CloseHandle(process);
        }
    } cleanup{process.hProcess,statusName};
    if (cadenceProbe) {
        // ONLY our isolated child process: reproduce background Windows 11
        // Sleep timer throttling. Never modifies the installed live runtime.
        PROCESS_POWER_THROTTLING_STATE power{};
        power.Version=PROCESS_POWER_THROTTLING_CURRENT_VERSION;
        power.ControlMask=power.StateMask=0x4; // IGNORE_TIMER_RESOLUTION
        check(SetProcessInformation(process.hProcess,ProcessPowerThrottling,&power,sizeof(power))!=0,"isolated timer-throttling simulation");
    }
    for(int i=0;i<100 && !status.data();++i) {
        status.open(statusName,sizeof(ipc::Status),false);
        if(!status.data()) std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    check(status.data()!=nullptr,"runtime status mapping missing");
    auto packet=target();
    ipc::Status r{};
    auto stage=[&](ipc::Mode expected,bool write,int durationMs=500) {
        bool seen=false;
        const auto start=std::chrono::steady_clock::now();
        do {
            if(write) { packet.timestampSeconds+=.020; packet.writerClock+=.020; ipc::publish(state.data(),packet); }
            if(ipc::snapshot(status.data(),r) && r.mode==static_cast<std::uint32_t>(expected)) seen=true;
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        } while(std::chrono::steady_clock::now()-start<std::chrono::milliseconds(durationMs));
        check(seen,ipc::modeName(expected));
        check(r.faults==0,"runtime engine fault");
    };
    stage(ipc::Mode::running,true,1500);
    check(auditMap.open(statusName+".Audit.v1",sizeof(ipc::AuditFrame),false),"opt-in QPC audit mapping");
    ipc::AuditFrame trace;
    check(ipc::snapshot(auditMap.data(),trace) && trace.magic==ipc::kAuditMagic && trace.version==1 && trace.size==136,"audit ABI/schema");
    check(trace.qpcFrequency>0 && trace.qpcInput>0 && trace.qpcRenderEnd>=trace.qpcInput && trace.qpcPublish>=trace.qpcRenderEnd,"audit QPC stage ordering");
    check(trace.rpm==3000 && trace.throttle==.3f && trace.ignitionAnomalies==0,"audit input/ignition provenance");
    check(r.requestedRpm==3000 && std::abs(r.effectiveRpm-3000)<1e-8,"external crank live RPM");
    check(audio.open(r.audioName,r.audioSize,false),"runtime CSP audio mapping");
    const auto* h=static_cast<const ipc::CspAudioHeader*>(audio.data());
    check(h->publishedBytes>0 && h->sampleRate==44100 && h->channels==1,"runtime PCM protocol");
    const auto* samples=reinterpret_cast<const float*>(static_cast<const char*>(audio.data())+64);
    double energy=0;
    const auto end=h->publishedBytes/4;
    // Debug/ASAN or a busy machine may end on a deliberately silent missed block.
    // Verify generated content over the last second, not just that final block.
    for(auto i=std::max<std::int64_t>(0,end-44100);i<end;++i) {
        const float sample=samples[i%ipc::CspStream::kCapacity];
        check(std::isfinite(sample) && std::abs(sample)<=1,"runtime PCM validity"); energy+=sample*sample;
    }
    check(energy>1e-6,"runtime silent PCM");
    if(cadenceProbe) {
        const auto initialFrames=r.frames,initialLate=r.lateBlocks;
        const auto begin=std::chrono::steady_clock::now();
        stage(ipc::Mode::running,true,4000);
        const auto elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count();
        std::cout<<"FA20 3000rpm isolated hidden runtime, ignore_timer_resolution=1: elapsed_s="<<elapsed
                 <<" source_frames_per_s="<<(r.frames-initialFrames)/elapsed
                 <<" producer_late_blocks="<<r.lateBlocks-initialLate<<" max_render_ms="<<r.maxRenderMs
                 <<" faults="<<r.faults<<"; not native listening or consumer latency\n";
        return;
    }
    packet.flags|=ipc::paused; stage(ipc::Mode::paused,true);
    const auto phase=r.phase; stage(ipc::Mode::paused,true);
    check(r.phase==phase,"paused crank advanced");
    packet.flags=ipc::active|ipc::player; packet.rpm=6000; stage(ipc::Mode::running,true);
    check(r.requestedRpm==6000 && std::abs(r.effectiveRpm-6000)<1e-8,"RPM step authority");
    ++packet.resetCounter; stage(ipc::Mode::running,true);
    check(r.resets>=2,"live reset not detected");
    std::strcpy(packet.carID,"wrong_car"); stage(ipc::Mode::wrongCar,true);
    std::strcpy(packet.carID,"ks_toyota_gt86"); stage(ipc::Mode::running,true);
    stage(ipc::Mode::stale,false,600);
    packet.rpm=std::numeric_limits<float>::quiet_NaN(); stage(ipc::Mode::invalid,true);
    packet.rpm=3000; packet.flags|=ipc::replay; stage(ipc::Mode::replay,true);
    packet.flags=ipc::active|ipc::player; ++packet.generation; stage(ipc::Mode::running,true,600);
    HANDLE stop=OpenEventA(EVENT_MODIFY_STATE,FALSE,("Local\\"+statusName+".Stop").c_str());
    check(stop!=nullptr,"stop control"); SetEvent(stop); CloseHandle(stop);
    check(WaitForSingleObject(process.hProcess,5000)==WAIT_OBJECT_0,"graceful stop timeout");
    DWORD code=1; GetExitCodeProcess(process.hProcess,&code); check(code==0,"runtime exit failure");
    check(h->publishedBytes==0,"runtime did not stop CSP stream");
    std::cout<<"PASS separate-process state -> actual FA20 PCM MMF; RPM step, pause, reset, wrong car, stale, NaN, replay, restart generation, graceful stop\n";
}
void processRestart(const std::string& executable) {
    const auto stateName=unique("restart-state"),statusName=unique("restart-status");
    ipc::Mapping state,status,oldAudio,newAudio;
    state.create(stateName,sizeof(ipc::State));
    auto packet=target(); ipc::Status observed{};
    struct Child {
        HANDLE process{}; std::string statusName;
        ~Child() {
            if(!process) return;
            HANDLE stop=OpenEventA(EVENT_MODIFY_STATE,FALSE,("Local\\"+statusName+".Stop").c_str());
            if(stop) { SetEvent(stop); CloseHandle(stop); }
            WaitForSingleObject(process,25000); CloseHandle(process);
        }
    } child;
    child.statusName=statusName;
    auto launch=[&]() {
        const auto logs=std::filesystem::current_path()/"artifacts"/"ipc-tests"/statusName;
        std::string command="\""+executable+"\" --seconds 20 --logs \""+logs.string()+"\" --state-name "+stateName+" --status-name "+statusName;
        STARTUPINFOA startup{}; startup.cb=sizeof(startup); PROCESS_INFORMATION process{};
        check(CreateProcessA(nullptr,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process)!=0,"restart child launch");
        CloseHandle(process.hThread); child.process=process.hProcess;
    };
    auto running=[&](const std::string& previousName) {
        const auto begin=std::chrono::steady_clock::now(); bool seen=false;
        while(std::chrono::steady_clock::now()-begin<std::chrono::seconds(3)) {
            packet.timestampSeconds+=.020; packet.writerClock+=.020; ipc::publish(state.data(),packet);
            if(!status.data()) status.open(statusName,sizeof(ipc::Status),false);
            if(status.data() && ipc::snapshot(status.data(),observed) && observed.mode==1 && !observed.faults && observed.frames>294
                && std::string(observed.audioName)!=previousName) { seen=true; break; }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        check(seen,"new child never published a healthy fresh stream");
    };
    launch(); running("");
    const auto oldName=std::string(observed.audioName); const auto oldGeneration=observed.generation;
    check(oldAudio.open(oldName,ipc::CspStream::kSize,false),"old audio read view");
    HANDLE stop=OpenEventA(EVENT_MODIFY_STATE,FALSE,("Local\\"+statusName+".Stop").c_str());
    check(stop!=nullptr,"restart stop event"); SetEvent(stop); CloseHandle(stop);
    check(WaitForSingleObject(child.process,5000)==WAIT_OBJECT_0,"restart graceful stop");
    check(static_cast<const ipc::CspAudioHeader*>(oldAudio.data())->publishedBytes==0,"old stream not silenced");
    CloseHandle(child.process); child.process=nullptr;
    // Retain the STATUS and OLD AUDIO views as CSP does. Producer must reuse
    // only the status mapping while creating a distinct new audio mapping.
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    launch(); running(oldName);
    check(observed.generation!=oldGeneration && observed.requestedRpm==3000,"restart generation/RPM provenance");
    check(newAudio.open(observed.audioName,ipc::CspStream::kSize,false),"fresh audio view");
    // Hard failure ONLY of our unique disposable child; never the live runtime.
    check(TerminateProcess(child.process,9)!=0,"isolated crash simulation");
    check(WaitForSingleObject(child.process,5000)==WAIT_OBJECT_0,"crash timeout");
    // A hard kill can leave an odd seqlock commit. Such a snapshot is correctly
    // unreadable, not a test failure: after process death ALL bytes must freeze.
    // These raw copies are diagnostic only, never accepted as coherent status.
    ipc::Status atCrash{}; std::memcpy(&atCrash,status.data(),sizeof(atCrash));
    std::this_thread::sleep_for(std::chrono::milliseconds(350));
    ipc::Status frozen{}; std::memcpy(&frozen,status.data(),sizeof(frozen));
    check(std::memcmp(&atCrash,&frozen,sizeof(frozen))==0,"crashed writer was not frozen");
    CloseHandle(child.process); child.process=nullptr;
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    const auto crashedName=std::string(observed.audioName);
    launch(); running(crashedName);
    std::cout<<"PASS real process stop/restart/crash/restart with retained status/audio views; bridge heartbeat fallback tested separately\n";
}
}
int main(int argc,char** argv) {
    try {
        if(argc==3 && std::string(argv[1])=="--runtime") integration(argv[2]);
        else if(argc==3 && std::string(argv[1])=="--cadence") integration(argv[2],true);
        else if(argc==3 && std::string(argv[1])=="--restart") processRestart(argv[2]);
        else unit();
        return 0;
    } catch(const std::exception& error) {std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}
}
