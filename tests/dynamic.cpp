#include "soundsim/headless_engine.hpp"
#include "soundsim/externally_driven_crank.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace soundsim;
namespace {
EngineInput input(double t) {
    if (t < 1) return {800,0,true};
    if (t < 2) return {800+(t-1)*6600,1,true};
    if (t < 2.10) return {7400,0,true};
    if (t < 3) return {4200+(t-2.10)*3200/.90,1,true};
    if (t < 4) return {7400-(t-3)*6600,.02,true};
    if (t < 4.5) return {0,0,false};
    return {800,0,true};
}
std::vector<double> arrivals(int hz, double offset) {
    std::vector<double> result{0};
    std::uint32_t random=0x13579bdu; // independent of Engine-Sim global rand()
    for (int i=0;; ++i) {
        double next;
        if (hz) next=offset+(i+1)/double(hz);
        else {
            random=random*1664525u+1013904223u;
            const double period=1.0/(30+(random%211));
            next=result.back()+period+(i%47==46 ? .050 : 0); // jitter + occasional delayed packet
        }
        if (next>6) break;
        result.push_back(next);
    }
    return result;
}
}
int main() {
    try {
        std::ofstream report("dynamic-gate.csv");
        report << "state_hz,warmup_blocks,packet_offset_s,frames,source_peak,firing_sequence_anomalies,gas_guards\n";
        for (int stateHz : {30,60,90,120,144,165,240,0}) {
            for (int warmup : {0,37}) {
                const double offset=warmup ? .0043 : 0;
                const auto packets=arrivals(stateHz,offset);
                std::srand(12345);
                HeadlessEngine engine(makeFa20Baseline(),EnginePreset::fa20ReferenceFull);
                std::array<std::int16_t,HeadlessEngine::kMaximumBlockFrames> pcm{};
                for (int b=0; b<warmup; ++b) engine.render({800,0,true},147,pcm.data(),int(pcm.size()));
                double expectedAngle=engine.diagnostics().totalAngle, peak=0;
                std::size_t packet=0;
                for (int b=0; b<900; ++b) {
                    const double t=b/150.0;
                    while (packet+1<packets.size() && packets[packet+1]<=t+1e-9) ++packet;
                    const auto held=input(packets[packet]);
                    const int n=engine.render(held,147,pcm.data(),int(pcm.size()));
                    if (n!=294) throw std::runtime_error("dynamic PCM cadence changed");
                    expectedAngle+=held.rpm*(2*ExternallyDrivenCrank::kPi/60)/150;
                    const auto& d=engine.diagnostics();
                    if (std::abs(d.totalAngle-expectedAngle)>1e-7 || std::abs(d.effectiveRpm-held.rpm)>1e-8)
                        throw std::runtime_error("ZOH RPM authority/continuous phase failed");
                    if (d.firingOrderErrors || d.instabilityGuards) {
                        std::cerr << "stateHz=" << stateHz << " warmup=" << warmup << " t=" << t
                                  << " firingErrors=" << d.firingOrderErrors << " guards=" << d.instabilityGuards << '\n';
                        throw std::runtime_error("M5A dynamic ignition/gas gate failed");
                    }
                    for (int i=0; i<n; ++i) {
                        if (pcm[i]==32767 || pcm[i]==-32768) throw std::runtime_error("dynamic source saturation");
                        peak=std::max(peak,std::abs(int(pcm[i]))/32768.0);
                    }
                }
                const auto& d=engine.diagnostics();
                if (peak<.001 || d.audioFrames!=static_cast<std::uint64_t>((900+warmup)*294))
                    throw std::runtime_error("dynamic output silent/incomplete");
                for (auto sparks:d.sparks) if (!sparks) throw std::runtime_error("cylinder silent");
                report << stateHz << ',' << warmup << ',' << offset << ',' << d.audioFrames << ',' << peak
                       << ',' << d.firingOrderErrors << ',' << d.instabilityGuards << '\n';
                report.flush();
                std::cout << (stateHz ? std::to_string(stateHz)+"Hz" : "jitter 30..240Hz + 50ms delays")
                          << " phaseVariant=" << warmup << " firing_sequence_anomalies=0 peak=" << peak << " PASS\n";
            }
        }
        if (!report) throw std::runtime_error("dynamic gate report write failed");
        // This strict SOURCE gate is not an in-game listening/spatial/latency PASS.
        return 0;
    } catch(const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
