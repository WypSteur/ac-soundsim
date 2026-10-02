#include "soundsim/engine_profile.hpp"
#include <yaml-cpp/yaml.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <initializer_list>
#include <stdexcept>
#include <vector>

namespace soundsim {
namespace {
void keys(const YAML::Node& node, std::initializer_list<const char*> allowed) {
    if (!node.IsMap()) throw std::invalid_argument("profile section must be a mapping");
    std::vector<std::string> seen;
    for (const auto& entry : node) {
        const auto key = entry.first.as<std::string>();
        if (std::find(seen.begin(), seen.end(), key) != seen.end())
            throw std::invalid_argument("duplicate profile key: " + key);
        seen.push_back(key);
        bool known = false;
        for (const auto* name : allowed) if (key == name) known = true;
        if (!known) throw std::invalid_argument("unknown profile key: " + key);
    }
    for (const auto* name : allowed)
        if (!node[name]) throw std::invalid_argument(std::string("missing profile key: ") + name);
}
void range(double value, double low, double high, const char* name) {
    if (!std::isfinite(value) || value < low || value > high)
        throw std::invalid_argument(std::string("invalid profile value: ") + name);
}
}
void validateEngineProfile(const EngineProfileV1& p) {
    if (p.schemaVersion != 1 || p.id != "subaru_fa20_gt86_baseline" ||
        p.manufacturer != "Subaru/Toyota" || p.engineCode != "FA20 / 4U-GSE" ||
        p.layout != "boxer-4" || p.cylinders != 4 || p.displacementCc != 1998 ||
        p.boreMm != 86 || p.strokeMm != 86 || p.firingOrderCount != 4 ||
        p.firingOrder[0] != 1 || p.firingOrder[1] != 3 ||
        p.firingOrder[2] != 2 || p.firingOrder[3] != 4)
        throw std::invalid_argument("adapter supports the FA20 baseline profile only");
    range(p.idleRpm, 100, 2000, "idle_rpm");
    range(p.redlineRpm, p.idleRpm, 12000, "redline_rpm");
    const auto& a = p.referenceAudio;
    range(a.masterVolume, 0.001, 1, "master_volume");
    range(a.levelerTarget, 1, 32767, "leveler_target");
    range(a.highFrequencyMix, 0, 1, "high_frequency_mix");
    range(a.airNoise, 0, 1, "air_noise");
    range(a.inputSampleNoise, 0, 1, "input_sample_noise");
    range(a.convolution, 0, 1, "convolution");
    range(a.impulseResponseGain, 0.000001, 1, "impulse_response_gain");
}
EngineProfileV1 loadEngineProfile(const std::filesystem::path& path) {
    // Bound input before parsing; no YAML parser/file access in render().
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::invalid_argument("cannot open engine profile: " + path.string());
    std::string text(65537, '\0');
    file.read(text.data(), static_cast<std::streamsize>(text.size()));
    text.resize(static_cast<std::size_t>(file.gcount()));
    if (file.bad() || text.empty() || text.size() > 65536)
        throw std::invalid_argument("empty, unreadable or oversized engine profile");
    try {
        const auto documents = YAML::LoadAll(text);
        if (documents.size() != 1) throw std::invalid_argument("engine profile must contain one YAML document");
        const auto root = documents.front();
        keys(root, {"format", "version", "identity", "geometry", "crankshaft", "aspiration", "defaults", "reference_audio", "provenance"});
        if (root["format"].as<std::string>() != "soundsim-engine")
            throw std::invalid_argument("unexpected engine profile format");
        EngineProfileV1 p;
        p.schemaVersion = root["version"].as<std::uint32_t>();
        const auto id = root["identity"];
        keys(id, {"id", "manufacturer", "family", "code"});
        if (id["family"].as<std::string>() != "FA") throw std::invalid_argument("unsupported engine family");
        p.id = id["id"].as<std::string>();
        p.manufacturer = id["manufacturer"].as<std::string>();
        p.engineCode = id["code"].as<std::string>();
        const auto g = root["geometry"];
        keys(g, {"layout", "cylinders", "displacement_cc", "bore_mm", "stroke_mm"});
        if (g["layout"].as<std::string>() != "boxer") throw std::invalid_argument("unsupported engine layout");
        p.layout = "boxer-4";
        p.cylinders = g["cylinders"].as<std::uint32_t>();
        p.displacementCc = g["displacement_cc"].as<float>();
        p.boreMm = g["bore_mm"].as<float>(); p.strokeMm = g["stroke_mm"].as<float>();
        keys(root["crankshaft"], {"firing_order"});
        const auto order = root["crankshaft"]["firing_order"];
        if (!order.IsSequence() || order.size() != 4) throw std::invalid_argument("expected four firing order entries");
        p.firingOrderCount = 4;
        for (int i = 0; i < 4; ++i) p.firingOrder[i] = order[i].as<std::uint32_t>();
        keys(root["aspiration"], {"type"});
        if (root["aspiration"]["type"].as<std::string>() != "naturally_aspirated")
            throw std::invalid_argument("unsupported aspiration");
        keys(root["defaults"], {"idle_rpm", "redline_rpm"});
        p.idleRpm = root["defaults"]["idle_rpm"].as<float>();
        p.redlineRpm = root["defaults"]["redline_rpm"].as<float>();
        const auto a = root["reference_audio"];
        keys(a, {"master_volume", "leveler_target", "high_frequency_mix", "air_noise", "input_sample_noise", "convolution", "impulse_response", "impulse_response_gain"});
        if (a["impulse_response"].as<std::string>() != "smooth_39")
            throw std::invalid_argument("only the audited smooth_39 impulse response is supported");
        p.referenceAudio = {a["master_volume"].as<double>(), a["leveler_target"].as<double>(),
            a["high_frequency_mix"].as<double>(), a["air_noise"].as<double>(),
            a["input_sample_noise"].as<double>(), a["convolution"].as<double>(), a["impulse_response_gain"].as<double>()};
        keys(root["provenance"], {"note"});
        root["provenance"]["note"].as<std::string>();
        validateEngineProfile(p);
        return p;
    } catch (const YAML::Exception& e) {
        throw std::invalid_argument(std::string("invalid engine profile YAML: ") + e.what());
    }
}
std::filesystem::path defaultEngineProfilePath() { return ACSOUNDSIM_FA20_PROFILE_PATH; }
EngineProfileV1 makeFa20Baseline() { return loadEngineProfile(defaultEngineProfilePath()); }
} // namespace soundsim
