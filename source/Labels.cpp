#include "Labels.h"

#include <algorithm>
#include <cmath>

namespace dice
{
namespace
{
//6/9 marks, in H units: an underline a little below the baseline, or a full
//stop after the number, as real dice print them.
constexpr float kUnderlineY0 = -0.21f;
constexpr float kUnderlineY1 = -0.11f;
constexpr float kDotRadius   = 0.075f;
constexpr float kDotGap      = 0.12f;
} // namespace

SlotLayout LayoutSlot( const geo::Slot& slot, const std::string& text, int value, bool marked, Mark mark,
                       const DigitAtlas& atlas, float size )
{
	SlotLayout l;
	l.slot  = slot;
	l.text  = text;
	l.value = value;
	l.mark  = marked ? mark : Mark::None;

	float pen    = 0.0f;
	float inkLo  = 1e9f, inkHi = -1e9f;
	for( char c : text )
	{
		if( c < '0' || c > '9' || l.glyphs >= 2 )
			continue;
		const int d                  = c - '0';
		const DigitAtlas::Glyph& g   = atlas.glyph[ d ];
		l.glyph[ l.glyphs ]          = d;
		l.pen[ l.glyphs ]            = pen;
		inkLo                        = std::min( inkLo, pen + g.inkX0 );
		inkHi                        = std::max( inkHi, pen + g.inkX1 );
		pen += g.advance;
		++l.glyphs;
	}
	if( l.glyphs == 0 )
	{
		inkLo = 0.0f;
		inkHi = 0.6f;
	}

	float lo = inkLo, hi = inkHi, bottom = 0.0f, top = 1.0f;
	if( l.mark == Mark::Underline )
	{
		l.markA[ 0 ] = inkLo;
		l.markA[ 1 ] = inkHi;
		l.markA[ 2 ] = kUnderlineY0;
		l.markA[ 3 ] = kUnderlineY1;
		bottom       = kUnderlineY0;
	}
	else if( l.mark == Mark::Dot )
	{
		l.markA[ 0 ] = inkHi + kDotGap;
		l.markA[ 1 ] = kDotRadius;
		l.markA[ 2 ] = kDotRadius;
		hi           = l.markA[ 0 ] + kDotRadius;
	}
	l.centreX = 0.5f * ( lo + hi );
	l.centreY = 0.5f * ( bottom + top );
	const float halfDiagonal = 0.5f * std::sqrt( ( hi - lo ) * ( hi - lo ) + ( top - bottom ) * ( top - bottom ) );
	l.height  = static_cast< float >( size * slot.room ) / std::max( halfDiagonal, 1e-3f );
	return l;
}

float LayoutDistance( const SlotLayout& l, const DigitAtlas& atlas, float x, float y )
{
	const float px = x + l.centreX, py = y + l.centreY;
	float d        = -1e3f;
	for( int i = 0; i < l.glyphs; ++i )
		d = std::max( d, atlas.Distance( l.glyph[ i ], px - l.pen[ i ], py ) );
	if( l.mark == Mark::Dot )
		d = std::max( d, l.markA[ 2 ] - std::hypot( px - l.markA[ 0 ], py - l.markA[ 1 ] ) );
	else if( l.mark == Mark::Underline )
	{
		const float cx = 0.5f * ( l.markA[ 0 ] + l.markA[ 1 ] ), cy = 0.5f * ( l.markA[ 2 ] + l.markA[ 3 ] );
		const float hx = 0.5f * ( l.markA[ 1 ] - l.markA[ 0 ] ), hy = 0.5f * ( l.markA[ 3 ] - l.markA[ 2 ] );
		const float ex = std::fabs( px - cx ) - hx, ey = std::fabs( py - cy ) - hy;
		const float outside = std::hypot( std::max( ex, 0.0f ), std::max( ey, 0.0f ) ) + std::min( std::max( ex, ey ), 0.0f );
		d = std::max( d, -outside );
	}
	return d;
}

} // namespace dice
