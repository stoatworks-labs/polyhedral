#pragma once

#include <cstdint>
#include <string>
#include <vector>

/**
    Fonts: finding them, and turning one into the ten digits a die needs.

    The scan and the loader are downpour's (MIT, same house), unchanged in what
    they do: walk the OS font directories reading only each file's `name`
    table, list one entry per family, load a chosen file through stb_truetype.
    What is new is the atlas.

    ## A signed-distance atlas, not a bitmap

    A number on a die is seen at every size and every slant -- small in a wide
    shot, filling the frame in a close one, foreshortened to a sliver on a face
    turning away. A bitmap glyph is right at one size. A signed-distance field
    stores, per texel, how far that texel is from the glyph's outline, and a
    bilinear sample of it is still a good distance at any magnification, so the
    shader cuts a crisp edge at the pixel footprint it is actually drawn at.
    `stbtt_GetCodepointSDF` builds it from the outline directly.

    The atlas is the digits 0-9, one 128-px cell each, scaled so the union of
    their ink fills a common height `H` (88 px). Layout is in units of `H`:
    a digit's ink sits in 0..1 vertically whatever the font's own metrics,
    which is what lets old-style figures (a 3 that descends) share a face with
    lining ones without the number drifting off centre.

    ## The built-in face

    Stroked digits defined here as polylines, with an exact distance field
    computed on the CPU. It is what a composition gets when the font it names
    is not installed on this machine, so the numbers never vanish -- and it is
    the default, because it is the same everywhere.
*/
namespace dice
{
struct FontFile
{
	std::string family;
	std::string path;
	int collectionIndex = 0;
};

/// Every font in the OS font directories, one per family, sorted. Scanned once
/// per process.
const std::vector< FontFile >& InstalledFonts();

/// Index of the font whose family matches (case-insensitive), or -1.
int FindFontByFamily( const std::string& family );

/// Font list values below this are positions, as v0.1.0 saved them: 0
/// the built-in face, 1 the first installed font. No name's value is below
/// it, so an old composition's number cannot be mistaken for a name's.
constexpr uint32_t kPositionValues = 1u << 16;

/// What the Font list gives installed font `font` as its element value, which
/// is what a host saves. A position in this machine's list is another family
/// on the next, so the value comes from the name: the same family gets the
/// same value whatever else is installed. 0 for -1, the built-in face.
float FontValue( int font );
/// The installed font whose FontValue is `value`, or -1: the built-in face
/// (0), or a composition saved with a family this machine has not got. A
/// value below kPositionValues is read as v0.1.0 read it, as a place in
/// this machine's list, clamped to its end.
int FindFontByValue( float value );
/// FontValue's arithmetic for any list of names in list order, exposed for
/// the harness. Knockout's: FNV-1a over the lowered name folded to 24 bits,
/// so a float holds it exactly; one that lands below kPositionValues moves up
/// by it. A name whose value is taken moves up by one, so only two colliding
/// names depend on order (a few hundred families give a pair about one
/// machine in 300).
std::vector< uint32_t > FontValues( const std::vector< std::string >& names );

struct DigitAtlas
{
	static constexpr int kCell    = 128;
	static constexpr int kColumns = 5;
	static constexpr int kWidth   = kCell * kColumns;
	static constexpr int kHeight  = kCell * 2;
	static constexpr float kOnEdge = 128.0f;///< the byte value on the outline
	static constexpr float kSpread = 12.0f; ///< px of distance either side the bytes cover

	struct Glyph
	{
		float originX = 0, originY = 0;   ///< atlas px (y up) of the glyph's local origin
		float x0 = 0, y0 = 0, x1 = 0, y1 = 0;///< the cell's usable rectangle, atlas px
		float advance = 0.7f;             ///< H units
		float inkX0 = 0.0f, inkX1 = 0.6f; ///< H units, from the pen
	};

	std::vector< uint8_t > pixels;///< kWidth x kHeight, row 0 at the bottom
	Glyph glyph[ 10 ];
	float pxPerUnit = 88.0f;      ///< atlas px per H
	bool builtin    = true;
	std::string family;           ///< empty for the built-in face

	/// Signed distance in H units at local point (x, y) of digit d: positive
	/// inside. Bilinear, exactly as the shader samples it -- the harness's
	/// readback uses this to know what a number should look like.
	float Distance( int d, float x, float y ) const;
};

class Typeface
{
public:
	bool Load( const std::string& path, int collectionIndex = 0 );
	void UseBuiltin();
	bool HasFont() const
	{
		return loaded;
	}
	const std::string& Family() const
	{
		return family;
	}

	/// The ten digits from this face, or from the built-in face for any digit
	/// it does not have (and for all ten when nothing is loaded).
	DigitAtlas Build() const;

private:
	std::vector< unsigned char > data;
	std::string family;
	bool loaded = false;
	/// stbtt_fontinfo, kept opaque so stb_truetype.h stays out of this header.
	std::vector< unsigned char > info;
};

/// The built-in stroked digits as polylines in H units, for the atlas and for
/// the harness. Each digit is a list of strokes; each stroke a list of points.
const std::vector< std::vector< std::pair< float, float > > >& BuiltinStrokes( int digit );
constexpr float kBuiltinHalfWidth = 0.075f;///< H units
constexpr float kBuiltinAdvance   = 0.74f;

} // namespace dice
