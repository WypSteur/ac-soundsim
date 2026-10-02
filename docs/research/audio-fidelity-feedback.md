# Audio fidelity — first public evidence inventory, 2026-10-02

Scope: official public Engine-Sim core integrated here; distinguish author
statements, source facts and subjective community reports. Not an exhaustive
Discord/Patreon survey or a community consensus. New private/Steam features are
not assumed present in the public pin. No private content accessed.

| Evidence | Attribution / age | What it supports | Project action |
|---|---|---|---|
| [Creator explanation](https://www.reddit.com/r/gamedev/comments/wkmu9l/simulating_an_entire_car_engine_to_make_realistic/) | AngeTheGreat_ reply, 2022 | Exhaust pressure pulses, convolution for resonances/reverb, noise for turbulence | Separate upstream exhaust coloring from listener/cabin transfer; no claim of hidden intake bus |
| [Creator interview](https://www.motortrend.com/features/engine-noise-simulator-video-ange-yaghi-interview) | Ange Yaghi interview, 2022-10-06 | Goal: roughly capture important effects, not complete scientific accuracy | Application match does not prove real FA20 audio fidelity |
| [Official public README](https://github.com/ange-yaghi/engine-sim) | Project documentation | Audio-oriented simulation, not an engineering/tuning validation tool | Real recording comparison still required; don't use script values as engineering truth |
| [BeamNG integration thread, post74](https://www.beamng.com/threads/engine-simulator-by-ange-yaghi.87487/page-4) | redpocl comment preserved in tobias95ng quote, 2023-02-08 | Subjective impression of mic at exhaust across views; proposes propagation filtering | Relevant hypothesis for our user's interior complaint, NOT verified facts about BeamNG or proof of our diagnosis |
| [Weak sound issue315](https://github.com/ange-yaghi/engine-sim/issues/315) | Report and Jckf response, 2022 | Report of low output; response attributes attenuation to exhaust length | Track source/headroom separately from gain/propagation; not proof of same fault in our port |
| [Engine brake issue308](https://github.com/ange-yaghi/engine-sim/issues/308) | Community report, 2022 | Subjective deceleration sound / braking criticism; no verified resolution here | Add overrun/load transition listening case; don't change engine physics based on opinion |
| [Project FAQ](https://github.com/ange-yaghi/engine-sim/wiki/Frequently-Asked-Questions) | Community-maintained project wiki, edited2023 | CPU/simulation frequency tradeoffs and possible unstable/jerky output | Distinguish timbre problems from deadlines/transport; avoid treating lower simulation frequency as free quality improvement |

Our user's feedback: app-like rendering accepted perceptually; weak level then
improved output requested; interior sounds too directly exposed to exhaust/engine
bay; preserve all good complementary FMOD/mod sounds. That leads to cabin transfer,
spatial listening and own headroom checks, not replacing everything with simulation.

Current public code facts (see architecture3.2): exhaust-system inputs filtered
individually then summed into mono; our FA20 one exhaust bus. Intake gas simulation
does not expose a dedicated acoustic output; mechanical sounds need further work
or appropriate existing native/sample layers. Whole EngineInt/Ext mute can remove
desired layers embedded in those same events: vehicle/mod compatibility is not
universal. No new family-specific source is invented from duplicated mono EQ.

Next evidence required: identified stock GT86 exterior recordings plus interior
recording conditions/RPM/load, and fresh CSP A/B/endpoint recording. Do not EQ-match
the mixed desktop stock-vs-Milltek comparison blindly. See
`../cabin-spatial-calibration.md` for implementation, measurements and acceptance.
