#include "soundsim/externally_driven_crank.hpp"

#include <cmath>
#include <stdexcept>

namespace soundsim {

void ExternallyDrivenCrank::advance(double rpm, double dt) {
    if (!std::isfinite(rpm) || rpm < 0 || rpm > kMaximumRpm ||
        !std::isfinite(dt) || dt <= 0 || dt > 0.001) {
        throw std::invalid_argument("External crank: invalid RPM or fixed timestep");
    }
    const double omega = rpm * (2.0 * kPi / 60.0);
    const double next = phase_ + omega * dt;
    if (next >= kCycle) {
        ++cycles_;
        phase_ = next - kCycle;
    } else {
        phase_ = next;
    }
    omega_ = omega;
}

void ExternallyDrivenCrank::reset(double phase) {
    if (!std::isfinite(phase)) throw std::invalid_argument("External crank: invalid phase");
    phase_ = std::fmod(phase, kCycle);
    if (phase_ < 0) phase_ += kCycle;
    omega_ = 0;
    cycles_ = 0;
}

} // namespace soundsim
