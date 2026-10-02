# ADR-0004 — Impose engine kinematics without a competing mechanical solver

Status: Accepted (M1, 2026-10-02)

Upstream `Simulator::simulateStep()` advances rigid-body mechanics, vehicle and
transmission, while `startFrame()` changes the number of simulation steps to chase
audio latency. Neither behavior is appropriate when AC supplies RPM.

M1 instead builds an explicit upstream acoustic dependency slice and supplies an
adapter around `Engine`. `ExternallyDrivenCrank` integrates external RPM on a fixed
clock, and analytical slider-crank kinematics populate the upstream bodies. The
existing cam, ignition, combustion, gas-flow, exhaust-delay and synthesizer code
consumes these states. Mechanical reaction forces cannot alter crank speed.

A small persisted upstream patch adds a synchronous audio path and guards/fixes
needed by the headless slice. Upstream vehicle/dyno/application implementations
are excluded. The patch and the source/submodule revisions are pinned.

Consequences: deterministic offline testing, reduced dependency graph and an API
ready for external telemetry. Geometry/cam/flow assumptions are provisional and
documented in `docs/m1-headless.md`. Runtime state interpolation, live audio
transport, session resets and in-game budgets remain later milestones.
