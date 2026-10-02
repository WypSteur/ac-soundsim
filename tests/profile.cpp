#include "soundsim/engine_profile.hpp"
#include <yaml-cpp/yaml.h>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace soundsim;
void require(bool condition) { if (!condition) throw std::runtime_error("profile assertion failed"); }
int main() {
    const auto temporary = std::filesystem::path("profile-test-input.yaml");
    try {
        const auto source = YAML::LoadFile(ACSOUNDSIM_TEST_PROFILE_PATH);
        const auto baseline = makeFa20Baseline();
        require(baseline.referenceAudio.masterVolume == 0.25);
        require(baseline.referenceAudio.highFrequencyMix == 0.05);
        require(baseline.referenceAudio.impulseResponseGain == 0.001);
        auto check = [&](const std::string& text, bool valid) {
            { std::ofstream out(temporary); out << text; require(bool(out)); }
            bool rejected = false;
            try { (void)loadEngineProfile(temporary); }
            catch (const std::invalid_argument&) { rejected = true; }
            require(rejected != valid);
        };
        auto changed = YAML::Clone(source);
        changed["reference_audio"]["master_volume"] = 0.125;
        check(YAML::Dump(changed), true);
        require(loadEngineProfile(temporary).referenceAudio.masterVolume == 0.125);
        for (const auto* field : {"master_volume", "leveler_target", "high_frequency_mix", "air_noise",
                                 "input_sample_noise", "convolution", "impulse_response_gain"}) {
            auto invalid = YAML::Clone(source);
            invalid["reference_audio"][field] = ".nan";
            check(YAML::Dump(invalid), false);
            invalid["reference_audio"][field] = -1;
            check(YAML::Dump(invalid), false);
            invalid["reference_audio"][field] = 100000;
            check(YAML::Dump(invalid), false);
            invalid["reference_audio"].remove(field);
            check(YAML::Dump(invalid), false);
        }
        for (const auto* section : {"identity", "geometry", "crankshaft", "aspiration", "defaults", "reference_audio", "provenance"}) {
            auto invalid = YAML::Clone(source);
            invalid[section]["typo"] = 1;
            check(YAML::Dump(invalid), false);
        }
        changed = YAML::Clone(source); changed["geometry"]["cylinders"] = 6;
        check(YAML::Dump(changed), false);
        changed = YAML::Clone(source); changed["version"] = 2;
        check(YAML::Dump(changed), false);
        changed = YAML::Clone(source); changed["reference_audio"]["impulse_response"] = "other";
        check(YAML::Dump(changed), false);
        check(YAML::Dump(source) + "\nversion: 1\n", false);
        check(YAML::Dump(source) + "\n---\nextra: 1\n", false);
        check("[broken:", false); check("", false); check(std::string(65537, ' '), false);
        std::filesystem::remove(temporary);
        bool rejected = false;
        try { (void)loadEngineProfile(temporary); } catch (const std::invalid_argument&) { rejected = true; }
        require(rejected);
        auto invalid = baseline; invalid.referenceAudio.masterVolume = std::numeric_limits<double>::infinity();
        rejected = false;
        try { validateEngineProfile(invalid); } catch (const std::invalid_argument&) { rejected = true; }
        require(rejected);
        std::cout << "Profile YAML loading, parameter changes and strict rejection PASS\n";
        return 0;
    } catch (const std::exception& e) {
        std::filesystem::remove(temporary);
        std::cerr << e.what() << '\n'; return 1;
    }
}
