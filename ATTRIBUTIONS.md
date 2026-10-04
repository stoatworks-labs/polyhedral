# Attributions

Polyhedral is built on other people's work. This file lists what that work is, who did
it, and what it is doing here.

It is meant to be generated — the master lists live in the `stoatworks-backend` repo
and are pushed out by `scripts/sync-attributions.py`. Polyhedral is not registered there
yet, so this is a provisional hand copy; the first sync after registration replaces it.

## Third-party code this project uses

Libraries, SDKs and frameworks the project is built on or bundles.

### Resolume FFGL SDK

<https://github.com/resolume/ffgl>  
Licence: BSD-3-Clause  
Copyright: FreeFrame

Vendored as a git submodule at external/ffgl.

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

Turns the chosen font's outlines into the signed-distance field the numbers are drawn from (`stbtt_GetGlyphSDF`), and reads the font files the Font list is built from.

### stb_image

<https://github.com/nothings/stb>  
Licence: MIT or Public Domain (Unlicense), at your choice  
Copyright: Sean Barrett

Single header vendored at external/stb/stb_image.h.

Decodes the Texture File (PNG, JPEG, BMP, TGA, GIF's first frame) for the faces.

## Fonts

No font is bundled. The Font list is the fonts installed on the machine the plugin
runs on, read where they are; the built-in face is stroked digits defined in this
repository's own source (`source/Typeface.cpp`).

## Getting this wrong

If your work is here and the description is inaccurate, the licence is wrong, or you would rather not be listed — open an issue and it will be fixed.
