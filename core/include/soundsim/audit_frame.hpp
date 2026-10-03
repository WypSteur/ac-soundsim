#pragma once
#include <cstddef>
#include <cstdint>

namespace soundsim::ipc {
inline constexpr char kAuditName[]="AcTools.ACSoundSim.Status.v1.Audit.v1";
inline constexpr std::uint32_t kAuditMagic=0x41535343;
// Separate opt-in channel. Existing state/status/audio ABIs stay unchanged.
struct alignas(8) AuditFrame {
    std::uint32_t commit{},magic{kAuditMagic},version{1},size{136};
    std::uint32_t generation{},mode{},stateCommit{},sourceFlags{};
    std::uint64_t qpcFrequency{},qpcInput{},qpcRenderEnd{},qpcPublish{};
    std::uint64_t sourceFrames{},heartbeat{},ignitionAnomalies{},lateBlocks{};
    double stateTimestampSeconds{},stateAgeSeconds{};
    float rpm{},throttle{};
    std::int32_t gear{};
    std::uint32_t stateFlags{},faults{},reserved{};
};
static_assert(sizeof(AuditFrame)==136 && offsetof(AuditFrame,qpcFrequency)==32 && offsetof(AuditFrame,rpm)==112);
}
