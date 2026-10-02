#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>

namespace soundsim {

// Source DSP only. Cabin transfer, output gain and spatial propagation belong
// to the listener/CSP, not to this engine profile.
struct ReferenceAudioV1 {
    double masterVolume{0.25};
    double levelerTarget{30000};
    double highFrequencyMix{0.05};
    double airNoise{1};
    double inputSampleNoise{0.5};
    double convolution{1};
    double impulseResponseGain{0.001};
};

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
    ReferenceAudioV1 referenceAudio;
};

// Startup-only file IO; invalid/missing profiles fail explicitly, never silently
// revert to duplicated C++ parameters. The FA20-only adapter is still deliberate.
EngineProfileV1 loadEngineProfile(const std::filesystem::path& path);
void validateEngineProfile(const EngineProfileV1& profile);
std::filesystem::path defaultEngineProfilePath();
EngineProfileV1 makeFa20Baseline();

} // namespace soundsim
