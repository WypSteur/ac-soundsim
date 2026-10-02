#pragma once
#if !defined(_WIN32) || !defined(_WIN64)
#error This IPC ABI requires Windows x64
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cstring>
#include <stdexcept>
#include <string>
#include <type_traits>

namespace soundsim::ipc {
class Mapping final {
public:
    Mapping() = default;
    ~Mapping() { close(); }
    Mapping(const Mapping&) = delete;
    Mapping& operator=(const Mapping&) = delete;
    bool open(const std::string& name, std::size_t size, bool write) {
        close();
        handle_ = OpenFileMappingA(write ? FILE_MAP_ALL_ACCESS : FILE_MAP_READ, FALSE, ("Local\\" + name).c_str());
        if (!handle_) return false;
        data_ = MapViewOfFile(handle_, write ? FILE_MAP_ALL_ACCESS : FILE_MAP_READ, 0, 0, size);
        if (!data_) { close(); return false; }
        return true;
    }
    void create(const std::string& name, std::size_t size, bool reuse = false) {
        close();
        handle_ = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
            static_cast<DWORD>(size), ("Local\\" + name).c_str());
        if (!handle_) throw std::runtime_error("CreateFileMapping failed: " + std::to_string(GetLastError()));
        // A producer never takes over an existing mapping or another producer.
        if (GetLastError() == ERROR_ALREADY_EXISTS && !reuse) { close(); throw std::runtime_error("MMF already owned: " + name); }
        data_ = MapViewOfFile(handle_, FILE_MAP_ALL_ACCESS, 0, 0, size);
        if (!data_) { auto error = GetLastError(); close(); throw std::runtime_error("MapViewOfFile failed: " + std::to_string(error)); }
    }
    void close() noexcept {
        if (data_) UnmapViewOfFile(data_);
        if (handle_) CloseHandle(handle_);
        data_ = nullptr; handle_ = nullptr;
    }
    void* data() const noexcept { return data_; }
private:
    HANDLE handle_{};
    void* data_{};
};
// Seqlock: aligned volatile loads/stores plus explicit x64 fences. Read-only views
// must NOT use InterlockedCompareExchange (it writes even when used as a load).
// Cross-process shared memory is a Windows ABI, not a portable std::atomic object.
template<class T> bool snapshot(const void* memory, T& result) noexcept {
    static_assert(std::is_trivially_copyable_v<T>);
    const auto* sequence = static_cast<const volatile LONG*>(memory);
    const auto before = static_cast<std::uint32_t>(*sequence);
    if (!before || (before & 1)) return false;
    MemoryBarrier();
    std::memcpy(&result, memory, sizeof(T));
    MemoryBarrier();
    return before == static_cast<std::uint32_t>(*sequence) && result.commit == before;
}
template<class T> void publish(void* memory, const T& value) noexcept {
    auto* sequence = static_cast<volatile LONG*>(memory);
    const auto next = (static_cast<std::uint32_t>(*sequence) + 2u) & ~1u;
    const auto even = next ? next : 2u;
    InterlockedExchange(sequence, static_cast<LONG>(even - 1));
    std::memcpy(static_cast<char*>(memory) + 4, reinterpret_cast<const char*>(&value) + 4, sizeof(T) - 4);
    InterlockedExchange(sequence, static_cast<LONG>(even));
}
}
