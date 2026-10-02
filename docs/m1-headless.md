# M1 — Headless Engine-Sim / external crank

## Scope and architecture

M0's GT86 identity and dynamic RPM/throttle/gear display were confirmed by the
user on 2026-10-02. This is a runtime observation, not an automated AC test. The
presence of `[ACSoundSim]` lines in the local CSP log has not been independently
verified in this workspace.

M1 uses the actual open-source Engine-Sim gas/combustion/exhaust implementation.
It does not replace it with an oscillator or an RPM sample bank. The standalone
adapter owns an upstream `Engine`, imposes its crank/piston/rod kinematics, runs
upstream camshafts, ignition, chambers, intake and exhaust flow, then uses the
upstream exhaust-pressure signal formula, delay and synthesizer filters.

The build does not include `Simulator`, `PistonEngineSimulator`, vehicle,
transmission, dyno, starter, rigid-body solvers, graphics, SDL, scripting or an
audio device. The minimal SCS dependency provides body types/transforms and the
base force/state methods referenced by upstream objects; no solver is executed.

Inputs are external RPM, normalized throttle and ignition enable. M1 supplies a
constant input for each block. In the offline sweep the input is sampled at the
block midpoint. Timestamped AC interpolation, pause/session handling and load
inference belong to M2. No separate calibrated AC load/boost model is claimed.

## Reproducible upstream

- Engine-Sim: `ange-yaghi/engine-sim`, revision
  `85f7c3b959a908ed5232ede4f1a4ac7eafe6b630`.
- SCS submodule: `e009f4ff1c9c4c5874e865e893cdb62e208fb2b3`.
- Source remains under gitignored `vendor/engine-sim`.
- Persistent patch: `integration/engine_sim/patches/0001-headless-block-renderer.patch`.
- `cmake/EngineSimHeadless.cmake` enumerates the required source files and checks
  revisions. Upstream's application CMake is deliberately not invoked.
- Bootstrap verifies existing revisions and applies the patch idempotently; it
  refuses to reset a checkout at a different revision.

The four-file patch makes `Engine::createSimulator()` return null only under
`ACSOUNDSIM_HEADLESS_ONLY`, removes an unused graphics include from the
synthesizer, adds a synchronous renderer, initializes audio bookkeeping, frees
transfer/jitter buffers and rejects non-finite samples before integer conversion.
Sample clamping happens before conversion to avoid integer overflow.
The combustion and gas algorithms are unchanged.

## External kinematics and timing

`ExternallyDrivenCrank` integrates positive four-stroke phase in `[0, 4*pi)`:

```text
omega = rpm * 2*pi / 60
phase += omega * dt
```

Completed 720-degree cycles are stored separately. Engine-Sim uses a clockwise
negative body angular velocity, so the adapter sets:

```text
body.theta = tdc - phase
body.v_theta = -omega
```

Slider-crank geometry solves piston positions analytically from the rotated
journals and rod length. Analytical velocities are supplied to chamber turbulence
history. The mechanical reaction forces do not feed back into RPM. The journal
angles, cams and ignition phases agree with the physical label order `1,3,2,4`.
Upstream arrays are bank-grouped: `[1,3]`, then `[2,4]`; reported counters use
physical labels `[1,2,3,4]`.

M1 clocks are fixed:

| Quantity | Value |
|---|---:|
| Kinematic / chamber update | 22,050 Hz |
| Fluid substeps | 8 |
| PCM | mono signed int16, 44,100 Hz |
| Harness block | 147 simulation steps = 294 frames = 6.667 ms |
| Maximum API block | 220 simulation steps |

The upstream resampler emits a startup sample at t=0. The adapter removes it once,
then delivers exactly two PCM frames per simulation step. Output capacity must be
at least `2*steps+1` to accommodate this initial scratch sample. Simulation progress
never depends on synthesizer latency, wall-clock scheduling or a rendering thread.
The CSP MMF format/sample rate is still unknown; these are M1 offline settings.

Normal `render()` does not allocate, log, parse files or take a lock. The caller
provides preallocated output storage. Inputs outside the supported range are
rejected before state mutation. Gas/volume/non-finite PCM faults increment a guard
counter and latch the instance as faulted. Recreate it before subsequent rendering.

## FA20 provenance and provisional acoustics

The builder consumes the existing C++ `makeFa20Baseline()` profile, matching the
baseline YAML identity, bore/stroke and firing order. A generic YAML loader is not
implemented. The adapter rejects incompatible geometry/firing order.

Confirmed baseline profile fields: boxer four, 86 mm bore, 86 mm stroke,
1-3-2-4 firing order, 7400 RPM fallback redline. AC will supply operating values in
M2; no independent RPM limiter is applied here.

The following are explicit development assumptions, not a measured stock GT86
sound model:

- 12.5:1 compression, 129.5 mm rods, 30 mm compression height;
- smooth provisional cam lobes, 280 crank degrees seat-to-seat, 10/9.5 mm lift,
  110-degree intake/exhaust centers, constant 25-degree ignition advance;
- generic intake/exhaust flow curves and fuel/turbulence curves;
- 2 L plenum, 0.30 m intake runners;
- equal 0.60 m primaries plus 0.05 m head allowance and 1.5 m collector path;
- one exhaust bus, identity IR, no measured muffler response;
- deterministic combustion efficiency and zero added air noise/jitter;
- 0.25 output volume and a conservative leveler target.

This follows Engine-Sim's acoustic mechanism, but does not certify FA20 tonal
fidelity. Header lengths, valve timing, flow and filtering require later calibration.
No native FMOD files or samples are imported or distributed.

## Build and run on this machine

Visual Studio 2022 C++ tools were already installed. CMake was absent from PATH;
CMake 3.31.10 was installed locally under gitignored `tools/build-runtime`.
The following script finds that runtime, or uses CMake from PATH:

```powershell
cd 'C:\Users\WypSteur\Desktop\ac engine sound mod\ac-soundsim'
.\scripts\build_m1.ps1 -GenerateAudio
```

Equivalent commands with CMake on PATH:

```powershell
.\scripts\bootstrap_engine_sim.ps1
cmake -S . -B build -G 'Visual Studio 17 2022' -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
.\build\Release\soundsim-m1.exe --output artifacts\m1
```

The harness writes `fa20-rpm-sweep.wav`, `sweep.csv`, `summary.json` and timestamped
logs. It naturally warms the chambers for two seconds, then records 15 seconds:
one second at 800 RPM, six seconds rising to 7400, one second held at 7400, six
seconds descending, one second at 800. Throttle is 0.75 during acceleration and
0.02 during deceleration, with 0.12 at idle. The output directory is explicit;
rerunning replaces the WAV/CSV/JSON in that directory.

## Verification and acceptance

Tests cover a 60-second high-RPM crank integration, stall/resume, invalid-input
rejection, known firing counts at 3000 RPM, repeatable PCM, changes with throttle
and ignition, and an 800->7400->800 sweep. The sweep checks phase error, RPM error,
gas bounds, combustion in all four cylinders, frame cadence, non-silent PCM and
zero clipped samples. A JSON PASS is written only after those checks succeed.

The first Release run passed all four tests, with 661,500 recorded PCM frames,
zero firing-order errors and zero instability guards. Peak sample amplitude was
3779/32768; phase error was below `1e-10` rad. The 15-second recording took about
6 seconds including file I/O on this machine: roughly 40% of one core's time.
This is an offline single-instance measurement, not an in-game CPU or latency
benchmark. Further performance work may be needed for the eventual AC runtime.

All four tests also passed in Debug and in an MSVC AddressSanitizer
RelWithDebInfo build (`/fsanitize=address /EHsc`), with no detected memory-access
errors. AddressSanitizer on this setup is not a leak detector. Bootstrap was tested
against a separate clean checkout, then rerun to verify idempotence. The Release
executable imports only Windows/C++ runtime DLLs; no SDL or graphics DLL is needed.
MIT notices are preserved in `licenses/` and listed in `THIRD_PARTY_NOTICES.md`.

The delivered Release audio has SHA-256
`42cb1b9e6e116ebe5f401bb4d7be2e5f40eb1881a8e380b335b04bdb0756c42e`.

M1's exit criteria are met offline. Next: M2 — live `RuntimeCarStateV1` over IPC,
then M3 — determine the actual CSP MMF audio protocol and connect the PCM emitter.
The generated WAV can already be auditioned; in-game playback is not implemented.
