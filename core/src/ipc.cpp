#include "soundsim/ipc.hpp"
#include <algorithm>
#include <cmath>

namespace soundsim::ipc {
const char* modeName(Mode mode) noexcept {
    switch (mode) {
    case Mode::waiting: return "waiting";
    case Mode::running: return "running";
    case Mode::paused: return "paused";
    case Mode::stale: return "stale";
    case Mode::wrongCar: return "wrong-car";
    case Mode::invalid: return "invalid-state";
    case Mode::fault: return "engine-fault";
    case Mode::replay: return "replay-muted";
    }
    return "unknown";
}
RuntimeCarStateV1 decode(const State& s) noexcept {
    RuntimeCarStateV1 r;
    r.carIndex = s.carIndex; r.sequence = s.commit; r.timestampSeconds = s.timestampSeconds;
    r.rpm = s.rpm; r.throttle = s.throttle; r.clutch = s.clutch; r.boost = s.boost;
    r.gear = s.gear; r.position = s.position; r.velocity = s.velocity;
    r.active = (s.flags & active) != 0; r.playerCar = (s.flags & player) != 0;
    return r;
}
bool valid(const State& s) noexcept {
    auto vectorOK = [](Vec3f v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); };
    return s.magic == kMagic && s.version == kVersion && s.size == sizeof(State)
        && s.commit != 0 && !(s.commit & 1) && s.generation != 0
        && std::memchr(s.carID, 0, sizeof(s.carID)) != nullptr
        && std::isfinite(s.timestampSeconds) && s.timestampSeconds >= 0
        && std::isfinite(s.writerClock) && s.writerClock >= 0
        && std::isfinite(s.rpm) && s.rpm >= 0 && s.rpm <= 12000
        && std::isfinite(s.throttle) && s.throttle >= 0 && s.throttle <= 1
        && std::isfinite(s.clutch) && s.clutch >= 0 && s.clutch <= 1
        && std::isfinite(s.boost) && vectorOK(s.position) && vectorOK(s.velocity)
        && vectorOK(s.look) && vectorOK(s.up) && !(s.flags & ~15u);
}
bool StateTracker::accept(const State& s, double now) noexcept {
    if (received_ && s.generation == latest_.generation && s.commit == latest_.commit) return false;
    const bool reset = !received_ || s.generation != latest_.generation
        || s.resetCounter != latest_.resetCounter || s.carIndex != latest_.carIndex
        || std::memcmp(s.carID, latest_.carID, sizeof(s.carID)) != 0
        || s.timestampSeconds < latest_.timestampSeconds || s.writerClock < latest_.writerClock;
    if (reset) ++resets_;
    else {
        const auto delta = static_cast<std::uint32_t>(s.commit - latest_.commit);
        if (delta > 2 && delta < 0x80000000u) dropped_ += delta / 2 - 1;
    }
    latest_ = s; received_ = true; valid_ = valid(s); arrival_ = now;
    return true;
}
double StateTracker::age(double now) const noexcept {
    return received_ ? std::max(0.0, now - arrival_) : -1;
}
Mode StateTracker::mode(double now) const noexcept {
    if (!received_) return Mode::waiting;
    if (!valid_) return Mode::invalid;
    if (age(now) > 0.250) return Mode::stale;
    if (!(latest_.flags & active)) return Mode::waiting;
    if (!(latest_.flags & player) || latest_.carIndex != 0
        || std::strcmp(latest_.carID, "ks_toyota_gt86") != 0) return Mode::wrongCar;
    if (latest_.flags & replay) return Mode::replay;
    if (latest_.flags & paused) return Mode::paused;
    return Mode::running;
}
}
