#include "soundsim/externally_driven_crank.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

void check(bool condition) { if (!condition) throw std::runtime_error("crank check failed"); }

int main() {
    try {
        soundsim::ExternallyDrivenCrank c;
        constexpr int rate = 22050;
        for (int i = 0; i < 60 * rate; ++i) c.advance(7400, 1.0 / rate);
        check(std::abs(c.totalAngle() - 7400 * 2 * c.kPi) < 1e-6);
        const double phase = c.phase();
        c.advance(0, 1.0 / rate);
        check(c.phase() == phase && c.angularVelocity() == 0);
        c.advance(800, 1.0 / rate);
        check(std::abs(c.totalAngle() - (7400 * 2 * c.kPi + 800 * 2 * c.kPi / 60 / rate)) < 1e-6);
        for (double bad : {-1.0, 12001.0, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
            const auto before = c.totalAngle();
            bool rejected = false;
            try { c.advance(bad, 1.0 / rate); } catch (const std::invalid_argument&) { rejected = true; }
            check(rejected && c.totalAngle() == before);
        }
        c.reset(-c.kPi);
        check(c.phase() == 3 * c.kPi && c.cycles() == 0);
        std::cout << "crank: long-run phase, stall, RPM step and invalid inputs PASS\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
