#include "soundsim/headless_engine.hpp"
#include "soundsim/wav.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>

using namespace soundsim;
namespace {
EngineInput schedule(int b) {
    const double t = b / 150.0;
    if (t < 3) return {800, 0, true};
    if (t < 6) return {3000, 0.35, true};
    if (t < 9) return {7400, 1.0, true};
    if (t < 12) return {7400-(t-9)*6600/3, 0.02, true};
    return {800, 0, true};
}
void require(bool test, const char* message) { if (!test) throw std::runtime_error(message); }
}
int main(int argc, char** argv) {
    std::filesystem::path dir = "artifacts/audio-audit";
    if (argc == 3 && std::string(argv[1]) == "--output") dir = argv[2];
    else if (argc != 1) { std::cerr << "Usage: soundsim-audio-audit [--output directory]\n"; return 2; }
    try {
        std::filesystem::create_directories(dir);
        std::ofstream report(dir / "metrics.csv"), inputs(dir / "inputs.csv");
        report << "preset,frames,rms,dbfs,peak,clipped_samples,burnt_fuel_kg,max_block_ms,p99_block_ms,guards,firing_errors\n";
        inputs << "time_s,rpm,throttle,ignition\n";
        for (int b=0; b<1950; ++b) { const auto in=schedule(b); inputs << b/150.0 << ',' << in.rpm << ',' << in.throttle << ",1\n"; }
        const std::array<const char*,3> names{"legacy-m1", "fa20d-dry", "fa20d-full"};
        const std::array<EnginePreset,3> presets{EnginePreset::legacyM1,EnginePreset::fa20ReferenceDry,EnginePreset::fa20ReferenceFull};
        for (int p=0; p<3; ++p) {
            // Public core uses global rand(). Serial renders, fixed same seed.
            // Full audio effects consume extra randomness: not a bitwise dry A/B.
            std::srand(12345);
            HeadlessEngine engine(makeFa20Baseline(), presets[p]);
            std::array<std::int16_t,HeadlessEngine::kMaximumBlockFrames> block{};
            for (int b=0; b<300; ++b) engine.render({800,0,true},147,block.data(),int(block.size()));
            std::vector<std::int16_t> pcm; pcm.reserve(13*44100);
            std::vector<double> times; times.reserve(1950);
            for (int b=0; b<1950; ++b) {
                const auto start=std::chrono::steady_clock::now();
                const int n=engine.render(schedule(b),147,block.data(),int(block.size()));
                times.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());
                pcm.insert(pcm.end(),block.begin(),block.begin()+n);
            }
            double sum=0; int peak=0; std::uint64_t clipped=0;
            for (auto s:pcm) { const double v=s/32768.0; sum+=v*v; peak=std::max(peak,std::abs(int(s))); if (s==32767 || s==-32768) ++clipped; }
            const double rms=std::sqrt(sum/pcm.size()); const auto& d=engine.diagnostics();
            require(pcm.size()==13*44100, "wrong frame cadence");
            require(rms>0.0001 && d.burntFuelKg>0 && d.instabilityGuards==0 && d.firingOrderErrors==0, "reference simulation failed");
            for(auto count:d.combustions) require(count>0,"reference cylinder never combusted");
            // No normalization: the files expose actual upstream PCM levels.
            const auto path=dir/(std::string(names[p])+".wav"); writeMonoPcm16(path,pcm);
            require(readMonoPcm16(path)==pcm,"WAV roundtrip differs");
            std::sort(times.begin(),times.end());
            report << std::setprecision(12) << names[p] << ',' << pcm.size() << ',' << rms << ',' << 20*std::log10(rms)
                << ',' << peak << ',' << clipped << ',' << d.burntFuelKg << ',' << times.back() << ',' << times[times.size()*99/100]
                << ',' << d.instabilityGuards << ',' << d.firingOrderErrors << '\n';
            std::cout << names[p] << " RMS=" << rms << " peak=" << peak << " clipped=" << clipped << " p99_ms=" << times[times.size()*99/100] << '\n';
            report.flush();
            require(clipped==0,"PCM saturation: lower master gain before enabling runtime");
        }
        report.flush(); inputs.flush(); require(bool(report)&&bool(inputs),"audit report write failed");
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
