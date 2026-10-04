#pragma once

#include "Controls.h"
#include "Geometry.h"
#include "Typeface.h"

#include <string>

/**
    Setting a number on a face: which digits, where, how big, and the 6/9 mark.

    Laid out on the CPU once per die/font/size change and handed to the shader
    in the data texture; the harness's readback lays out every CANDIDATE number
    with the same function, so "the 17 is on top" is checked against what a 17
    would look like in that slot and not against a second idea of it.

    Units: the layout is in H, the digits' common ink height (Typeface.h); the
    slot's `height` turns H into metres on the face.
*/
namespace dice
{
struct SlotLayout
{
	geo::Slot slot;
	std::string text;
	int value       = 0;    ///< the item's number (pips use it)
	int glyphs      = 0;    ///< 1 or 2
	int glyph[ 2 ]  = { 0, 0 };
	float pen[ 2 ]  = { 0, 0 };///< H units: where each glyph's origin sits
	float centreX   = 0.0f; ///< H units: the block's centre in layout coordinates
	float centreY   = 0.0f;
	Mark mark       = Mark::None;
	float markA[ 4 ] = { 0, 0, 0, 0 };///< dot: cx, cy, r. underline: x0, x1, y0, y1
	float height    = 0.0f; ///< metres per H
};

/// Lay `text` out in `slot`, scaled so its block (marks included) fits the
/// slot's room times `size`.
SlotLayout LayoutSlot( const geo::Slot& slot, const std::string& text, int value, bool marked, Mark mark,
                       const DigitAtlas& atlas, float size );

/// The layout's signed distance (H units, + inside) at slot-local point
/// (x, y) in H units. The same sum the shader's slotDist() does.
float LayoutDistance( const SlotLayout& layout, const DigitAtlas& atlas, float x, float y );

} // namespace dice
