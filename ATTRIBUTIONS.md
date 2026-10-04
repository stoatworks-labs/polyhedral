# Attributions

Polyhedral is built on other people's work. This file lists what that work is, who did
it, and what it is doing here.

It is generated — the master lists live in the `stoatworks-backend` repo and are
pushed out by `scripts/sync-attributions.py`. Edit it there, not here.

## Code we derived from other people's work

Someone else solved this first, and this project would not exist in its current form without their work.

### Source-plus-Over shape, GL state and harness — Stoatworks boreal, downpour and flyback

<https://github.com/stoatworks-labs/boreal>  
Licence: MIT  
Copyright: Stoatworks Labs

One core registered as a source and an Over effect is downpour's shape, as boreal carries it. GLState.h, Diag, the host-clock unit vote and the About block are boreal's; the harness's GL plumbing, PNG writer, parameters by name, --pipe and --film with their cue sheets, tools/verify.sh, tools/sweep.py, tools/mutate.sh, tools/glslc.sh and the negative-control pattern are boreal's and flyback's.

### Font scan, loader and portable font name — Stoatworks downpour

<https://github.com/stoatworks-labs/downpour>  
Licence: MIT  
Copyright: Stoatworks Labs

source/Typeface.cpp's installed-font scan (reading only each file's name table), its loader through stb_truetype, and the idea of keeping the family name beside the dropdown's index so a composition finds its font on another machine, are downpour's. The signed-distance digit atlas, the built-in stroked face and the plain-face-first choice within a family are new here.

## Third-party code this project uses

Libraries, SDKs and frameworks the project is built on or bundles.

### Resolume FFGL SDK

<https://github.com/resolume/ffgl>  
Licence: BSD-3-Clause  
Copyright: FreeFrame

Vendored as a git submodule at external/ffgl (third_party/ffgl in oxbow).

The plugin ABI itself. An FFGL effect or source is defined by this SDK's headers — there is no other way to be loadable by Resolume Arena and Avenue.

### GLEW — the OpenGL Extension Wrangler Library

<https://github.com/nigels-com/glew>  
Licence: BSD-3-Clause (with Mesa 3-D and Khronos components)  
Copyright: Milan Ikits, Marcelo E. Magallon and Lev Povalahev

Arrives inside the FFGL submodule at external/ffgl/deps/glew-2.1.0. Not fetched separately.

Resolves OpenGL entry points on Windows, where the system headers stop at OpenGL 1.1.

### libpng

<http://www.libpng.org/pub/png/libpng.html>  
Licence: PNG Reference Library License (libpng)  
Copyright: the PNG Reference Library authors

Arrives inside the FFGL submodule, under the SDK's CustomThumbnail sample.

Part of the upstream SDK tree rather than something these plugins call directly — listed because it is present in the checkout.

### stb_truetype

<https://github.com/nothings/stb>  
Licence: MIT or Public Domain (Unlicense), at your choice  
Copyright: Sean Barrett

Single header vendored at external/stb/stb_truetype.h.

Rasterises glyphs into the atlas the plugin samples. A whole font stack would be a large dependency for one job this header already does.

### stb_image

<https://github.com/nothings/stb>  
Licence: MIT or Public Domain (dual, at your option)  
Copyright: Sean Barrett

Single-header decoder vendored under external/stb/ and compiled into one translation unit.

Decodes PNG, JPEG and GIF. A sprite-sheet player has to open whatever the operator exported.

## Work we checked ourselves against

No code was taken from these — but they were how we knew we had it right, and that is worth saying out loud.

### Sequential impulses — Erin Catto (Box2D)

<https://box2d.org/publications/>

The contact solver is the sequential-impulse method Catto presented at GDC: accumulated normal impulses clamped at zero, Coulomb friction clamped to the normal impulse, restitution from the approach speed before the solve, and penetration removed with split-impulse pseudo-velocities. Implemented from the method; no Box2D code is used.

### Uniform random rotations — Ken Shoemake, Graphics Gems III (1992)

Each die's starting orientation is a uniformly random rotation by Shoemake's construction from three uniform numbers.

### Fast random integer generation in an interval — Daniel Lemire, ACM Transactions on Modeling and Computer Simulation (2019)

<https://arxiv.org/abs/1805.10941>

A Random result is drawn by Lemire's multiply-shift with rejection, so no face is favoured by modulo bias; polytest --uniform measures it by chi-square.

### Signed distance fields for glyphs — Chris Green, Valve, SIGGRAPH 2007

<https://steamcdn-a.akamaihd.net/apps/valve/2007/SIGGRAPH2007_AlphaTestedMagnification.pdf>

The numbers are drawn from a signed-distance atlas of the ten digits, cut at the pixel footprint they are drawn at, the technique Green described for magnified vector textures. The fields themselves are stb_truetype's.

## Inspirations

What this set out to be. No code, assets or binaries from any of these were used or examined — the debt is to the idea.

### A real set of polyhedral dice

The d4 to d20 and the percentile pair as tabletop role-playing games use them, with their conventions: opposite faces summing to n + 1 (9 on the d10), the western d6's 1-2-3 corner, the d4 read at its top vertex, 6 and 9 marked. Built from the solids and those conventions; nothing is copied from anyone's model or source.

## Getting this wrong

If your work is here and the description is inaccurate, the licence is wrong, or you would rather not be listed — open an issue and it will be fixed.
