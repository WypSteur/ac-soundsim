// Explicit user-started WASAPI LOOPBACK capture, never microphone/playback.
// Refuses to start without live GT86, normal SoundSim and --audit producer.
#define NOMINMAX
#include "soundsim/audit_frame.hpp"
#include "soundsim/ipc.hpp"
#include "soundsim/win_mmf.hpp"
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <ksmedia.h>
#include <mmsystem.h>
#include <wrl/client.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
using Microsoft::WRL::ComPtr;
using namespace soundsim;
namespace {
void checked(HRESULT hr) { if (FAILED(hr)) throw std::runtime_error("WASAPI HRESULT="+std::to_string(static_cast<unsigned long>(hr))); }
struct ComLifetime { ComLifetime() { checked(CoInitializeEx(nullptr,COINIT_MULTITHREADED)); } ~ComLifetime() { CoUninitialize(); } };
struct Timer { Timer() { timeBeginPeriod(1); } ~Timer() { timeEndPeriod(1); } };
struct FreeFormat { WAVEFORMATEX* value{}; ~FreeFormat() { CoTaskMemFree(value); } };
struct StopClient { IAudioClient* value{}; ~StopClient() { if (value) value->Stop(); } };
struct Packet { std::uint64_t frameOffset{},qpc100ns{},deviceFrame{}; std::uint32_t frames{},flags{}; };
struct Observed { ipc::AuditFrame frame; double observerQpc{}; };
double qpcSeconds() { LARGE_INTEGER q{},f{}; QueryPerformanceCounter(&q); QueryPerformanceFrequency(&f); return q.QuadPart/double(f.QuadPart); }
void writeWave(const std::filesystem::path& path,const std::vector<float>& samples,unsigned rate,unsigned channels) {
    std::ofstream out(path,std::ios::binary);
    auto u16=[&](unsigned v) { const char b[2]{char(v&255),char(v>>8)}; out.write(b,2); };
    auto u32=[&](std::uint32_t v) { u16(v&65535); u16(v>>16); };
    const auto bytes=static_cast<std::uint32_t>(samples.size()*4);
    // Non-PCM IEEE float: WAVEFORMATEX with cbSize=0 + fact frame count.
    out.write("RIFF",4); u32(bytes+50); out.write("WAVEfmt ",8); u32(18);
    u16(3); u16(channels); u32(rate); u32(rate*channels*4); u16(channels*4); u16(32);
    u16(0); out.write("fact",4); u32(4); u32(static_cast<std::uint32_t>(samples.size()/channels));
    out.write("data",4); u32(bytes); out.write(reinterpret_cast<const char*>(samples.data()),bytes);
    out.flush(); if (!out) throw std::runtime_error("capture WAV write failed");
}
}
int main(int argc,char** argv) {
    try {
        double seconds=20; std::filesystem::path output;
        for (int i=1;i<argc;++i) {
            const std::string arg=argv[i];
            if (arg=="--help") { std::cout << "soundsim-m5-capture --output NEW-directory [--seconds 1..120]\nCaptures SYSTEM OUTPUT (other apps included), no microphone; keep other apps silent. No upload.\n"; return 0; }
            if (++i>=argc) throw std::invalid_argument("missing argument");
            if (arg=="--output") output=argv[i];
            else if (arg=="--seconds") seconds=std::stod(argv[i]);
            else throw std::invalid_argument("unknown option");
        }
        if (output.empty() || !std::isfinite(seconds) || seconds<1 || seconds>120) throw std::invalid_argument("invalid capture duration/output");
        if (std::filesystem::exists(output)) throw std::invalid_argument("output must be a new directory (no overwrite)");
        ipc::Mapping stateMap,statusMap,auditMap;
        if (!stateMap.open(ipc::kStateName,sizeof(ipc::State),false) || !statusMap.open(ipc::kStatusName,sizeof(ipc::Status),false)
            || !auditMap.open(ipc::kAuditName,sizeof(ipc::AuditFrame),false))
            throw std::runtime_error("live GT86 + normal runtime -Audit required; no audio captured");
        ipc::State state; ipc::Status status; ipc::AuditFrame initial;
        if (!ipc::snapshot(stateMap.data(),state) || !ipc::valid(state) || state.carIndex!=0 || std::strncmp(state.carID,"ks_toyota_gt86",64)
            || (state.flags & (ipc::active|ipc::player|ipc::paused|ipc::replay)) != (ipc::active|ipc::player)
            || !ipc::snapshot(statusMap.data(),status) || status.magic!=ipc::kMagic || status.version!=ipc::kVersion || status.size!=sizeof(status)
            || status.mode!=static_cast<unsigned>(ipc::Mode::running) || status.flags!=2 || status.faults
            || !ipc::snapshot(auditMap.data(),initial) || initial.magic!=ipc::kAuditMagic || initial.version!=1 || initial.size!=sizeof(initial)
            || !initial.qpcFrequency || initial.generation!=status.generation || initial.mode!=status.mode || initial.sourceFlags!=2 || initial.faults
            || !initial.qpcInput || initial.qpcRenderEnd<initial.qpcInput || initial.qpcPublish<initial.qpcRenderEnd
            || qpcSeconds()-initial.qpcPublish/double(initial.qpcFrequency)>.5 || initial.qpcPublish/double(initial.qpcFrequency)>qpcSeconds())
            throw std::runtime_error("GT86/SoundSim audit inactive/stale; no audio captured");
        ComLifetime com; Timer timer;
        ComPtr<IMMDeviceEnumerator> enumerator;
        checked(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,IID_PPV_ARGS(enumerator.GetAddressOf())));
        ComPtr<IMMDevice> device; checked(enumerator->GetDefaultAudioEndpoint(eRender,eConsole,&device));
        ComPtr<IAudioClient> client;
        checked(device->Activate(__uuidof(IAudioClient),CLSCTX_ALL,nullptr,reinterpret_cast<void**>(client.GetAddressOf())));
        FreeFormat format; checked(client->GetMixFormat(&format.value));
        const auto* f=format.value;
        if (!f || (f->wFormatTag==WAVE_FORMAT_EXTENSIBLE && f->cbSize<sizeof(WAVEFORMATEXTENSIBLE)-sizeof(WAVEFORMATEX)))
            throw std::runtime_error("incomplete endpoint format");
        const bool floating=f->wFormatTag==WAVE_FORMAT_IEEE_FLOAT || (f->wFormatTag==WAVE_FORMAT_EXTENSIBLE &&
            reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(f)->SubFormat==KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);
        const bool integerPcm=f->wFormatTag==WAVE_FORMAT_PCM || (f->wFormatTag==WAVE_FORMAT_EXTENSIBLE &&
            reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(f)->SubFormat==KSDATAFORMAT_SUBTYPE_PCM);
        if (!((floating && f->wBitsPerSample==32) || (integerPcm && (f->wBitsPerSample==16 || f->wBitsPerSample==32)))
            || !f->nChannels || f->nChannels>8 || f->nSamplesPerSec>192000 || !f->nSamplesPerSec
            || f->nBlockAlign!=f->nChannels*(f->wBitsPerSample/8))
            throw std::runtime_error("unsupported endpoint format");
        const auto capacity=static_cast<std::size_t>((seconds+1)*f->nSamplesPerSec*f->nChannels);
        if (capacity>64*1024*1024/sizeof(float)) throw std::runtime_error("capture >64MiB; shorten duration");
        std::vector<float> samples; samples.reserve(capacity);
        std::vector<Packet> packets; packets.reserve(static_cast<std::size_t>(seconds*1000+1024));
        std::vector<Observed> observed; observed.reserve(static_cast<std::size_t>(seconds*200+1024));
        checked(client->Initialize(AUDCLNT_SHAREMODE_SHARED,AUDCLNT_STREAMFLAGS_LOOPBACK,10000000,0,f,nullptr));
        ComPtr<IAudioCaptureClient> capture; checked(client->GetService(IID_PPV_ARGS(capture.GetAddressOf())));
        if (!output.parent_path().empty()) std::filesystem::create_directories(output.parent_path());
        if (!std::filesystem::create_directory(output)) throw std::runtime_error("capture directory already exists");
        std::cout << "SYSTEM LOOPBACK recording for " << seconds << "s; no mic/upload. Output: " << output.string() << '\n';
        checked(client->Start()); StopClient stop{client.Get()}; const double start=qpcSeconds();
        std::uint32_t lastCommit=0; std::uint64_t invalidSamples=0;
        while (qpcSeconds()-start<seconds) {
            ipc::AuditFrame a;
            if (ipc::snapshot(auditMap.data(),a) && a.commit!=lastCommit && a.magic==ipc::kAuditMagic && a.version==1 && a.size==sizeof(a) && a.qpcFrequency) {
                observed.push_back({a,qpcSeconds()}); lastCommit=a.commit;
            }
            UINT32 available{}; checked(capture->GetNextPacketSize(&available));
            while (available) {
                BYTE* data{}; UINT32 frames{}; DWORD flags{}; UINT64 position{},timestamp{};
                checked(capture->GetBuffer(&data,&frames,&flags,&position,&timestamp));
                if (samples.size()+static_cast<std::size_t>(frames)*f->nChannels>capacity) {
                    capture->ReleaseBuffer(frames); throw std::runtime_error("capture buffer overflow");
                }
                packets.push_back({samples.size()/f->nChannels,timestamp,position,frames,flags});
                for (std::size_t i=0;i<static_cast<std::size_t>(frames)*f->nChannels;++i) {
                    double v=0;
                    if (!(flags&AUDCLNT_BUFFERFLAGS_SILENT)) v=floating ? reinterpret_cast<const float*>(data)[i]
                        : f->wBitsPerSample==16 ? reinterpret_cast<const std::int16_t*>(data)[i]/32768.0
                        : reinterpret_cast<const std::int32_t*>(data)[i]/2147483648.0;
                    if (!std::isfinite(v)) { ++invalidSamples; v=0; }
                    samples.push_back(static_cast<float>(v)); // no normalization/clamp
                }
                checked(capture->ReleaseBuffer(frames)); checked(capture->GetNextPacketSize(&available));
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        checked(client->Stop()); stop.value=nullptr;
        writeWave(output/"loopback.wav",samples,f->nSamplesPerSec,f->nChannels);
        std::ofstream audio(output/"audio-packets.csv"),telemetry(output/"runtime-telemetry.csv"),meta(output/"capture.json");
        audio << "frame_offset,frames,qpc_100ns,device_frame,flags\n";
        for (const auto& p:packets) audio << p.frameOffset << ',' << p.frames << ',' << p.qpc100ns << ',' << p.deviceFrame << ',' << p.flags << '\n';
        telemetry << "observer_qpc_s,generation,heartbeat,state_commit,mode,rpm,throttle,gear,qpc_input_s,qpc_render_end_s,qpc_publish_s,source_frames,late_blocks,ignition_anomalies,faults,state_age_s,ac_timestamp_s\n" << std::setprecision(17);
        for (const auto& r:observed) {
            const auto& a=r.frame; const double frequency=double(a.qpcFrequency);
            telemetry << r.observerQpc << ',' << a.generation << ',' << a.heartbeat << ',' << a.stateCommit << ',' << a.mode << ',' << a.rpm << ',' << a.throttle << ',' << a.gear
                << ',' << a.qpcInput/frequency << ',' << a.qpcRenderEnd/frequency << ',' << a.qpcPublish/frequency
                << ',' << a.sourceFrames << ',' << a.lateBlocks << ',' << a.ignitionAnomalies << ',' << a.faults << ',' << a.stateAgeSeconds << ',' << a.stateTimestampSeconds << '\n';
        }
        meta << "{\"scope\":\"SYSTEM LOOPBACK + observed runtime input, not physical pedal or ear latency\",\"sample_rate\":" << f->nSamplesPerSec
             << ",\"channels\":" << f->nChannels << ",\"audio_frames\":" << samples.size()/f->nChannels
             << ",\"telemetry_rows\":" << observed.size() << ",\"invalid_audio_samples\":" << invalidSamples
             << ",\"m5_status\":\"PENDING_MANUAL_REVIEW\"}\n";
        audio.flush(); telemetry.flush(); meta.flush();
        if (!audio || !telemetry || !meta || samples.empty() || observed.empty()) throw std::runtime_error("empty/incomplete capture");
        std::cout << "Saved packets=" << packets.size() << " telemetry=" << observed.size() << "; M5 not automatically validated\n";
        return 0;
    } catch(const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
