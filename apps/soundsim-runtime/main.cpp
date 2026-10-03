#include "soundsim/csp_stream.hpp"
#include "soundsim/headless_engine.hpp"
#include "soundsim/ipc.hpp"
#include "soundsim/logger.hpp"
#include "soundsim/audit_frame.hpp"
#include "soundsim/audio_scheduler.hpp"
#include <array>
#include <chrono>
#include <cmath>
#include <iostream>
#include <sstream>

using namespace soundsim;
namespace {
volatile LONG quit = 0;
BOOL WINAPI consoleHandler(DWORD) { InterlockedExchange(&quit, 1); return TRUE; }
struct Handle {
    HANDLE value{};
    ~Handle() { if (value) CloseHandle(value); }
};
double clockSeconds() {
    using Clock = std::chrono::steady_clock;
    static const auto start = Clock::now();
    return std::chrono::duration<double>(Clock::now() - start).count();
}
int run(int argc, char** argv) {
    std::string stateName = ipc::kStateName, statusName = ipc::kStatusName;
    std::filesystem::path logs = "logs/runtime";
    std::filesystem::path profilePath = defaultEngineProfilePath();
    double seconds = 0;
    bool diagnosticTone = false;
    bool legacyModel = false;
    bool auditEnabled = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help") {
            std::cout << "soundsim-runtime [--seconds N] [--logs directory] [--profile yaml] [--state-name name] [--status-name name] [--diagnostic-tone] [--legacy-model] [--audit]\n";
            return 0;
        }
        if (arg == "--diagnostic-tone") { diagnosticTone = true; continue; }
        if (arg == "--legacy-model") { legacyModel = true; continue; }
        if (arg == "--audit") { auditEnabled = true; continue; }
        if (i + 1 >= argc) throw std::invalid_argument("missing option value");
        const std::string value = argv[++i];
        if (arg == "--logs") logs = value;
        else if (arg == "--profile") profilePath = value;
        else if (arg == "--state-name") stateName = value;
        else if (arg == "--status-name") statusName = value;
        else if (arg == "--seconds") {
            seconds = std::stod(value);
            if (!std::isfinite(seconds) || seconds <= 0) throw std::invalid_argument("invalid duration");
        } else throw std::invalid_argument("unknown option " + arg);
    }
    Handle owner{CreateMutexA(nullptr, FALSE, ("Local\\" + statusName + ".Owner").c_str())};
    if (!owner.value || GetLastError() == ERROR_ALREADY_EXISTS) throw std::runtime_error("another runtime owns this status channel");
    Handle stop{CreateEventA(nullptr, TRUE, FALSE, ("Local\\" + statusName + ".Stop").c_str())};
    if (!stop.value) throw std::runtime_error("stop event creation failed");
    ResetEvent(stop.value);
    Logger::instance().initialize(logs, "soundsim-runtime");
    SS_LOG_INFO("bootstrap", "M2/M3 Windows x64; Engine-Sim=" + std::string(HeadlessEngine::upstreamRevision()));
    const auto profile = loadEngineProfile(profilePath); // startup only, no hot reload
    std::ostringstream profileInfo;
    profileInfo << "loaded=" << std::filesystem::absolute(profilePath).string() << " schema=" << profile.schemaVersion
        << " ks_toyota_gt86 -> " << profile.id << (legacyModel ? " legacy provisional M1" : " FA20D reference port")
        << "; reference IR smooth_39 gain=" << profile.referenceAudio.impulseResponseGain
        << " master=" << profile.referenceAudio.masterVolume << " levelerTarget=" << profile.referenceAudio.levelerTarget
        << "; public core; AC RPM authority; calibration pending";
    SS_LOG_INFO("profile", profileInfo.str());
    SS_LOG_INFO("ignition", "external continuous phase+advance crossing; per-cylinder 720-cycle identity; no RPM smoothing");
    const auto generation = GetTickCount64();
    const auto audioName = "AcTools.ACSoundSim.Audio." + std::to_string(GetCurrentProcessId()) + "." + std::to_string(generation);
    ipc::CspStream stream(audioName);
    ipc::Mapping statusMapping, stateMapping;
    // Lua may retain a read view across producer restarts. Singleton owns writes.
    statusMapping.create(statusName, sizeof(ipc::Status), true);
    ipc::Status status;
    status.generation = static_cast<std::uint32_t>(generation) | 1u;
    status.sampleRate = ipc::CspStream::kRate;
    status.audioSize = ipc::CspStream::kSize;
    status.flags = diagnosticTone ? 1u : (legacyModel ? 0u : 2u);
    ipc::Mapping auditMapping;
    ipc::AuditFrame audit;
    if (auditEnabled) {
        LARGE_INTEGER frequency{}; if (!QueryPerformanceFrequency(&frequency)) throw std::runtime_error("QPC unavailable");
        audit.qpcFrequency=frequency.QuadPart;
        audit.generation=status.generation; audit.sourceFlags=status.flags;
        auditMapping.create(statusName+".Audit.v1",sizeof(audit),true);
        SS_LOG_INFO("audit", "opt-in 136-byte QPC trace; telemetry observation -> source publish only; not consumer latency");
    }
    auto stamp=[]() { LARGE_INTEGER value{}; QueryPerformanceCounter(&value); return static_cast<std::uint64_t>(value.QuadPart); };
    if (diagnosticTone) SS_LOG_WARN("audio", "DIAGNOSTIC 440 Hz TONE ONLY; not Engine-Sim and not a replacement engine model");
    std::strncpy(status.audioName, audioName.c_str(), sizeof(status.audioName) - 1);
    SS_LOG_INFO("ipc", "status=" + statusName + " size=368 schema=1; awaiting " + stateName);
    SS_LOG_INFO("audio", "MMF=" + audioName + " size=" + std::to_string(ipc::CspStream::kSize)
        + " mono float32 44100 Hz; block=294 (6.667 ms); decode hint=40 ms; ring=2.56 s capacity, not latency; consumer cursor/fill/underruns unavailable");
    AudioWakeTimer timer;
    AudioThreadTask audioTask;
    SS_LOG_INFO("scheduler", "high-resolution waitable timer; fixed time grid; scoped MMCSS Pro Audio only while rendering");
    SetConsoleCtrlHandler(consoleHandler, TRUE);
    constexpr int steps = 147, frames = steps * 2;
    constexpr double period = static_cast<double>(frames) / HeadlessEngine::kSampleRate;
    std::array<std::int16_t, HeadlessEngine::kMaximumBlockFrames> raw{};
    std::array<float, frames> pcm{};
    ipc::StateTracker tracker;
    std::unique_ptr<HeadlessEngine> engine;
    std::uint64_t engineEpoch = 0;
    ipc::Mode previousMode = ipc::Mode::fault;
    double next = clockSeconds(), nextOpen = 0, nextLog = 0, gain = 0;
    bool faulted = false;
    while (!quit && WaitForSingleObject(stop.value, 0) != WAIT_OBJECT_0) {
        const double now = clockSeconds();
        if (seconds > 0 && now >= seconds) break;
        if (!stateMapping.data() && now >= nextOpen) {
            nextOpen = now + 0.5;
            if (stateMapping.open(stateName, sizeof(ipc::State), false)) SS_LOG_INFO("ipc", "opened state MMF size=192 schema=1");
        }
        ipc::State candidate;
        if (stateMapping.data()) {
            if (ipc::snapshot(stateMapping.data(), candidate)) tracker.accept(candidate, now);
            else ++status.tornStates; // includes not-yet-committed packets
            // Reopen after a stale writer dies, so a later AC session can create a
            // fresh mapping rather than retaining the old read view indefinitely.
            if (tracker.age(now) > 0.5) { stateMapping.close(); nextOpen = now + 0.5; }
        }
        if (tracker.resets() != engineEpoch) {
            engineEpoch = tracker.resets(); engine.reset(); faulted = false; gain = 0;
            SS_LOG_INFO("runtime", "reset/session transition epoch=" + std::to_string(engineEpoch));
        }
        auto mode = tracker.mode(now);
        if (faulted && mode == ipc::Mode::running) mode = ipc::Mode::fault;
        if (mode!=previousMode) {
            if (!audioTask.setActive(mode==ipc::Mode::running))
                SS_LOG_WARN("scheduler", "MMCSS unavailable error="+std::to_string(audioTask.error()));
        }
        pcm.fill(0);
        if (auditEnabled) { audit.qpcInput=stamp(); audit.qpcRenderEnd=0; }
        status.renderMs = 0;
        if (mode == ipc::Mode::running && diagnosticTone) {
            for (int i = 0; i < frames; ++i) {
                gain = std::min(1.0, gain + 1.0 / (HeadlessEngine::kSampleRate * 0.050));
                const double angle = (stream.frames() + i) * (2 * 3.14159265358979323846 * 440 / HeadlessEngine::kSampleRate);
                pcm[i] = static_cast<float>(0.05 * gain * std::sin(angle));
            }
        } else if (mode == ipc::Mode::running) {
            try {
                if (!engine) engine = std::make_unique<HeadlessEngine>(profile,
                    legacyModel ? EnginePreset::legacyM1 : EnginePreset::fa20ReferenceFull);
                const auto& s = tracker.latest();
                const auto runtime = ipc::decode(s);
                // Bounded zero-order hold: latest AC sample, no guessed future RPM.
                const auto begin = clockSeconds();
                engine->render({runtime.rpm, runtime.throttle, runtime.rpm > 0}, steps, raw.data(), static_cast<int>(raw.size()));
                if (auditEnabled) audit.qpcRenderEnd=stamp();
                status.renderMs = (clockSeconds() - begin) * 1000;
                status.maxRenderMs = std::max(status.maxRenderMs, status.renderMs);
                const auto& d = engine->diagnostics();
                status.requestedRpm = d.requestedRpm; status.effectiveRpm = d.effectiveRpm; status.phase = d.phase;
                for (int i = 0; i < frames; ++i) {
                    gain = std::min(1.0, gain + 1.0 / (HeadlessEngine::kSampleRate * 0.050));
                    pcm[i] = static_cast<float>(raw[i] / 32768.0 * gain);
                }
            } catch (const std::exception& error) {
                faulted = true; mode = ipc::Mode::fault; ++status.faults;
                audioTask.setActive(false);
                std::strncpy(status.error, error.what(), sizeof(status.error) - 1);
                SS_LOG_ERROR("engine-sim", status.error);
            }
        }
        if (mode != ipc::Mode::running) gain = 0;
        if (mode != previousMode) {
            SS_LOG_INFO("runtime", std::string("state=") + ipc::modeName(mode)); previousMode = mode;
        }
        stream.push(pcm.data(), frames); // Paused/stale/fault -> silence, crank frozen.
        status.mode = static_cast<std::uint32_t>(mode);
        status.stateCommit = tracker.received() ? tracker.latest().commit : 0;
        status.stateAgeSeconds = tracker.age(now);
        status.droppedStates = tracker.dropped(); status.resets = tracker.resets();
        status.frames = stream.frames(); ++status.heartbeat;
        ipc::publish(statusMapping.data(), status);
        if (auditEnabled) {
            audit.qpcPublish=stamp(); audit.mode=status.mode; audit.stateCommit=status.stateCommit;
            audit.sourceFrames=status.frames; audit.heartbeat=status.heartbeat;
            audit.ignitionAnomalies=engine ? engine->diagnostics().firingOrderErrors : 0;
            audit.lateBlocks=status.lateBlocks; audit.stateAgeSeconds=status.stateAgeSeconds; audit.faults=status.faults;
            if (tracker.received()) {
                const auto& sample=tracker.latest(); audit.rpm=sample.rpm; audit.throttle=sample.throttle;
                audit.gear=sample.gear; audit.stateTimestampSeconds=sample.timestampSeconds; audit.stateFlags=sample.flags;
            }
            ipc::publish(auditMapping.data(),audit);
        }
        if (now >= nextLog) {
            nextLog = now + 1;
            std::ostringstream line;
            line << "mode=" << ipc::modeName(mode) << " writerSeq=" << status.stateCommit
                 << " ageMs=" << status.stateAgeSeconds * 1000 << " rpm=" << status.requestedRpm
                 << " effective=" << status.effectiveRpm << " phase=" << status.phase
                 << " frames=" << status.frames << " renderMs=" << status.renderMs
                 << " maxRenderMs=" << status.maxRenderMs << " producerLateBlocks=" << status.lateBlocks
                 << " torn=" << status.tornStates << " dropped=" << status.droppedStates << " faults=" << status.faults;
            SS_LOG_INFO("runtime", line.str());
        }
        next += period;
        const auto after = clockSeconds();
        const auto recovery=recoverAudioDeadline(next,after,period);
        if (recovery.missed) {
            // Do not render a burst of outdated crank states to catch up. Publish
            // silence for missed wall-time blocks and resynchronize the scheduler.
            pcm.fill(0);
            for (int i = 0; i < recovery.missed; ++i) stream.push(pcm.data(), frames);
            status.lateBlocks += recovery.missed; gain = 0;
        }
        const auto delay = next - clockSeconds();
        if (!timer.wait(delay,stop.value)) break;
    }
    status.mode = static_cast<std::uint32_t>(ipc::Mode::waiting);
    status.audioName[0] = 0; ++status.heartbeat;
    ipc::publish(statusMapping.data(), status);
    if (auditEnabled) { audit.mode=status.mode; audit.qpcPublish=stamp(); ++audit.heartbeat; ipc::publish(auditMapping.data(),audit); }
    stream.stop();
    SS_LOG_INFO("bootstrap", "runtime stopped; frames=" + std::to_string(stream.frames()));
    SetConsoleCtrlHandler(consoleHandler, FALSE);
    return status.faults ? 2 : 0;
}
}
int main(int argc, char** argv) {
    try { return run(argc, argv); }
    catch (const std::exception& error) { std::cerr << "SoundSim runtime: " << error.what() << '\n'; return 1; }
}
