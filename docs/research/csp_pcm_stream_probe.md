# CSP live PCM stream probe — confirmed facts

Update 2026-10-02: the producer ABI was subsequently recovered from official CSP
Mumble companion code and implemented in M3. See `csp-stream-format.md` and
`../m2-m3-live.md` for the header/ring, pinned sources and actual output A/B proof.
The unknowns below describe the earlier M0 probe; consumer cursor/fill/underruns
and measured latency remain unknown. The original probe app is historical and
its untested reference-tone status is not the current M3 result.

## Test environment

Observed in user-provided probe logs:

```text
CSP v0.3.0-preview445/3978
```

## Confirmed installed SDK signature

The local CSP SDK contained the following `AudioEvent` parameter contract (excerpted/summarized):

```lua
{
  filename: string,
  stream: {
    name: string,
    size: integer
  },
  group: ac.AudioEvent,
  use3D: boolean,
  useOcclusion: boolean,
  loop: boolean,
  insideConeAngle: number,
  outsideConeAngle: number,
  outsideVolume: number,
  minDistance: number,
  maxDistance: number,
  dopplerEffect: number,
  dsp: ac.AudioDSP[]
}
```

The installed documentation describes `stream` as:

> Audio stream (as an alternative to `filename` for live streaming data using a memory mapped file).

This was present in the locally installed CSP SDK under `extension/internal/lua-sdk`, including `ac_apps/lib.lua` and corresponding generated README documentation.

## Architectural conclusion

The following path is supported at the API level:

```text
external SoundSim producer
        ↓
Windows memory-mapped file
        ↓
CSP live audio stream
        ↓
AudioEvent 3D
        ↓
position / velocity / Doppler / cone / distance / occlusion / DSP
```

This closes the previous question of whether CSP has a first-class streaming source mechanism.

## Still to determine

Do not guess these:

```text
MMF binary layout
sample format
channel count
sample rate semantics
ring/cursor/header structure
producer write protocol
consumer timing expectations
underrun behavior
```

Find the actual protocol in local CSP SDK examples/implementation clues, or derive it with a minimal producer test.

## Suggested protocol spike

Build a tiny C++ producer independent from Engine-Sim:

```text
440 Hz sine
48 kHz or SDK-required rate
continuous sequence
```

Then feed the CSP `stream={name,size}` source.

Success criteria:

```text
audible source
3D position follows GT86
distance attenuation works
track-camera fly-by changes spatial position
Doppler responds to velocity
no recurring underruns
```

Only after this protocol is known should the production `AudioTransport` be frozen.
