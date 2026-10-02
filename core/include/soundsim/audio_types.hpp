#pragma once

#include <cstdint>
#include <vector>

namespace soundsim {

struct AudioFormat {
    std::uint32_t sampleRate{48000};
    std::uint16_t channels{1};
};

struct AudioBlock {
    AudioFormat format{};
    std::vector<float> samples;
};

class IAudioSink {
public:
    virtual ~IAudioSink() = default;
    virtual AudioFormat format() const = 0;
    virtual void submit(const AudioBlock& block) = 0;
};

} // namespace soundsim
