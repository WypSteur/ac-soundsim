#include "soundsim/engine_profile.hpp"
#include "soundsim/runtime_state.hpp"

#include <cstdlib>

int main() {
    const auto p = soundsim::makeFa20Baseline();
    if (p.cylinders != 4) return EXIT_FAILURE;
    if (p.boreMm != 86.0F || p.strokeMm != 86.0F) return EXIT_FAILURE;
    if (p.firingOrderCount != 4) return EXIT_FAILURE;
    if (soundsim::RuntimeCarStateV1::kSchemaVersion != 1) return EXIT_FAILURE;
    return EXIT_SUCCESS;
}
