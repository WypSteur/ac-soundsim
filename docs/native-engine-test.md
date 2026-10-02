# Native engine listening test

Current 0.0.8: SoundSim output-only gain (default2.5), no auxiliary FMOD gain
controls. Only EngineInt/Ext mute/restoration remains. See `hybrid-mix-policy.md`.
The 0.0.7 limiter/backfire experiment below was removed, not left active at 100%.

Bridge 0.0.7 adds separate relative-volume balance for native Limiter and
BackfireExt/Int while the mod owns continuous engine audio; other native events
remain unchanged. See `native-fx-balance.md`. Defaults are limiter .25, backfire
.5 at SoundSim gain2; captured original gains are restored when the mod loses
ownership. Historical claims below that all complementary events are unchanged
apply to 0.0.6 and earlier, not these three explicitly selected FX.

Current bridge 0.0.6 retains gain2/cabin1 and the same mute/restore safeguards,
but runtime now uses the stronger FA20D reference port. Begin at gain1 or2;
the former gain4 can overdrive this source. **Listen in 2D (audit)** disables
only CSP spatial processing for the same live PCM. See `acoustic-audit.md` for
source changes, measured headroom and listening instructions. Earlier 0.0.5
gain ratios below are historical, not a claim that its quiet-source issue was
resolved by gain alone.

Listening-level update (bridge 0.0.5): user reported barely audible output.
Old event gain was 0.65 with interior factor 0.25 (combined 0.1625 before spatial
effects). New default gain is 2.0 with interior factor 1.0: ~12.3x / +21.8 dB
relative cabin gain, ~3.1x / +9.8 dB outside. These are gain ratios, not measured
sound pressure or guaranteed subjective loudness. **SoundSim gain** slider changes
the current event live from 0.1x to 4x, reset to 2x each session. Reduce if distorted.
Upstream PCM, simulation, cones/occlusion, native mute and global audio settings
remain unchanged. No end-to-end clipping measurement or cabin calibration is
claimed. LuaJIT tests cover live gain, clamp bounds and rejection of non-finite UI
values. The earlier gain-zero readbacks below refer to bridge 0.0.4.

Bridge 0.0.4 adds a reversible, session-only native engine mute for the GT86.
It uses CSP `ac.CarAudioTweak.getVolume/setVolume` with `EngineExt` and `EngineInt`
only. Contract: [official SDK](https://github.com/ac-custom-shaders-patch/acc-lua-sdk/blob/7cf60f5a406820ee4e39f3a62ed121fa36274001/common/ac_car_control.lua).

Start `scripts/start_runtime.ps1`, launch the GT86, open **AC SoundSim Bridge**.
The test is enabled by default each session but only takes effect with the normal
Engine-Sim runtime running and a valid/playing CSP stream. Look for
`Native engine: MUTED EngineExt / EngineInt (gain=0 / 0)`.
**Restore native engine** returns the captured original gains. **Mute native
engine for test** enables it again. This setting is not persisted.

Muting the SoundSim source, stopping/stalling the runtime, pause/replay or app
release restores the captured native gains. Unsupported API or unavailable gains
leave native audio unchanged. Another script changing a muted gain wins: the test
disables rather than repeatedly fighting it, and only still-owned zero gains are
restored. If car identity changed, old gains are discarded rather than written to
the wrong car. Restart the session if another audio app/config interferes.

No bank, GUID, car configuration or global sound setting is edited. Backfire,
transmission, tyres, wind, turbo and other separate events are not touched. This
is gain-zero suppression, NOT evidence of stopped/bypassed FMOD processing or CPU
savings. It is the listening-test subset of M4, not final production ownership.

LuaJIT tests check non-default gain restoration, pause, stale producer, UI toggles,
SoundSim mute, conflict handling and release; other event IDs are forbidden by
the mock setter. Native CSP gain/readback validation is recorded separately from
subjective listening and timbre calibration.

Actual installed CSP/GT86 validation (2026-10-02): source valid=true/playing=true,
native readback EngineExt=0 and EngineInt=0, runtime running and no lastError.
The user entered driving during the test; observed RPM changed to about 1495,
with requested/crank matching and zero producer-late blocks in that snapshot.
The session was left open for user listening, with a manually launched normal
runtime (no time limit, stop with `scripts/stop_runtime.ps1`). No subjective sound
assessment is inferred from gain readbacks. The old **PCM Probe** window is a
historical feasibility tool, not the current bridge status.
