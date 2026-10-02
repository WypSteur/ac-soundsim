# Hybrid sound ownership — bridge 0.0.10

Current revision adds OUR listener DSP/explicit own peak guard while preserving
the native ownership policy below. See `cabin-spatial-calibration.md` for controls,
actual old-FMOD unit checks, source/DSP measurements and live acceptance limits.
Gain remains8/.1..12, but now precedes our optional limiter; event/camera volume
unity. The following output-only/no-compressor paragraphs describe **0.0.9**, not
the latest DSP implementation. No complementary FMOD gains are read or changed.

User decision (2026-10-02): keep the existing native/modded FMOD ecosystem as the
mix reference. Replace the continuous RPM-driven engine sound with simulation,
not every sound in the car. Quality prerecorded/transient effects such as turbo
flutter may remain samples. Preserving them is intentional, not an unfinished
attempt to simulate every event.

This supersedes the 0.0.7 per-event Limiter/Backfire balance experiment. That code,
its three-event snapshot/ownership logic and both FX sliders have been removed.
No fixed FX whitelist is used to decide what should survive: everything outside
the explicitly replaced continuous engine events remains untouched, including
arbitrary events added/configured by other mods. No complementary FMOD gains
are even queried. SoundSim owns only its own output gain and the existing
reversible EngineInt/EngineExt mute.

## Current implementation

`SoundSim gain` is the sole mix-level control, applied to our CSP AudioEvent only.
Default 8.0, range .1..12, session-only. User's latest live log showed gain4 and
they requested at least twice that level: gain8 is exactly 2x amplitude / +6.02 dB
relative to that observed setting (not a guarantee of 2x perceived loudness).
The previous .1..4 slider could not reach it. Original FMOD
gains, pitches, parameter maps, banks, global audio sliders and other scripts'
FX settings are not modified. Upstream engine parameters/DSP/PCM stay unchanged;
no compressor, limiter, loudness normalizer or timbre change was added to hide an
incorrect mix. The existing 2D/3D source audit is retained.

The previous full-preset reference audit had peak 11458/32768, so gain2.5 would
give .874 peak (~-1.17 dBFS) and RMS .2705 BEFORE spatial propagation/mixing for
that specific file. This is not a live clipping measurement or an end-to-end
headroom guarantee: the rest of the mix and other operating conditions differ.
Gain8 gives reference peak ~2.80 and RMS ~.866 before spatial/mix processing, so
there is NO unity-headroom guarantee: 3D attenuation may reduce it but 2D/near
cameras can distort. Gain4 already could exceed full scale on that reference
peak. A high-gain warning is shown above2.85, based on this reference file only;
no compressor/limiter was silently added or end-to-end clipping test claimed.
If distorted, reduce our
gain; do not lower all preserved FMOD events to compensate. Default8 is an
initial trim, not a measured loudness match to every car/camera.

## Scope and compatibility boundary

Only `ks_toyota_gt86` is supported currently. Supporting more vehicles means
resolving a simulation profile and calibrating OUR output level per vehicle,
while retaining whatever FMOD effects that vehicle/mod supplies. No arbitrary
car's native engine will be muted before a matching simulation is available.

The mute granularity is entire EngineInt/EngineExt events. If a mod embeds flutter
or other desired transients inside one of those engine events rather than a
separate event, muting it also silences those layers. Merely retaining separate
Limiter/Turbo/Backfire events cannot solve that case. Audit sound ownership per
vehicle; use a compatible separated event layout/mod-author integration rather
than guessing how to extract layers or muting every event. No banks have been
modified and no universal compatibility is claimed.

## Verification and next listening step

LuaJIT/FFI mocked-host tests pass. Every CarAudioTweak read/write outside
EngineInt/EngineExt now fails the test. Non-default complementary native gains
and 101 arbitrary simulated mod-event gains stay unchanged across live output
trim, bounds/nonfinite UI inputs, native restore/conflict/reacquire, pause, stale
producer, mod mute, 2D/3D switch, target loss and release. External mods changing
their own FX gains while SoundSim plays are left untouched.

Close/restart the AC session for installation/reload; 0.0.7's onRelease restores
its captured FX gains. 0.0.8 does not blindly reset native multipliers to 1, which
would overwrite car configuration or other mods. No migration uses guessed
original gains. Installer keeps a recoverable backup. The unchanged runtime
can remain running between AC sessions.

Listen with the original FMOD mix and adjust ONLY SoundSim gain. Compare idle,
steady loaded RPM, overrun and limiter, in both interior and exterior views.
Future per-car calibration should measure our source/mix headroom and subjective
balance under those conditions, not attenuate each retained event individually.
New native listening and exact mix matching remain unverified until the user
tests the installed revision.

0.0.8 was installed with AC closed and its Lua source hash matches the workspace.
Previous app backup: `artifacts/bridge-backups/20261002-210648-456`. The unchanged
normal runtime PID35628 was left running; no session or other mod was modified.
