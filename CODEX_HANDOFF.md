# CODEX_HANDOFF — AC SoundSim

## Mission

Continue local development of **AC SoundSim**, a data-driven real-time engine sound simulation mod for Assetto Corsa.

First supported vertical slice:

```text
Kunos Toyota GT86
car_id = ks_toyota_gt86
engine = Subaru/Toyota FA20 / 4U-GSE family baseline
```

The architecture and implementation plan in `docs/` are the source of truth. Do not redesign the project around a conventional RPM sample bank.

## Core architecture that must not be broken

```text
Assetto Corsa
= physics authority + car/world state

SoundSim
= engine acoustics at the source

CSP / AC audio
= 3D propagation, distance, Doppler, cones, occlusion/reverb

Native FMOD events
= retained selectively for backfire/turbo/transmission/tyres/etc.
```

AC remains the authority for RPM. SoundSim must **not** run a second competing vehicle/engine-speed solution.

Target final path:

```text
AC/CSP RuntimeCarState
        ↓
Resolver + EngineProfile
        ↓
SoundSim C++ core
        ↓
live PCM
        ↓
memory-mapped audio stream
        ↓
CSP AudioEvent 3D
        ↓
distance / Doppler / cone / occlusion / reverb
```

## Current status

### User listening validation — bridge 0.0.10 baseline

On2026-10-02 user validated the current interior/exterior behavior as a useful
base: clear cabin effect, no perceived crushed/weakened sound. They require much
stronger cabin filtering than the first preset. Latest bridge snapshot shows
trim=-8dB, mids=-8dB, highs=-24dB, high crossover2087Hz, body0dB at80Hz,
gain8, guard on; snapshot itself is paused/free-camera, not an acoustic measurement.
Settings remain session-only and these are NOT new shipped defaults. Preserve
this listening reference before changing the preset/persistence. User says timbre
is clearly not stock, so neither real-world GT86 fidelity nor complete native
mix headroom/latency/fly-by is validated by this feedback.

Next proposed task: independent admission PCM from existing simulated intake
pressure/flow, two synchronized source buses and two CSP3D emitters (front airbox,
rear exhaust), independent gain/transfer/solo controls. Not a filtered duplicate
of exhaust; no second engine/vehicle simulation. Public upstream has intake gas
state but no ready acoustic output, so that new acoustic model is experimental.
No intake implementation has started. User requested private repository + baseline
commit BEFORE continuing feature development.

### Current listener revision — bridge 0.0.10

User authorized cabin/spatial calibration implementation. Read
`docs/cabin-spatial-calibration.md` first; supersedes 0.0.9 output-only DSP policy,
NOT the policy of leaving every complementary FMOD/mod event untouched. New
`acoustics.lua`: adjustable cabin ThreeEQ/optional body ParamEQ, separate trim,
80ms view transitions, no-restart cabin A/B, explicit camera diagnostic override;
midpoint of actual installed CSP GT86 tailpipes; live cone/distance/offset knobs.
Output gain8 retained, moved BEFORE explicit optional own -1dB/25ms peak limiter,
default on, no makeup. Camera/event gains unity. No engine/model/IR/PCM changes.

IMPORTANT native version trap: actual AC FMOD1.08.12 Fader has NO gain parameter;
ParamEQ GAIN is dB, not old FMOD Ex linear gain. Use three uniform ThreeEQ gain
stages each below +10dB. Descriptor/readback test and real isolated NOSOUND_NRT
graph pass: gain8 outputs8.00115 for100Hz sine, own guard outputs .891251; cabin
preserves bass and muffles5kHz. This is NOT tested CSP DSP order/output/global mix.
Source PCM read-only meter added (peak/RMS/near-full, no consumer cursor fiction).
LuaJIT lifecycle/camera/DSP/meters/FX tests pass; 7/7 C++ Release pass. Report in
`artifacts/cabin-audit/levels-and-fmod.json`; source reference gain8 projects2.7974
peak pre3D, but source itself has0 clipped samples. Guard can lower dynamics/level.
Desktop GT86/Milltek clip inventoried, not blindly used as cabin/stock EQ target:
stock/modified segments and recording conditions unknown. Native listening and
real-car cabin/exterior calibration are still pending; do not claim completion.

Install only with AC closed using existing backup installer; runtime unchanged.
No automated AC session was launched. See calibration doc for A/B procedure.
0.0.10 installed with AC closed; SHA256 of main Lua, acoustics module and manifest
match workspace. Previous bridge backup: `artifacts/bridge-backups/20261002-215820-810`.
Normal runtime PID35628 left running. No native session/listening run for0.0.10.

### Current level revision — bridge 0.0.9

User requested simulated sound at least twice as loud. Latest native log already
showed gain4, so default is now8 (2x amplitude / +6.02dB vs actual setting), slider
.1..12 instead of .1..4. Own AudioEvent gain only: PCM/source/timbre, native FX and
AC/global settings unchanged. High-gain warning: reference peak at8 is~2.80 BEFORE
3D attenuation; no guaranteed headroom/no native distortion measurement. Do not
claim perceptual2x or silently compress the user-approved source. Mock test now
checks gain4->8 with same stream/event, unchanged arbitrary FMOD and bounds12.
Historical defaults2.5 below are superseded. See `docs/hybrid-mix-policy.md`.
0.0.9 installed with AC closed; source/installed hashes match. Mock tests passed.
Previous app backup `artifacts/bridge-backups/20261002-211359-843`. Existing runtime
unchanged; gain8 and new range must still be heard in a fresh AC session.

### Authoritative hybrid-mix decision — bridge 0.0.8

User clarified we must preserve all suitable existing FMOD/mod sounds, not tune
three FX individually. Read `docs/hybrid-mix-policy.md` first. 0.0.7 auxiliary FX
gain code/sliders REMOVED; no complementary FMOD read/write at all. Only continuous
EngineInt/EngineExt muted/restored, as before. Single SoundSim output gain now
defaults2.5 (was2), range .1..4; source PCM/timbre unchanged. Initial +1.94dB trim,
not a guaranteed mix match. LuaJIT tests forbid any native gain access outside
EngineInt/Ext and preserve arbitrary mod FX, including external changes.
Only GT86 supported. Embedded desired FX within a continuous engine event cannot
be separated with whole-event mute; future per-car compatibility/ownership audit
must handle that explicitly. Do not expand a hardcoded preserved-FX whitelist or
change global FMOD levels. 0.0.7 notes below are historical and superseded.
0.0.8 installed with AC closed; SHA matches source. Backup
`artifacts/bridge-backups/20261002-210648-456`, normal runtime PID35628 remains
running. Mock tests passed; revised output trim still needs native listening.

### Latest mix revision — bridge 0.0.7

User now reports rendered engine matches the reference app perceptually; do NOT
inflate that into sample-parity/engineering validation. They requested native
FMOD balance because the rev limiter dominates. See `docs/native-fx-balance.md`.
Limiter (15) defaults .25 of captured native gain, BackfireExt/Int (10/11) .5;
two live sliders, follows SoundSim gain relative2, capped at original native gain.
Only active when native engine ownership + valid normal mod stream are active.
Restoration on pause/stale/mod mute/native restore/release; external conflicts
release owned gains and disable FX balance. Missing IDs/NaN skipped, never nil
fallback to EngineExt. Tyres/wind/transmission/other events unchanged. No core/PCM
or AC limiter physics changes. Initial subjective trims, not measured auto-EQ.
LuaJIT host tests extended and passed; bridge installed with AC closed, backup
created. Existing runtime left running. New native listening/readbacks pending.

### Latest acoustic revision — takes precedence over historical notes below

Read `docs/acoustic-audit.md` first. User confirmed reference is desktop
Community Edition `assets/engines/subaru/Subaru_FA20D.mr` and requires latest
OFFICIAL PUBLIC source; remote HEAD still equals pinned 85f7c3b on 2026-10-02.
Original app/main.mr untouched. Default runtime now explicitly selects FA20D full
C++ port + public smooth_39 IR + convolution/noise/jitter. Old M1 constructor/
harness preset preserved for regression; runtime `--legacy-model` rollback.
Differences/unsupported intake gamma, simulation clock and model validation are
documented. Do not claim app bit-parity or engineering accuracy.

New `soundsim-audio-audit` writes three unnormalized WAVs + inputs/metrics to
`artifacts/audio-audit`. Full source RMS -19.32 dBFS vs old -27.98 (+8.67 dB), zero
clipped samples through 7400 RPM schedule, no instability/firing errors. Master
.25 before quantization: app-default 1 clipped and was not deployed. Bridge 0.0.6
adds same-live-PCM 2D spatial bypass with UI restore to 3D; defaults gain2/cabin1.
Mock tests verify switch lifecycle; new perceptual/native 2D-3D test still pending.
Seven C++ tests now include reference audit; full-preset ignition/throttle/seeded
repeatability added. This source correction supersedes the gain-only response.
All seven passed Release/Debug/ASAN; LuaJIT mock passed. Bridge 0.0.6 is installed
(SHA matches), backup `artifacts/bridge-backups/20261002-201448-208`. Normal Release
runtime PID35628 manually launched, waiting silently for telemetry; AC was closed
and has not been automatically launched for this revision. New in-game listening
must still be performed. ASAN runtime DLL needs process-local MSVC Hostx64/x64 PATH.

### M0 scaffold

Implemented:

- C++17 / CMake project;
- `soundsim_core` static library;
- timestamped structured logger;
- `RuntimeCarStateV1`;
- baseline FA20 engine profile;
- GT86 car profile;
- CSP Lua diagnostic bridge;
- PowerShell installer for CSP bridge;
- upstream Engine-Sim bootstrap script;
- smoke test;
- architecture + ADR + implementation plan.

The scaffold has compiled and its smoke test has passed in the preparation environment.

### Local validation and M1 implementation (2026-10-02)

The user confirmed in an actual GT86 session: target and detected ID both equal
`ks_toyota_gt86`, target match is true, and RPM/throttle/gear follow dynamically.
Local `[ACSoundSim]` log presence has not been independently inspected.

M1 is implemented and passes its offline exit test in Release, Debug and an
AddressSanitizer build. The clean-checkout bootstrap and its idempotence were also
verified. Generated audio and metrics are in `artifacts/m1/`; those outputs are
gitignored, so preserve them separately if handing off only tracked source.
Read `docs/m1-headless.md`
and ADR-0004 before continuing. Relevant files:

- `core/include/soundsim/externally_driven_crank.hpp`;
- `core/include/soundsim/headless_engine.hpp` and `core/src/headless_engine.cpp`;
- `cmake/EngineSimHeadless.cmake`;
- `integration/engine_sim/patches/0001-headless-block-renderer.patch`;
- `apps/soundsim-m1/main.cpp`, `tests/crank.cpp`, `tests/headless.cpp`;
- `scripts/build_m1.ps1`.

Use `scripts/build_m1.ps1 -GenerateAudio` to build, test and generate the sweep WAV.
The upstream adapter uses fixed kinematics and real upstream gas/combustion/audio
code. No vehicle/transmission/rigid-body solver or UI is built. FA20 geometry and
firing order follow the baseline; extra cam/flow/exhaust parameters remain
provisional. Audio is 44.1 kHz mono s16 offline, not a frozen CSP transport format.

### M2/M3 implemented and locally exercised (2026-10-02)

Read `docs/m2-m3-live.md` and `docs/research/csp-stream-format.md` next.
`soundsim-runtime` reads CSP telemetry through a 192-byte explicit/seqlocked MMF,
decodes `RuntimeCarStateV1`, renders real FA20 blocks at 150 Hz and publishes a
mono float32/44100 Hz ring. The official Mumble producer source supplied the actual
64-byte CSP header and cumulative byte protocol, not a guessed custom audio ABI.
CSP Lua attaches a 3D AudioEvent at the provisional tailpipe world pose, with
velocity, cones, occlusion, Doppler and reverb. Native continuous engine remains
ON: no M4 policy/car configuration was changed.

Actual GT86 sessions showed ~700 RPM requested/effective matching, source
valid=true/playing=true, pause-phase freeze, producer restarts reattaching, zero
producer-late blocks and zero faults during observed Release runs. Six C++ tests
pass in Release, Debug and ASAN; a dev-only LuaJIT/FFI mock-host test passes too.
Debug passing is functional, not realtime performance acceptance. A separate
440 Hz diagnostic-tone source was tested through the same transport and detected
at the real Windows output endpoint: amplitude enabled=.0019251, muted=.0002596,
re-enabled=.0018066. It was stopped and real Engine-Sim restored. This proves output
transport, not calibrated engine fidelity, actual fly-by/Doppler or measured latency.
Native `getDSPMetering` returned zeros even during this successful output test.

Relevant new code: `core/include/soundsim/{ipc,win_mmf,csp_stream}.hpp`,
`core/src/ipc.cpp`, `apps/soundsim-runtime/main.cpp`, `tests/ipc.cpp`,
`tests/bridge_host.py`, updated Lua bridge and `scripts/{start_runtime,stop_runtime,test_bridge}.ps1`.
`soundsim-loopback-meter` is a read-only scalar output diagnostic (no mic, no audio
file); it is not a second audio playback backend. `--diagnostic-tone` is explicit
and NEVER the normal engine source. Lupa 2.6 under local build tools is test-only.

Install with AC closed (automatic bridge backup), start with
`scripts/start_runtime.ps1`, stop with `scripts/stop_runtime.ps1`.
Runtime logs are under `logs/runtime/`. Tagged CSP snapshots are at
`Documents/Assetto Corsa/logs/ac_soundsim_bridge.txt`; `ac.log` is not necessarily
persisted in the default CSP text log. Historical report in the app folder is not
the current one. Do not interpret coalesced writer sequences as network loss or
producer deadline misses as measured CSP underruns. Consumer cursors/fill and
underrun behavior remain unknown. Replay is muted, pedal/load and idle calibration
remain provisional. Next implementation: M4 selective mute; M5 isolated listening,
dynamic pedal RPM, spatial fly-by and end-to-end latency validation.

Final installed Lua smoke test passed too (report in AC logs, finite camera
distance, Engine-Sim mode, event valid/playing, no lastError or producer faults).
Evidence is under `artifacts/m2-m3/final-*`; AC and runtime test processes were
closed afterwards. The runtime is not currently running; launch it manually.

### Native-engine listening test added after M2/M3

User subsequently reported barely audible mod output. Bridge 0.0.5 now defaults to
gain 2x (old 0.65x) and interior multiplier 1 (old 0.25), with a live 0.1..4x gain
slider. This changes playback gain only, not upstream PCM or global audio settings.
Mock tests cover live gain/bounds/NaN; perceptual calibration still needs the user.

Bridge 0.0.4 defaults to a session-only test: while normal Engine-Sim has a valid,
playing stream on GT86, `CarAudioTweak` mutes only EngineExt and EngineInt. UI can
restore or re-enable it. Captured gains are restored on pause/stale/stream mute/
release; conflicts disable the test. No persistent car/bank/global audio edits.
See `docs/native-engine-test.md`. This is a gain-zero M4 test subset, not a native
FMOD stop/bypass or CPU optimization. Earlier M2/M3 evidence used native ON.
Actual CSP/GT86 readback now confirms both engine gains=0 with mod stream valid/
playing and no lastError. The user entered driving; session was left open with
the normal runtime manually launched without time limit for listening. Stop with
`scripts/stop_runtime.ps1` (restores native); no service/autostart was installed.
This supersedes the earlier cleanup note for the M2/M3 tests.

### Historical M0 runtime checklist (now confirmed for UI/state)

The runtime checklist was:

```text
car_id == ks_toyota_gt86
RPM changes
throttle changes
gear changes
[ACSoundSim] logs appear
```

UI/state validation was explicitly confirmed by the user; log evidence remains separate.

## Confirmed CSP research

A probe was run against:

```text
CSP v0.3.0-preview445/3978
```

The installed CSP Lua SDK exposes an audio-file constructor parameter:

```lua
stream = {
  name = string,
  size = integer
}
```

with the explicit description:

```text
Audio stream (as an alternative to filename for live streaming data using a memory mapped file).
```

The same constructor also exposes:

```text
use3D
useOcclusion
loop
insideConeAngle
outsideConeAngle
outsideVolume
minDistance
maxDistance
dopplerEffect
dsp
```

Therefore the architecture `live PCM/MMF -> CSP 3D audio source` is considered **API-confirmed**.

The producer protocol is now recovered from official CSP Mumble source and
implemented/tested as described above. Unknowns are consumer cursor/fill, native
underrun behavior and measured latency. Never invent metrics for those fields.

See:

```text
docs/research/csp_pcm_stream_probe.md
```

## M1 specification (implemented offline)

### Goal

Fork/extract upstream `ange-yaghi/engine-sim` and create a **headless externally-driven crank mode**.

Bootstrap:

```powershell
.\scripts\bootstrap_engine_sim.ps1
```

Expected location:

```text
vendor\engine-sim
```

Upstream is pinned at `85f7c3b959a908ed5232ede4f1a4ac7eafe6b630` with a small
persisted patch. The dependency slice and kinematic adapter are documented in M1.

### Required behavior

Input:

```text
RPM supplied externally
throttle/load input
engine profile
time step
```

SoundSim must maintain:

```text
crank angular velocity
crank phase
firing events
combustion/gas/exhaust simulation
```

but must **not** let Engine-Sim vehicle/transmission dynamics determine RPM.

Conceptually:

```text
AC rpm(t)
    ↓
externally driven crank
    ↓
crank phase
    ↓
piston/valve/ignition state
    ↓
combustion / gas flow
    ↓
exhaust-oriented PCM
```

### M1 exit test

Offline RPM sweep:

```text
800 -> 7400 -> 800 RPM
```

with:

- no Engine-Sim vehicle dynamics controlling crank speed;
- stable crank phase;
- no NaN/solver blow-up;
- stable PCM generation;
- logs for requested RPM, effective angular velocity, phase, step rate and audio frames.

Do **not** begin multi-car, resolver generalization, intake bus or muffler work before this is stable.

## Logging requirements

Logging is mandatory from the start.

C++ log categories should remain concise and subsystem-based:

```text
bootstrap
profile
runtime
engine-sim
crank
audio
ipc
csp
native-audio
lod
```

Log:

- startup/version;
- upstream Engine-Sim commit/revision;
- selected car/engine profile;
- state schema;
- transitions/errors;
- MMF creation/open failures;
- sample format;
- underrun/overrun counters;
- NaN/instability guards;
- fallback decisions.

Do not log every audio sample or every simulation tick.

CSP logs must use:

```text
[ACSoundSim]
```

## Native audio policy

Baseline target:

```text
engine_int/ext  -> SoundSim
backfire        -> native
turbo           -> native
transmission    -> native
gear            -> native
tyres/wind      -> native
limiter         -> hybrid candidate
```

Functional mute and CPU bypass are distinct issues.

For initial development it is acceptable to silence native continuous engine audio by supported CSP configuration. Later investigate whether native engine FMOD processing can be fully stopped/bypassed instead of merely multiplied by zero.

Never extract or redistribute FMOD bank samples.

## Scope discipline

Do not work on these yet unless they are required to unblock M1–M5:

- detailed muffler/catalyst/resonator simulation;
- intake acoustic bus;
- mechanical acoustic bus;
- community profile registry;
- automatic online database;
- multi-car high-quality simulation;
- advanced traffic LOD;
- acoustic ray tracing.

They are documented future work, not current blockers.

## Repo navigation

Read in this order:

1. `docs/architecture.md`
2. `docs/implementation-plan.md`
3. `docs/adr/`
4. `docs/research/csp_pcm_stream_probe.md`
5. `profiles/engines/subaru_fa20.yaml`
6. `profiles/cars/ks_toyota_gt86.yaml`
7. `core/include/soundsim/`
8. `integration/csp/apps/ac_soundsim_bridge/`

The long historical design discussion is preserved at:

```text
docs/reference/initial_design_full.md
```

Use it for rationale, not as a substitute for the consolidated architecture.

## Build

Typical clean build:

```powershell
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

On single-config generators, omit `-C Release` as appropriate.

## First local validation

Install CSP bridge:

```powershell
.\scripts\install_csp_bridge.ps1 -AssettoRoot "D:\SteamLibrary\steamapps\common\assettocorsa"
```

Run the Kunos GT86 and verify:

```text
Target: ks_toyota_gt86
Detected: ks_toyota_gt86
GT86 target match: true

RPM changes with engine speed
Throttle changes with pedal
Gear changes correctly
```

CSP log:

```text
Documents\Assetto Corsa\logs\custom_shaders_patch.log
```

Look for:

```text
[ACSoundSim]
```

## Development principle

Prefer one working vertical path over broad scaffolding.

The first meaningful end-to-end success is:

```text
Kunos GT86
→ live AC RPM
→ externally-driven FA20 SoundSim
→ generated PCM
→ CSP live stream
→ 3D emitter on car
→ native continuous engine suppressed
→ native complementary events preserved
```

Once that works, generalize.
