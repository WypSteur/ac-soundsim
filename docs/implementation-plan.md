# Implementation plan — GT86 / FA20 vertical slice

## Consolidated state after the external audit

The repository remains public by explicit user decision. Preserve the accepted
bridge 0.0.10 listening base and the complementary FMOD/mod event levels.
No second engine/car and no independent intake audio before the M5 gate.

## M0 — Observable baseline (user-confirmed)

GT86 identity and dynamic RPM/throttle/gear are confirmed. Tagged bridge reports,
C++ logs and smoke tests exist.

## M1 — Actual public Engine-Sim, external crank (offline qualified)

Pinned public acoustic core with imposed crank/piston kinematics. AC is RPM
authority; no second vehicle/transmission/mechanical-speed solution.
Fixed simulation 22050Hz, fluid substeps8, mono44100Hz, blocks294 / 150Hz.
Normal runtime uses the explicit FA20D reference port plus smooth_39 IR;
legacy M1 and dry remain comparison presets. Not a dynamic .mr interpreter,
not a bit-identical Community Edition app, not real-engine engineering validation.
See m1-headless.md and acoustic-audit.md.

## M2 — AC IPC (implemented and observed live)

192-byte state / 368-byte status, seqlocks, stale/invalid/schema guards,
reset/session detection and external RPM. Separate-process tests cover running,
RPM change, pause, reset, wrong car, stale writer, NaN, replay and graceful stop.
Native driving/listening qualification is still M5.

## M3 — PCM to CSP (implemented; native output observed)

Official CSP Mumble stream contract, continuous mono float32 PCM, AudioEvent 3D.
Source position/velocity follow the GT86 exhaust; distance/cones are adjustable,
Doppler requested. Actual native Doppler/attenuation/mix/latency still need M5.
Ring capacity is NOT latency. No consumer cursor means fill/consumer underrun/
overrun are unknown. Report producer deadline misses under their own name.
Source PCM peak/RMS meter is producer-side, not final output measurement.
See m2-m3-live.md and research/csp-stream-format.md.

## M4 — Hybrid native/listener policy (implemented; base accepted)

Only EngineInt/EngineExt gains reversibly zeroed when our valid stream takes over.
All complementary FMOD events/mod levels untouched, no three-effect whitelist.
Whole engine events might contain other bundled effects; universal mod
compatibility and bypass of native CPU computation are not established.

Bridge0.0.10: separate cabin EQ/body resonance/trim, smooth camera transition,
3D tailpipe controls, gain8 before an optional own -1dB peak guard, no makeup.
User accepts interior/exterior base, requests strong cabin filtering.
Source PCM remains unchanged. DSP units/native isolated graph tested, not
CSP graph ordering or global output headroom. See cabin-spatial-calibration.md.

## M5 — Drivable/native qualification (OPEN)

Read m5-validation.md and use the read-only capture_m5.py collector.
Required: fast acceleration, gear changes, fast decel, live fixed-camera fly-by,
Doppler, distance/orientation, cabin transitions, complementary FMOD mix,
fallback/restart and perceptual latency. Document actual settings and versions.

Synthetic ZOH characterization at60/90/144Hz checks phase, cadence, source rails
and gas guards. It also reports ignition sequence anomalies during fast decel.
These counters are NOT certified by a green test result: characterize the public
timing-advance threshold with external RPM, then qualify/fix before intake.
No automatic test or 1Hz snapshot proves perceptual latency or native propagation.
M5 closes only with documented dynamic/native acceptance, not only compilation.

## Repository consolidation / data-driven extraction

Windows Release/Debug CI: pinned public bootstrap + CTest + mocked LuaJIT bridge
+ Python audit tools. No AC, CSP, proprietary FMOD, personal recordings or logs.
Third-party notices retained; own license is a user decision, still pending.

First profile tranche implemented: real startup YAML loading, strict errors,
identity/geometry/default RPM/source DSP. All three seeded source PCM audits
match the previous baseline exactly on the same MSVC Release toolchain.
No I/O/parsing during render. Runtime --profile supports an explicit file.
See profile-extraction.md for residual C++ specialization and portability limits.

After M5, continue extracting fuel/head/cam/flow/intake/exhaust parameters in
small regress-tested groups. Do not advertise generic engine support while
the FA20-only topology/resolver remains hardcoded.

## Later — Independent intake source

Experimental new acoustic model from existing intake pressure/flow, not a
filtered exhaust duplicate or a second engine simulation. Two synchronized
source buses, separate front/rear CSP3D emitters, gain/transfer/solo controls.
The public upstream does not ship a ready intake PCM bus. Keep AC/CSP propagation
and source acoustics separate. Begin only after the M5 gate above.
