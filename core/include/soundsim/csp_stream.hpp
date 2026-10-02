#pragma once
#include "win_mmf.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace soundsim::ipc {
// Derived from CSP's official Mumble AudioSource.InitializeStream/PushStream,
// acc-lua-internal commit 2031bd2d7c913e0aad3ff18a3bab86ffccad1326.
// Unknown reserved fields are zero and are never interpreted as consumer cursors.
struct alignas(8) CspAudioHeader {
    std::int32_t sampleRate{}, channels{}, fmodFormat{}, decodeSamples{}, decodeBytes{}, reserved20{};
    std::int64_t publishedBytes{};
    std::int32_t reserved32[8]{};
};
static_assert(sizeof(CspAudioHeader) == 64 && offsetof(CspAudioHeader, publishedBytes) == 24);
class CspStream {
public:
    static constexpr int kRate = 44100;
    static constexpr int kCapacity = (kRate / 25) * 64;
    static constexpr int kSize = 64 + kCapacity * sizeof(float);
    explicit CspStream(const std::string& name) {
        mapping_.create(name, kSize);
        auto* h = header();
        h->sampleRate = kRate; h->channels = 1; h->fmodFormat = 5; // PCMFLOAT
        h->decodeSamples = kRate / 25;
        h->decodeBytes = h->decodeSamples * sizeof(float);
    }
    void push(const float* pcm, int frames) {
        if (frames < 0 || frames > kCapacity) throw std::invalid_argument("invalid PCM block");
        auto* ring = reinterpret_cast<float*>(static_cast<char*>(mapping_.data()) + 64);
        const int first = std::min(frames, kCapacity - cursor_);
        std::memcpy(ring + cursor_, pcm, first * sizeof(float));
        if (frames > first) std::memcpy(ring, pcm + first, (frames - first) * sizeof(float));
        cursor_ = (cursor_ + frames) % kCapacity;
        bytes_ += static_cast<std::int64_t>(frames) * sizeof(float);
        // Producer copy completes before publishing the cumulative byte counter.
        InterlockedExchange64(reinterpret_cast<volatile LONG64*>(&header()->publishedBytes), bytes_);
    }
    void stop() noexcept {
        InterlockedExchange64(reinterpret_cast<volatile LONG64*>(&header()->publishedBytes), 0);
    }
    ~CspStream() { stop(); }
    std::uint64_t frames() const noexcept { return bytes_ / sizeof(float); }
private:
    CspAudioHeader* header() { return static_cast<CspAudioHeader*>(mapping_.data()); }
    Mapping mapping_;
    int cursor_{};
    std::int64_t bytes_{};
};
}
