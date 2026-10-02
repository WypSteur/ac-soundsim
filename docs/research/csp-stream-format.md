# CSP PCM MMF — recovered producer contract (2026-10-02)

Primary evidence, pinned at `2031bd2d7c913e0aad3ff18a3bab86ffccad1326`:

- [Official Mumble AudioSource.cs](https://github.com/ac-custom-shaders-patch/acc-lua-internal/blob/2031bd2d7c913e0aad3ff18a3bab86ffccad1326/plugins/AcTools.Extra.MumbleClient/UnityEngine/AudioSource.cs): `InitializeStream()`, `PushStream()`, `Dispose()`.
- [Official MumbleWrapper.lua](https://github.com/ac-custom-shaders-patch/acc-lua-internal/blob/2031bd2d7c913e0aad3ff18a3bab86ffccad1326/included-apps/Mumble/src/MumbleWrapper.lua): `_createStream()` connects that mapping to `AudioEvent.fromFile` with 3D/occlusion/cones/Doppler/reverb.
- [Public MMF API](https://github.com/ac-custom-shaders-patch/acc-lua-sdk/blob/7cf60f5a406820ee4e39f3a62ed121fa36274001/common/ac_extras_connectmmf.lua): script names omit the `Local\\` namespace prefix.

This is a recovered companion-producer ABI, not a published/versioned native CSP
specification. It has been accepted by installed CSP preview445/3978 and its output
verified through a 440 Hz mute/unmute loopback measurement. It is not guessed from
arbitrary process memory or FMOD banks. Reserved native fields remain unknown.

## Header: 64 bytes, little-endian, aligned to 8

| Byte offset | Type | Meaning from official producer | SoundSim value |
|---|---|---|---|
| 0 | int32 | Sample rate | 44100 Hz |
| 4 | int32 | Number of channels | 1 (mono) |
| 8 | int32 | FMOD sound format | 5 (`PCMFLOAT`) |
| 12 | int32 | Decode sample hint, rate / 25 | 1764 |
| 16 | int32 | Decode byte hint, rate * sizeof(float) / 25 | 7056 |
| 20 | int32 | Not initialized by companion beyond zeroed mapping | 0 |
| 24 | int64 | Cumulative PCM bytes published | increases by 1176 per block |
| 32–63 | 8 × int32 | Reserved/unknown | initially zero; no interpretation |
| 64 onward | float32[] | PCM ring | 112896 samples |

SoundSim's mapping size is **451648 bytes**, including the header. Ring capacity
is 2.56 seconds; this is NOT asserted playback latency. The decode hint is 40 ms;
it is NOT an end-to-end latency measurement. M1 signed int16 is converted to
normalized float32 by dividing by 32768. No resampling is needed at 44.1 kHz.

Copy PCM at the current ring offset, wrapping at capacity, then publish the
cumulative byte counter with a release/full barrier. The official C# producer
uses `Interlocked.MemoryBarrier()` before the counter write. SoundSim uses aligned
`InterlockedExchange64`. On dispose publish zero. SoundSim does not reset an active
ring/counter on car reset: it publishes silence while resetting the engine. Each
new runtime has a unique audio name, preventing cached event/restart confusion.

## What remains unobservable

No verified consumer cursor, fill level, actual overruns/underruns, native retry
policy or latency negotiation is exposed by these sources. Do not label producer
deadline misses as measured CSP underruns. Do not assign meanings to reserved
fields. `getDSPMetering(0, 'both')` returned four zeros in this installed stream
test even when the 440 Hz signal demonstrably reached the output endpoint.
Its zeros are therefore not an audio-transport failure test here.

The production stream is actual Engine-Sim output. `--diagnostic-tone` is an
explicit, separate protocol diagnostic, never the engine model or default mode.
Fly-by/Doppler/occlusion/reverb quality and latency still require M5 testing.
