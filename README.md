# AC SoundSim

## Clone / development setup

Windows x64, Visual Studio 2022 C++ build tools, Git, Python x64 (3.13 tested).
Downloaded dependencies, builds, generated audio, runtime logs and backups are
deliberately excluded from Git. Bootstrap retrieves the exact public Engine-Sim
revision/submodule and reapplies the tracked headless patch.

```powershell
# Optional pinned CMake + LuaJIT test tools (or supply CMake separately):
python -m pip install --target tools/build-runtime -r requirements-dev.txt
.\scripts\build_m1.ps1
.\scripts\test_bridge.ps1
```

See `CODEX_HANDOFF.md` before continuing development. The latest user-approved
listening base is bridge0.0.10; the separate intake audio bus is the next proposed
feature, not implemented yet.

Development repository scaffold for a data-driven Assetto Corsa engine sound simulator based on the open-source Engine-Sim core.

## First target

- Car: `ks_toyota_gt86`
- Engine: FA20 / 4U-GSE
- Architecture goal: Assetto Corsa owns physics/RPM; SoundSim generates engine acoustics; CSP owns 3D propagation.

## Current milestone: M2/M3 — live IPC and CSP PCM stream

The GT86 CSP bridge's identity and dynamic RPM/throttle/gear display were confirmed
by the user on 2026-10-02. M1 now builds the actual Engine-Sim acoustic core with
externally imposed crank/piston kinematics and synchronous PCM rendering.

```powershell
.\scripts\build_m1.ps1 -GenerateAudio
```

This builds, runs seven tests and generates an `800 -> 7400 -> 800 RPM` recording
under `artifacts/m1/`. See `docs/m1-headless.md` for provenance, architecture,
acceptance evidence and provisional FA20 acoustic parameters.

M2 now reads live GT86 telemetry over a versioned Windows MMF. M3 publishes real
FA20 PCM using the recovered official CSP Mumble stream contract, with a 3D Lua
AudioEvent. Live RPM/crank agreement, source valid/playing, pause and restart were
observed locally. A diagnostic-tone mute/unmute measurement confirms the output
transport; listening/fly-by calibration remains pending. A reversible session-only
native EngineInt/EngineExt mute is now available in the bridge; see
`docs/native-engine-test.md`. Bridge 0.0.5 adds a live SoundSim gain slider and
removes the provisional 0.25 cabin multiplier after a barely-audible user report.
See `docs/m2-m3-live.md` and `docs/research/csp-stream-format.md`.

The normal runtime now uses an explicit port of the user's `Subaru_FA20D.mr`
reference, including public Engine-Sim convolution/noise/jitter and the matching
public impulse response. Bridge 0.0.6 adds **Listen in 2D (audit)** to isolate CSP
spatial effects. Three comparison WAVs and metrics can be generated with
`build/Release/soundsim-audio-audit.exe --output artifacts/audio-audit`.
See `docs/acoustic-audit.md` for measured levels, version boundaries and omissions;
this is not a bit-identical Community Edition app or validated real-world FA20.
Stop and use `scripts/start_runtime.ps1 -LegacyModel` for the old source.

Bridge **0.0.10** adds a separate adjustable cabin transfer and no-restart A/B,
live exterior cone/distance/tailpipe controls, read-only producer peak/RMS meter,
and an explicit optional own peak guard (default on, -1dBFS, no makeup).
Gain8 stays available; the guard can reduce peaks/dynamics. Old FMOD parameter
units and an isolated native DSP graph were tested, but native CSP ordering,
global mix headroom and real GT86 calibration still need listening.
See `docs/cabin-spatial-calibration.md`. Source/core/IR and complementary FMOD
events remain unchanged. The following describes historical **0.0.9**:

Bridge 0.0.9 follows an **output-only hybrid mix**: match our SoundSim output to
the original FMOD/mod mix using the single `SoundSim gain` control (default8,
range .1..12; doubled from the user's observed gain4). High gain can distort
before sufficient 3D attenuation; lower it for 2D/close cameras if needed.
Only continuous EngineInt/EngineExt are replaced; all complementary effects and
their configured levels remain untouched. The earlier per-FX trim experiment was
removed. See `docs/hybrid-mix-policy.md` for scope, headroom and mod compatibility.

```powershell
.\scripts\install_csp_bridge.ps1 -AssettoRoot 'D:\SteamLibrary\steamapps\common\assettocorsa'
.\scripts\start_runtime.ps1
# Launch the GT86 and open AC SoundSim Bridge.
# When finished:
.\scripts\stop_runtime.ps1
```

Installation requires AC closed and backs up the previous bridge. The runtime
starts hidden, is manually controlled, and is not a system service/startup item.
Logs: `logs/runtime/` and `Documents/Assetto Corsa/logs/ac_soundsim_bridge.txt`.

## M0 baseline

The original scaffold established:

1. versioned runtime/profile interfaces;
2. persistent C++ logging;
3. GT86/FA20 profiles;
4. CSP bridge diagnostics with `[ACSoundSim]` logs;
5. isolated upstream bootstrap script;
6. smoke-testable CMake scaffold.

## Build the C++ scaffold

```powershell
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Run `soundsim-poc`. A timestamped log is written under `logs/` relative to the executable working directory.

## Install the CSP bridge

```powershell
.\scripts\install_csp_bridge.ps1 -AssettoRoot "D:\SteamLibrary\steamapps\common\assettocorsa"
```

Start Assetto Corsa with the Kunos GT86 and open **AC SoundSim Bridge**. It should display:

- `Detected: ks_toyota_gt86`
- live RPM
- live throttle
- live gear

CSP log location:

```text
Documents\Assetto Corsa\logs\custom_shaders_patch.log
```

Native stream creation/release is logged there. Tagged bridge snapshots are in
`Documents/Assetto Corsa/logs/ac_soundsim_bridge.txt` and CSP's debug app:

```text
[ACSoundSim]
```

## Bring in Engine-Sim upstream

```powershell
.\scripts\bootstrap_engine_sim.ps1
```

Upstream remains under `vendor/engine-sim/` and is gitignored. Bootstrap pins the
audited revision, initializes the minimal SCS submodule and applies the persisted
headless patch. It preserves existing work and refuses incompatible revisions.
Use `-DACSOUNDSIM_ENGINE_SIM=OFF` to build only the baseline without upstream.

## Architecture

See `docs/architecture.md` and `docs/adr/`.
