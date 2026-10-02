#pragma once

#include <cstdint>

namespace soundsim {

// Positive cycle phase in [0, 4*pi), independent of torque or vehicle dynamics.
// advance() samples external RPM at the midpoint of a fixed simulation step.
class ExternallyDrivenCrank {
public:
    static constexpr double kPi = 3.14159265358979323846;
    static constexpr double kCycle = 4.0 * kPi;
    static constexpr double kMaximumRpm = 12000.0;

    void advance(double rpm, double dt);
    void reset(double phase = 0.0);
    double phase() const noexcept { return phase_; }
    double angularVelocity() const noexcept { return omega_; }
    double totalAngle() const noexcept { return double(cycles_) * kCycle + phase_; }
    std::uint64_t cycles() const noexcept { return cycles_; }

private:
    double phase_{};
    double omega_{};
    std::uint64_t cycles_{};
};

} // namespace soundsim
