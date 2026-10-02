#include "soundsim/engine_profile.hpp"
#include "soundsim/logger.hpp"
#include "soundsim/runtime_state.hpp"

#include <filesystem>
#include <sstream>

int main() {
    namespace fs = std::filesystem;
    using namespace soundsim;

    Logger::instance().initialize(fs::current_path() / "logs", "soundsim-poc");
    SS_LOG_INFO("bootstrap", std::string("AC SoundSim v") + ACSOUNDSIM_VERSION + " starting");

    const auto profile = makeFa20Baseline();
    {
        std::ostringstream ss;
        ss << "profile=" << profile.id
           << " layout=" << profile.layout
           << " cylinders=" << profile.cylinders
           << " displacement_cc=" << profile.displacementCc
           << " bore_mm=" << profile.boreMm
           << " stroke_mm=" << profile.strokeMm;
        SS_LOG_INFO("profile", ss.str());
    }

    RuntimeCarStateV1 state;
    state.active = true;
    state.playerCar = true;
    state.rpm = profile.idleRpm;
    state.throttle = 0.0F;

    {
        std::ostringstream ss;
        ss << "runtime schema=v" << state.schemaVersion
           << " initial_rpm=" << state.rpm
           << " throttle=" << state.throttle;
        SS_LOG_INFO("runtime", ss.str());
    }

    SS_LOG_INFO("engine-sim", "this executable is the M0 logger demo; use soundsim-m1 for the headless acoustic sweep");
    SS_LOG_INFO("bootstrap", "baseline scaffold OK");

    Logger::instance().shutdown();
    return 0;
}
