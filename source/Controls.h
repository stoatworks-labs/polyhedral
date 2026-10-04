#pragma once

/**
    The host's parameters, and what they mean in physical units.

    Every ranged parameter the host sees is 0..1, because `SetParamInfo` clamps
    an `FF_TYPE_STANDARD` default into 0..1 before `SetParamRange` could widen
    it. The conversions live in Controls.cpp, one function per control. Option,
    boolean, event and integer parameters hold the value itself.

    Units: metres, seconds, degrees.
*/
namespace dice
{
/**
    Parameter ids. **Append only**: FFGL's ABI is by index, and other hosts than
    Arena may store compositions that way.

    The Over group sits AFTER the About block, on purpose: the source declares
    ids 0 .. PT_SOURCE_COUNT-1 and the effect all of them, so both share every id
    they have in common and the About static_assert holds for both.
*/
enum ParamId : unsigned int
{
	// -- Roll ----------------------------------------------------------------
	PT_DIE = 0,
	PT_COUNT,
	PT_ROLL,
	PT_ROLL_TIME,
	PT_RESULT,
	PT_FIXED_TOTAL,
	PT_SEED,
	PT_AUTO_ROLL,
	PT_INTERVAL,
	PT_THROW,
	PT_SPIN,
	PT_BOUNCE,

	// -- Dice ----------------------------------------------------------------
	PT_TEXTURE,
	PT_TEXTURE_FILE,
	PT_COLOUR_R,
	PT_COLOUR_G,
	PT_COLOUR_B,
	PT_SECOND_R,
	PT_SECOND_G,
	PT_SECOND_B,
	PT_GLOSS,
	PT_EDGE,
	PT_LINE_WIDTH,

	// -- Numbers -------------------------------------------------------------
	PT_FONT,
	PT_FONT_FILE,
	PT_FONT_NAME,
	PT_NUMBER_SIZE,
	PT_WEIGHT,
	PT_INK_R,
	PT_INK_G,
	PT_INK_B,
	PT_NUMBER_STYLE,
	PT_MARK,
	PT_D6_FACES,

	// -- Scene ---------------------------------------------------------------
	PT_SIZE,
	PT_CAMERA_ANGLE,
	PT_LIGHT_ANGLE,
	PT_SHADOW,
	PT_TABLE,
	PT_TABLE_R,
	PT_TABLE_G,
	PT_TABLE_B,

	// -- The Stoatworks About block --------------------------------------------
	PT_ABOUT_TEXT,
	PT_ABOUT_BUTTON_1,
	PT_ABOUT_BUTTON_2,
	PT_ABOUT_BUTTON_3,
	PT_ABOUT_BUTTON_4,
	PT_SOURCE_COUNT,

	// -- Over (the effect only) ------------------------------------------------
	PT_MIX = PT_SOURCE_COUNT,

	PT_COUNT_ALL
};

enum class Result
{
	Random = 0,
	Fixed,
	Count
};

/// What the faces are made of. `Clip` exists on the effect only (it is the
/// input), so it is last and the source declares one element fewer.
enum class Texture
{
	Plastic = 0,
	Marble,
	Pearl,
	Metal,
	Gem,
	Stone,
	Wood,
	Galaxy,
	Image,
	Wireframe,
	Clip,
	Count
};

enum class NumberStyle
{
	Painted = 0,
	Engraved,
	Count
};

enum class Mark
{
	None = 0,
	Dot,
	Underline,
	Count
};

enum class D6Faces
{
	Numbers = 0,
	Pips,
	Count
};

enum class Table
{
	None = 0,///< transparent on the source; the clip, on the effect
	Felt,
	Wood,
	Count
};

constexpr int kMaxCount  = 6;  ///< dice (pairs, for the d100)
constexpr int kMaxBodies = 12; ///< six d100 pairs
constexpr int kMaxTotal  = 600;

//---------------------------------------------------------------------------
// The mappings.
//---------------------------------------------------------------------------

/// Seconds from release to rest, 0.4 to 8, geometric.
double RollTimeFromParam( float v );
float ParamFromRollTime( double seconds );
/// Seconds between automatic rolls, 1 to 60, geometric.
double IntervalFromParam( float v );
float ParamFromInterval( double seconds );
/// The table's coefficient of restitution, 0.1 to 0.8.
double BounceFromParam( float v );
float ParamFromBounce( double e );
/// 0..1, the throw's spin as a fraction of 55 rad/s.
double SpinFromParam( float v );

/// Specular exponent-like gloss, 0..1 (linear).
float GlossFromParam( float v );
/// Edge rounding as a fraction of the die's inradius, 0 to 0.3.
float EdgeFromParam( float v );
/// Wireframe line width as a fraction of the die's inradius, 0.01 to 0.15.
float LineWidthFromParam( float v );

/// The number's height relative to the room on its face, 0.3 to 1.1.
float NumberSizeFromParam( float v );
/// Glyph dilation in em, -0.06 (thin) to +0.06 (bold).
float WeightFromParam( float v );

/// The die's height as a fraction of the frame's height, 0.06 to 0.6, geometric.
double SizeFromParam( float v );
float ParamFromSize( double fraction );
/// The camera's elevation above the table, 30 (low) to 90 (straight down) degrees.
double CameraAngleFromParam( float v );
/// The key light's azimuth, -180 to 180 degrees.
double LightAngleFromParam( float v );

/// Option parameters arrive as the element's value; the count is how many
/// elements, and anything outside rounds to the nearest end.
int OptionIndex( float value, int count );

} // namespace dice
