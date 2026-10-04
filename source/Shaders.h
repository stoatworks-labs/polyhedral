#pragma once

#include <string>

/**
    The GLSL, as text.

    One pass. Every pixel casts a ray from the camera, intersects each die
    ANALYTICALLY -- a die is the intersection of its face planes, so a ray
    enters at the latest entering plane and leaves at the earliest leaving one
    (a slab test, as for a box, with up to twenty slabs) -- and the plane it
    entered by is the face it hit. No mesh, no depth buffer: the same planes
    the physics collides with are the ones drawn, read from one data texture.

    Pieces, assembled as kVersion + kCommon + the rest (see Assemble):

      kCommon     hashing, noise, the data texture's layout, the environment
      kDice       intersection, the numbers (SDF atlas), shadows
      kMaterial   the textures, lighting, the gem's refraction, the wireframe
      kFragment   per pixel: supersampled near a die, composited, gamma

    Each piece is kept under MSVC's ~16 KB string-literal cap; tools/glslc.sh
    reassembles them in this order.

    **The data texture's layout** is written down twice -- the ROW_ constants
    here and DataRow in Dice.cpp -- and `ditest --data` checks they agree by
    reading values back through a probe shader.
*/
namespace dice::shaders
{
extern const char* const kVersion;
extern const char* const kCommon;
extern const char* const kDice;
extern const char* const kMaterial;
extern const char* const kQuadVertex;
extern const char* const kFragment;

std::string Assemble( const char* a, const char* b = nullptr, const char* c = nullptr );

} // namespace dice::shaders
