#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace soundsim {

struct EngineProfileV1 {
    static constexpr std::uint32_t kSchemaVersion = 1;

    std::uint32_t schemaVersion{kSchemaVersion};
    std::string id;
    std::string manufacturer;
    std::string engineCode;
    std::string layout;

    std::uint32_t cylinders{0};
    float displacementCc{0.0F};
    float boreMm{0.0F};
    float strokeMm{0.0F};

    std::array<std::uint32_t, 12> firingOrder{};
    std::uint32_t firingOrderCount{0};

    float idleRpm{0.0F};
    float redlineRpm{0.0F};
};

inline EngineProfileV1 makeFa20Baseline() {
    EngineProfileV1 p;
    p.id = "subaru_fa20_gt86_baseline";
    p.manufacturer = "Subaru/Toyota";
    p.engineCode = "FA20 / 4U-GSE";
    p.layout = "boxer-4";
    p.cylinders = 4;
    p.displacementCc = 1998.0F;
    p.boreMm = 86.0F;
    p.strokeMm = 86.0F;
    p.firingOrder = {1, 3, 2, 4};
    p.firingOrderCount = 4;
    p.idleRpm = 700.0F;
    p.redlineRpm = 7400.0F; // fallback only; AC data should override this.
    return p;
}

} // namespace soundsim
