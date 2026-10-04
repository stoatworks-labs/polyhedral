#include "Scene.h"

#include <algorithm>
#include <cmath>

namespace dice
{
namespace
{
/// Where two lines in the table's plane meet: a + s u = b + t v (x and z only).
V3 meet( V3 a, V3 u, V3 b, V3 v )
{
	const double den = u.x * v.z - u.z * v.x;
	if( std::fabs( den ) < 1e-12 )
		return a;
	const double s = ( ( b.x - a.x ) * v.z - ( b.z - a.z ) * v.x ) / den;
	return { a.x + s * u.x, 0.0, a.z + s * u.z };
}
} // namespace

//---------------------------------------------------------------------------
V3 Camera::Ray( double px, double py, int w, int h ) const
{
	const double nx = px / w * 2.0 - 1.0, ny = py / h * 2.0 - 1.0;
	return Normalise( forward + right * ( nx * tanHalf * aspect ) + up * ( ny * tanHalf ) );
}

bool Camera::Project( V3 p, int w, int h, double& px, double& py ) const
{
	const V3 d     = p - position;
	const double z = Dot( d, forward );
	if( z <= 1e-9 )
		return false;
	const double nx = Dot( d, right ) / z / ( tanHalf * aspect );
	const double ny = Dot( d, up ) / z / tanHalf;
	px              = ( nx + 1.0 ) * 0.5 * w;
	py              = ( ny + 1.0 ) * 0.5 * h;
	return true;
}

void BuildView( const ViewSettings& view, int width, int height, Camera& camera, roll::Arena& arena )
{
	const geo::Solid& solid = geo::GetSolid( geo::ShapeOf( view.die ) );
	const double nominal    = 2.0 * solid.midradius;
	const double elevation  = view.elevationDegrees * kPi / 180.0;

	camera.tanHalf     = std::tan( kHalfFov * kPi / 180.0 );
	camera.aspect      = static_cast< double >( width ) / std::max( height, 1 );
	camera.frameHeight = nominal / view.size;
	//Several dice must all fit: a frame too tight for Count of them is widened
	//until the table in shot holds a square of them with room to land. One die
	//keeps any close-up Size asks for (its walls move out instead, below).
	{
		const int dice = view.count * geo::BodiesPerDie( view.die );
		if( dice > 1 )
		{
			const double rise    = 2.0 * solid.circumradius * std::cos( elevation );
			const double margin  = 0.35 * nominal + rise;
			const double need    = solid.circumradius * ( 3.5 + 2.3 * ( std::ceil( std::sqrt( static_cast< double >( dice ) ) ) - 1.0 ) );
			const double tallest = ( need + 2.0 * margin ) * std::sin( elevation );
			const double widest  = ( need + 2.0 * margin ) / camera.aspect;
			camera.frameHeight   = std::max( { camera.frameHeight, tallest, widest } );
		}
	}
	const double distance = camera.frameHeight / ( 2.0 * camera.tanHalf );
	//On the +z side looking toward -z, so screen right is world +x. The basis
	//must be right-handed as a view (right x up = toward the viewer): the other
	//way round mirrors the picture, and every number reads backwards.
	const V3 toward       = { 0.0, std::sin( elevation ), std::cos( elevation ) };
	camera.position       = toward * distance;
	camera.forward        = -toward;
	camera.right          = { 1.0, 0.0, 0.0 };
	camera.up             = Cross( camera.right, camera.forward );

	//The four corner rays onto the table; one that misses it (a low camera's
	//top edge) is stopped three frame-heights away.
	const double reach = 3.0 * camera.frameHeight;
	V3 corner[ 4 ];
	const int sx[ 4 ] = { -1, 1, 1, -1 }, sy[ 4 ] = { -1, -1, 1, 1 };
	for( int i = 0; i < 4; ++i )
	{
		const V3 ray = Normalise( camera.forward + camera.right * ( sx[ i ] * camera.tanHalf * camera.aspect )
		                          + camera.up * ( sy[ i ] * camera.tanHalf ) );
		V3 hit;
		if( ray.y < -1e-6 )
			hit = camera.position + ray * ( -camera.position.y / ray.y );
		else
			hit = camera.position + ray * reach;
		hit.y = 0.0;
		if( Length( hit ) > reach )
			hit = Normalise( hit ) * reach;
		corner[ i ] = hit;
	}

	//Pull each edge in by the margin and intersect neighbours: a third of a
	//die, plus how far a die's top can stand out past its footprint in the
	//picture (its height, foreshortened by the camera's tilt -- a d4's apex at
	//the far wall is otherwise cut by the top of the frame). The margin
	//shrinks if the footprint is small, so there is always room for a die.
	const double width0 = std::min( Length( corner[ 1 ] - corner[ 0 ] ), Length( corner[ 2 ] - corner[ 1 ] ) );
	const double rise   = 2.0 * solid.circumradius * std::cos( elevation );
	const double margin = std::min( 0.35 * nominal + rise, 0.2 * width0 );
	V3 centre {};
	for( const V3& c : corner )
		centre += c * 0.25;
	V3 offsetPoint[ 4 ], direction[ 4 ];
	for( int i = 0; i < 4; ++i )
	{
		const V3 a = corner[ i ], b = corner[ ( i + 1 ) % 4 ];
		V3 n       = Normalise( Cross( { 0, 1, 0 }, b - a ) );
		if( Dot( n, centre - a ) < 0.0 )
			n = -n;
		offsetPoint[ i ] = a + n * margin;
		direction[ i ]   = b - a;
	}
	for( int i = 0; i < 4; ++i )
	{
		const int prev     = ( i + 3 ) % 4;
		arena.corner[ i ] = meet( offsetPoint[ prev ], direction[ prev ], offsetPoint[ i ], direction[ i ] );
	}

	//A close-up shows less table than a throw needs. Then the walls stand
	//off-screen, far enough apart for the dice to land, and the planner moves
	//the whole throw so the dice come to rest in the middle of the frame.
	const geo::Solid& thrown = geo::GetSolid( geo::ShapeOf( view.die ) );
	const int bodies         = view.count * geo::BodiesPerDie( view.die );
	//Room for one die to come in off an edge and land. More dice share the
	//frame like a tray -- walls at its edges keep every die in shot -- and
	//only a close-up too tight for even one has its walls moved out.
	const double needed      = thrown.circumradius * 3.5;
	(void)bodies;
	V3 mid {};
	for( const V3& c : arena.corner )
		mid += c * 0.25;
	const double across = std::min( Length( arena.corner[ 1 ] - arena.corner[ 0 ] ), Length( arena.corner[ 2 ] - arena.corner[ 3 ] ) );
	const double deep   = std::min( Length( arena.corner[ 3 ] - arena.corner[ 0 ] ), Length( arena.corner[ 2 ] - arena.corner[ 1 ] ) );
	const double growX = std::max( 1.0, needed / std::max( across, 1e-9 ) );
	const double growZ = std::max( 1.0, needed / std::max( deep, 1e-9 ) );
	arena.enlargedX    = growX > 1.0;
	arena.enlargedZ    = growZ > 1.0;
	arena.enlarged     = arena.enlargedX || arena.enlargedZ;
	arena.viewX        = 0.5 * across;
	arena.viewZ        = 0.5 * deep;
	for( V3& c : arena.corner )
	{
		c.x = mid.x + ( c.x - mid.x ) * growX;
		c.z = mid.z + ( c.z - mid.z ) * growZ;
	}
	arena.ceiling = 0.6 * camera.position.y;
	//The camera looks at the origin, but a die's centre stands an inradius
	//above the table, and a tilted camera sees that height as a shift up the
	//frame. The point the dice are aimed at and centred on is the one whose
	//die-centre lies on the frame's centre ray: an inradius cot(elevation)
	//toward the camera.
	arena.middle = { 0.0, 0.0, solid.inradius * std::cos( elevation ) / std::max( std::sin( elevation ), 1e-3 ) };
}

V3 LightDirection( double lightAngleDegrees )
{
	const double azimuth   = ( lightAngleDegrees + 135.0 ) * kPi / 180.0;
	const double elevation = kLightElevation * kPi / 180.0;
	return { std::cos( elevation ) * std::cos( azimuth ), std::sin( elevation ), std::cos( elevation ) * std::sin( azimuth ) };
}

std::vector< float > BuildDataTexture( geo::DieType die, Mark mark, float numberSize, const DigitAtlas& atlas,
                                       std::vector< SlotLayout > layouts[ 2 ] )
{
	const geo::Shape shape  = geo::ShapeOf( die );
	const geo::Solid& solid = geo::GetSolid( shape );
	const auto& slots       = geo::GetSlots( shape );

	std::vector< float > data( static_cast< size_t >( kDataWidth ) * kDataRows * 4, 0.0f );
	auto put = [ & ]( int row, int i, double x, double y, double z, double w ) {
		if( i < 0 || i >= kDataWidth )
			return;
		float* p = data.data() + ( static_cast< size_t >( row ) * kDataWidth + static_cast< size_t >( i ) ) * 4;
		p[ 0 ]   = static_cast< float >( x );
		p[ 1 ]   = static_cast< float >( y );
		p[ 2 ]   = static_cast< float >( z );
		p[ 3 ]   = static_cast< float >( w );
	};

	for( size_t f = 0; f < solid.faces.size(); ++f )
	{
		const geo::Face& face = solid.faces[ f ];
		put( ROW_PLANES, static_cast< int >( f ), face.normal.x, face.normal.y, face.normal.z, face.offset );
		put( ROW_FACE, static_cast< int >( f ), face.incentre.x, face.incentre.y, face.incentre.z, face.inradius );
	}
	for( size_t v = 0; v < solid.vertices.size(); ++v )
		put( ROW_VERTS, static_cast< int >( v ), solid.vertices[ v ].x, solid.vertices[ v ].y, solid.vertices[ v ].z, 0.0 );
	for( size_t e = 0; e < solid.edges.size(); ++e )
	{
		const V3 a = solid.vertices[ static_cast< size_t >( solid.edges[ e ][ 0 ] ) ];
		const V3 b = solid.vertices[ static_cast< size_t >( solid.edges[ e ][ 1 ] ) ];
		put( ROW_EDGE_A, static_cast< int >( e ), a.x, a.y, a.z, 0.0 );
		put( ROW_EDGE_B, static_cast< int >( e ), b.x, b.y, b.z, 0.0 );
	}

	for( int part = 0; part < 2; ++part )
	{
		const geo::Labelling& labels = geo::GetLabelling( die, die == geo::DieType::D100 ? part : 0 );
		layouts[ part ].clear();
		const int base = ROW_SLOTS + 5 * part;
		for( size_t s = 0; s < slots.size(); ++s )
		{
			const geo::Slot& slot = slots[ s ];
			const size_t item     = static_cast< size_t >( slot.item );
			const SlotLayout l    = LayoutSlot( slot, labels.text[ item ], labels.digit[ item ], labels.marked[ item ], mark, atlas, numberSize );
			layouts[ part ].push_back( l );
			const int i = static_cast< int >( s );
			put( base, i, slot.centre.x, slot.centre.y, slot.centre.z, l.height );
			put( base + 1, i, slot.up.x, slot.up.y, slot.up.z, l.glyphs );
			put( base + 2, i, l.glyph[ 0 ], l.glyph[ 1 ], static_cast< int >( l.mark ), l.value );
			put( base + 3, i, l.pen[ 0 ], l.pen[ 1 ], l.centreX, l.centreY );
			put( base + 4, i, l.markA[ 0 ], l.markA[ 1 ], l.markA[ 2 ], l.markA[ 3 ] );
		}
	}
	//A face's picture frame: its first slot's up (the d4's slots are three
	//per face, in face order).
	const size_t perFace = slots.size() / std::max< size_t >( solid.faces.size(), 1 );
	for( size_t f = 0; f < solid.faces.size(); ++f )
	{
		const V3 up = slots[ f * perFace ].up;
		put( ROW_FACE_UP, static_cast< int >( f ), up.x, up.y, up.z, 0.0 );
	}
	for( int d = 0; d < 10; ++d )
	{
		const DigitAtlas::Glyph& g = atlas.glyph[ d ];
		put( ROW_GLYPH, d, g.originX, g.originY, 0.0, 0.0 );
		put( ROW_CELL, d, g.x0, g.y0, g.x1, g.y1 );
	}
	return data;
}

} // namespace dice
