#pragma once

#include "Controls.h"
#include "Geometry.h"
#include "Labels.h"
#include "Maths.h"
#include "Roll.h"
#include "Typeface.h"

#include <vector>

/**
    The parts of a frame that are not GL: where the camera is, where the walls
    stand, which way the light falls, and the data texture's contents.

    Kept free of the FFGL SDK on purpose, so the browser demo compiles THIS code
    to WebAssembly rather than carrying a second copy of it: the page's camera,
    walls and numbers are the plugin's, to the bit.
*/
namespace dice
{
/// The data texture's rows: mirrored by the ROW_ constants in Shaders.cpp,
/// and `ditest --data` reads them back through the GPU to prove it.
enum DataRow : int
{
	ROW_PLANES  = 0,
	ROW_VERTS   = 1,
	ROW_EDGE_A  = 2,
	ROW_EDGE_B  = 3,
	ROW_SLOTS   = 4,///< five rows per numbering, two numberings
	ROW_GLYPH   = 14,
	ROW_CELL    = 15,
	ROW_FACE    = 16,
	ROW_FACE_UP = 17,
	kDataRows   = 18,
	kDataWidth  = 64
};

/// What the camera sees, in doubles: for the shader and for the harness's
/// projection of a face onto pixels.
struct Camera
{
	V3 position, right, up, forward;
	double tanHalf     = 0.0;
	double frameHeight = 0.0;///< metres of table spanned by the frame's height at the target
	double aspect      = 16.0 / 9.0;

	/// The world ray through pixel (px, py), y up from the bottom, of a w x h frame.
	V3 Ray( double px, double py, int w, int h ) const;
	/// The pixel a world point lands on.
	bool Project( V3 p, int w, int h, double& px, double& py ) const;
};

constexpr double kHalfFov        = 15.0;///< degrees: a 30-degree lens, a tabletop close-up
constexpr double kLightElevation = 55.0;///< degrees above the table

/// What decides the view: the die (its size frames the shot), how many, the
/// Size control's fraction and the camera's elevation.
struct ViewSettings
{
	geo::DieType die        = geo::DieType::D20;
	int count               = 1;
	double size             = 0.22;///< the die's nominal diameter over the frame's height
	double elevationDegrees = 70.0;
};

/// The camera, and the walls: the frame's footprint on the table, pulled in
/// so a die touching a wall is still wholly in shot.
void BuildView( const ViewSettings& view, int width, int height, Camera& camera, roll::Arena& arena );

/// Toward the key light, world frame, for Light Angle in degrees.
V3 LightDirection( double lightAngleDegrees );

/// The data texture (kDataWidth x kDataRows RGBA floats, row-major) for a die,
/// laying out every number in `layouts` as it goes.
std::vector< float > BuildDataTexture( geo::DieType die, Mark mark, float numberSize, const DigitAtlas& atlas,
                                       std::vector< SlotLayout > layouts[ 2 ] );

} // namespace dice
