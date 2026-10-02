# Third-party notices

The headless Engine-Sim integration uses these MIT-licensed sources:

- [Engine-Sim](https://github.com/ange-yaghi/engine-sim), copyright 2022
  AngeTheGreat (Ange Yaghi), revision `85f7c3b959a908ed5232ede4f1a4ac7eafe6b630`.
  Full notice: `licenses/engine-sim.LICENSE`.
- [Simple 2D Constraint Solver](https://github.com/ange-yaghi/simple-2d-constraint-solver),
  copyright 2022 Ange Yaghi, revision `e009f4ff1c9c4c5874e865e893cdb62e208fb2b3`.
  Full notice: `licenses/simple-2d-constraint-solver.LICENSE`.

Keep these notices with distributions containing the corresponding code or
binaries. Local modifications are preserved as a patch in
`integration/engine_sim/patches/`. Historical M1 used an identity impulse response.
The current full FA20D preset loads upstream `smooth_39.wav` from the separately
bootstrapped public source checkout; this asset is not copied into this Git
repository. Upstream application, scripting and video dependencies are not built.
The user's Community Edition installation, engine script and real-car reference
recordings are not redistributed. Assetto Corsa, CSP and native
FMOD banks are not redistributed by this project.

The M3 PCM ring/header producer is based on the official CSP Mumble companion
implementation in `ac-custom-shaders-patch/acc-lua-internal`, revision
`2031bd2d7c913e0aad3ff18a3bab86ffccad1326`, copyright 2022 Ilja Jusupov, MIT.
Its notice is preserved in `licenses/csp-lua-internal.LICENSE`. The SDK clone
under gitignored `vendor/csp-lua-sdk` is research-only and not linked/shipped.
Lupa 2.6 is a local LuaJIT test dependency under `tools/build-runtime`, not a
runtime dependency. Retain its bundled notices if distributing those tools.

CMake 3.31.10 is a gitignored local build tool under `tools/build-runtime`, installed
from its Python package. It is not part of the resulting runtime executable. Keep
its bundled license notices if distributing that build-tool directory.
