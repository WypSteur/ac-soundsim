#include "soundsim/headless_engine.hpp"
#include "soundsim/externally_driven_crank.hpp"

#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {
using namespace soundsim;
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }

struct Result { std::vector<std::int16_t> pcm; EngineDiagnostics diagnostics; };
Result hold(double throttle, bool ignition, EnginePreset preset=EnginePreset::legacyM1,
            const EngineProfileV1* customProfile=nullptr,
            IgnitionPolicy clock=IgnitionPolicy::externalContinuous) {
    std::srand(12345);
    HeadlessEngine engine(customProfile ? *customProfile : makeFa20Baseline(),preset,clock);
    Result r;
    std::array<std::int16_t, HeadlessEngine::kMaximumBlockFrames> block{};
    for (int b = 0; b < 450; ++b) {
        const int n = engine.render({3000, throttle, ignition}, 147, block.data(), int(block.size()));
        check(n == 294, "audio clock mismatch");
        if (b >= 300) r.pcm.insert(r.pcm.end(), block.begin(), block.begin() + n);
    }
    r.diagnostics = engine.diagnostics();
    return r;
}
}

int main() {
    try {
        const auto high = hold(0.8, true);
        const auto repeat = hold(0.8, true);
        const auto low = hold(0.05, true);
        const auto off = hold(0.8, false);
        check(high.pcm == repeat.pcm, "offline PCM not repeatable");
        check(high.pcm != low.pcm, "throttle did not affect PCM");
        check(high.pcm != off.pcm, "ignition did not affect PCM");
        check(high.diagnostics.burntFuelKg > low.diagnostics.burntFuelKg, "throttle did not affect combustion");
        check(high.diagnostics.burntFuelKg > 0 && off.diagnostics.burntFuelKg == 0, "combustion/ignition mismatch");
        check(high.diagnostics.firingOrderErrors == 0, "wrong firing order");
        for (auto sparks : high.diagnostics.sparks) check(sparks == 75, "expected 75 firings per cylinder at 3000 RPM / 3 s");

        const auto reference=hold(0.8,true,EnginePreset::fa20ReferenceFull);
        const auto referenceRepeat=hold(0.8,true,EnginePreset::fa20ReferenceFull);
        const auto referenceOff=hold(0.8,false,EnginePreset::fa20ReferenceFull);
        const auto referenceLow=hold(0.05,true,EnginePreset::fa20ReferenceFull);
        check(reference.pcm==referenceRepeat.pcm,"seeded public-core reference not repeatable");
        const auto upstreamHold=hold(0.8,true,EnginePreset::fa20ReferenceFull,nullptr,IgnitionPolicy::upstreamAudit);
        check(reference.pcm==upstreamHold.pcm,"steady reference PCM changed with corrected ignition");
        auto changedProfile = makeFa20Baseline(); changedProfile.referenceAudio.masterVolume = 0.125;
        const auto changedAudio = hold(0.8,true,EnginePreset::fa20ReferenceFull,&changedProfile);
        check(reference.pcm!=changedAudio.pcm,"profile source volume not applied by synthesizer");
        check(reference.diagnostics.burntFuelKg==changedAudio.diagnostics.burntFuelKg,
              "source DSP profile unexpectedly changed combustion");
        check(reference.pcm!=high.pcm && reference.pcm!=referenceOff.pcm,"reference DSP/combustion not active");
        check(reference.diagnostics.burntFuelKg>referenceLow.diagnostics.burntFuelKg,"reference throttle did not affect combustion");
        check(reference.diagnostics.burntFuelKg>0 && referenceOff.diagnostics.burntFuelKg==0,"reference ignition did not affect combustion");
        check(reference.diagnostics.instabilityGuards==0 && reference.diagnostics.firingOrderErrors==0,"reference unstable");
        for (auto s:reference.pcm) check(s!=32767 && s!=-32768,"reference hold PCM clipped");
        for (auto sparks:reference.diagnostics.sparks) check(sparks==75,"reference firing cadence wrong");

        HeadlessEngine engine(makeFa20Baseline());
        std::array<std::int16_t, HeadlessEngine::kMaximumBlockFrames> block{};
        double angle = 0;
        for (double rpm : {0.0, 800.0, 7400.0, 0.0, 3000.0}) {
            engine.render({rpm, 0.2, true}, 147, block.data(), int(block.size()));
            angle += rpm * ExternallyDrivenCrank::kPi * 2 / 60 / 150;
            check(std::abs(engine.diagnostics().totalAngle - angle) < 1e-8, "phase discontinuity on RPM step/stall");
            check(std::abs(engine.diagnostics().effectiveRpm - rpm) < 1e-8, "RPM authority lost");
        }
        const auto before = engine.diagnostics().simulationSteps;
        for (EngineInput bad : {EngineInput{-1, 0, true}, EngineInput{800, 2, true},
                              EngineInput{std::numeric_limits<double>::quiet_NaN(), 0, true}}) {
            bool rejected = false;
            try { engine.render(bad, 147, block.data(), int(block.size())); }
            catch (const std::invalid_argument&) { rejected = true; }
            check(rejected && engine.diagnostics().simulationSteps == before, "invalid input changed state");
        }
        bool profileRejected = false;
        auto invalidProfile = makeFa20Baseline(); invalidProfile.firingOrder[1] = 2;
        try { HeadlessEngine bad(invalidProfile); } catch (const std::invalid_argument&) { profileRejected = true; }
        check(profileRejected, "unsupported firing order accepted");
        std::cout << "headless: repeatability, gas/combustion, throttle, ignition, FA20 firing, RPM steps/stall PASS\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
