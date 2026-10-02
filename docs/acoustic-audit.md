# FA20D source and CSP listening audit — 2026-10-02

Source/model evidence below remains current. Listener/gain instructions near the
end describe bridge0.0.6; current bridge0.0.10 adds cabin transfer, own gain/guard
and source metering. Use `cabin-spatial-calibration.md` for current A/B procedure,
native-FMOD verification and pending in-game/real-car acceptance.

This supersedes the M1 provisional acoustic model for the **normal runtime**.
M1's original preset remains the default of the low-level constructor and its
historical harness, for regression and explicit rollback only.

## Provenance and version boundary

The integrated source is AngeTheGreat's official public repository, pinned to
`85f7c3b959a908ed5232ede4f1a4ac7eafe6b630`. `git ls-remote` on 2026-10-02 confirmed
HEAD/master still match that pin. No fork, newer private source, reverse-engineered
Community Edition DLL or second mechanical RPM solver was substituted.

The user's listening reference is:
`C:/Users/WypSteur/Desktop/engine-sim-v0.1.14a/assets/engines/subaru/Subaru_FA20D.mr`
(SHA256 `AA5BE042CCAD9BA8B5DDF83CD71A2489700BBA7466A80F3AC5895C91C3973336`).
The original installation and its main.mr have NOT been modified. This is an
explicit C++ parameter port, not a dynamic .mr loader. The local reference script
is not bundled or assigned an assumed redistribution license.

Public source and Community Edition 0.1.14a are distinct versions. The
[Community Edition repository](https://github.com/Engine-Simulator/engine-sim-community-edition)
distributes binaries rather than its application source. No bit-identical match
to that app is claimed. Engine-Sim's own
[README](https://github.com/ange-yaghi/engine-sim) describes it as an audio-oriented
simulation, not an engineering validation tool. Porting this script does not prove
real-world FA20 fidelity; its high ignition advances are reference values, not a
calibration endorsed for a real engine.

## Implemented port and omissions

The FA20D presets use the upstream harmonic cam formula (255 degrees at 50 thou,
lifts 10.75/10.47 mm), 8.25-degree cam advance, flow tables and RPM-dependent
ignition curve from the script. Rod 129.3 mm, compression height 32.8 mm, deck
205.05/205 mm, chamber 40.3 cc and piston displacement 2.7 cc replace provisional
values. Two bank-specific 1 L intakes use the reference flow/runner lengths and
velocity decay. Exhaust uses 2.5 m length, 3.5-inch collector diameter, 8-inch
primary and individual cylinder lengths/attenuation. Fuel uses public script
defaults including dilution effect 10, efficiency .8 and randomness .5.

The original public gas loop's ignite-before-chamber-update order is restored;
the former adapter reversed that order. Audio pressure collection now includes
the upstream per-cylinder head attenuation. RPM, phase, ignition availability and
limiter authority remain AC-owned. Vehicle/transmission/torque-solving parameters
are not imported; masses do not drive a second RPM solution.

Full preset restores upstream DSP: HF mix .05, air noise 1, jitter/input noise .5,
convolution 1 with `default_0`, public `smooth_39.wav`, IR gain .001. IR SHA256
`75DE9DB47063395665D36B6D4232F477AAE385FEAA9BA158353FBDAF122DB5CC` matches the
user's copy. This short acoustic impulse is a filter, not a prerecorded engine
sample bank. The realtime waveform still comes from simulated combustion/gas.
The binary currently loads this public IR from the build-time vendor path;
standalone packaging/asset relocation is not implemented yet.

Known differences: Community intake `throttle_gamma: 2` is absent from the public
Intake API, so public direct-throttle behavior is retained rather than inventing
its closed implementation. Simulation remains 22,050 Hz / 8 fluid steps / 44,100
Hz PCM (reference app default 10,000 Hz). No variable cam timing, measured AC
cylinder load, or real-world exhaust/cabin calibration has been added. Public
global rand() scheduling is not assumed equivalent to the app's threaded audio.

Master volume .25 (fixed -12.04 dB before S16 conversion) and leveler target 30000
leave headroom. App default volume 1 clipped this externally imposed test, so it
was NOT deployed. No per-file normalization, sample-bank production path or
post-clipping boost is used.

## Reproducible offline comparison

Build/tests: `scripts/build_m1.ps1`. Generate comparisons:

```powershell
.\build\Release\soundsim-audio-audit.exe --output artifacts/audio-audit
```

Three unnormalized mono S16 WAVs share a 13-second input schedule after 2 seconds
warm-up: 800 RPM/zero pedal; 3000/.35; 7400/1; descent/.02; idle. Inputs and actual
levels are written to CSV. Each serial render resets the public random seed to
12345. Dry means the port without convolution/noise/jitter, not raw gas pressure
without the other upstream filters. This is **adapter preset comparison**, not
an independent reference recording made by the Community app.

Release artifacts measured locally:

| Preset | RMS dBFS | Peak S16 | Clipped samples |
|---|---:|---:|---:|
| legacy M1 | -27.98 | 7045 | 0 |
| FA20D dry | -19.77 | 27044 | 0 |
| FA20D full | -19.32 | 11458 | 0 |

Full is +8.67 dB RMS relative to legacy at the source, before CSP. All three have
573300 frames, no gas guard/firing-order error and combustion on all cylinders.
Full's measured Release p99 block render is 4.97 ms and max 5.90 ms against a
6.667 ms deadline in this isolated run. These are host timings, not guaranteed
in-game performance or measured consumer underruns. Dry had an occasional 7.68 ms
block; it is offline only. Debug/ASAN are functional diagnostics, not realtime.
Tests also check seeded full-preset repeatability, throttle/ignition effects,
combustion, 75 firings per cylinder at 3000 RPM/3 s and WAV roundtrip equality.

All seven CTest cases passed in Release, Debug and MSVC AddressSanitizer
RelWithDebInfo, plus the LuaJIT/FFI bridge mock. ASAN requires the installed MSVC
`bin/Hostx64/x64` directory on the test process PATH for its runtime DLL; the first
run without that DLL search path could not start, then all cases passed with
the process-local path restored. No system PATH was modified.

## In-game transport/spatial separation

Runtime default is full reference; status source flags: 0 legacy, 1 diagnostic
tone, 2 FA20D port. Bridge 0.0.6 identifies the source and adds **Listen in 2D
(audit)**. It recreates the SAME live MMF PCM event with 3D, occlusion, Doppler and
reverb all disabled. Gain and camera multipliers stay unchanged. **Return to 3D
CSP** restores the normal spatial path. Toggling can briefly interrupt playback;
it does not reset the runtime's engine or crank. LuaJIT mock tests cover both
constructors, disposal and restoration, not native perceptual quality.

With AC closed, install the bridge and start the Release runtime, then GT86.
Begin at gain 1 or 2 (default 2), native engine muted by the existing reversible
test. Compare stationary idle / steady RPM in 2D, then 3D with the same camera and
gain. If WAV/2D are acceptable but 3D is quiet, investigate CSP propagation; if
2D is degraded against WAV, investigate the live stream/timing. If WAV itself is
poor, work on source/model parity first. Do not call gain alone a fidelity fix.
The old gain 4 may distort the stronger source: gain 2 stays below full scale for
this measured reference file, NOT a proof of end-to-end clipping immunity.

The revised bridge/full engine has not yet been subjectively validated in an AC
session. No isolated Community-app recording, sample-aligned app null test,
end-to-end latency or new 2D/3D native listening evidence is claimed.

Installed bridge matches workspace source by SHA256. Previous installed bridge
is backed up at `artifacts/bridge-backups/20261002-201448-208`. Normal Release
runtime was manually started (PID 35628), reports the FA20D port/IR in its bootstrap
log and waits silently for AC telemetry. AC was closed at installation; no session
was launched or driven automatically. Stop with the normal stop script.

Rollback (stop first): `scripts/start_runtime.ps1 -LegacyModel`. Restore native
with the bridge button or stop runtime. No car banks/config/global settings are
modified. No automatic service/startup task is installed.
