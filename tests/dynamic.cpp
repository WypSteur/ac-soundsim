#include "soundsim/headless_engine.hpp"
#include "soundsim/externally_driven_crank.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

using namespace soundsim;
namespace {
EngineInput input(double t) {
    if (t < 1) return {800, 0, true};
    if (t < 2) return {800 + (t-1)*6600, 1, true};
    if (t < 2.10) return {7400, 0, true}; // synthetic gear-change pedal cut
    if (t < 3) return {4200 + (t-2.10)*3200/0.90, 1, true};
    if (t < 4) return {7400 - (t-3)*6600, 0.02, true};
    if (t < 4.5) return {0, 0, false};
    return {800, 0, true};
}
}
int main() {
    try {
        for (int stateHz : {60, 90, 144}) {
            std::srand(12345);
            HeadlessEngine engine(makeFa20Baseline(), EnginePreset::fa20ReferenceFull);
            std::array<std::int16_t, HeadlessEngine::kMaximumBlockFrames> pcm{};
            double expectedAngle = 0, peak = 0;
            EngineInput held = input(0);
            int lastState = -1;
            for (int b = 0; b < 900; ++b) {
                const double t = b / 150.0;
                const int latestState = static_cast<int>(std::floor(t*stateHz + 1e-9));
                if (latestState != lastState) { held = input(latestState/double(stateHz)); lastState = latestState; }
                const int n = engine.render(held, 147, pcm.data(), int(pcm.size()));
                if (n != 294) throw std::runtime_error("dynamic PCM cadence changed");
                expectedAngle += held.rpm * (2*ExternallyDrivenCrank::kPi/60) / 150;
                const auto& d = engine.diagnostics();
                if (std::abs(d.totalAngle-expectedAngle) > 1e-7 || std::abs(d.effectiveRpm-held.rpm) > 1e-8)
                    throw std::runtime_error("ZOH RPM authority or continuous phase failed");
                if (d.instabilityGuards) {
                    std::cerr << "stateHz=" << stateHz << " t=" << t << " rpm=" << held.rpm
                        << " firingErrors=" << d.firingOrderErrors << " guards=" << d.instabilityGuards << '\n';
                    throw std::runtime_error("dynamic simulation guard");
                }
                for (int i = 0; i < n; ++i) {
                    if (pcm[i] == 32767 || pcm[i] == -32768) throw std::runtime_error("dynamic source PCM saturation");
                    peak = std::max(peak, std::abs(int(pcm[i]))/32768.0);
                }
            }
            if (peak < .001 || engine.diagnostics().audioFrames != 6*44100)
                throw std::runtime_error("dynamic output silent or incomplete");
            std::cout << "ZOH " << stateHz << " Hz: phase/cadence/guards/source rails PASS; peak=" << peak
                << "; OBSERVED firing_sequence_anomalies=" << engine.diagnostics().firingOrderErrors << '\n';
        }
        // CHARACTERIZATION: the public timing-advance threshold can move across
        // the previous angle on a ZOH RPM change, missing/repeating an ignition.
        // Report the real counter; DO NOT label firing order or M5 as validated.
        // Synthetic producer cadence is not AC driving, consumer latency or Doppler.
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
