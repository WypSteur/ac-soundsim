#include "soundsim/externally_driven_ignition.hpp"
#include "soundsim/externally_driven_crank.hpp"
#include <cmath>
#include <stdexcept>

namespace soundsim {
IgnitionDecision ExternallyDrivenIgnition::step(double r0, double r1, double advance, bool enabled) {
    constexpr double pi=ExternallyDrivenCrank::kPi, cycle=ExternallyDrivenCrank::kCycle;
    if (!std::isfinite(r0) || !std::isfinite(r1) || !std::isfinite(advance) ||
        r0 < 0 || r1 < r0 || r1 > 1e12 || r1-r0 >= pi || std::abs(advance) > pi/2)
        throw std::invalid_argument("external ignition: invalid phase/advance/step");
    // On startup/re-enable there is no active previous timing trajectory to
    // interpolate. Do not synthesize a spark solely from switching ignition on.
    const double previous = initialized_ && enabledPrevious_ && enabled ? advancePrevious_ : advance;
    const double p0=r0+previous, p1=r1+advance;
    IgnitionDecision result;
    if (enabled && p1 > p0) {
        for (int i=0; i<4; ++i) {
            const double base=i*pi;
            const auto turn=static_cast<std::int64_t>(std::ceil((p0-base)/cycle));
            const double target=base+turn*cycle;
            // Same half-open interval as public Engine-Sim at constant advance.
            if (target >= p0 && target < p1) {
                if (turn <= lastCycle_[i]) result.duplicateSuppressed[i]=true;
                else { result.fired[i]=true; lastCycle_[i]=turn; }
            }
        }
    }
    advancePrevious_=advance; enabledPrevious_=enabled; initialized_=true;
    return result;
}
}
