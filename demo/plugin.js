/**
 * Polyhedral — browser demo.
 *
 * Polyhedral dice thrown onto a table (SW Polyhedral, PY01, a source) or over
 * the clip (SW Dice Over, PY02, an effect). The one idea, from AGENTS.md: every
 * die is face-transitive, so the plugin throws the dice honestly, reads which
 * face the physics left on top, and draws each die as R(t)·S, where S is the
 * rotation of the solid onto itself that carries the wanted face onto that
 * one. Every frame of the outline, every bounce and shadow is the physics'; only
 * the paint has turned.
 *
 * ---------------------------------------------------------------------------
 * What runs here, and what does not
 * ---------------------------------------------------------------------------
 *
 * **This page does not port the plugin. It runs it.** `polyhedral-core.wasm`
 * is the plugin's own C++ compiled UNMODIFIED with emscripten
 * (demo/tools/build-wasm.sh): `source/Dice.cpp` — the plugin class, with its
 * constructor, its clock and seconds-or-milliseconds vote, Roll and Auto Roll,
 * the request it makes, font resolution, the uploads and every uniform it sets
 * — and everything it calls (Scene, Roll, Physics, Geometry, Labels, Typeface,
 * Picture, Controls, Shaders, Diag), with the FFGL SDK's CFFGLPlugin, its
 * parameter bookkeeping, FFGLShader and FFGLScreenQuad. Its GL calls go to this
 * page's WebGL2 context through emscripten's GL library.
 *
 * What is the page's and not the plugin's:
 *
 *   - **The host.** demo/wasm/glue.cpp constructs the plugin (what the two
 *     registration files do in a bundle), reads its parameter declarations
 *     back through the SDK's host getters — the panel below is built from
 *     them, not from a list typed here — and forwards SetFloatParameter,
 *     SetTextParameter, SetTime and ProcessOpenGL, as tools/polytest's Rig does.
 *   - **Four GL entry points** (demo/wasm/gl_shim.cpp). glShaderSource: the
 *     plugin hands GL desktop GLSL 4.10; `shaderSource()` below refuses any
 *     text that is not byte-for-byte this page's copy (shaders.js, held to
 *     source/Shaders.cpp by demo/tools/check_shaders.py), then applies the kit's
 *     `port()` — the version line and the ES precision defaults, nothing else.
 *     glEnable / glDisable / glIsEnabled: GL_PROGRAM_POINT_SIZE, which the
 *     plugin saves and restores every frame, does not exist in WebGL2.
 *   - **Planning on the page's thread.** The plugin plans a throw with
 *     std::async; this module has no threads, so glue.cpp calls the plugin's
 *     own SetSynchronousForTest(true), as its harness does.
 *   - **The panel's read-outs.** The number beside a slider is Controls.cpp's
 *     conversion, compiled in (glue.cpp's poly_convert pairs each control with
 *     the function Dice.cpp applies to it); the units printed after it, and
 *     every tooltip, are this page's words.
 *   - **Files.** Texture File and Font File are paths in the plugin; here a
 *     file you pick is written into emscripten's in-memory file system and the
 *     path handed to the plugin, which opens it with its own loaders.
 *
 * ---------------------------------------------------------------------------
 * Decisions this page made, and why
 * ---------------------------------------------------------------------------
 *
 * **Both plugins, one page**, as conway's demo does it: the Plugin dropdown is
 * a new instance at that constructor's defaults. The effect declares one more
 * Texture element (Clip) and Mix; Texture is two rows of the same name and only
 * the chosen plugin's is shown. `?plugin=over` opens the effect.
 *
 * **Kit types for FFGL types.** The kit has no event, integer or file control.
 * Roll (FF_TYPE_EVENT) is a button that sends the rising edge and the release,
 * 1 then 0, as the harness's Press() does. Count, Fixed Total and Seed
 * (FF_TYPE_INTEGER) are number fields clamped to the declared range. The two
 * file parameters show their path with a picker beside it. Font Name is sent
 * when the field is committed (Enter, or leaving it), as a host sends text.
 *
 * **The About block is not on the panel**, as on every page in the suite.
 */

import { mountDemo } from './vendor/demo.js';
import { port, GLError } from './vendor/gl.js';
import * as S from './shaders.js';
import createPolyhedral from './polyhedral-core.js';

//===========================================================================
// FFGL.h's parameter types, by number: what GetParamType returns.
//===========================================================================

const FF_TYPE_BOOLEAN = 0;
const FF_TYPE_EVENT = 1;
const FF_TYPE_RED = 2;
const FF_TYPE_GREEN = 3;
const FF_TYPE_BLUE = 4;
const FF_TYPE_STANDARD = 10;
const FF_TYPE_OPTION = 11;
const FF_TYPE_INTEGER = 13;
const FF_TYPE_FILE = 14;
const FF_TYPE_TEXT = 100;

//===========================================================================
// The GLSL: the page's checked copy, assembled as InitGL assembles the
// plugin's (kVersion + kQuadVertex; Assemble( kDice, kMaterial, kFragment ),
// which is kVersion + kCommon + the three).
//===========================================================================

const PAGE_SHADERS = {
  vertex: S.VERSION + S.QUAD_VERTEX,
  fragment: S.VERSION + S.COMMON + S.DICE + S.MATERIAL + S.FRAGMENT,
};

const shaderCheck = { vertex: null, fragment: null };

/**
 * Called by gl_shim.cpp's glShaderSource with the text the compiled plugin
 * handed to GL. Anything but this page's own copy is refused (an empty source,
 * so the plugin's InitGL fails and says so) rather than compiled.
 */
function shaderSource(stage, text) {
  const ours = PAGE_SHADERS[stage];
  if (text !== ours) {
    shaderCheck[stage] = false;
    return '';
  }
  shaderCheck[stage] = true;
  return port(ours, { stage });
}

//===========================================================================
// The module.
//===========================================================================

const printed = [];
let core;
try {
  core = await createPolyhedral({
    polyhedralShaderSource: shaderSource,
    print: (text) => { printed.push(text); console.log(text); },
  });
} catch (error) {
  const root = document.querySelector('#demo');
  root.textContent = `The demo could not start: this browser would not run its WebAssembly (${error.message}). The plugin, its source and its downloads are at https://github.com/stoatworks-labs/polyhedral.`;
  throw error;
}

const text = (pointer) => core.UTF8ToString(pointer);

/** A C string for one call: allocated, handed over, freed. */
function withCString(value, fn) {
  const pointer = core.stringToNewUTF8(value);
  try {
    return fn(pointer);
  } finally {
    core._free(pointer);
  }
}

//===========================================================================
// The declarations, read from each plugin's constructor through the SDK.
//===========================================================================

function declarations(effect) {
  const h = core._poly_new(effect ? 1 : 0);
  const list = [];
  const n = core._poly_param_count(h);
  for (let i = 0; i < n; i += 1) {
    const type = core._poly_param_type(h, i);
    const d = { index: i, name: text(core._poly_param_name(h, i)), type, group: text(core._poly_param_group(h, i)) };
    if (type === FF_TYPE_TEXT || type === FF_TYPE_FILE) d.default = text(core._poly_param_default_text(h, i));
    else d.default = core._poly_param_default(h, i);
    if (type === FF_TYPE_OPTION) {
      d.elements = [];
      d.values = [];
      for (let e = 0; e < core._poly_param_element_count(h, i); e += 1) {
        d.elements.push(text(core._poly_param_element_name(h, i, e)));
        d.values.push(core._poly_param_element_value(h, i, e));
      }
    }
    if (type === FF_TYPE_INTEGER) {
      d.min = core._poly_param_range_min(h, i);
      d.max = core._poly_param_range_max(h, i);
    }
    if (type === FF_TYPE_FILE) {
      d.extensions = [];
      for (let e = 0; e < core._poly_param_extension_count(h, i); e += 1) d.extensions.push(text(core._poly_param_extension(h, i, e)));
    }
    list.push(d);
  }
  const maxInputs = core._poly_max_inputs(h);
  core._poly_delete(h);
  return { list, maxInputs };
}

const DECLARED = { source: declarations(false), over: declarations(true) };

//===========================================================================
// The read-outs: Controls.cpp's number, this page's unit.
//===========================================================================

const fixed = (n, d) => n.toFixed(d);
const UNITS = {
  'Roll Time': (x) => `${fixed(x, 2)} s`,
  Interval: (x) => `${fixed(x, x < 10 ? 1 : 0)} s`,
  Spin: (x) => fixed(x, 2),
  Bounce: (x) => `e ${fixed(x, 2)}`,
  Gloss: (x) => fixed(x, 2),
  Edge: (x) => `${fixed(x, 3)} r`,
  'Line Width': (x) => `${fixed(x, 3)} r`,
  'Number Size': (x) => `× ${fixed(x, 2)}`,
  Weight: (x) => `${x >= 0 ? '+' : '−'}${fixed(Math.abs(x), 3)} H`,
  Size: (x) => `${fixed(100 * x, 1)}% h`,
  'Camera Angle': (x) => `${fixed(x, 0)}°`,
  'Light Angle': (x) => `${fixed(x, 0)}°`,
};

function readout(d) {
  return (v) => {
    const x = core._poly_convert(d.index, Math.fround(v));
    if (Number.isNaN(x)) return fixed(v, 2);
    return (UNITS[d.name] ?? ((y) => fixed(y, 3)))(x);
  };
}

/** Tooltips: this page's words, from the README's description of each control. */
const HINTS = {
  Die: 'Which die: D4, D6, D8, D10, D12, D20, or D100 (a tens die and a units die thrown together). Changing it lays the dice out at rest.',
  Count: 'How many dice are thrown together, 1–6 (pairs, for the d100). FF_TYPE_INTEGER in the plugin.',
  Roll: 'Throw. The dice tumble, bounce off the frame’s edges and each other, and come to rest on the result at exactly Roll Time. FF_TYPE_EVENT: a press, then a release.',
  'Roll Time': 'Seconds from release to rest, 0.4 to 8, geometric. Real dice settle in under a second; a longer Roll Time is slow motion.',
  Result: 'Random: each die uniform, from an integer PRNG. Fixed: the dice show Fixed Total.',
  'Fixed Total': 'The sum the dice show when Result is Fixed, split uniformly over every combination that makes it, and clamped to what the dice can make. FF_TYPE_INTEGER, 1–600.',
  Seed: 'The same Seed replays the same sequence of throws. FF_TYPE_INTEGER, 0–9999.',
  'Auto Roll': 'Roll every Interval. Switching it on rolls now.',
  Interval: 'Seconds between automatic rolls, 1 to 60, geometric.',
  Throw: 'Where the dice come in from: over the Left, Right, Bottom or Top edge of the frame, Drop from above, or Any.',
  Spin: 'How much spin the throw puts on the dice.',
  Bounce: 'The table’s restitution, 0.1 to 0.8.',
  Texture: 'Plastic, Marble, Pearl, Metal, Gem (refracts, and shows the far faces’ numbers through the near ones), Stone, Wood, Galaxy, Image (the Texture File on every face), Wireframe; on SW Dice Over, Clip — the clip itself on every face.',
  'Texture File': 'A picture for the Image texture (PNG, JPEG, BMP, TGA, GIF). The plugin takes a path; here, a file you pick is written into the page’s memory and opened by the plugin’s own loader. Nothing is uploaded.',
  Colour: 'The dice’s colour.',
  'Second Colour': 'Marble’s veins, stone’s flecks, wood’s grain, the galaxy’s nebula.',
  Gloss: 'How glossy the surface is.',
  Edge: 'How rounded the edges look, as a fraction of the die’s inradius r. Shading only: the outline stays the sharp polyhedron.',
  'Line Width': 'The Wireframe’s lines, as a fraction of the die’s inradius r. Wireframe only.',
  Font: 'Built-in, then every font installed on the machine. A web page cannot list this computer’s fonts, and the plugin’s own scan finds none in the page’s memory, so the list here is Built-in alone.',
  'Font File': 'A .ttf, .otf, .ttc or .otc you pick, written into the page’s memory and loaded by the plugin’s own font loader. It wins over Font and Font Name; Font Name then shows its family.',
  'Font Name': 'The family, as text: it travels with a composition, and wins over Font. With no installed fonts here, any name falls back to the built-in face and is kept as typed — the plugin’s own path for a font a machine lacks. Sent on Enter.',
  'Number Size': 'The number’s height relative to the room on its face, 0.3 to 1.1.',
  Weight: 'Thinner or bolder strokes, ±0.06 of the digits’ height H.',
  Ink: 'The numbers’ colour.',
  'Number Style': 'Painted on, or Engraved into the face.',
  'Mark 6 and 9': 'How a 6 and a 9 are told apart on the dice that carry both: None, Dot, Underline.',
  'D6 Faces': 'Numbers, or Pips.',
  Size: 'The die as a share of the frame’s height h. Several dice widen the view as far as they must for all of them to land in shot.',
  'Camera Angle': 'The camera’s elevation above the table: 30° to straight down.',
  'Light Angle': 'Where the key light stands, round the table.',
  Shadow: 'How dark the dice’s shadows are.',
  Table: 'None: transparent, only the shadows, so the layer goes over whatever is under it (on SW Dice Over, the clip). Felt. Wood.',
  'Table Colour': 'The felt’s or the wood’s colour.',
  Mix: 'The effect against the clip. At 0 the clip is returned exactly.',
};

//===========================================================================
// Kit parameters from the declarations. The id is the name as Arena
// addresses it (lower case, spaces removed).
//===========================================================================

const KIT_TYPES = {
  [FF_TYPE_STANDARD]: 'standard',
  [FF_TYPE_OPTION]: 'option',
  [FF_TYPE_BOOLEAN]: 'boolean',
  [FF_TYPE_EVENT]: 'boolean',
  [FF_TYPE_RED]: 'colour',
  [FF_TYPE_GREEN]: 'colour',
  [FF_TYPE_BLUE]: 'colour',
  [FF_TYPE_INTEGER]: 'text',
  [FF_TYPE_FILE]: 'text',
  [FF_TYPE_TEXT]: 'text',
};

/**
 * The kit reads `embed`, `size`, `clip` and `bg` from the query string itself,
 * and this page reads `plugin`; a parameter id equal to one would be set by it.
 * `?size=640` (the raster) set the plugin's Size to its maximum before this.
 */
const RESERVED_QUERY = new Set(['embed', 'size', 'clip', 'bg', 'plugin']);
const addressOf = (name) => {
  const address = name.toLowerCase().replace(/\s+/g, '');
  return RESERVED_QUERY.has(address) ? `param-${address}` : address;
};

function kitDefault(d) {
  switch (d.type) {
    case FF_TYPE_OPTION: {
      const i = d.values.findIndex((v) => v === d.default);
      return i >= 0 ? i : Math.round(d.default);
    }
    case FF_TYPE_INTEGER: return String(Math.round(d.default));
    case FF_TYPE_BOOLEAN:
    case FF_TYPE_EVENT: return d.default > 0.5 ? 1 : 0;
    default: return d.default;
  }
}

function kitParam(d, id, only) {
  const p = { id, name: d.name, type: KIT_TYPES[d.type], group: d.group, hint: HINTS[d.name], ff: d, only };
  if (p.type === undefined) throw new Error(`no kit control for FFGL type ${d.type} (${d.name})`);
  if (d.type === FF_TYPE_OPTION) p.elements = d.elements;
  if (d.type === FF_TYPE_STANDARD) p.display = readout(d);
  return p;
}

const sameDeclaration = (a, b) => JSON.stringify({ ...a, index: 0 }) === JSON.stringify({ ...b, index: 0 });

const PARAMS = [];
for (const d of DECLARED.over.list) {
  if (d.group === 'About') continue;
  const s = DECLARED.source.list.find((x) => x.index === d.index);
  if (s && s.group !== 'About' && sameDeclaration(s, d)) {
    PARAMS.push(kitParam(d, addressOf(d.name)));
  } else {
    if (s) PARAMS.push(kitParam(s, addressOf(s.name), 'source'));
    PARAMS.push(kitParam(d, addressOf(d.name) + (s ? '_over' : ''), 'over'));
  }
}
for (const s of DECLARED.source.list) {
  if (s.group !== 'About' && !DECLARED.over.list.some((d) => d.index === s.index)) PARAMS.push(kitParam(s, addressOf(s.name), 'source'));
}

/** Each constructor's defaults, by kit id. */
const DEFAULTS = { source: {}, over: {} };
for (const p of PARAMS) {
  for (const variant of ['source', 'over']) {
    const d = DECLARED[variant].list.find((x) => x.index === p.ff.index);
    DEFAULTS[variant][p.id] = d && (!p.only || p.only === variant) ? kitDefault(d) : kitDefault(p.ff);
  }
}

const query = new URLSearchParams(window.location.search);
const initialVariant = query.get('plugin') === 'over' ? 'over' : 'source';
for (const p of PARAMS) p.default = DEFAULTS[initialVariant][p.id];

const indexOf = (name) => DECLARED.over.list.find((d) => d.name === name)?.index;
const PT_ROLL = indexOf('Roll');

//===========================================================================
// The renderer: one plugin instance, driven as a host drives it.
//===========================================================================

/** The value the host would send for a kit control, or null for none. */
function hostValue(p, v) {
  const d = p.ff;
  switch (d.type) {
    case FF_TYPE_OPTION: return Math.fround(d.values[Math.round(v)] ?? 0);
    case FF_TYPE_BOOLEAN: return v > 0.5 ? 1 : 0;
    case FF_TYPE_EVENT: return null;
    case FF_TYPE_INTEGER: {
      const n = Number.parseInt(String(v).trim(), 10);
      if (!Number.isFinite(n)) return null;
      return Math.min(d.max, Math.max(d.min, n));
    }
    case FF_TYPE_FILE:
    case FF_TYPE_TEXT: return String(v);
    default: return Math.fround(v);
  }
}

function createRenderer(gl) {
  // emscripten's GL library, pointed at the kit's context. Nothing is turned
  // on behind the kit's back: no extensions are enabled on its behalf.
  const handle = core.GL.registerContext(gl, { majorVersion: 2, minorVersion: 0, enableExtensionsByDefault: false });
  core.GL.makeContextCurrent(handle);
  core._poly_prepare_log();

  let instance = 0;
  let variant = null;
  let pushed = new Map();
  let clipName = 0;
  let clipTexture = null;
  let frames = 0;

  /** A kit texture under a GL name the plugin can bind, in emscripten's tables. */
  function glName(texture) {
    if (texture === clipTexture) return clipName;
    if (clipName) core.GL.textures[clipName] = null;
    clipName = core.GL.getNewId(core.GL.textures);
    core.GL.textures[clipName] = texture;
    texture.name = clipName;
    clipTexture = texture;
    return clipName;
  }

  function start(which, width, height) {
    if (instance) core._poly_delete(instance);
    instance = core._poly_new(which === 'over' ? 1 : 0);
    variant = which;
    pushed = new Map();
    frames = 0;
    if (!core._poly_init_gl(instance, width, height)) {
      const why = shaderCheck.vertex === false || shaderCheck.fragment === false
        ? 'the GLSL the compiled plugin handed to GL is not this page’s checked copy of source/Shaders.cpp, so it was not compiled'
        : `the plugin’s InitGL failed${printed.length ? `: ${printed.slice(-3).join(' ')}` : ''}`;
      throw new GLError(`${which === 'over' ? 'SW Dice Over' : 'SW Polyhedral'} could not start: ${why}.`);
    }
  }

  function push(params) {
    for (const p of PARAMS) {
      if (p.only && p.only !== variant) continue;
      const value = hostValue(p, params.get(p.id));
      if (value === null || pushed.get(p.id) === value) continue;
      pushed.set(p.id, value);
      if (typeof value === 'string') withCString(value, (s) => core._poly_set_text(instance, p.ff.index, s));
      else core._poly_set_float(instance, p.ff.index, value);
    }
  }

  /** What the plugin changed of its own accord: Font Name after a Font File. */
  function readBack(params) {
    for (const p of PARAMS) {
      if (p.ff.type !== FF_TYPE_TEXT || (p.only && p.only !== variant)) continue;
      const now = text(core._poly_get_text(instance, p.ff.index));
      if (now !== pushed.get(p.id)) {
        pushed.set(p.id, now);
        params.set(p.id, now);
      }
    }
  }

  return {
    render({ input, params, width, height, time, variant: wanted }) {
      core.GL.makeContextCurrent(handle);
      if (!instance || wanted !== variant) start(wanted ?? 'source', width, height);
      push(params);
      const clip = variant === 'over' ? glName(input.texture) : 0;
      if (!core._poly_process(instance, time, clip, input.width, input.height)) {
        throw new GLError('The plugin’s ProcessOpenGL failed.');
      }
      readBack(params);
      // For anything checking the page from outside: frames drawn by this
      // instance, and the throws it has started (the harness's counter).
      frames += 1;
      gl.canvas.dataset.frames = String(frames);
      gl.canvas.dataset.rollsStarted = String(core._poly_rolls_started(instance));
    },
    press(index) {
      if (!instance) return;
      core._poly_set_float(instance, index, 1);
      core._poly_set_float(instance, index, 0);
    },
    get rollsStarted() { return instance ? core._poly_rolls_started(instance) : 0; },
    /** The plan the plugin is playing (CurrentPlan, the harness's accessor). */
    plan() {
      if (!instance) return null;
      const dice = core._poly_plan_dice(instance);
      return {
        results: Array.from({ length: dice }, (_, i) => core._poly_plan_result(instance, i)),
        total: core._poly_plan_total(instance),
        trials: core._poly_plan_trials(instance),
        idle: core._poly_plan_idle(instance) === 1,
        natural: core._poly_plan_natural(instance),
        warp: core._poly_plan_warp(instance),
        ms: core._poly_plan_ms(instance),
        finished: core._poly_plan_finished(instance) === 1,
        rolls: core._poly_rolls_started(instance),
      };
    },
    log() {
      try {
        return core.FS.readFile(text(core._poly_log_path()), { encoding: 'utf8' });
      } catch {
        return '';
      }
    },
  };
}

//===========================================================================
// The page.
//===========================================================================

let renderer = null;

const mounted = mountDemo({
  name: 'Polyhedral',
  pluginId: 'PY01 · PY02',
  kind: ['source', 'effect'],
  tagline:
    'Polyhedral dice for Resolume: a d4, d6, d8, d10, d12, d20 or d100 thrown onto the table (SW Polyhedral), or over your clip (SW Dice Over). Press Roll: the dice tumble, bounce off the edges of the frame and each other, and come to rest on a random result — or on the Fixed Total you set — at exactly Roll Time. The throw is real physics; the result is chosen by turning each die by a symmetry of its own solid, which changes no pixel of its outline, its bounces or its shadow.',
  repo: 'https://github.com/stoatworks-labs/polyhedral',
  page: 'https://stoatworks-labs.com/software/polyhedral/',

  blurb:
    'It is the plugin’s own C++ — the plugin class, its physics, planner, numbering and fonts, with the FFGL SDK classes it uses — compiled unmodified to WebAssembly, drawing with its own GLSL in WebGL2. The host is this page: throws are planned on the page’s thread, there are no installed fonts, and SW Dice Over runs on a generated clip.',

  // The source is transparent where there is no table: the backdrop shows it.
  showBackdrop: true,

  variants: {
    label: 'Plugin',
    default: initialVariant,
    options: [
      { id: 'source', name: 'SW Polyhedral (source)', hint: 'PY01, FF_SOURCE: dice on a table, or on nothing but their shadows.' },
      { id: 'over', name: 'SW Dice Over (effect)', hint: 'PY02, FF_EFFECT: the same over the clip, which can also be the faces’ texture.' },
    ],
  },

  // For SW Dice Over only.
  sources: ['scene', 'bars', 'grid', 'alpha', 'spot'],

  params: PARAMS,

  differences: [
    'This page runs the plugin rather than a port of it. polyhedral-core.wasm is source/Dice.cpp — the plugin class: its constructor, clock, Roll and Auto Roll, the request it makes, font resolution, the uploads and every uniform — and everything it calls (Scene, Roll, Physics, Geometry, Labels, Typeface, Picture, Controls, Shaders, Diag), with the FFGL SDK’s CFFGLPlugin, parameter bookkeeping, FFGLShader and FFGLScreenQuad, all compiled unmodified by emscripten (demo/tools/build-wasm.sh). Only the two registration files are left out; the page constructs the plugin itself, as the plugin’s harness does. The panel is read back from the plugin’s own declarations through the SDK’s host getters, and the numbers beside the sliders are Controls.cpp’s conversions, compiled in; their units and every tooltip are this page’s.',
    'The GLSL is the plugin’s: demo/shaders.js is spliced from source/Shaders.cpp and the repository’s verify script fails if a character differs. At start-up the page also requires the text the compiled plugin hands to glShaderSource to equal that copy, and refuses to compile anything else. The kit’s port() then changes the version line and adds the ES precision defaults. GL is emscripten’s WebGL2 library on this page’s context, not a GL 4.1 driver, and four entry points are the page’s (demo/wasm/gl_shim.cpp): glShaderSource, for that check and port, and glEnable, glDisable and glIsEnabled, because GL_PROGRAM_POINT_SIZE — state the plugin saves and restores each frame, as a plugin in Resolume must — does not exist in WebGL2. It reads as off here; the plugin draws no points.',
    'A throw is planned on the page’s thread. The plugin plans on a worker thread (std::async) and starts the roll a frame or two after the press; this WebAssembly has no threads, so the page switches on the plugin’s own synchronous planning (SetSynchronousForTest, the switch its harness uses), and the frame after a press waits for the planner. The plugin’s log line under the picture says how long it took: a few milliseconds for one die, tens for twelve.',
    'No installed fonts. The plugin lists every font on the machine; a web page cannot read them, and the plugin’s own scan of the font folders finds none in the page’s in-memory file system, so Font offers Built-in alone. Font File takes a .ttf, .otf, .ttc or .otc you pick: it is written into that in-memory file system and loaded by the plugin’s own loader through stb_truetype. Font Name looks a family up in the same empty list, so any name falls back to the built-in face and is kept as typed — the plugin’s own path for a composition opened on a machine without its font.',
    'Texture File is a picture you pick, written into the page’s memory and decoded by the plugin’s own loader (stb_image). In Resolume a file parameter is a path on the disk; here the path is in the page’s memory, so Copy link does not carry either file. Nothing you load leaves this page.',
    'The clock is the page’s: seconds from frame deltas, capped at a tenth of a second, handed to SetTime; the plugin’s own vote settles the unit (seconds) within four frames, as in Arena. Pause stops its clock and Step advances it 1/60 s. Restart is a backward jump, which the plugin takes as no time passing: the dice stay where they are.',
    'Compared once with the plugin’s own harness, polytest, on an Apple Silicon Mac when this page was built: the resting layout, and Auto Roll throws of one and of six d20s, three d100 pairs, four gem d4s and two d6s with pips, came to rest where polytest’s did, and those resting frames matched its pixels to within 3/255 (rendered here by SwiftShader, there by the Mac’s GPU). One case did not match exactly: two d6s with a Fixed Total landed in the same places on the same numbers, but with some numbers turned differently on their faces. The plugin builds the d6’s and d10’s tables (which way up a number is printed, the order of the symmetries) through comparisons that clang’s fused multiply-adds round differently on Apple Silicon; WebAssembly has none, and the page agrees bit for bit — plan, poses and tables — with a native build made without them (-ffp-contract=off). Results come from an integer PRNG or from Fixed Total and are the same everywhere; a throw’s path is not promised to be, as the plugin’s AGENTS.md says of any two compilers. That comparison does not run again.',
    'Count, Fixed Total and Seed are FF_TYPE_INTEGER in the plugin and number fields here, clamped to the declared ranges. Roll is FF_TYPE_EVENT; the button sends the press and the release, 1 then 0, as the harness does. The About block (a text and four buttons) is declared by the plugin and left off this panel.',
    'SW Dice Over’s clip is the kit’s generated one, premultiplied, or your own image or video; its whole texture is used (ClipScale 1), where a host may pad it.',
    'Switching the Plugin dropdown is a new instance at that constructor’s defaults, as in Resolume; Texture is shown as two rows of the same name because the effect’s list has one more element, Clip, and the kit’s dropdowns have a fixed list.',
    'The plugin itself has never been loaded into Resolume (its README’s Status says so). Its harness, polytest, renders the same classes headlessly and checks the result shown, read back out of the pixels, against the one asked for; that harness, not this page, is the reason to believe it.',
  ],

  createRenderer: (gl) => {
    renderer = createRenderer(gl);
    return renderer;
  },
});

//===========================================================================
// The controls the kit has no type for, and the plugin's log.
//
// By inline style, not the `hidden` attribute: kit.css gives these elements a
// `display` of their own, which beats the attribute's user-agent rule.
//===========================================================================

if (mounted) {
  const params = mounted.params;
  const embed = query.has('embed') && query.get('embed') !== '0';
  let shownVariant = mounted.state.variant;

  // Copy link carries the plugin, and never a file path (it is in this page's memory).
  const toQuery = params.toQuery.bind(params);
  params.toQuery = () => {
    const q = toQuery();
    for (const p of PARAMS) if (p.ff.type === FF_TYPE_FILE) q.delete(p.id);
    if (mounted.state.variant === 'over') q.set('plugin', 'over');
    return q;
  };

  const show = (node, on) => { if (node) node.style.display = on ? '' : 'none'; };
  const rowOf = (p) => document.getElementById(`p-${p.id}`)?.closest('.prow')
    ?? [...document.querySelectorAll('.prow')].find((row) => row.querySelector('.prow__name')?.textContent === p.name);

  function showForVariant(which) {
    for (const p of PARAMS) if (p.only) show(rowOf(p), p.only === which);
    // A group with nothing of this plugin's in it (the source has no Over).
    for (const group of document.querySelectorAll('.pgroup')) {
      show(group, [...group.querySelectorAll('.prow')].some((row) => row.style.display !== 'none'));
    }
    const effect = which === 'over';
    for (const field of document.querySelectorAll('.transport__field')) {
      if (field.querySelector('.transport__label')?.textContent === 'Clip') show(field, effect);
    }
    show(document.querySelector('.transport__file'), effect);
  }

  document.addEventListener('demo:state', () => {
    const which = mounted.state.variant;
    if (which !== shownVariant) {
      shownVariant = which;
      // A new instance: that constructor's defaults, for Defaults too.
      params.defaults = { ...DEFAULTS[which] };
      params.reset();
    }
    if (!embed) showForVariant(which);
  });

  if (!embed) {
    showForVariant(shownVariant);

    const button = (label, title) => {
      const b = document.createElement('button');
      b.type = 'button';
      b.className = 'btn';
      b.textContent = label;
      if (title) b.title = title;
      return b;
    };

    for (const p of PARAMS) {
      const row = rowOf(p);
      if (!row) continue;

      // FF_TYPE_EVENT: a press and a release.
      if (p.ff.type === FF_TYPE_EVENT) {
        const toggle = row.querySelector('.prow__toggle');
        const press = button(p.name, p.hint);
        press.className = 'prow__toggle';
        press.style.textAlign = 'center';
        press.addEventListener('click', () => {
          renderer?.press(p.ff.index);
          mounted.redraw();
        });
        toggle?.replaceWith(press);
        continue;
      }

      const field = row.querySelector('.prow__text');
      if (!field) continue;

      // FF_TYPE_INTEGER: a number field in the declared range.
      if (p.ff.type === FF_TYPE_INTEGER) {
        field.type = 'number';
        field.min = String(p.ff.min);
        field.max = String(p.ff.max);
        field.step = '1';
        field.inputMode = 'numeric';
        continue;
      }

      // FF_TYPE_TEXT: sent when committed, as a host sends text.
      if (p.ff.type === FF_TYPE_TEXT) {
        field.addEventListener('input', (event) => event.stopImmediatePropagation(), true);
        field.addEventListener('change', () => params.set(p.id, field.value));
        continue;
      }

      // FF_TYPE_FILE: the path, and a picker.
      if (p.ff.type === FF_TYPE_FILE) {
        field.readOnly = true;
        field.placeholder = 'no file';
        const cell = document.createElement('div');
        cell.style.display = 'flex';
        cell.style.gap = '6px';
        cell.style.minWidth = '0';
        field.replaceWith(cell);
        field.style.flex = '1 1 auto';
        field.style.minWidth = '0';
        const picker = document.createElement('input');
        picker.type = 'file';
        picker.accept = p.ff.extensions.map((e) => `.${e}`).join(',');
        picker.style.display = 'none';
        const choose = button('Choose…', `${p.ff.extensions.join(', ')} — read in this page, never uploaded`);
        const clear = button('×', 'No file');
        choose.addEventListener('click', () => picker.click());
        clear.addEventListener('click', () => params.set(p.id, ''));
        picker.addEventListener('change', async () => {
          const file = picker.files?.[0];
          picker.value = '';
          if (!file) return;
          const bytes = new Uint8Array(await file.arrayBuffer());
          const dir = `/files/${Date.now().toString(36)}`;
          core.FS.mkdirTree(dir);
          const path = `${dir}/${file.name.replace(/[/\\]/g, '_')}`;
          core.FS.writeFile(path, bytes);
          params.set(p.id, path);
        });
        cell.append(field, choose, clear, picker);
      }
    }

    //-----------------------------------------------------------------------
    // What the plugin is playing, once the dice are down (never before: the
    // plan knows the result at the press), and the plugin's own log
    // (Diag.cpp: fonts, files). Both report; neither measures.
    //-----------------------------------------------------------------------
    const stage = document.querySelector('.stage');
    if (stage) {
      const status = document.createElement('p');
      status.className = 'stage__status';
      status.id = 'polyhedral-plan';
      status.setAttribute('aria-live', 'polite');
      const log = document.createElement('p');
      log.className = 'stage__status';
      log.id = 'polyhedral-log';
      log.setAttribute('aria-live', 'off');
      log.style.fontFamily = 'ui-monospace, SFMono-Regular, Menlo, monospace';
      log.style.fontSize = '12px';
      log.style.whiteSpace = 'pre-wrap';
      stage.append(status, log);
      const plural = (n, word) => `${n} ${word}${n === 1 ? '' : 's'}`;
      let lastLog = '';
      setInterval(() => {
        const plan = renderer?.plan();
        if (plan) {
          const shown = plan.results.length > 1 ? `${plan.results.join(' + ')} = ${plan.total}` : String(plan.total);
          status.textContent = plan.idle
            ? `At rest before the first roll, laid out showing ${shown}. Press Roll.`
            : !plan.finished
              ? `Throw ${plan.rolls}: rolling…`
              : `Throw ${plan.rolls}: ${shown}. ${plural(plan.trials, 'throw')} simulated to find it; ${plan.natural.toFixed(2)} s of physics played at ${plan.warp.toFixed(2)}× to stop on Roll Time; planned in ${plan.ms.toFixed(1)} ms on this page’s thread.`;
        }
        const text = renderer?.log() ?? '';
        if (text === lastLog) return;
        lastLog = text;
        const lines = text.trim().split('\n').filter(Boolean).map((l) => l.replace(/^\S+ (INFO |WARN |ERROR) polyhedral: /, '$1 '));
        log.textContent = lines.length
          ? `The plugin’s own log (Diag.cpp, in this page’s memory):\n${lines.slice(-2).join('\n')}`
          : '';
      }, 250);
    }
  }
}
