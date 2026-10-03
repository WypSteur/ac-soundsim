# Runtime cadence regression — 2026-10-03

User reports completely stuttering/crackling simulated sound after restarting the
M5 runtime. This is a native FAIL, not overridden by successful offline/CI tests.

## Evidence

The live bridge was valid/playing, native EngineInt/Ext correctly muted, FA20
source not on the rails and zero simulation faults. The producer was missing
roughly20–40 blocks/s, inserting silence and repeatedly resetting its gain ramp.
Its common render time remained4.5–5.5ms for a6.667ms block, similar to the
previous accepted source; a41ms peak occurred near session exit. The112k late
counter included idle time and must NOT be treated as112k in-session failures.
During live running, output advanced around37–38k frames/s instead of44100.
These producer deadline misses are not a measured native CSP underrun counter.

Two controlled tests on this Windows11 machine, forcing timer-resolution requests
to be ignored in isolated probe processes only (no AC/MMF output playback):

| Probe | Old Sleep loop | Fixed timer/time grid/MMCSS |
| --- | --- | --- |
| Synthetic4.8ms CPU work,4s |375 rendered blocks,130 missed |600 blocks,0 missed |
| Real FA20 at3000rpm,4s after warmup |37498 published frames/s,93 missed |44149.9 frames/s,0 missed |

The real-runtime frame-rate estimate uses separately sampled counters at20ms,
hence its small boundary uncertainty; it does not change the44100Hz PCM contract.
Both binaries used the SAME corrected ignition/gas/DSP. No listening verdict or
pedal/consumer/ear latency is inferred from these tests.

Windows11 can ignore `timeBeginPeriod` for hidden/inaudible window-owning processes.
Our console is hidden and sends PCM to CSP rather than owning an output device.
The mechanism is reproduced under forced throttling; the original process's
actual power-policy flags were not captured, so this is not proof of their state.
[Microsoft timer behavior](https://learn.microsoft.com/en-us/windows/win32/api/timeapi/nf-timeapi-timebeginperiod),
[process policy](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-setprocessinformation).

## Correction

- Replace `sleep_for` + `timeBeginPeriod` with a high-resolution waitable timer
  (Windows10 1803+), interruptible by the existing SoundSim stop event.
- Keep the original150Hz time grid after small missed deadlines. The previous
  `next=after+period` added extra idle time, worsening persistent underproduction.
- Register only the actively rendering thread with scoped MMCSS `Pro Audio`;
  release it on pause/wait/stale and exit. No process-wide realtime class, CPU
  affinity, system power plan, registry or FMOD configuration changes.
- Keep bounded silence recovery for true overload; maximum150 blocks on long
  suspend, no unbounded catch-up. Silence recovery is still audible degradation,
  not a claim of perfect crank continuity or a substitute for native validation.

[High-resolution waitable timer](https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-createwaitabletimerexw),
[MMCSS](https://learn.microsoft.com/en-us/windows/win32/procthread/multimedia-class-scheduler-service).

Source mechanics, AC RPM authority, corrected ignition, gas simulation, DSP/IR,
gains and bridge0.0.10 are unchanged. Native listening must be repeated.

## Tests / reproduction

`audio_scheduler` tests deadline/grid recovery, bounded long suspend and an
interruptible real high-resolution timer. Release/Debug runtime integration
still checks actual FA20 MMF, pause/resume/reset/stale/replay/fault/stop behavior.
Hardware performance probes are diagnostic, NOT CI realtime acceptance gates.

```powershell
.\build\Release\soundsim-scheduler-test.exe --probe legacy
.\build\Release\soundsim-scheduler-test.exe --probe precise
.\build\Release\soundsim-ipc-test.exe --cadence .\build\Release\soundsim-runtime.exe
```

The probe forces timer-resolution throttling only on its disposable process or
its uniquely named child runtime. It never writes the normal AC state channel,
opens an output device, captures audio or changes another program's settings.

M5A native sound: FAIL observed on old scheduler; fixed build pending user retest.
M5D cadence/robustness: FAIL observed; fixed scheduler locally measured, native
load/latency/fallback acceptance still pending. GT86 reference gate remains OPEN.
