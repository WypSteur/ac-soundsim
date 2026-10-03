# M2 + M3 — AC runtime to Engine-Sim to CSP

Update: bridge 0.0.4 now includes a reversible [native-engine listening test](native-engine-test.md).
The observations below describe the original M2/M3 additive validation, before
that test was added. Native gain-zero testing is not a full FMOD stop/bypass.

Implemented 2026-10-02. M2 was observed in an actual Kunos GT86 session. M3 creates
a valid/playing CSP 3D stream and the transport was verified at the output endpoint
with a separate diagnostic-tone mute/unmute test. This is not a calibrated GT86
sound, a full fly-by validation or a native-engine replacement yet (M4 pending).

## Run

From `ac-soundsim`, with AC closed when installing:

```powershell
.\scripts\build_m1.ps1
.\scripts\install_csp_bridge.ps1 -AssettoRoot 'D:\SteamLibrary\steamapps\common\assettocorsa'
.\scripts\start_runtime.ps1
```

Launch the Kunos GT86 in Content Manager, open **AC SoundSim Bridge**, and check
`Runtime: running`, matching requested/crank RPM, increasing PCM frames and
`CSP event valid / playing: true / true`. Its mute button affects only this test
source. The native engine is deliberately still ON, so levels/timbres overlap.
Start the runtime before or after AC; it retries the state mapping. Starting a
second runtime fails without taking over the active producer. It is manual, not
an automatically installed service, scheduler, startup item or CSP process hook.

Stop gracefully:

```powershell
.\scripts\stop_runtime.ps1
```

`start_runtime.ps1 -Seconds 60` bounds a test. `-DiagnosticTone` explicitly enables
only a 440 Hz transport diagnostic, NOT Engine-Sim. Normal mode is the real FA20
core. Rebuild uses the existing local CMake/VS tools and runs six C++ tests.

The installer preserves the previous bridge under timestamped
`artifacts/bridge-backups/`; no car physics, bank, native audio configuration or
global sound level is changed. To restore a previous bridge, exit AC and copy the
chosen backup contents to `apps/lua/ac_soundsim_bridge`. The development workspace
itself is not currently a Git checkout.

## M2 telemetry ABI

The domain object `RuntimeCarStateV1` is decoded from a separate explicit wire
struct (`ipc::State`), not memcpy'd with its C++ bool padding. ABI is Windows x64,
little-endian, packing 8. C++ static assertions and real LuaJIT FFI layout tests
check sizes/offsets. CSP raw struct bodies match the C++ types exactly.

| Mapping (names without Local prefix in Lua) | Bytes | Sole writer / reader |
|---|---:|---|
| `AcTools.ACSoundSim.State.v1` | 192 | CSP Lua / C++ read-only |
| `AcTools.ACSoundSim.Status.v1` | 368 | C++ / CSP Lua read-only |
| `AcTools.ACSoundSim.Audio.<pid>.<tick>` | 451648 | C++ / native CSP |

State has magic/version/size, seqlock commit, script generation, active/player/
pause/replay flags, reset counter, index, bounded car ID, AC timestamp in seconds,
script-local heartbeat clock, RPM/gas/clutch/boost/gear and world vectors.
`ac.getSim().time` is milliseconds and is divided by 1000. `os.preciseClock()` is
script-local: it is not subtracted from the C++ clock. State age means time since
a new coherent packet arrived in the reader, NOT absolute physics/IPC latency.

The publisher writes odd commit, payload, even commit. Lua publication/read
functions disable JIT and use volatile commit fields, relying explicitly on x64
store ordering. C++ fences around copies and retries torn/odd/zero reads; its
read-only views never use a writing Interlocked operation. This ABI is not a
portable multi-platform lock-free claim. Script generation changes at reload;
reset counter, identity or backward game/script time resets the engine instance.

Latest coherent AC state is held at the 150 Hz render cadence (zero-order hold,
no guessed future RPM, speed controller or second vehicle solution). Skipped
writer sequences usually mean intentional coalescing when CSP runs >150 FPS,
not packet loss. Torn read counts include contention/uncommitted startup packets.
RPM is finite and 0..12000; gas/clutch are 0..1; schema, vectors and car ID are
validated. Only index 0, player, connected `ks_toyota_gt86` can render the FA20.

New states stop arriving -> silence after 250 ms. Read views are periodically
closed/reopened after stale writers so a later AC session can create a fresh map.
Pause freezes crank/gas, still publishing silence. Resume ramps gain over 50 ms.
Replays are explicitly muted pending a proper replay-time adapter. A simulation
fault latches mute until reset/new generation and is reported, not silently retried.

## M3 audio

See `research/csp-stream-format.md` for the recovered official Mumble producer
contract and pinned source links. Output is mono float32 44100 Hz. Each render is
147 physics/acoustic steps, 294 output frames, 6.667 ms. Native CSP reads the PCM,
not a Windows playback backend; the runtime does not open an output device.

The Lua app creates an AudioEvent with 3D, occlusion, reverb response, Doppler=1,
distance 1..200 m and 120/240-degree cones/outside volume .6. Source uses world
position from `car.bodyTransform` at provisional model offset `(0,.35,-2)` m,
rear-facing direction, car up and velocity in m/s. The offset needs calibration.
Conservative test volume=.65, interior multiplier=.25, exterior/track=1.

A torn status read holds the last valid snapshot for at most 300 ms; it does not
dispose/recreate the event immediately. Paused/wrong-car/fault/replay states,
producer timeout or invalid stream contract dispose it. Producer restarts use
new audio names; retained status views are safely reused under a producer owner
mutex. The supported stop event signals only SoundSim, not AC or another app.

The original producer scheduler used steady time and scoped1ms timer resolution.
It is superseded by a high-resolution waitable timer + scoped audio-thread MMCSS
after the native stuttering regression; see runtime-cadence.md for measured proof.
Missed blocks are counted as `producerLateBlocks` and filled with silence rather
than rendering a burst of old engine states. This degrades phase/time continuity
on overload; it is logged, not claimed perfect. Small misses now retain the original
time grid instead of introducing an extra idle period. No consumer cursor/fill/underrun
metric is invented. Logging and engine recreation can cost time outside normal
`render()`; no hard realtime guarantee is made.

## Evidence

Six C++ tests pass in Release, Debug and MSVC ASAN RelWithDebInfo. The integration
test starts a separate real runtime process and drives its Windows state mapping,
reads the actual generated PCM mapping, checks header/ring/finite nonzero content,
exact RPM authority, pause-phase freeze, reset, stale, wrong car, NaN, replay,
generation transition and graceful stop. It is not a mocked FMOD consumer test.
Debug can miss producer deadlines: its passing functional test is not a realtime
performance approval. The PCM assertion scans one second to allow intentionally
silent late blocks. The initial last-block-only Debug assertion was corrected.

`scripts/test_bridge.ps1` additionally passes using dev-only `lupa==2.6` LuaJIT21
with real FFI and a mocked CSP host. It checks Lua/C++ offsets, packet publication,
world pose and event lifecycle, including torn-status hold and heartbeat expiry.
The local Lupa dependency is not shipped by, or needed in, the mod.

Local GT86 / EK Tsubaki Line session observations with CSP preview445/3978:

- actual external RPM about 700, requested/effective matching;
- source valid=true / playing=true, increasing PCM frames;
- source world pose around `(312.4,129.2,-1632.3)` m, finite stationary velocity;
- normal render around 2.7–4 ms; max seen 6.7404 ms in initial session;
- zero producer-late blocks and engine faults during the observed Release runs;
- Escape pause froze phase (`8.38292` rad in a later run) with renderMs=0;
- producer restart reattached a new stream while AC remained open;
- final subjective sound fidelity and dynamic pedal/fly-by tests are still pending.

Final installed-bridge smoke test also passed: real Engine-Sim source valid/playing,
matching 700.001 RPM, camera distance 3.310 m, no lastError, no producer late blocks
or faults, and maxRenderMs=4.0728. The report was written outside the watched app
folder without the prototype reload noise. Snapshots are preserved in
`artifacts/m2-m3/final-bridge-snapshot.txt` and `final-csp-session.log`.
The test AC session and all SoundSim helper processes were stopped afterwards.

`getDSPMetering` returned zeros even for the successfully output diagnostic tone.
To avoid treating that as proof of silence, a read-only WASAPI loopback meter was
built (`soundsim-loopback-meter.exe`). It uses the default render endpoint for
two seconds, prints scalar RMS/peak/440 Hz amplitude, never opens the microphone,
never writes an audio recording and never uploads audio. It is NOT an alternate
SoundSim output backend. Other apps share this endpoint: only the matching
SoundSim toggle comparison is relevant. Based on [Microsoft's loopback API](https://learn.microsoft.com/en-us/windows/win32/coreaudio/loopback-recording).

Measured endpoint: 96000 Hz, stereo, roughly 192000 frames per check:

| Source state | Measured amplitude at 440 Hz |
|---|---:|
| Tone enabled | 0.001925137131 |
| SoundSim source muted | 0.0002596371523 |
| Tone re-enabled | 0.001806649378 |

This is an output-path A/B observation (~7× tone amplitude), not a human listening
report, not pure isolated engine audio, not a latency/underrun measurement and not
a Doppler calibration. The diagnostic was stopped and normal Engine-Sim restored.

## Logs and remaining work

C++: `logs/runtime/soundsim-runtime-*.log` (schema, sequence, age, RPM/phase, frames,
render times, resets, contention/coalescing, faults and producer deadline misses).
CSP: `[ACSoundSim]` in the debug app, plus latest snapshot at
`Documents/Assetto Corsa/logs/ac_soundsim_bridge.txt`. Default `ac.log()` messages
are not necessarily written to `custom_shaders_patch.log`; native stream creation
and release are written there. Reports are outside the watched app folder to avoid
file-watcher reload noise. Prototype `runtime_report.txt` in the installed app is
historical, not the current report.

Still pending: native continuous-engine mute (M4), isolated listening/timbre/level
calibration, actual dynamic RPM and fly-by/Doppler/distance/occlusion/reverb quality
and latency/buffer behavior under load (M5). No multi-car, load/boost inference,
new intake/mechanical bus or muffler work was introduced. Throttle is pedal gas,
not a calibrated physical load/idle-control signal; M1 acoustics remain provisional.
