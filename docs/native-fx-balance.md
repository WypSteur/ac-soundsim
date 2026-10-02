# Selective native FMOD FX balance — bridge 0.0.7

**Historical experiment, removed in 0.0.8 at the user's request.** Current policy
keeps all complementary FMOD/mod events untouched and calibrates only SoundSim
output. See `hybrid-mix-policy.md`; do not implement the trims below again as the
default architecture. The original document is retained as an audit trail.

The user confirmed the revised live FA20D sounds like their reference app, then
reported native FMOD dominating at the rev limiter. That is a listening report,
not an independently measured app null test or real-world FA20 validation.

The installed CSP app SDK exposes `ac.CarAudioEventID.Limiter` (15),
`BackfireExt` (10) and `BackfireInt` (11) independently from EngineExt/Int.
The [official gain API](https://github.com/ac-custom-shaders-patch/acc-lua-sdk/blob/7cf60f5a406820ee4e39f3a62ed121fa36274001/common/ac_car_control.lua)
supports live event multipliers, reports NaN for absent events and warns about
conflicts with car configuration/scripts. No FMOD bank is edited or unpacked.

## Defaults and live controls

While SoundSim is playing on the GT86 AND its native continuous engine is muted:

- `FMOD limiter balance`: .25 of the captured original multiplier (-12.04 dB).
- `FMOD backfire balance`: .50 for both interior and exterior (-6.02 dB).

These are initial listening trims, **not measured loudness equalization** and not
frequency EQ. Engine-Sim PCM, simulation, AC limiter physics and all event pitches
are unchanged. Tyres, wind, transmission, gear, TC, turbo and other native events
are untouched. If another native effect dominates, identify its event before
expanding this policy. The separate Limiter event is the first target suggested
by the user's report; no recording/event isolation has proven it exclusively.

The factors use each event's actual non-default/configured original gain, not
an assumed value of 1. They follow the SoundSim listening gain relative to its
default of 2:

`applied = original * min(1, balance * SoundSim_gain / 2)`.

No native effect is amplified above its captured original multiplier. A factor of
1 at SoundSim gain2 means original volume; use **Restore native FX levels** for
unconditionally original gains. Sliders span 0..1, independent for limiter and
backfire. Zero mutes only those FX; it does not change limiter behavior. The
values reset each app session; no global audio settings are changed or persisted.

## Lifecycle and ownership

Pause, stale producer, mod mute, diagnostic tone, release, replay or restoring
the native continuous engine releases FX balance and restores saved original
gains where they are still owned. The live 2D/3D diagnostic switch briefly
releases and reacquires if playback remains available. A different car identity
discards old snapshots instead of writing old values to a different car.

Before changing a factor, current gains are checked against our last applied
values with float tolerance. An external gain change disables the whole FX
balance; conflicting values are preserved and still-owned gains are restored.
Re-enable explicitly to capture the new baseline. Missing enum values NEVER go
through CSP's nil-to-event0 fallback. Missing/NaN gains are skipped; the UI/log
reports how many of the three events could be balanced.

## Verification and listening

LuaJIT/FFI mocked-host tests passed for non-default original gains, relative
trims, following SoundSim gain, zero/clamp/nonfinite sliders, independent FX
restore, pause/stale/mod mute/native-engine restore/release, engine and FX
ownership conflicts, re-acquisition, missing enum and NaN event. The mock rejects
any write outside EngineExt/Int and the three selected FX IDs. It does not prove
native audibility or an acoustically matched mix.

Bridge installed with AC closed; previous app is backed up by the installer.
The existing Release runtime can keep running: it and the PCM path are unchanged.
Next GT86 session, compare limiter/backfire with the two sliders; lower Limiter
further if it still masks the simulated engine. **Restore native FX levels** gives
an immediate original-FX A/B. After restoring the native continuous engine, FX
also return to original, so use the FX button alone for an isolated FX comparison.

No new native CSP listening/readback evidence is claimed yet. The snapshot at
`Documents/Assetto Corsa/logs/ac_soundsim_bridge.txt` now includes `FMOD_FX` status.
