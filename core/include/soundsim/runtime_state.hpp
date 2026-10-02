#pragma once

#include <cstdint>

namespace soundsim {

struct Vec3f {
    float x{};
    float y{};
    float z{};
};

// Versioned boundary between Assetto/CSP and SoundSim.
// Keep this POD-like: it will later be serialized into shared memory.
struct RuntimeCarStateV1 {
    static constexpr std::uint32_t kSchemaVersion = 1;

    std::uint32_t schemaVersion{kSchemaVersion};
    std::uint32_t carIndex{0};
    std::uint64_t sequence{0};
    double timestampSeconds{0.0};

    float rpm{0.0F};
    float throttle{0.0F};
    float clutch{0.0F};
    float boost{0.0F};
    std::int32_t gear{0};

    Vec3f position{};
    Vec3f velocity{};

    bool active{false};
    bool playerCar{false};
};

} // namespace soundsim
