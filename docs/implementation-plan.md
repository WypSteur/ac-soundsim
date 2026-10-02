# Implementation plan — GT86 / FA20 vertical slice

## M0 — Observable baseline (UI/state confirmed 2026-10-02)

User confirmed correct GT86 ID/target match and dynamic RPM/throttle/gear.
C++ build/smoke test also passed locally. CSP log presence remains uninspected.

**Exit criteria**

- C++ scaffold builds and smoke test passes.
- C++ logger creates timestamped logs.
- CSP bridge detects `ks_toyota_gt86`.
- CSP bridge shows changing RPM and throttle.
- CSP logs carry `[ACSoundSim]` tag once per second.

## M1 — Engine-Sim headless, externally-driven crank (offline PASS)

Implemented through an analytical kinematic adapter and pinned upstream acoustic
slice. Four automated tests include stable `800 -> 7400 -> 800 RPM` PCM output.
See `m1-headless.md` for measurements, assumptions and reproduction.

**Goal**: remove Engine-Sim vehicle RPM authority.

Required logs:

- upstream revision;
- engine profile loaded;
- requested RPM;
- effective crank angular velocity;
- integrated crank phase;
- simulation step rate;
- generated audio frames;
- NaN/instability guards.

**Exit criteria**: offline harness sweeps 800 → 7400 → 800 RPM with no mechanical vehicle model and produces stable exhaust-oriented PCM.

## M2 — AC runtime IPC (implemented; observed in GT86)

**Goal**: publish `RuntimeCarStateV1` from CSP/bridge side to C++.

Explicit 192-byte wire ABI, seqlock/schema guards, reader-local age, reset/session
handling and live external RPM are implemented. Six C++ tests plus a LuaJIT host
test pass; actual GT86 idle RPM and pause-phase freeze were observed. See
`m2-m3-live.md`. Dynamic pedal sweep remains a final driving/listening check.

Required logs:

- MMF opened/created;
- schema version;
- writer/reader sequence;
- state age;
- dropped/torn state detection;
- reset/session transition.

## M3 — PCM stream to CSP (implemented; output transport verified)

**Goal**: create CSP audio stream with `{stream={name,size}, use3D=true}` and feed PCM continuously.

Official CSP Mumble producer supplied the actual ABI. Real FA20 mono float32
PCM is connected to a valid/playing 3D event; a separate diagnostic 440 Hz tone
was measured at the output with SoundSim-only mute/unmute. See
`research/csp-stream-format.md`. Native engine remains ON; subjective fidelity,
fly-by/Doppler/distance and latency qualification belong to M5. Consumer fill and
actual underrun/overrun counts are unavailable: log that limitation explicitly,
and expose producer deadline misses under their own name instead.

Required logs:

- audio MMF size;
- producer/consumer state;
- sample rate / format;
- fill level;
- underrun/overrun count;
- CSP event valid/playing;
- emitter position/velocity sanity.

## M4 — Native engine policy

**Goal**: suppress native continuous engine while preserving useful events.

Policy baseline:

```text
engine_int/ext  -> SoundSim
backfire        -> native
transmission    -> native
tyres/wind      -> native
limiter         -> hybrid candidate
```

## M5 — First drivable vertical slice

**Success**:

- start Kunos GT86;
- correct profile selected;
- SoundSim follows live RPM;
- native continuous engine is absent;
- native event sounds remain;
- 3D source follows car;
- track camera/fly-by exhibits CSP Doppler and attenuation;
- logs identify every active subsystem and fallback.
