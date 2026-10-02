#include "soundsim/headless_engine.hpp"
#include "soundsim/externally_driven_crank.hpp"
#include "soundsim/logger.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {
using soundsim::HeadlessEngine;
namespace fs = std::filesystem;

void require(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}

// Little-endian RIFF writer. Audio stays mono signed 16-bit upstream PCM.
class WaveWriter {
public:
    explicit WaveWriter(const fs::path& path) : out_(path, std::ios::binary) {
        require(bool(out_), "Cannot open output WAV");
        out_.write("RIFF", 4); word(0, 4); out_.write("WAVEfmt ", 8);
        word(16, 4); word(1, 2); word(1, 2); word(HeadlessEngine::kSampleRate, 4);
        word(HeadlessEngine::kSampleRate * 2, 4); word(2, 2); word(16, 2);
        out_.write("data", 4); word(0, 4);
    }
    void append(const std::int16_t* data, int count) {
        for (int i = 0; i < count; ++i) word(std::uint16_t(data[i]), 2);
        frames_ += count;
    }
    void finish() {
        out_.seekp(4); word(36 + frames_ * 2, 4);
        out_.seekp(40); word(frames_ * 2, 4);
        out_.flush();
        require(bool(out_), "WAV write failed");
    }
    std::uint64_t frames() const { return frames_; }
private:
    void word(std::uint64_t v, int n) {
        for (int i = 0; i < n; ++i) out_.put(char((v >> (8 * i)) & 255));
    }
    std::ofstream out_;
    std::uint64_t frames_{};
};

double sweepRpm(double t) {
    if (t < 1) return 800;
    if (t < 7) return 800 + (t - 1) * 1100;
    if (t < 8) return 7400;
    if (t < 14) return 7400 - (t - 8) * 1100;
    return 800;
}
}

int main(int argc, char** argv) {
    using namespace soundsim;
    fs::path output = fs::current_path() / "artifacts" / "m1";
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--output" && i + 1 < argc) output = argv[++i];
        else { std::cerr << "Usage: soundsim-m1 [--output directory]\n"; return 2; }
    }
    try {
        fs::create_directories(output);
        Logger::instance().initialize(output, "m1-sweep");
        SS_LOG_INFO("bootstrap", std::string("Engine-Sim revision=") + HeadlessEngine::upstreamRevision());
        SS_LOG_INFO("profile", "car=ks_toyota_gt86 engine=subaru_fa20_gt86_baseline firing_order=1,3,2,4 calibration=provisional");
        SS_LOG_INFO("crank", "authority=external vehicle_solver=absent dt=fixed simulation_hz=22050 fluid_substeps=8");
        SS_LOG_INFO("audio", "sample_rate=44100 channels=1 encoding=s16 renderer=synchronous identity_IR=true");
        HeadlessEngine engine(makeFa20Baseline());
        std::array<std::int16_t, HeadlessEngine::kMaximumBlockFrames> pcm{};
        constexpr int blockSteps = 147; // 1/150 s, 294 PCM frames
        constexpr int warmupBlocks = 300; // 2 seconds of natural filling/combustion
        for (int b = 0; b < warmupBlocks; ++b) {
            engine.render({800, 0.12, true}, blockSteps, pcm.data(), int(pcm.size()));
        }
        const auto before = engine.diagnostics();
        WaveWriter wav(output / "fa20-rpm-sweep.wav");
        std::ofstream csv(output / "sweep.csv");
        require(bool(csv), "Cannot open sweep CSV");
        csv << "time_s,requested_rpm,effective_rpm,angular_velocity_rad_s,phase_rad,total_angle_rad,audio_frames,burnt_fuel_kg,maximum_pressure_pa\n";
        const auto start = std::chrono::steady_clock::now();
        constexpr int blocks = 2250; // 15 s: idle/up/hold/down/idle
        double expectedAngle = before.totalAngle;
        double maxPhaseError = 0;
        double maxRpmError = 0;
        double squared = 0;
        int peak = 0;
        std::uint64_t clipped = 0;
        std::uint64_t nonzero = 0;
        for (int b = 0; b < blocks; ++b) {
            const double t = (b + 0.5) / 150.0;
            const double rpm = sweepRpm(t);
            // Open throttle on acceleration and close on deceleration. This is
            // an offline stimulus; M2 will supply the actual AC pedal state.
            const double throttle = t < 1 || t >= 14 ? 0.12 : t < 8 ? 0.75 : 0.02;
            const int frames = engine.render({rpm, throttle, true}, blockSteps, pcm.data(), int(pcm.size()));
            require(frames == 2 * blockSteps, "PCM block cadence mismatch");
            wav.append(pcm.data(), frames);
            expectedAngle += rpm * ExternallyDrivenCrank::kPi * 2 / 60 / 150;
            const auto& d = engine.diagnostics();
            maxPhaseError = std::max(maxPhaseError, std::abs(d.totalAngle - expectedAngle));
            maxRpmError = std::max(maxRpmError, std::abs(d.effectiveRpm - rpm));
            for (int i = 0; i < frames; ++i) {
                const int amplitude = std::abs(int(pcm[i]));
                peak = std::max(peak, amplitude);
                squared += double(pcm[i]) * pcm[i];
                nonzero += amplitude > 0;
                clipped += amplitude >= 32767;
            }
            if ((b + 1) % 150 == 0) {
                csv << std::setprecision(12) << (b + 1) / 150.0 << ',' << rpm << ','
                    << d.effectiveRpm << ',' << d.angularVelocity << ',' << d.phase << ','
                    << d.totalAngle << ',' << d.audioFrames << ',' << d.burntFuelKg << ','
                    << d.maximumPressure << '\n';
                std::ostringstream log;
                log << "t=" << (b + 1) / 150.0 << " requested_rpm=" << rpm
                    << " effective_rpm=" << d.effectiveRpm << " omega=" << d.angularVelocity
                    << " phase=" << d.phase << " audio_frames=" << d.audioFrames
                    << " burnt_fuel_kg=" << d.burntFuelKg << " guards=" << d.instabilityGuards;
                SS_LOG_INFO("crank", log.str());
            }
        }
        const double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        const auto& d = engine.diagnostics();
        const double rms = std::sqrt(squared / wav.frames()) / 32768;
        require(maxRpmError < 1e-6, "External RPM tracking failed");
        require(maxPhaseError < 1e-6, "Crank phase drift");
        require(d.instabilityGuards == 0 && d.firingOrderErrors == 0, "Instability or firing-order error");
        require(d.burntFuelKg > before.burntFuelKg, "No combustion during sweep");
        for (auto c : d.combustions) require(c > 0, "A cylinder did not combust");
        require(wav.frames() == 15 * HeadlessEngine::kSampleRate, "WAV duration mismatch");
        require(nonzero > wav.frames() * 0.95 && rms > 0.0001 && clipped == 0, "PCM silent or clipped");
        wav.finish();
        csv.flush();
        require(bool(csv), "CSV write failed");
        std::ofstream report(output / "summary.json");
        require(bool(report), "Cannot open summary report");
        report << std::setprecision(12)
            << "{\n  \"status\": \"PASS\",\n  \"upstream_revision\": \"" << HeadlessEngine::upstreamRevision()
            << "\",\n  \"rpm_authority\": \"external\",\n  \"vehicle_solver\": false,\n"
            << "  \"simulation_hz\": " << HeadlessEngine::kSimulationRate
            << ",\n  \"fluid_substeps\": " << HeadlessEngine::kFluidSubsteps
            << ",\n  \"sample_rate\": " << HeadlessEngine::kSampleRate
            << ",\n  \"audio_frames\": " << wav.frames()
            << ",\n  \"warmup_seconds\": 2,\n  \"sweep_seconds\": 15,\n  \"wall_seconds_including_file_io\": " << wall
            << ",\n  \"max_rpm_error\": " << maxRpmError
            << ",\n  \"max_phase_error_rad\": " << maxPhaseError
            << ",\n  \"minimum_chamber_volume_m3\": " << d.minimumVolume
            << ",\n  \"maximum_pressure_pa\": " << d.maximumPressure
            << ",\n  \"maximum_temperature_k\": " << d.maximumTemperature
            << ",\n  \"burnt_fuel_kg\": " << d.burntFuelKg
            << ",\n  \"pcm_rms_normalized\": " << rms
            << ",\n  \"pcm_peak\": " << peak
            << ",\n  \"clipped_samples\": " << clipped
            << ",\n  \"instability_guards\": " << d.instabilityGuards
            << ",\n  \"firing_order_errors\": " << d.firingOrderErrors
            << ",\n  \"sparks_cylinders_1_2_3_4\": [" << d.sparks[0] << ',' << d.sparks[1] << ',' << d.sparks[2] << ',' << d.sparks[3]
            << "],\n  \"combustions_cylinders_1_2_3_4\": [" << d.combustions[0] << ',' << d.combustions[1] << ',' << d.combustions[2] << ',' << d.combustions[3]
            << "]\n}\n";
        report.flush();
        require(bool(report), "Summary write failed");
        SS_LOG_INFO("audio", "PASS: 800->7400->800 RPM, stable phase, combustion and unclipped PCM; artifacts=" + output.string());
        Logger::instance().shutdown();
        return 0;
    } catch (const std::exception& e) {
        SS_LOG_ERROR("engine-sim", e.what());
        Logger::instance().shutdown();
        return 1;
    }
}
