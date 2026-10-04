# polyhedral

Polyhedral dice for Resolume Arena/Avenue, as two FFGL plugins from one core:
`SW Polyhedral` (`PY01`, source: dice on a table, or on nothing but their shadows) and
`SW Dice Over` (`PY02`, effect: the same over the clip, which can also be the
faces' texture). C++/GLSL, CMake MODULE → two universal `.bundle`s (macOS) +
Windows `.dll`s (built by CI, MSVC). MIT. Bundle ids `com.stoatworks.ffgl.polyhedral` and
`com.stoatworks.ffgl.polyhedral.over`.

Read `AGENTS.md` before touching the planner, the physics, the symmetry choice,
the label layout or the data texture's rows.

## Commands (CMake)
- Configure: `cmake -B build -DCMAKE_BUILD_TYPE=Release`
- Fast dev build: add `-DCMAKE_OSX_ARCHITECTURES=arm64`
- Build: `cmake --build build`
- Install to Resolume: `cmake --install build` (never from `~/Projects`)
- One throw to rest: `./build/polytest --out /tmp/polyhedral.png`
- The effect on the harness's card: `./build/polytest --over --out /tmp/o.png`
- List parameters: `./build/polytest --list` (`--over` for the effect's)
- Set anything by name, text and file parameters too:
  `./build/polytest --set "Die=1" --set "Count=3" --set "Font Name=Georgia"`
- Press Roll on frame N: `--roll N` (repeatable; default frame 0). `--frames N`
  renders exactly N frames instead of one throw to rest.
- Film: `./build/polytest --film 300 --size 1280x720 --roll 10 --set "Table=1" | ffmpeg -f rawvideo -pix_fmt rgba -s 1280x720 -r 60 -i - -c:v libx264 -pix_fmt yuv420p dice.mp4`
- A clip through the effect: `ffmpeg -i in.mov -f rawvideo -pix_fmt rgba - | ./build/polytest --over --pipe --size WxH | ffmpeg …`
  A cue line is `frame  Parameter Name  value` (`#` starts a comment), in the same
  units as `--set`. Values interpolate linearly between a name's cues and hold
  before the first and after the last, so a step needs two cues a frame apart and
  a button press is three (0, 1, 0). Frame *n* is clocked at n / 60 s. An unknown
  name exits 2 before any frame; a partial frame at EOF ends the stream with exit
  0; a reader that hangs up ends `--pipe`/`--film` with exit 1 (SIGPIPE is
  ignored), never a silent 141.
- Debug a readback: `DITEST_DUMP=/tmp/d.png DITEST_DUMP_WANT=17 ./build/polytest --readback`
  writes the slot's pixels as red = ink seen, green = the wanted number, blue =
  the rival it disputes worst.

## Verify
- Everything: `tools/verify.sh` (~1.5 min: reserved words, shaders through
  glslc, fresh universal build, both bundles through lipo/plist/codesign/oxbow
  probe+selftest, every check, `--offline`, the `--pipe` format, the negative
  controls, the mutants, the sweep, the bench)
- **The result**: `--outcome` (every value of every die, Fixed totals, Random),
  `--readback` (the number on top read out of the pixels, built-in face and
  Georgia), `--silhouette` (R and R·S cover the same pixels, two rasters),
  `--symmetry`, `--labels`, `--uniform`.
- **The throw**: `--rest`, `--duration`, `--physics`, `--determinism`,
  `--geometry` (volume and inertia against a Monte Carlo of the planes).
- **The plugin**: `--fonts`, `--defaults`, `--names`, `--data` (shader rows vs
  C++ rows, read back through the GPU), `--over-check`, `--state`, `--resize`.
- **The checks can fail**: `--negative` (10 wrong models), `tools/mutate.sh`
  (two GLSL and four C++ one-character mutants).
- What CI runs: `--offline` (the checks that need no GL context, and their
  negative controls; says loudly that the pixel checks were not run) and
  `tools/glslc.sh`.
- No dead controls: `python3 tools/sweep.py` (43 parameters, both plugins).
- Cost: `--bench` (720p/1080p/4K for 1, 6 and 12 dice; the planner's ms).

## Notes
- **Units are metres, kilograms, seconds.** A real set's sizes: a 16 mm d6, a
  d20 20 mm across its faces. Every host parameter is 0..1 (Count, Fixed Total
  and Seed are real integers) and mapped in `Controls.cpp`.
- **A die is drawn at `R(t) · S`.** `R(t)` is the simulated rotation, `S` the
  symmetry that puts the wanted item on top. Anything that reads "the face on
  top" must use the RENDERED rotation (`Plan::RenderRotation`), never `R` alone.
- **The planner runs on a worker** (`std::async`); a roll starts on the frame
  its plan is ready. The harness plans on the render thread
  (`SetSynchronousForTest`). An abandoned plan's future is kept until it
  finishes: a `std::async` future's destructor blocks.
- **The data texture's rows are written twice**: `DataRow` in `Dice.h` and the
  `ROW_` constants in `Shaders.cpp`. `--data` checks they agree.
- **`noise1`–`noise4` are GLSL built-ins**: a user function called `noise3`
  is shadowed by the built-in (which returns vec3), and only Apple's driver
  said so. `verify.sh` greps for them with the reserved words.
- **The camera basis must be right-handed as a view** (right × up = toward the
  viewer). The other way round mirrors the picture, and only the numbers show it.
- **Arena addresses parameters by name**, lower case, spaces removed: names
  must be unique that way (`--names`) and must not contain `/` (it is the OSC
  separator), hence "Mark 6 and 9".
- Option parameters arrive as the element's value; `OptionIndex()` clamps.
- Override `SetTextParameter` to return FF_SUCCESS for the About block, or no
  host can instantiate the plugin.
- `polyhedral_core` is an OBJECT library: the registrations are file-scope
  constructors nothing references.
- Randomness is PCG integer hashing, never `fract(sin(...))`; draws are
  Lemire's unbiased multiply-shift.
- Public at `stoatworks-labs/polyhedral`, released v0.1.0 on 2026-10-04. A
  `v*` tag runs `release.yml` (both platforms, the Windows installer, the
  release); `workflow_dispatch` on a branch builds the same without publishing.
- `polytest --set` takes an option by name or index and refuses anything else;
  until v0.1.0 a name went through `strtof` and silently became 0.

## Browser demo
- `demo/` is https://polyhedral-demo.stoatworks-labs.com (Worker
  `polyhedral-demo`, a ROUTE on a proxied AAAA 100:: record -- the zone's custom
  domains are full; deleting the record takes the page dark with a green
  deploy). `deploy.yml` redeploys it on a push to main; by hand:
  `cf-run npx wrangler deploy`.
- It runs the plugin's C++ (Dice.cpp and everything it calls, plus the FFGL
  SDK's plugin/param/shader classes) as `demo/polyhedral-core.wasm`, COMMITTED.
  **After changing anything under `source/`, run `demo/tools/build-wasm.sh`**
  (emscripten); verify.sh fails until you do (`demo/wasm/inputs.sha256`).
- `demo/shaders.js` is GENERATED: `python3 demo/tools/check_shaders.py --write`.
- The page's own parts: `demo/wasm/glue.cpp` (the host), `demo/wasm/gl_shim.cpp`
  (glShaderSource, PROGRAM_POINT_SIZE), `demo/plugin.js` (panel, units, files).
- `demo/vendor/` is the shared kit: never edit it, re-vendor with
  `stoatworks-backend/resolume-demo/sync.sh`.

## Not done yet
- Never loaded into Resolume on macOS. On Windows, gated in Arena 7.27.1 on
  software rendering only (plugin-bench `arena/expect/polyhedral.json`). No
  OpenFX port, no presets.
- `StoatworksAbout.h`, `ATTRIBUTIONS.md` and the issue forms are generated by
  stoatworks-backend's sync scripts: never edit them here.

## Diagnostics

`source/Diag.{h,cpp}` is a log file only, with no crash handler (this runs
inside Resolume). It records the GL vendor/renderer, a shader that failed to
compile, the font in use (and a named font that is not installed), a Texture
File that will not load, and every roll: results, throws (cocked, unsettled),
natural time, warp, planning ms.

    ~/Library/Logs/polyhedral/polyhedral.YYYY-MM-DD.log
