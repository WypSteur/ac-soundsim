#pragma once

#include "engine_profile.hpp"

#include <array>
#include <cstdint>
#include <memory>

namespace soundsim {

// Legacy is retained for M1 regression. The reference presets explicitly port
// Subaru_FA20D.mr onto the public core; they are not a dynamic .mr interpreter.
enum class EnginePreset { legacyM1, fa20ReferenceDry, fa20ReferenceFull };
enum class IgnitionPolicy { externalContinuous, upstreamAudit };

struct EngineInput {
    double rpm{800.0};
    double throttle{0.0};
    bool ignitionEnabled{true};
};

// Optional bounded caller-owned offline trace. Four rows per simulation step;
// includes non-firing steps. No allocation/file IO on the render path.
struct IgnitionTraceRow {
    double timeSeconds{}, previousRpm{}, rpm{};
    double crankPrevious{}, crankCurrent{}, advancePrevious{}, advanceCurrent{};
    double thresholdPrevious{}, thresholdCurrent{};
    std::int64_t sparkCycle{};
    int cylinder{};
    bool upstreamFired{}, selectedFired{}, upstreamDuplicate{}, sequenceAnomaly{};
};

struct EngineDiagnostics {
    double requestedRpm{};
    double effectiveRpm{};
    double angularVelocity{};
    double phase{};
    double totalAngle{};
    double minimumVolume{1.0};
    double maximumPressure{};
    double maximumTemperature{};
    double burntFuelKg{};
    std::uint64_t simulationSteps{};
    std::uint64_t audioFrames{};
    std::array<std::uint64_t, 4> sparks{}; // physical cylinder labels 1,2,3,4
    std::array<std::uint64_t, 4> combustions{};
    std::uint64_t firingOrderErrors{};
    std::uint64_t instabilityGuards{};
    std::uint64_t traceRows{}, traceDropped{};
};

class HeadlessEngine final {
public:
    static constexpr int kSimulationRate = 22050;
    static constexpr int kSampleRate = 44100;
    static constexpr int kFluidSubsteps = 8;
    static constexpr int kMaximumBlockSteps = 220;
    static constexpr int kMaximumBlockFrames = 2 * kMaximumBlockSteps + 1;

    explicit HeadlessEngine(const EngineProfileV1& profile,
                            EnginePreset preset = EnginePreset::legacyM1,
                            IgnitionPolicy ignition = IgnitionPolicy::externalContinuous);
    ~HeadlessEngine();
    HeadlessEngine(const HeadlessEngine&) = delete;
    HeadlessEngine& operator=(const HeadlessEngine&) = delete;

    // Fixed simulation clock. No allocation or I/O in this call. The caller
    // owns output storage (2*steps+1 scratch samples). Returns exactly 2*steps
    // frames; the upstream initial t=0 sample is discarded on the first call.
    int render(const EngineInput& input, int steps, std::int16_t* pcm, int capacity);
    const EngineDiagnostics& diagnostics() const noexcept;
    void traceIgnition(IgnitionTraceRow* rows, std::size_t capacity,
                       double minimumRpm=1800, double maximumRpm=2200);
    static const char* upstreamRevision() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace soundsim
