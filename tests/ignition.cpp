#include "soundsim/externally_driven_ignition.hpp"
#include "soundsim/externally_driven_crank.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace soundsim;
void check(bool condition) { if (!condition) throw std::runtime_error("ignition regression assertion failed"); }
int main() {
    try {
        constexpr double pi=ExternallyDrivenCrank::kPi, cycle=ExternallyDrivenCrank::kCycle;
        ExternallyDrivenIgnition clock;
        // Measured cylinder2/cycle122 case, not a speculative missed event.
        const double r0=1538.1656510996161, r1=1538.1750228448968;
        const double a0=1.2201015023458679, a1=1.2054407380881662;
        check(clock.step(r0-.02,r0,a0,true).fired[2]);
        const double threshold=2*pi+122*cycle-a1;
        check(threshold>=r0 && threshold<r1); // old algorithm fires again
        check(!clock.step(r0,r1,a1,true).fired[2]);

        // Retard below the same already-fired target, then forward recross.
        ExternallyDrivenIgnition recross;
        check(recross.step(r0-.02,r0,a0,true).fired[2]);
        check(!recross.step(r0,r1,a1-.03,true).fired[2]);
        const auto again=recross.step(r1,r1+.04,a1-.03,true);
        check(!again.fired[2] && again.duplicateSuppressed[2]);

        // Advancing the threshold behind r0 used to miss a forward crossing.
        ExternallyDrivenIgnition advance;
        check(!advance.step(pi-.51,pi-.50,.25,true).fired[1]);
        check(advance.step(pi-.50,pi-.49,.60,true).fired[1]);

        ExternallyDrivenIgnition steady;
        std::array<int,4> counts{}; int previous=-1;
        for (int tick=0; tick<125664; ++tick) {
            const auto result=steady.step(tick*.01,(tick+1)*.01,.35,true);
            for (int i=0; i<4; ++i) if (result.fired[i]) {
                check(previous<0 || i==(previous+1)%4); previous=i; ++counts[i];
            }
        }
        for (int n:counts) check(n==100);
        ExternallyDrivenIgnition cut;
        check(!cut.step(0,.1,.3,false).fired[0]);
        check(!cut.step(.1,.2,.9,true).fired[0]); // no synthetic startup event
        bool rejected=false;
        try { cut.step(.2,.1,.3,true); } catch(const std::invalid_argument&) { rejected=true; }
        check(rejected); rejected=false;
        try { cut.step(.2,.3,std::numeric_limits<double>::quiet_NaN(),true); }
        catch(const std::invalid_argument&) { rejected=true; }
        check(rejected);
        std::cout << "ignition: measured duplicate, forward threshold crossing, recross, 720 wrap, cut/re-enable and guards PASS\n";
        return 0;
    } catch(const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
