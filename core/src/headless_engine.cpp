#include "soundsim/headless_engine.hpp"
#include "soundsim/externally_driven_crank.hpp"
#include "soundsim/wav.hpp"

#include "engine.h"
#include "direct_throttle_linkage.h"
#include "standard_valvetrain.h"
#include "synthesizer.h"
#include "delay_filter.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string>

namespace soundsim {
namespace {
constexpr double pi = ExternallyDrivenCrank::kPi;
constexpr double cycle = ExternallyDrivenCrank::kCycle;
constexpr double dt = 1.0 / HeadlessEngine::kSimulationRate;
constexpr double rodLength = 0.1295; // provisional, see docs/m1-headless.md
constexpr double compressionHeight = 0.030;
constexpr std::array<int, 4> labels{1, 3, 2, 4}; // upstream array layout: bank0 then bank1
constexpr std::array<double, 4> firingPhase{0, pi, 2 * pi, 3 * pi};

void validateProfile(const EngineProfileV1& p) {
    validateEngineProfile(p);
}

void buildLobe(Function& f, double lift) {
    // Provisional smooth cam profile, 280 crank degrees seat-to-seat.
    constexpr int points = 201;
    const double halfWidth = 70.0 * pi / 180.0; // cam angle
    f.initialize(points, 2.0 * pi / (points - 1));
    for (int i = 0; i < points; ++i) {
        const double a = -pi + 2.0 * pi * i / (points - 1);
        const double y = std::abs(a) < halfWidth
            ? lift * std::pow(std::cos(a * pi / (2.0 * halfWidth)), 2.0) : 0;
        f.addSample(a, y);
    }
}

void buildReferenceLobe(Function& f, double lift) {
    // Exact public GenerateHarmonicCamLobeNode formula, gamma=1, steps=100.
    const double k = std::acos(2 * 0.00127 / lift - 1) / (255 * pi / 180 / 4);
    const double extent = pi / k, step = extent / 95;
    f.initialize(199, step);
    f.addSample(0, lift);
    for (int i = 1; i < 100; ++i) {
        const double x = i * step;
        const double y = x >= extent ? 0 : lift * (0.5 + 0.5 * std::cos(k*x));
        f.addSample(x, y); f.addSample(-x, y);
    }
}

} // namespace

struct HeadlessEngine::Impl {
    Engine engine;
    ExternallyDrivenCrank crank;
    Synthesizer synth;
    std::array<Camshaft, 2> intakeCams;
    std::array<Camshaft, 2> exhaustCams;
    std::array<StandardValvetrain, 2> valvetrains;
    Function intakeLobe, exhaustLobe, intakeFlow, exhaustFlow;
    Function timing, turbulence, flameSpeed;
    std::array<DelayFilter, 4> delays;
    EngineDiagnostics stats;
    int lastFiringSlot{-1};
    bool engineInitialized{};
    bool synthInitialized{};
    bool failed{};
    EnginePreset preset;
    double connectingRodLength = rodLength;
    bool reference() const { return preset != EnginePreset::legacyM1; }

    explicit Impl(const EngineProfileV1& p, EnginePreset selected) : preset(selected) {
        validateProfile(p);
        if (preset != EnginePreset::legacyM1 && preset != EnginePreset::fa20ReferenceDry && preset != EnginePreset::fa20ReferenceFull)
            throw std::invalid_argument("Unsupported Engine-Sim preset");
        if (reference()) connectingRodLength = 0.1293;
        try { configure(p); }
        catch (...) { cleanup(); throw; }
    }

    ~Impl() { cleanup(); }

    void cleanup() {
        if (synthInitialized) { synth.destroy(); synthInitialized = false; }
        if (engineInitialized) {
            // Upstream Engine::destroy() does not destroy heads/cams/functions.
            for (int b = 0; b < 2; ++b) engine.getHead(b)->destroy();
            engine.destroy();
            engineInitialized = false;
        }
        for (auto& c : intakeCams) c.destroy();
        for (auto& c : exhaustCams) c.destroy();
        for (Function* f : {&intakeLobe, &exhaustLobe, &intakeFlow, &exhaustFlow,
                           &timing, &turbulence, &flameSpeed}) f->destroy();
    }

    [[noreturn]] void unstable(const char* detail) {
        failed = true;
        ++stats.instabilityGuards;
        throw std::runtime_error(std::string("M1 instability guard: ") + detail);
    }

    void configure(const EngineProfileV1& p) {
        auto* throttle = new DirectThrottleLinkage;
        throttle->initialize({1.0});
        Engine::Parameters ep{};
        ep.cylinderBanks = 2;
        ep.cylinderCount = 4;
        ep.crankshaftCount = 1;
        ep.exhaustSystemCount = 1;
        ep.intakeCount = reference() ? 2 : 1;
        ep.name = p.id;
        ep.redline = units::rpm(p.redlineRpm);
        ep.throttle = throttle;
        ep.initialSimulationFrequency = kSimulationRate;
        ep.initialHighFrequencyGain = reference() ? p.referenceAudio.highFrequencyMix : 0.01;
        ep.initialNoise = reference() ? p.referenceAudio.airNoise : 0;
        ep.initialJitter = reference() ? p.referenceAudio.inputSampleNoise : 0;
        engine.initialize(ep);
        engineInitialized = true;

        Crankshaft::Parameters cp{};
        cp.mass = 10;
        cp.flywheelMass = 7;
        cp.momentOfInertia = 0.2;
        cp.crankThrow = p.strokeMm * 0.001 / 2;
        cp.tdc = pi;
        cp.rodJournals = 4;
        auto* shaft = engine.getCrankshaft(0);
        shaft->initialize(cp);
        shaft->m_body.theta = pi;
        for (int i = 0; i < 4; ++i) shaft->setRodJournalAngle(i, (i == 1 || i == 2) ? pi : 0);

        if (reference()) {
            buildReferenceLobe(intakeLobe, 0.01075);
            buildReferenceLobe(exhaustLobe, 0.01047);
            constexpr double inFlows[]{0,30,60,90,125,160,195,210,235,270,305,320,335,335,335};
            constexpr double exFlows[]{0,30,55,85,115,140,160,180,205,220,240,260,280,280,280};
            intakeFlow.initialize(15, 0.001); exhaustFlow.initialize(15, 0.001);
            for (int i = 0; i < 15; ++i) {
                intakeFlow.addSample(i * 0.001, GasSystem::k_28inH2O(inFlows[i]));
                exhaustFlow.addSample(i * 0.001, GasSystem::k_28inH2O(exFlows[i]));
            }
            constexpr double advance[]{10,35,70,68,66,64,62,60,60,60};
            timing.initialize(10, units::rpm(1000));
            for (int i = 0; i < 10; ++i) timing.addSample(units::rpm(i*1000), advance[i]*pi/180);
        } else {
            buildLobe(intakeLobe, 0.010); buildLobe(exhaustLobe, 0.0095);
            intakeFlow.initialize(12, 0.001); exhaustFlow.initialize(12, 0.001);
            for (int i = 0; i < 12; ++i) {
                intakeFlow.addSample(i * 0.001, GasSystem::k_28inH2O(std::min(i * 27.0, 270.0)));
                exhaustFlow.addSample(i * 0.001, GasSystem::k_28inH2O(std::min(i * 23.0, 230.0)));
            }
            timing.initialize(2, units::rpm(12000));
            timing.addSample(0, 25 * pi / 180); timing.addSample(units::rpm(12000), 25 * pi / 180);
        }
        turbulence.initialize(31, 1);
        flameSpeed.initialize(reference() ? 10 : 31, reference() ? 5 : 1);
        for (int i = 0; i < 31; ++i) {
            turbulence.addSample(double(i), i * 0.5);
            if (!reference()) flameSpeed.addSample(double(i), 1 + i * 2.0);
        }
        if (reference()) for (int i = 0; i < 10; ++i) flameSpeed.addSample(i*5.0, i ? i*7.5 : 3.0);
        Fuel::Parameters fp;
        fp.burningEfficiencyRandomness = reference() ? 0.5 : 0;
        fp.maxBurningEfficiency = reference() ? 0.8 : 0.9;
        if (reference()) fp.maxDilutionEffect = 10;
        fp.turbulenceToFlameSpeedRatio = &flameSpeed;
        engine.getFuel()->initialize(fp);

        Intake::Parameters ip{};
        ip.volume = 0.002;
        ip.CrossSectionArea = 0.002;
        ip.InputFlowK = GasSystem::k_carb(400);
        ip.IdleFlowK = 0;
        ip.RunnerFlowRate = GasSystem::k_carb(100);
        ip.RunnerLength = 0.30;
        ip.IdleThrottlePlatePosition = 0.997;
        ip.VelocityDecay = 1;
        if (reference()) {
            ip.volume = 0.001; ip.CrossSectionArea = 0.005;
            ip.InputFlowK = GasSystem::k_carb(200); ip.IdleFlowK = GasSystem::k_carb(0.001);
            ip.RunnerFlowRate = GasSystem::k_carb(300); ip.IdleThrottlePlatePosition = 0.9965;
            ip.VelocityDecay = 0.1;
        }
        for (int i = 0; i < ep.intakeCount; ++i) {
            if (reference()) ip.RunnerLength = (i ? 7.5 : 7.0) * 0.0254;
            engine.getIntake(i)->initialize(ip);
        }

        ExhaustSystem::Parameters xp{};
        xp.length = 1.5;
        xp.collectorCrossSectionArea = pi * 0.025 * 0.025;
        xp.outletFlowRate = GasSystem::k_carb(1000);
        xp.primaryTubeLength = 0.60;
        xp.primaryFlowRate = GasSystem::k_carb(400);
        xp.velocityDecay = 1;
        xp.audioVolume = 0.01;
        if (reference()) {
            xp.length = 2.5; xp.collectorCrossSectionArea = pi*std::pow(3.5*0.0254/2,2);
            xp.outletFlowRate = GasSystem::k_carb(400); xp.primaryTubeLength = 8*0.0254;
            xp.primaryFlowRate = GasSystem::k_carb(200);
        }
        engine.getExhaustSystem(0)->initialize(xp);

        for (int b = 0; b < 2; ++b) {
            CylinderBank::Parameters bp{};
            bp.crankshaft = shaft;
            bp.angle = (b == 0 ? 90 : -90) * pi / 180;
            bp.bore = p.boreMm * 0.001;
            bp.deckHeight = cp.crankThrow + rodLength + compressionHeight;
            if (reference()) bp.deckHeight = b == 0 ? 0.20505 : 0.205;
            bp.cylinderCount = 2;
            bp.index = b;
            auto* bank = engine.getCylinderBank(b);
            bank->initialize(bp);
            Camshaft::Parameters cam{};
            cam.crankshaft = shaft;
            cam.lobes = 2;
            cam.lobeProfile = &intakeLobe;
            if (reference()) { cam.advance = 8.25*pi/180; cam.baseRadius = 0.017; }
            intakeCams[b].initialize(cam);
            cam.lobeProfile = &exhaustLobe;
            exhaustCams[b].initialize(cam);
            for (int j = 0; j < 2; ++j) {
                const double fire = firingPhase[2 * b + j];
                intakeCams[b].setLobeCenterline(j, fire + 2 * pi + 110 * pi / 180);
                exhaustCams[b].setLobeCenterline(j, fire + 2 * pi - 110 * pi / 180);
            }
            valvetrains[b].initialize({&intakeCams[b], &exhaustCams[b]});
            CylinderHead::Parameters hp{};
            hp.Bank = bank;
            hp.IntakePortFlow = &intakeFlow;
            hp.ExhaustPortFlow = &exhaustFlow;
            hp.Valvetrain = &valvetrains[b];
            hp.CombustionChamberVolume = bank->boreSurfaceArea() * (2 * cp.crankThrow) / (12.5 - 1);
            hp.IntakeRunnerVolume = 0.000149;
            hp.IntakeRunnerCrossSectionArea = 0.0018;
            hp.ExhaustRunnerVolume = 0.00005;
            hp.ExhaustRunnerCrossSectionArea = 0.0018;
            if (reference()) {
                hp.CombustionChamberVolume = 40.3e-6;
                hp.IntakeRunnerCrossSectionArea = pi*std::pow(0.0254,2);
                hp.ExhaustRunnerCrossSectionArea = pi*std::pow(0.65*0.0254,2);
                hp.IntakeRunnerVolume = hp.IntakeRunnerCrossSectionArea * 5*0.0254;
                hp.ExhaustRunnerVolume = hp.ExhaustRunnerCrossSectionArea * 5*0.0254;
            }
            auto* head = engine.getHead(b);
            head->initialize(hp);
            head->setAllIntakes(engine.getIntake(reference() ? b : 0));
            head->setAllExhaustSystems(engine.getExhaustSystem(0));
            head->setAllHeaderPrimaryLengths(0.05);
            if (reference()) {
                head->setHeaderPrimaryLength(0, (b ? 2 : 1)*0.0254);
                head->setHeaderPrimaryLength(1, (b ? 1 : 0)*0.0254);
                head->setSoundAttenuation(0, 0.9);
            }

            for (int j = 0; j < 2; ++j) {
                const int i = 2 * b + j;
                auto* piston = engine.getPiston(i);
                auto* rod = engine.getConnectingRod(i);
                ConnectingRod::Parameters rp{};
                rp.mass = 0.5;
                rp.momentOfInertia = 0.001;
                rp.length = connectingRodLength;
                if (reference()) { rp.mass = 0.3468; rp.momentOfInertia = rp.mass*rp.length*rp.length/12; }
                rp.piston = piston;
                rp.crankshaft = shaft;
                rp.journal = i;
                rod->initialize(rp);
                Piston::Parameters pp{};
                pp.Rod = rod;
                pp.Bank = bank;
                pp.CylinderIndex = j;
                pp.BlowbyFlowCoefficient = GasSystem::k_28inH2O(0.001);
                pp.CompressionHeight = compressionHeight;
                pp.mass = 0.5;
                if (reference()) {
                    pp.BlowbyFlowCoefficient = GasSystem::k_28inH2O(0.1);
                    pp.CompressionHeight = 0.0328; pp.mass = 0.4025; pp.Displacement = 2.7e-6;
                }
                piston->initialize(pp);
            }
        }
        imposeKinematics();
        for (int i = 0; i < 4; ++i) {
            auto* chamber = engine.getChamber(i);
            CombustionChamber::Parameters cc{};
            cc.Piston = engine.getPiston(i);
            cc.Head = engine.getHead(i / 2);
            cc.Fuel = engine.getFuel();
            cc.MeanPistonSpeedToTurbulence = &turbulence;
            cc.StartingPressure = cc.CrankcasePressure = units::atm;
            cc.StartingTemperature = units::celcius(25);
            chamber->initialize(cc);
            chamber->m_system.initialize(units::atm, chamber->getVolume(), units::celcius(25));
            const double length = engine.getHead(i / 2)->getHeaderPrimaryLength(i % 2)
                + engine.getExhaustSystem(0)->getLength();
            delays[i].initialize(length / 343.0, kSimulationRate);
        }
        IgnitionModule::Parameters ignition{};
        ignition.crankshaft = shaft;
        ignition.cylinderCount = 4;
        ignition.timingCurve = &timing;
        // AC owns limiter policy too; M1 does not invent an independent cutoff.
        ignition.revLimit = units::rpm(ExternallyDrivenCrank::kMaximumRpm + 1);
        engine.getIgnitionModule()->initialize(ignition);
        for (int i = 0; i < 4; ++i) engine.getIgnitionModule()->setFiringOrder(i, firingPhase[i]);
        engine.getIgnitionModule()->reset();

        Synthesizer::Parameters sp;
        sp.inputChannelCount = 1;
        sp.inputBufferSize = 2048;
        sp.audioBufferSize = 2048;
        sp.inputSampleRate = kSimulationRate;
        sp.audioSampleRate = kSampleRate;
        sp.initialAudioParameters.airNoise = 0;
        sp.initialAudioParameters.inputSampleNoise = 0;
        sp.initialAudioParameters.convolution = 0;
        sp.initialAudioParameters.volume = 0.25;
        sp.initialAudioParameters.levelerTarget = 12000;
        if (reference()) {
            // App default 1 clips this externally-driven reference schedule.
            // Fixed -12.04 dB pre-quantization headroom, NOT PCM normalization.
            sp.initialAudioParameters.volume = static_cast<float>(p.referenceAudio.masterVolume);
            sp.initialAudioParameters.levelerTarget = static_cast<float>(p.referenceAudio.levelerTarget);
            sp.initialAudioParameters.dF_F_mix = static_cast<float>(p.referenceAudio.highFrequencyMix);
        }
        if (preset == EnginePreset::fa20ReferenceFull) {
            sp.initialAudioParameters.airNoise = static_cast<float>(p.referenceAudio.airNoise);
            sp.initialAudioParameters.inputSampleNoise = static_cast<float>(p.referenceAudio.inputSampleNoise);
            sp.initialAudioParameters.convolution = static_cast<float>(p.referenceAudio.convolution);
        }
        synth.initialize(sp);
        synthInitialized = true;
        // Upstream always evaluates convolution even with mix=0; identity IR
        // avoids null/zero-length convolution without distributing any samples.
        if (preset == EnginePreset::fa20ReferenceFull) {
            const auto impulse = readMonoPcm16(ACSOUNDSIM_FA20_IR_PATH);
            synth.initializeImpulseResponse(impulse.data(), int(impulse.size()), static_cast<float>(p.referenceAudio.impulseResponseGain), 0);
        } else {
            const std::int16_t identityImpulse = 32767;
            synth.initializeImpulseResponse(&identityImpulse, 1, 1, 0);
        }
    }

    void imposeKinematics() {
        auto* shaft = engine.getOutputCrankshaft();
        shaft->m_body.theta = shaft->getTdc() - crank.phase();
        shaft->m_body.v_theta = -crank.angularVelocity(); // upstream CW convention
        const double c = std::cos(shaft->m_body.theta);
        const double s = std::sin(shaft->m_body.theta);
        for (int i = 0; i < 4; ++i) {
            auto* piston = engine.getPiston(i);
            auto* rod = engine.getConnectingRod(i);
            auto* bank = piston->getCylinderBank();
            double jx, jy;
            shaft->getRodJournalPositionLocal(i, &jx, &jy);
            const double x = c * jx - s * jy;
            const double y = s * jx + c * jy;
            const double vx = -shaft->m_body.v_theta * y;
            const double vy = shaft->m_body.v_theta * x;
            const double along = x * bank->getDx() + y * bank->getDy();
            const double across = -x * bank->getDy() + y * bank->getDx();
            const double acrossSpeed = -vx * bank->getDy() + vy * bank->getDx();
            const double root = std::sqrt(connectingRodLength * connectingRodLength - across * across);
            const double travel = along + root;
            const double speed = vx * bank->getDx() + vy * bank->getDy() - across * acrossSpeed / root;
            piston->m_body.p_x = bank->getX() + travel * bank->getDx();
            piston->m_body.p_y = bank->getY() + travel * bank->getDy();
            piston->m_body.v_x = speed * bank->getDx();
            piston->m_body.v_y = speed * bank->getDy();
            piston->m_body.theta = bank->getAngle() + pi;
            const double dx = piston->m_body.p_x - x;
            const double dy = piston->m_body.p_y - y;
            rod->m_body.theta = std::atan2(dy, dx) - pi / 2;
            rod->m_body.p_x = (x + piston->m_body.p_x) / 2;
            rod->m_body.p_y = (y + piston->m_body.p_y) / 2;
            rod->m_body.v_x = (vx + piston->m_body.v_x) / 2;
            rod->m_body.v_y = (vy + piston->m_body.v_y) / 2;
            rod->m_body.v_theta = (dx * (piston->m_body.v_y - vy)
                - dy * (piston->m_body.v_x - vx)) / (connectingRodLength * connectingRodLength);
        }
    }

    void checkGas(const GasSystem& gas) {
        const double p = gas.pressure();
        const double t = gas.temperature();
        if (!std::isfinite(p) || p <= 0 || p > 1e9 ||
            !std::isfinite(t) || t <= 0 || t > 30000 ||
            !std::isfinite(gas.n()) || gas.n() <= 0 ||
            !std::isfinite(gas.velocity_x()) || !std::isfinite(gas.velocity_y())) {
            unstable("gas pressure/temperature/moles/velocity out of bounds");
        }
        stats.maximumPressure = std::max(stats.maximumPressure, p);
        stats.maximumTemperature = std::max(stats.maximumTemperature, t);
    }

    void step(const EngineInput& in) {
        crank.advance(in.rpm, dt);
        imposeKinematics();
        engine.update(dt);
        auto* ignition = engine.getIgnitionModule();
        ignition->m_enabled = in.ignitionEnabled && in.rpm > 0;
        ignition->update(dt);
        for (int i = 0; i < 4; ++i) {
            auto* cc = engine.getChamber(i);
            const double volume = cc->getVolume();
            if (!std::isfinite(volume) || volume <= 0) unstable("non-positive chamber volume");
            stats.minimumVolume = std::min(stats.minimumVolume, volume);
            // Match public PistonEngineSimulator: ignition BEFORE chamber update.
            if (ignition->getIgnitionEvent(i)) {
                ++stats.sparks[labels[i] - 1];
                if (lastFiringSlot >= 0 && i != (lastFiringSlot + 1) % 4) ++stats.firingOrderErrors;
                lastFiringSlot = i;
                cc->ignite();
                if (cc->popLitLastFrame()) ++stats.combustions[labels[i] - 1];
            }
            cc->update(dt);
            cc->resetLastTimestepExhaustFlow();
            cc->resetLastTimestepIntakeFlow();
        }
        for (int sub = 0; sub < kFluidSubsteps; ++sub) {
            engine.getExhaustSystem(0)->process(dt / kFluidSubsteps);
            for (int i = 0; i < engine.getIntakeCount(); ++i) engine.getIntake(i)->process(dt / kFluidSubsteps);
            for (int i = 0; i < 4; ++i) engine.getChamber(i)->flow(dt / kFluidSubsteps);
        }
        double exhaustSignal = 0;
        const double attenuation = std::pow(std::min(in.rpm, 40.0) / 40.0, 3);
        for (int i = 0; i < 4; ++i) {
            auto* cc = engine.getChamber(i);
            checkGas(cc->m_system);
            checkGas(cc->m_intakeRunnerAndManifold);
            checkGas(cc->m_exhaustRunnerAndPrimary);
            const double raw = attenuation * 1600 * (
                cc->m_exhaustRunnerAndPrimary.pressure() - units::atm
                + 0.1 * cc->m_exhaustRunnerAndPrimary.dynamicPressure(1, 0)
                + 0.1 * cc->m_exhaustRunnerAndPrimary.dynamicPressure(-1, 0));
            const double length = engine.getHead(i / 2)->getHeaderPrimaryLength(i % 2)
                + engine.getExhaustSystem(0)->getLength();
            exhaustSignal += engine.getHead(i/2)->getSoundAttenuation(i%2) * engine.getExhaustSystem(0)->getAudioVolume()
                * delays[i].fast_f(raw) / 4 / (length * length);
        }
        for (int i = 0; i < engine.getIntakeCount(); ++i) checkGas(engine.getIntake(i)->m_system);
        checkGas(*engine.getExhaustSystem(0)->getSystem());
        if (!std::isfinite(exhaustSignal)) unstable("non-finite exhaust signal");
        synth.writeInput(&exhaustSignal);
        ignition->resetIgnitionEvents();
        ++stats.simulationSteps;
    }
};

HeadlessEngine::HeadlessEngine(const EngineProfileV1& p, EnginePreset preset) : impl_(std::make_unique<Impl>(p, preset)) {}
HeadlessEngine::~HeadlessEngine() = default;

int HeadlessEngine::render(const EngineInput& in, int steps, std::int16_t* pcm, int capacity) {
    if (!pcm || steps <= 0 || steps > kMaximumBlockSteps || capacity < 2 * steps + 1 ||
        !std::isfinite(in.rpm) || in.rpm < 0 || in.rpm > ExternallyDrivenCrank::kMaximumRpm ||
        !std::isfinite(in.throttle) || in.throttle < 0 || in.throttle > 1) {
        throw std::invalid_argument("M1: invalid input, block size or output capacity");
    }
    if (impl_->failed) throw std::logic_error("M1 engine faulted: recreate before rendering");
    impl_->engine.setSpeedControl(in.throttle);
    for (int i = 0; i < steps; ++i) impl_->step(in);
    int frames;
    try { frames = impl_->synth.renderAudioSynchronous(pcm, capacity); }
    catch (...) { impl_->failed = true; ++impl_->stats.instabilityGuards; throw; }
    auto& s = impl_->stats;
    if (s.audioFrames == 0 && frames == 2 * steps + 1) {
        --frames;
        std::memmove(pcm, pcm + 1, std::size_t(frames) * sizeof(std::int16_t));
    }
    if (frames != 2 * steps) impl_->unstable("PCM cadence mismatch");
    s.audioFrames += frames;
    s.requestedRpm = in.rpm;
    // Upstream units::toRpm() uses a rounded constant. Measure the actual body
    // angular velocity with the same precise conversion as the phase clock.
    s.effectiveRpm = std::abs(impl_->engine.getOutputCrankshaft()->m_body.v_theta) * 60 / (2 * pi);
    s.angularVelocity = impl_->crank.angularVelocity();
    s.phase = impl_->crank.phase();
    s.totalAngle = impl_->crank.totalAngle();
    s.burntFuelKg = 0;
    for (int i = 0; i < 4; ++i) s.burntFuelKg += impl_->engine.getChamber(i)->m_nBurntFuel;
    return frames;
}

const EngineDiagnostics& HeadlessEngine::diagnostics() const noexcept { return impl_->stats; }
const char* HeadlessEngine::upstreamRevision() noexcept { return ACSOUNDSIM_ENGINE_SIM_REVISION; }

} // namespace soundsim
