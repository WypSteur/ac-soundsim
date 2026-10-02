#pragma once
#include "runtime_state.hpp"
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace soundsim::ipc {
inline constexpr char kStateName[] = "AcTools.ACSoundSim.State.v1";
inline constexpr char kStatusName[] = "AcTools.ACSoundSim.Status.v1";
inline constexpr std::uint32_t kMagic = 0x53534143;
inline constexpr std::uint32_t kVersion = 1;
enum Flags : std::uint32_t { active = 1, player = 2, paused = 4, replay = 8 };
enum class Mode : std::uint32_t { waiting, running, paused, stale, wrongCar, invalid, fault, replay };
const char* modeName(Mode mode) noexcept;

// Windows x64 little-endian ABI, default packing 8. Never serialize bool/string.
struct alignas(8) State {
    std::uint32_t commit{}, magic{kMagic}, version{kVersion}, size{192};
    std::uint32_t generation{}, flags{}, resetCounter{}, carIndex{};
    double timestampSeconds{}, writerClock{}; // AC seconds, script-local heartbeat clock
    float rpm{}, throttle{}, clutch{}, boost{};
    std::int32_t gear{};
    std::uint32_t reserved{};
    Vec3f position{}, velocity{}, look{}, up{};
    char carID[64]{};
    std::uint32_t padding[2]{};
};
struct alignas(8) Status {
    std::uint32_t commit{}, magic{kMagic}, version{kVersion}, size{368};
    std::uint32_t generation{}, mode{}, stateCommit{}, flags{}; // source: 0 legacy M1, 1 tone, 2 FA20D port
    double stateAgeSeconds{}, requestedRpm{}, effectiveRpm{}, phase{};
    std::uint64_t frames{}, lateBlocks{}, tornStates{}, droppedStates{}, resets{};
    std::uint32_t audioSize{}, sampleRate{};
    char audioName[96]{};
    std::uint64_t heartbeat{};
    double renderMs{}, maxRenderMs{};
    std::uint32_t faults{}, reserved{};
    char error[128]{};
};
static_assert(sizeof(State) == 192 && offsetof(State, timestampSeconds) == 32
    && offsetof(State, carID) == 120);
static_assert(sizeof(Status) == 368 && offsetof(Status, audioName) == 112
    && offsetof(Status, heartbeat) == 208 && offsetof(Status, error) == 240);

RuntimeCarStateV1 decode(const State& state) noexcept;
bool valid(const State& state) noexcept;
class StateTracker {
public:
    bool accept(const State& state, double now) noexcept;
    Mode mode(double now) const noexcept;
    double age(double now) const noexcept;
    const State& latest() const noexcept { return latest_; }
    std::uint64_t dropped() const noexcept { return dropped_; }
    std::uint64_t resets() const noexcept { return resets_; }
    bool received() const noexcept { return received_; }
private:
    State latest_{};
    double arrival_{};
    std::uint64_t dropped_{}, resets_{};
    bool received_{}, valid_{};
};
}
