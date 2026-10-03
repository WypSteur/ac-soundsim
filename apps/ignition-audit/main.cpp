#include "soundsim/headless_engine.hpp"
#include <array>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <vector>

using namespace soundsim;
EngineInput input(double t) {
    if (t < 1) return {800,0,true};
    if (t < 2) return {800+(t-1)*6600,1,true};
    if (t < 2.10) return {7400,0,true};
    if (t < 3) return {4200+(t-2.10)*3200/.90,1,true};
    if (t < 4) return {7400-(t-3)*6600,.02,true};
    if (t < 4.5) return {0,0,false};
    return {800,0,true};
}
int main(int argc, char** argv) {
    try {
        if ((argc != 3 && argc != 4) || std::string(argv[1]) != "--output" || (argc == 4 && std::string(argv[3]) != "--upstream"))
            throw std::invalid_argument("Usage: soundsim-ignition-audit --output directory [--upstream]");
        const std::filesystem::path dir = argv[2]; std::filesystem::create_directories(dir);
        std::srand(12345);
        HeadlessEngine engine(makeFa20Baseline(), EnginePreset::fa20ReferenceFull,
            argc == 4 ? IgnitionPolicy::upstreamAudit : IgnitionPolicy::externalContinuous);
        std::vector<IgnitionTraceRow> rows(32768);
        engine.traceIgnition(rows.data(), rows.size());
        std::array<std::int16_t,HeadlessEngine::kMaximumBlockFrames> pcm{};
        EngineInput held{}; int last=-1;
        for (int b=0; b<900; ++b) {
            const int state = static_cast<int>(std::floor(b/150.0*90+1e-9));
            if (state != last) { held=input(state/90.0); last=state; }
            engine.render(held,147,pcm.data(),int(pcm.size()));
        }
        std::ofstream out(dir/"ignition-90hz.csv");
        out << "time_s,cylinder,rpm_previous,rpm,crank_r0,crank_r1,advance_previous,advance_current,threshold_previous,threshold_current,spark_cycle,upstream_fired,selected_fired,upstream_duplicate,sequence_anomaly\n" << std::setprecision(17);
        std::uint64_t duplicates=0;
        for (std::size_t i=0; i<engine.diagnostics().traceRows; ++i) {
            const auto& r=rows[i]; duplicates+=r.upstreamDuplicate;
            out << r.timeSeconds << ',' << r.cylinder << ',' << r.previousRpm << ',' << r.rpm << ',' << r.crankPrevious << ',' << r.crankCurrent
                << ',' << r.advancePrevious << ',' << r.advanceCurrent << ',' << r.thresholdPrevious << ',' << r.thresholdCurrent
                << ',' << r.sparkCycle << ',' << r.upstreamFired << ',' << r.selectedFired << ',' << r.upstreamDuplicate << ',' << r.sequenceAnomaly << '\n';
        }
        out.flush(); if (!out || engine.diagnostics().traceDropped) throw std::runtime_error("incomplete ignition trace");
        std::cout << "90 Hz: anomalies=" << engine.diagnostics().firingOrderErrors << " duplicates=" << duplicates
            << " rows=" << engine.diagnostics().traceRows << " dropped=" << engine.diagnostics().traceDropped << '\n';
        if (duplicates != 1 || engine.diagnostics().firingOrderErrors != (argc == 4 ? 1u : 0u))
            throw std::runtime_error("90 Hz ignition regression changed");
        return 0;
    } catch(const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
