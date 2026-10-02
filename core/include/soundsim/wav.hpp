#pragma once
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace soundsim {
// RIFF PCM only: bounded parsing, unknown chunks and odd-byte padding accepted.
// Used on initialization/offline, never on the realtime render path.
inline std::vector<std::int16_t> readMonoPcm16(const std::filesystem::path& path,
                                              std::uint32_t expectedRate = 44100) {
    std::ifstream in(path, std::ios::binary);
    auto fail = [&]() -> void { throw std::runtime_error("Invalid mono PCM16 WAV: " + path.string()); };
    auto u16 = [&]() { unsigned char b[2]; in.read(reinterpret_cast<char*>(b), 2);
        if (!in) fail(); return std::uint16_t(b[0] | (b[1] << 8)); };
    auto u32 = [&]() { const auto lo = u16(); const auto hi = u16();
        return std::uint32_t(lo) | (std::uint32_t(hi) << 16); };
    char id[4]; in.read(id, 4); if (!in || std::memcmp(id, "RIFF", 4)) fail();
    const auto riffSize = u32();
    in.read(id, 4); if (!in || std::memcmp(id, "WAVE", 4) || riffSize < 4 || riffSize > 64*1024*1024) fail();
    bool format = false, dataSeen = false;
    std::vector<std::int16_t> pcm;
    std::uint32_t remaining = riffSize - 4;
    while (remaining >= 8) {
        in.read(id, 4); const auto size = u32(); remaining -= 8;
        const auto padded = std::uint64_t(size) + (size & 1u);
        if (padded > remaining) fail();
        if (!std::memcmp(id, "fmt ", 4)) {
            if (size < 16 || format) fail();
            const auto encoding = u16(), channels = u16(); const auto rate = u32(), bytes = u32();
            const auto align = u16(), bits = u16();
            if (encoding != 1 || channels != 1 || rate != expectedRate || bytes != rate*2 || align != 2 || bits != 16) fail();
            format = true; in.seekg(size - 16, std::ios::cur);
        } else if (!std::memcmp(id, "data", 4)) {
            if (!format || dataSeen || size == 0 || size % 2) fail();
            dataSeen = true; pcm.resize(size / 2);
            for (auto& s : pcm) s = static_cast<std::int16_t>(u16());
        } else in.seekg(size, std::ios::cur);
        if (size & 1u) in.seekg(1, std::ios::cur);
        if (!in) fail(); remaining -= static_cast<std::uint32_t>(padded);
    }
    if (!format || !dataSeen || remaining != 0) fail();
    return pcm;
}
inline void writeMonoPcm16(const std::filesystem::path& path,
                           const std::vector<std::int16_t>& pcm) {
    if (pcm.empty() || pcm.size() > (UINT32_MAX - 36u)/2u) throw std::runtime_error("WAV size out of bounds");
    std::ofstream out(path, std::ios::binary);
    auto u16 = [&](std::uint16_t v) { const char b[2]{char(v & 255), char(v >> 8)}; out.write(b, 2); };
    auto u32 = [&](std::uint32_t v) { u16(std::uint16_t(v)); u16(std::uint16_t(v >> 16)); };
    const auto size = static_cast<std::uint32_t>(pcm.size()*2);
    out.write("RIFF", 4); u32(size+36); out.write("WAVEfmt ", 8); u32(16);
    u16(1); u16(1); u32(44100); u32(88200); u16(2); u16(16);
    out.write("data", 4); u32(size); for (auto s : pcm) u16(static_cast<std::uint16_t>(s));
    out.flush(); if (!out) throw std::runtime_error("Cannot write WAV: " + path.string());
}
} // namespace soundsim
