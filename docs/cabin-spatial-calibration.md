# Cabin / spatial calibration — bridge 0.0.10, 2026-10-02

## Status and boundaries

User subsequently approved0.0.10 as the interior/exterior listening base, reporting
no crushed/weakened sound, but not stock GT86 timbre. Latest session settings:
level/mids -8dB, highs -24dB, high crossover2087Hz, body0dB/80Hz, gain8, guard on.
These remain session settings, not shipped defaults. This perceptual validation
does NOT replace endpoint headroom/latency/real-car measurements. Separate intake
source is the next proposed feature, not implemented. Sections below describe
initial presets and measurement boundaries; listening acceptance is partial now.

Implemented, offline tested, ready for user listening. NOT a measured GT86 cabin,
not a validated real-world exhaust, and not an end-to-end CSP clipping guarantee.
AC owns RPM/phase/vehicle physics. Public Engine-Sim pin and source PCM, IR,
combustion, throttle behavior and output stream ABI are unchanged. Native banks,
configurations, complementary FMOD events and global levels are untouched.

The previously approved application-like source remains the reference. Listener
transfer is distinct from the existing upstream exhaust convolution. There is
no fabricated intake or mechanical bus and no extra global muffler convolution.

## Implemented listener transfer

`acoustics.lua` builds only our AudioEvent DSP chain. Camera multipliers and
event volume stay at unity, avoiding a second gain AFTER our limiter. DSP order
requested from CSP (0-based indices):

1. ThreeEQ cabin transfer: bass neutral, mids -2 dB, highs -10 dB, crossovers
   400 Hz / 2200 Hz, native default slope.
2. ParamEQ optional body resonance: 180 Hz / 1.4 octaves / **0 dB by default**.
3. Three uniform ThreeEQ gain stages, equal low/mid/high gains per stage.
4. Optional own limiter, enabled initially: -1 dBFS, 25 ms release, zero makeup.

Cabin level defaults -2 dB, added to requested output trim only in the cabin.
Output gain stays 8, range .1..12. Three gain stages split the dB gain so no band
exceeds native FMOD's +10 dB range, even at gain12. Outside: cabin band/resonance
gains neutral, trim 0; the optional limiter is still active. This is not a claim
of sample-identical exterior output: gain stages/filters may alter phase and the
limiter intentionally alters overload dynamics. No pitch/RPM/sample-bank effect.

Important version-specific checks on the actual `fmod64.dll` **1.08.12**:

- Fader has **no gain parameter** here; treating it like modern FMOD silently
  fails. It was used only as a metering tap before this revision.
- ThreeEQ band gains are dB, -80..+10.
- ParamEQ gain is **dB**, -30..+30, NOT legacy FMOD Ex's linear .05..3 gain.
- Crossover slope and limiter channel mode retain defaults; no unverified
  int/bool parameter is sent through a float-only call. Source is mono.

Primary API index reference: [official FMOD wrapper](https://github.com/fmod/fmod-for-unity/blob/master/Assets/Plugins/FMOD/src/fmod_dsp.cs).
Actual old-version ranges/units are queried from installed DSP descriptors by
`tests/audio_calibration.py`, not inferred from current-version documentation.

Auto detection uses installed SDK `isCameraOnBoard` (documented inside this car),
restricted to player focus. If unavailable, Cockpit / Drivable-Dash modes are
used; no blanket F5/F6/bonnet classification. Logs expose mode/onboard fields;
manual auto/interior/exterior override is available to diagnose native behavior.
Cabin strength transitions with 80 ms exponential smoothing. Cabin A/B does NOT
recreate the stream. Switching 2D/3D or guard recreates the event and can briefly
interrupt playback. 2D audit bypasses cabin treatment; guard has a separate
switch. Settings remain session-only; Reset acoustic settings leaves gain alone.

## Spatial source

The installed GT86 CSP config lists tailpipes at (+/-0.455, .33, -2.20).
Use their midpoint `(0,.33,-2.20)` for the existing single mono exhaust-system
bus. Do not duplicate coherent mono streams into two fake independent exhausts.
Evidence: `extension/config/cars/loaded/ks_toyota_gt86.ini`, sections
`PARTICLES_FX_EXHAUST_0/1`. These are visual geometry references, not measured
acoustic centers. Default cones 120/240 degrees, outside factor .6, distances
1/200 m remain the old reference values, now adjustable live. Tailpipe height and
longitudinal position are adjustable. Source world pose/velocity follow AC.
Doppler=1; occlusion and tunnel reverb are delegated to CSP. No manual duplicate
occlusion/reverb added, no assertions of measured air absorption. Replay remains
muted by the existing runtime policy.

## Headroom evidence and live meter

Release regression source render repeats the old approved full-preset metrics:
13 s / 573300 frames, peak .3496704, RMS .1081996 (-19.3155 dBFS), 0 quantization
rail samples. With *gain only*, before EQ/guard/3D:

| Gain | Projected peak | Samples above unity |
|---|---:|---:|
| 2.5 | .8742 | 0 |
| 4 | 1.3987 | 7942 |
| 8 | 2.7974 | 157053 |
| 12 | 4.1960 | 227544 |

This explains why more gain alone is not a clean-headroom solution. Projection
is NOT observed clipping at the Windows endpoint. Guard can reduce perceived
level/dynamics, particularly at gain8. Do not promise twice the perceived volume.
Own -1 dB ceiling does not reserve enough room for every complementary/native
sound and does not bound the sum of all events at the final mixer.

Bridge reads the known producer PCM MMF **read-only**, without ABI modifications,
once per second over at most the newest 1 s. Header/format/counter are validated;
counter changes during scanning discard that observation. Peak, RMS and samples
near source quantization rails are displayed/logged. This is pre-listener DSP,
not a consumer cursor/fill/underrun metric. Zeros from CSP's raw DSP meter remain
inconclusive. Native output order, channel behavior and global clipping need a
fresh AC session / endpoint recording; no test has validated these here.

## Reproducible verification

```powershell
.\scripts\test_bridge.ps1
python tests/audio_calibration.py --source artifacts/audio-audit/fa20d-full.wav --fmod 'D:/SteamLibrary/steamapps/common/assettocorsa/fmod64.dll' --output artifacts/cabin-audit/levels-and-fmod.json
.\tools\build-runtime\cmake\data\bin\ctest.exe --test-dir build -C Release --output-on-failure
```

LuaJIT mock: ABI/layout, camera selection, smoothing/endpoints, live cabin A/B
without event restart, neutral exterior EQ, old-FMOD dB units, gain bounds/NaN,
guard chain replacement, reset, spatial setters and tailpipe world geometry,
read-only PCM meter/ring wrap/format/NaN rejection; continuous engine lifecycle
ownership and all complementary/arbitrary mod events unchanged. Seven C++ tests
pass Release; C++ source/runtime is unchanged this revision.

Actual FMOD isolated **no-output** graph (NOSOUND_NRT, no device / no AC session):

| Test signal | Native output peak |
|---|---:|
| 100 Hz, gain1, exterior, guard off | 1.00014 |
| 100 Hz, gain8, exterior, guard off | 8.00115 |
| 100 Hz, gain8, exterior, guard on | .891251 (-1 dBFS) |
| 100 Hz, gain1, interior, guard off | .793811 |
| 5000 Hz, gain1, exterior, guard off | .984311 |
| 5000 Hz, gain1, interior, guard off | .258957 |

This validates our intended DSP graph, not CSP's stream graph/order. Parameter
readbacks match requests; no endpoint audio was emitted. Report includes version,
parameter descriptor ranges/units, source levels and these measurements.

## Real GT86 reference inventory — not calibrated yet

User's desktop contains `Gt86 Stock Exhaust Vs Milltek Cat-back Resonated
homologated.wav` and an MP3 counterpart. WAV inventory: 18.5984 s, stereo PCM16,
44100 Hz, peak .6204834, RMS -20.6761 dBFS, no quantization rail samples. Inputs
are not modified. Filename implies a comparison but does NOT establish segment
boundaries, stock/modified section, microphone position, recording processing,
RPM or load. Do not EQ-match the whole clip or normalize RMS against it and call
that a calibrated stock GT86. Need identified exterior stock segments plus a
known interior reference. No real-car timbre match or cabin impulse measured.

## User listening acceptance

1. Fresh GT86 session, check bridge0.0.10, source FA20D/reference, camera mode and
   producer meter. Normal runtime can remain running; no second producer.
2. Same gain/view/RPM: guard on/off A/B, first at gain2..2.5 for safe reference,
   then requested gain8. Start with low hardware volume. Do not confuse limiting
   with faithful loudness; report pumping/crunch and exterior timbre changes.
3. Cockpit: idle, steady ~3000, acceleration, overrun, limiter. Use **Bypass cabin
   treatment (A/B)** without switching views; compare same RPM/load. Adjust level
   separately from high-band attenuation/crossover. Body EQ optional, not mandatory.
4. Exterior stationary near/far and front/rear: cabin EQ must return neutral.
   Confirm onboard classification, or use camera override diagnostically.
5. Drive-by/free/track views in a live session: source location, attenuation,
   cone continuity, Doppler. Replay still muted. Tunnel/wall behavior is track/CSP
   dependent and must be listened to, not inferred from mock setters.
6. Preserve native transient mix; only our gain/transfer changes. Identify real
   GT86 reference segments/conditions, then calibrate. Change exhaust IR only if
   reference evidence supports it; intake/mechanical buses remain future work.

Acceptance remains open until native CSP output/listening and real reference
conditions are available. Installation/testing offline is not perceptual approval.

Installation: bridge0.0.10 installed with AC closed; main Lua, acoustics module
and manifest SHA256 match the workspace. Recoverable previous bridge backup:
`artifacts/bridge-backups/20261002-215820-810`. Existing normal runtime PID35628
unchanged and left running. No AC session was automatically launched.
