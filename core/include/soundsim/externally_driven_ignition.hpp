#pragma once
#include <array>
#include <cstdint>
#include <limits>

namespace soundsim {
struct IgnitionDecision {
    std::array<bool,4> fired{};
    std::array<bool,4> duplicateSuppressed{};
};
// FA20 forward-only 720-degree spark clock. AC crank motion is untouched.
// Crossing of phase+advance tracks both trajectories over the fixed step.
// A cylinder/720-cycle identity prevents re-firing after a retard/recross.
class ExternallyDrivenIgnition {
public:
    IgnitionDecision step(double crankPrevious, double crankCurrent,
                          double advanceCurrent, bool enabled);
private:
    double advancePrevious_{};
    bool initialized_{}, enabledPrevious_{};
    std::array<std::int64_t,4> lastCycle_{
        std::numeric_limits<std::int64_t>::min(), std::numeric_limits<std::int64_t>::min(),
        std::numeric_limits<std::int64_t>::min(), std::numeric_limits<std::int64_t>::min()};
};
}
