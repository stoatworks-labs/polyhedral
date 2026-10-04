# AGENTS.md — polyhedral

The why. `CLAUDE.md` is the command reference; `README.md` is for people who
will use the plugin. This file is for whoever changes it next.

## The idea

Allan asked (2026-10-04) for a plugin that rolls polyhedral dice: the usual
D&D set, a choice of font and face texture (or a wireframe), a roll duration,
and a result that is either predefined or random.

Two easy answers were both wrong. **Animate a canned tumble and swap in the
right number at the end**: the number visibly changes, or the tumble never
varies. **Simulate a throw and report whatever lands**: the result cannot be
chosen, and "random" is only as fair as the initial conditions.

What the plugin does instead rests on one fact: **every die in the set is
face-transitive.** Its rotation group acts transitively on the faces (on the
d4, on the vertices), so for any wanted face *t* and any face *l* the physics
happened to leave on top, there is a rotation `S` of the solid onto itself with
`S n_t = n_l`. Drawing the die as `R(t) · S` instead of `R(t)` changes nothing
about the solid's occupancy of space — same outline, same contacts, same
shadow, every frame — and puts face *t* where face *l* was. The physics is
honest and complete; the result is chosen afterwards and painted on.

`--silhouette` is the proof that this is not a trick of the numbers: R and R·S
render the same pixels (at most one differs, from a supersample on an edge
landing within a float rounding of it).

## Shape of the code

    Maths.h       V3, M3, quaternions, PCG hashing, Lemire's unbiased draw
    Geometry.*    the solids (built from vertices: hull, faces, edges, inertia,
                  rotation group), real-set numbering, label slots
    Physics.*     rigid bodies: vertex-plane and edge-edge contacts, sequential
                  impulses, split-impulse correction, energy guard, sleep
    Roll.*        the planner: draw results, throw, simulate, reject, snap,
                  choose S, the playback time map
    Typeface.*    font scan (downpour's), the digit SDF atlas, the built-in face
    Labels.*      laying a number out in a slot (shared with the harness)
    Picture.*     the Texture File through stb_image
    Shaders.*     one fragment pass: ray/polytope slabs, numbers, materials
    Dice.*        the plugin: parameters, clock, camera + arena, uploads, draw
    tools/polytest  the harness

## Decisions

- **Name and ids** (Allan's, 2026-10-04): the project is **polyhedral**;
  the source is `SW Polyhedral` / `PY01`, the effect `SW Dice Over` / `PY02`,
  because "SW Polyhedral Over" is 18 characters and FFGL names stop at 16. The
  two names cover both searches in Resolume's browser ("polyhedral", "dice").
  Bundles are `Polyhedral.bundle` and `Polyhedral Over.bundle`. It was built as
  "dice" (`DI01`/`DI02`) and renamed before its first release. No fleet id
  collides (checked against every `CFFGLPluginInfo` in `~/Projects/resolume`).
  The C++ namespace stays `dice`: it names the domain, not the product.
- **A source and an effect.** The effect exists for two things the source
  cannot do: the dice over the clip in one layer, and **Texture: Clip**, the
  clip on every face. Its Texture list has one more element than the source's.
- **Real units.** A 16 mm d6 under real gravity. Size frames the die (the camera
  backs off), so the physics never changes with the framing.
- **Walls are the frame's edges** (the table footprint of the view, pulled in by
  a margin that allows for a die's height at the camera's tilt), restitution
  0.8: lively, so a throw uses the frame. Only a close-up too tight for even
  one die (3.5 circumradii) has its walls moved out, and then the whole throw is
  translated along the enlarged axes so the dice rest mid-frame.
- **Several dice widen the view.** With more than one body the camera backs off
  until the table in shot holds a square of ceil(sqrt(n)) of them with room to
  land. Before this, six pointy d4s at the default Size never settled flat in
  eight throws and fell back to the resting row (wider than the table), and
  three d20s at a moderate Size left one out of shot. Size is therefore a
  request for several dice; for one die it is exact.
- **Fonts**: downpour's scan and loader, a signed-distance atlas instead of a
  bitmap (a number is seen at every size and slant), a built-in stroked face
  as the default (the same everywhere). The family name travels in Font Name
  and wins when a composition restores both it and the index (see below).
- **No presets, no audio trigger, no total readout** in 0.1.0. All three are
  obvious next steps; none is needed for the request.
- **`StoatworksAbout.h` and `ATTRIBUTIONS.md` are provisional hand copies**
  (`guide = ""`, so three About buttons), as graticule's were.

## Meeting Roll Time, honestly

Real dice are quick. A 20 mm die thrown across a frame-sized table is still in
**0.3–0.7 s** however hard it is thrown: most of the energy goes in the first
impacts, and the motion is fast until nine tenths of the way (measured: the
last 5 cm/s is within 0.05 s of rest). Livelier walls, lower friction and less
rolling damping were all tried and move it by tenths. So:

- The planner throws, measures when the last die settled, and rescales the
  throw speed toward Roll Time (capped at 2 m/s: faster crosses the frame in
  under a tenth of a second), with a fresh sub-seed each try.
- Playback maps playback time onto simulated time so the last die stops at
  exactly Roll Time. Shorter than the throw: uniformly fast. Longer: slow
  motion, with a **gentle** ease (the end rate at least 65% of the start).
  **A hard ease was tried first and was wrong**: with the end at 15% of the
  start, the last tumbles froze and the dice sat still for a second before Roll
  Time. The throw has no long tail to stretch.
- The settle time is **quantised to a keyframe** (240 Hz). Between keys the
  last frames before Roll Time interpolated two copies of the final pose and
  `--duration`'s "still moving a frame before" failed; settling on a key fixed it.
- The default Roll Time is 1.2 s (about 0.5× slow motion).

## Rejected throws

A throw is thrown again (fresh sub-seed) if, at rest:

- a die is more than **1°** off flat (resting on another die or against a
  wall: a cocked die, rerolled as at a table), or not on the table;
- after snapping flat, two dice overlap by more than 0.1 mm (separating axes);
- anything is still moving after 14 s.

And, softly, for the first four tries (two with more than three dice): a die
resting within 1.5 reaches of a wall that is the frame's edge, i.e. cut by it.
In a close-up, dice that would not fit in shot. Eight tries; then the best;
then, if nothing settled at all, the resting layout (counted by `--outcome`
and `--rest` as "resting layouts" — zero in every run).

**The snap** turns a die flat about its bottom face's centre (not its centre
of mass: that slides the top sideways into a neighbour — measured 0.9 mm on a
d4), over its last 0.15 s, then slides it back inside any wall it crossed.

## The traps, in the order they cost time

**A left-handed camera mirrors everything, and a die hides it.** The first
basis had right × up = forward. Every die looked right — polyhedra are
symmetric — and every number read backwards. Only the digits gave it away. The
camera is now on the +z side looking at the origin, right = +x, up = right ×
forward, and screen order is what the arena's corners follow.

**`noise3` is a GLSL built-in.** So are `noise1`, `noise2` and `noise4` (they
return vectors). A user `float noise3( vec3 )` is shadowed, and Apple's
compiler says only "Incompatible types in initialization" at the line that
uses it — while **glslc accepted the shader**. The helper is `valueNoise`, and
verify.sh's reserved-word grep now looks for the noise built-ins and for
function declarations too.

**Vertices are not enough for die against die.** Two dice meet edge to edge
with neither's vertex inside the other; with vertex (and edge-midpoint) probes
only, d4s came to rest **0.9 mm** into each other. Edge-edge contacts (closest
points of each edge pair, each inside the other die, pushed apart along the
edges' common normal) fixed it; the insphere test remains as a backstop.

**A check that cannot run cannot fail.** The energy check first went through
the planner. Under its negative control (restitution 1.3, no guard) no throw
ever settled, the planner fell back to the resting layout, zero steps were
simulated, and "no solve added energy" passed. It now drives `physics::World`
directly and requires steps > 0 and contacts > 0.

**The readback's first targets were resting layouts.** At the largest Size the
arena was smaller than a die, every throw failed, and the plan fell back to
placing the dice at rest with the wanted face up — which passes a readback by
construction. The arena is now enlarged for close-ups, and `--readback`
requires a throw.

**Reading a serif number out of a picture.** A luminance threshold misses a
Georgia hairline (a pixel wide, mostly grey); pure coverage counts every
shaded paper pixel as faint ink. The readback takes ink as a ramp from 85% of
the face's own brightness (its 90th percentile) down to 25%, and judges the
wanted number against each rival only on pixels where the two disagree **by
more than a pixel**: "30" and "80" centre differently by a fraction of a pixel,
and their antialiased outline slivers otherwise outvote the stroke that
actually differs. One pixel is the tolerance because it is what antialiasing
touches.

**A side effect looked like a live control.** Line Width widened the
supersampled region on every texture, so the sweep found it live without the
Wireframe context. It now acts in Wireframe only; emptying the sweep's context
table proves it (it goes dead).

**A contact shadow with infinite reach.** The Over effect darkened the clip by
about 1% everywhere. The occlusion now ends at three circumradii, and
`--over-check` holds the clip bit-exact beyond five.

**downpour's font dedupe picks the wrong face.** Within a family the first path
won, and "Georgia Bold Italic.ttf" sorts before "Georgia.ttf" (a space is
lower than a full stop). Here the first face of a collection, then the
shortest file name, wins. downpour still has the bug.

**"Fits in shot" as a circle never fit.** The first close-up rule asked the
dice to rest within 0.45 of the frame's narrow side, which six d20s at the
default size never did, so every roll used all eight throws (34 ms, 90 ms for
twelve). The rule is now the visible rectangle, and several dice share the
frame like a tray instead of enlarging it.

**zsh does not split words.** `./polytest $ARGS` in the Bash tool passes one
argument; the harness then read argv[2] as NULL and crashed. Scripts that build
argument lists go in a bash file.

## Would this hold on another rasteriser, at another raster?

- `--geometry`, `--symmetry`, `--labels`, `--outcome`, `--uniform`, `--rest`,
  `--duration`, `--physics`, `--determinism`, `--defaults`, `--names`: CPU,
  double, no rasteriser. They hold on any IEEE-754 machine. **The trajectories
  are not promised identical across architectures or compilers** (clang may
  fuse multiply-adds on arm64 and not on x86_64; MSVC differs again), so the
  same seed may throw differently on a Windows rig. The RESULTS are identical
  everywhere: they come from the integer PRNG or from the Fixed Total, not
  from the physics.
- `--silhouette`: 320×180 and 640×360. Tolerance two pixels, derived: a
  supersample on an edge can flip when the plane set is rotated in a different
  order (float rounding); measured 0 or 1.
- `--readback`: 640×640 and 1280×1280 only. At 320×180 a d20's number is too
  few pixels to read a serif face; the check's tolerance (one pixel of outline)
  is raster-independent but its signal is not. The 75% agreement threshold is
  a choice; measured 81–92%.
- `--over-check`: 320×180 and 640×360, bit-exact by construction (alpha 0
  leaves the clip as sampled, nearest).
- `--data`: exact, any raster (it is a texel copy).
- `--state`, `--resize` (640×360 → 320×180), `--fonts`: raster-independent.

## Mutation testing

`tools/mutate.sh`, six one-character mutants, each caught by the named check:

| mutant | caught by |
| --- | --- |
| GLSL slab test: entering plane `den < 0.0` → `>` | `--readback` (fits fall to 0.18) |
| GLSL number offset `q + r3.zw` → `-` | `--readback` (every printing wrong) |
| C++ symmetry search `g a - b` → `+` | `--outcome` (the opposite face shows) |
| C++ restitution target sign `-e vn` → `+` | `--physics` (rebound 0) |
| C++ random draw `1 + Below()` → `0 +` | `--outcome` (its Random section: **added because this mutant first survived** — `--outcome` covered Fixed only) |
| C++ eased playback integral `0.5` → `0.6` | `--duration` |

## What is verified, and what is assumed

Verified (numbers in README "Status"): the result shown is the result asked for
(Fixed, every value, every die; Random; Fixed totals over several dice), read
both from the geometry and out of the pixels; S changes no pixel of the
outline; groups, numbering and uniformity; rest, duration, free flight, energy,
restitution; fonts by name, index, file and missing; GL state; the clip
untouched by the effect.

Assumed or chosen, not measured:

- The physical constants: table friction 0.3, die friction 0.3, walls 0.2 and
  restitution 0.8, rolling damping 0.6 s⁻¹, resin density. Chosen to look
  like a felt table; not fitted to filmed dice.
- The d10's proportions (1.1 times as tall as wide) and every die's size.
- The look of every material; the Gem's single refraction.
- That Resolume restores Font Name and Font in the same pause between frames
  (the name-wins logic depends on it). Never seen in Arena.
- That Arena's FFGL clock behaves as boreal measured it (seconds or
  milliseconds, voted on against the wall clock).

## Not done

Never loaded into Resolume; never built on Windows; no presets, no OpenFX
port, no browser demo, no user guide. The release workflows are boreal's,
renamed, and have never run.
