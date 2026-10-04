#include "Roll.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace dice::roll
{
namespace
{
constexpr int kStepsPerKey     = 4;   ///< 960 Hz physics, 240 Hz keys
constexpr int kMaxTrials       = 8;
constexpr double kMaxSim       = 14.0;///< s: a throw still moving by then is rejected
constexpr double kCockedDeg    = 1.0; ///< further off flat than this is resting on something
constexpr double kOverlap      = 1e-4;///< m: dice ending further into each other than this are a bad throw
constexpr double kOnTable      = 3e-4;///< m: lowest vertex this close to the table
constexpr double kBlendSeconds = 0.15;
constexpr double kSpeedLow     = 0.3;///< m/s
constexpr double kSpeedHigh    = 2.0;///< harder looks violent at this scale: a frame width in 0.1 s
constexpr double kWarpLow      = 0.85;///< accept a throw this close to Roll Time at once
constexpr double kWarpHigh     = 1.18;

/// A stream of 32-bit draws from one seed, for one purpose.
struct Stream
{
	uint32_t state;
	uint32_t counter = 0;
	explicit Stream( uint32_t seed ) : state( seed ) {}
	uint32_t Next()
	{
		return Hash( state, counter++ );
	}
	double Uniform()
	{
		return Unit( Next() );
	}
	double Range( double lo, double hi )
	{
		return lo + ( hi - lo ) * Uniform();
	}
};

/// A uniformly random rotation (Shoemake).
Quat RandomRotation( Stream& s )
{
	const double u1 = s.Uniform(), u2 = s.Uniform(), u3 = s.Uniform();
	const double a = std::sqrt( 1.0 - u1 ), b = std::sqrt( u1 );
	return Normalise( Quat { a * std::sin( 2 * kPi * u2 ), a * std::cos( 2 * kPi * u2 ), b * std::sin( 2 * kPi * u3 ),
	                         b * std::cos( 2 * kPi * u3 ) } );
}

V3 Centre( const Arena& arena )
{
	V3 c {};
	for( const V3& p : arena.corner )
		c += p;
	return c * 0.25;
}

/// The throw speed a first attempt uses for a wanted natural duration: fitted
/// to what this world's dice do (see AGENTS.md, "Meeting Roll Time"); the
/// trials correct whatever it gets wrong.
double FirstSpeed( double rollTime )
{
	return std::clamp( 4.0 * ( rollTime - 0.35 ), kSpeedLow, kSpeedHigh );
}

/// Place and launch the bodies for one attempt.
std::vector< physics::Body > Launch( const Request& r, const std::vector< geo::Shape >& shapes, Throw from, double speed,
                                     uint32_t subSeed )
{
	Stream s( subSeed );
	const V3 centre = Centre( r.arena );
	double size     = 0.0;
	for( geo::Shape shape : shapes )
		size = std::max( size, geo::GetSolid( shape ).circumradius );

	//The edge a throw comes in over: the arena's corners are in screen order
	//(bottom-left, bottom-right, top-right, top-left), so edge i runs from
	//corner i to corner i + 1 and the bottom of the frame is edge 0.
	V3 a {}, b {};
	auto edge = [ & ]( int i ) {
		a = r.arena.corner[ i ];
		b = r.arena.corner[ ( i + 1 ) % 4 ];
	};
	switch( from )
	{
	case Throw::Left: edge( 3 ); break;
	case Throw::Right: edge( 1 ); break;
	case Throw::Bottom: edge( 0 ); break;
	case Throw::Top: edge( 2 ); break;
	default: break;
	}

	std::vector< physics::Body > bodies;
	const size_t n   = shapes.size();
	const double gap = 2.3 * size;
	for( size_t i = 0; i < n; ++i )
	{
		physics::Body body;
		body.solid = &geo::GetSolid( shapes[ i ] );
		body.q     = RandomRotation( s );
		const double spin = r.spin * s.Range( 25.0, 55.0 );
		const V3 axis     = Normalise( V3 { s.Range( -1, 1 ), s.Range( -1, 1 ), s.Range( -1, 1 ) } );
		body.w            = axis * spin;

		if( from == Throw::Drop )
		{
			//A loose cluster over the middle, dropped from a height that gives
			//the impact speed the wanted throw would have.
			const int columns = static_cast< int >( std::ceil( std::sqrt( static_cast< double >( n ) ) ) );
			const double gx   = ( static_cast< double >( i % columns ) - 0.5 * ( columns - 1 ) ) * gap;
			const double gz   = ( static_cast< double >( i / columns ) - 0.5 * ( columns - 1 ) ) * gap;
			const double h    = std::clamp( speed * speed / ( 2.0 * 9.81 ), 0.03, std::max( 0.03, r.arena.ceiling - 2.0 * size ) );
			body.x            = centre + V3 { gx + s.Range( -0.2, 0.2 ) * size, h + size, gz + s.Range( -0.2, 0.2 ) * size };
			body.v            = V3 { s.Range( -1, 1 ), 0.0, s.Range( -1, 1 ) } * ( 0.15 * speed );
		}
		else
		{
			//Along the edge, side by side, a row further in for every row that
			//will not fit; each aimed at a point near the middle.
			const V3 along      = b - a;
			const double length = Length( along );
			const V3 tangent    = along * ( 1.0 / std::max( length, 1e-9 ) );
			V3 inward           = Normalise( Cross( { 0, 1, 0 }, tangent ) );
			if( Dot( inward, centre - a ) < 0.0 )
				inward = -inward;
			const int perRow  = std::max( 1, static_cast< int >( ( length - gap ) / gap ) );
			const int row     = static_cast< int >( i ) / perRow;
			const int inRow   = static_cast< int >( i ) % perRow;
			const int rowSize = std::min( perRow, static_cast< int >( n ) - row * perRow );
			const double t    = 0.5 * length + ( inRow - 0.5 * ( rowSize - 1 ) ) * gap + s.Range( -0.15, 0.15 ) * size;
			body.x            = a + tangent * t + inward * ( 1.2 * size + row * gap );
			body.x.y          = size * s.Range( 1.6, 2.6 );
			const V3 aim      = centre + V3 { s.Range( -0.25, 0.25 ), 0, s.Range( -0.25, 0.25 ) } * Length( b - a );
			V3 heading        = aim - body.x;
			heading.y         = 0.0;
			body.v            = Normalise( heading ) * speed;
			body.v.y          = s.Range( 0.0, 0.25 ) * speed;
		}
		bodies.push_back( body );
	}
	return bodies;
}

/// The face a body at rest lies on, and how far off flat it is (degrees).
int BottomFace( const geo::Solid& solid, const M3& rotation, double& offFlat )
{
	int best    = 0;
	double low  = 2.0;
	for( size_t f = 0; f < solid.faces.size(); ++f )
	{
		const double y = ( rotation * solid.faces[ f ].normal ).y;
		if( y < low )
		{
			low  = y;
			best = static_cast< int >( f );
		}
	}
	offFlat = std::acos( std::clamp( -low, -1.0, 1.0 ) ) * 180.0 / kPi;
	return best;
}

double LowestVertex( const physics::Body& body )
{
	const M3 r   = body.Rotation();
	double lowest = 1e30;
	for( const V3& v : body.solid->vertices )
		lowest = std::min( lowest, ( body.x + r * v ).y );
	return lowest;
}

/// How far two dice at rest overlap (m; negative: the gap between them), by
/// separating axes: every face normal of either, and every cross product of
/// an edge of one with an edge of the other. Complete for convex polyhedra.
double Overlap( const geo::Solid& a, const Pose& pa, const geo::Solid& b, const Pose& pb )
{
	const M3 ra = ToMatrix( pa.q ), rb = ToMatrix( pb.q );
	std::vector< V3 > va, vb, axes;
	for( const V3& v : a.vertices )
		va.push_back( pa.x + ra * v );
	for( const V3& v : b.vertices )
		vb.push_back( pb.x + rb * v );
	for( const geo::Face& f : a.faces )
		axes.push_back( ra * f.normal );
	for( const geo::Face& f : b.faces )
		axes.push_back( rb * f.normal );
	for( const auto& ea : a.edges )
		for( const auto& eb : b.edges )
		{
			const V3 c = Cross( va[ static_cast< size_t >( ea[ 1 ] ) ] - va[ static_cast< size_t >( ea[ 0 ] ) ],
			                    vb[ static_cast< size_t >( eb[ 1 ] ) ] - vb[ static_cast< size_t >( eb[ 0 ] ) ] );
			if( Length( c ) > 1e-12 )
				axes.push_back( Normalise( c ) );
		}
	double least = 1e30;
	for( const V3& axis : axes )
	{
		double a0 = 1e30, a1 = -1e30, b0 = 1e30, b1 = -1e30;
		for( const V3& p : va )
		{
			a0 = std::min( a0, Dot( p, axis ) );
			a1 = std::max( a1, Dot( p, axis ) );
		}
		for( const V3& p : vb )
		{
			b0 = std::min( b0, Dot( p, axis ) );
			b1 = std::max( b1, Dot( p, axis ) );
		}
		least = std::min( least, std::min( a1 - b0, b1 - a0 ) );
	}
	return least;
}

struct Attempt
{
	std::vector< Track > tracks;
	double natural  = 0.0;
	double spread   = 0.0;///< m: furthest a resting die's edge is from the dice's centroid
	bool valid      = false;
	bool cancelled  = false;
	physics::Stats stats;
};

Attempt Simulate( const Request& r, const std::vector< geo::Shape >& shapes, Throw from, double speed, uint32_t subSeed,
                  const std::atomic< bool >* cancel )
{
	Attempt out;
	physics::World world = MakeWorld( r );
	world.bodies         = Launch( r, shapes, from, speed, subSeed );
	const size_t n       = world.bodies.size();

	out.tracks.resize( n );
	std::vector< long > asleepSince( n, -1 );
	auto record = [ & ]() {
		for( size_t i = 0; i < n; ++i )
			out.tracks[ i ].keys.push_back( { world.bodies[ i ].x, world.bodies[ i ].q } );
	};
	record();

	const long maxSteps = static_cast< long >( kMaxSim / world.settings.dt );
	long step           = 0;
	for( ; step < maxSteps; ++step )
	{
		if( cancel != nullptr && ( step & 127 ) == 0 && cancel->load() )
		{
			out.cancelled = true;
			return out;
		}
		world.Step();
		for( size_t i = 0; i < n; ++i )
		{
			if( !world.bodies[ i ].asleep )
				asleepSince[ i ] = -1;
			else if( asleepSince[ i ] < 0 )
				asleepSince[ i ] = step + 1;
		}
		if( ( step + 1 ) % kStepsPerKey == 0 )
			record();
		if( world.AllAsleep() )
		{
			++step;
			break;
		}
	}
	//Pad to a whole key so the last state is recorded.
	if( step % kStepsPerKey != 0 )
		record();
	out.stats = world.stats;

	if( !world.AllAsleep() )
		return out;

	//-------------------------------------------------------------------
	// Every die flat on the table, or the throw is no good.
	//-------------------------------------------------------------------
	const double dt = world.settings.dt;
	out.natural     = 0.0;
	for( size_t i = 0; i < n; ++i )
	{
		const physics::Body& body = world.bodies[ i ];
		double offFlat            = 0.0;
		const int bottom          = BottomFace( *body.solid, body.Rotation(), offFlat );
		(void)bottom;
		if( offFlat > kCockedDeg || LowestVertex( body ) > kOnTable )
			return out;
		//Settled on a key: the first keyframe that holds the final pose. A
		//settle time between keys would leave the last few playback frames
		//interpolating between two copies of the final pose -- still, but
		//before Roll Time.
		const double settle    = std::max( 0.0, asleepSince[ i ] * dt - world.settings.sleepTime );
		out.tracks[ i ].settle = std::floor( settle * Plan::kKeyRate ) / Plan::kKeyRate;
		out.natural            = std::max( out.natural, out.tracks[ i ].settle );
	}

	//-------------------------------------------------------------------
	// Snap flat: the exact rotation that lays the bottom face on the table,
	// turning about the face's own centre (so the footprint stays where the
	// physics put it), blended in over the last kBlendSeconds before the die
	// settled. A snap that pushes one die into another is a bad throw.
	//-------------------------------------------------------------------
	std::vector< Pose > finals( n );
	for( size_t i = 0; i < n; ++i )
	{
		const physics::Body& body = world.bodies[ i ];
		double offFlat            = 0.0;
		const M3 rotation         = body.Rotation();
		const int bottom          = BottomFace( *body.solid, rotation, offFlat );
		const geo::Face& face     = body.solid->faces[ static_cast< size_t >( bottom ) ];
		const M3 flat             = Align( rotation * face.normal, { 0, -1, 0 } ) * rotation;
		const V3 footprint        = body.x + rotation * face.centroid;
		finals[ i ].q             = FromMatrix( flat );
		finals[ i ].x             = footprint - flat * face.centroid;
		finals[ i ].x.y           = face.offset;
	}
	//A die snapped flat against a wall can turn a fraction of a millimetre
	//through it: slide it back along the wall's normal.
	for( size_t i = 0; i < n; ++i )
	{
		const M3 rotation = ToMatrix( finals[ i ].q );
		for( size_t w = 1; w < world.planes.size(); ++w )
		{
			const physics::Plane& wall = world.planes[ w ];
			double deepest             = 0.0;
			for( const V3& v : world.bodies[ i ].solid->vertices )
				deepest = std::min( deepest, Dot( wall.normal, finals[ i ].x + rotation * v ) - wall.offset );
			finals[ i ].x += wall.normal * -deepest;
		}
	}
	for( size_t i = 0; i < n; ++i )
		for( size_t j = i + 1; j < n; ++j )
			if( Overlap( *world.bodies[ i ].solid, finals[ i ], *world.bodies[ j ].solid, finals[ j ] ) > kOverlap )
				return out;
	for( size_t i = 0; i < n; ++i )
	{
		const Pose final = finals[ i ];

		Track& track       = out.tracks[ i ];
		const double rate  = Plan::kKeyRate;
		const long settled = std::lround( track.settle * rate );
		const long from    = std::max( 0L, settled - static_cast< long >( kBlendSeconds * rate ) );
		for( long k = 0; k < static_cast< long >( track.keys.size() ); ++k )
		{
			Pose& key = track.keys[ static_cast< size_t >( k ) ];
			if( k >= settled )
				key = final;
			else if( k > from )
			{
				const double w = static_cast< double >( k - from ) / static_cast< double >( settled - from );
				key.q          = Slerp( key.q, final.q, w );
				key.x          = key.x + ( final.x - key.x ) * w;
			}
		}
	}
	V3 centroid {};
	for( const Pose& f : finals )
		centroid += f.x * ( 1.0 / static_cast< double >( n ) );
	for( size_t i = 0; i < n; ++i )
	{
		V3 d = finals[ i ].x - centroid;
		d.y  = 0.0;
		out.spread = std::max( out.spread, Length( d ) + world.bodies[ i ].solid->circumradius );
	}
	out.valid = true;
	return out;
}

/// Uniform per die, or a uniformly chosen tuple with the fixed sum.
std::vector< int > Draw( const Request& r, int count, int sides )
{
	std::vector< int > results;
	if( !r.fixed )
	{
		for( int i = 0; i < count; ++i )
		{
			Stream s( Hash( r.seed, r.roll, static_cast< uint32_t >( i ), 0x5eedu ) );
			if( r.biasedDraw )
				results.push_back( 1 + static_cast< int >( s.Next() % static_cast< uint32_t >( sides - 1 ) ) );
			else
				results.push_back( 1 + static_cast< int >( Below( static_cast< uint32_t >( sides ), [ & ]() { return s.Next(); } ) ) );
		}
		return results;
	}

	const int total = std::clamp( r.fixedTotal, count, count * sides );
	//ways[ k ][ t ]: how many ordered tuples of k dice sum to t. Exact in a
	//double up to 2^53; six d100s make at most 100^6 = 1e12.
	std::vector< std::vector< double > > ways( static_cast< size_t >( count ) + 1,
	                                           std::vector< double >( static_cast< size_t >( count * sides ) + 1, 0.0 ) );
	ways[ 0 ][ 0 ] = 1.0;
	for( int k = 1; k <= count; ++k )
		for( int t = 0; t <= count * sides; ++t )
			for( int v = 1; v <= sides && v <= t; ++v )
				ways[ static_cast< size_t >( k ) ][ static_cast< size_t >( t ) ] += ways[ static_cast< size_t >( k - 1 ) ][ static_cast< size_t >( t - v ) ];

	Stream s( Hash( r.seed, r.roll, 0xf1edu ) );
	int left = total;
	for( int i = 0; i < count; ++i )
	{
		const int k = count - i;
		if( k == 1 )
		{
			results.push_back( left );
			break;
		}
		double u = s.Uniform() * ways[ static_cast< size_t >( k ) ][ static_cast< size_t >( left ) ];
		int chosen = 1;
		for( int v = 1; v <= sides && v < left; ++v )
		{
			const double w = ways[ static_cast< size_t >( k - 1 ) ][ static_cast< size_t >( left - v ) ];
			if( u < w )
			{
				chosen = v;
				break;
			}
			u -= w;
			chosen = v;
		}
		results.push_back( chosen );
		left -= chosen;
	}
	return results;
}

void ChooseSymmetries( const Request& r, Plan& plan, const std::vector< int >& results )
{
	for( size_t i = 0; i < plan.bodies.size(); ++i )
	{
		Track& track         = plan.bodies[ i ];
		const geo::Solid& s  = geo::GetSolid( track.shape );
		const auto& labels   = geo::GetLabelling( r.die, track.part );
		track.result         = results[ static_cast< size_t >( track.die ) ];
		track.target         = geo::ItemForResult( r.die, track.part, track.result );
		const M3 rest        = ToMatrix( track.keys.back().q );
		track.landed         = geo::TopItem( s, labels.atVertex, rest );
		const std::vector< int > options = geo::SymmetriesTaking( s, labels.atVertex, track.target, track.landed );
		//Any of them is right; choosing among them by the seed turns the side
		//faces as well, so the same number does not always rest the same way.
		track.symmetry = options.empty() ? 0
		                                 : options[ Hash( r.seed, r.roll, static_cast< uint32_t >( i ), 0x5e77u ) % options.size() ];
		track.S = s.group[ static_cast< size_t >( track.symmetry ) ];
		if( r.identitySymmetry )
			track.S = M3 {};
		if( r.reflectSymmetry )
			track.S = M3 {} * -1.0;
	}
}

std::vector< geo::Shape > ShapesFor( const Request& r, std::vector< int >& dieOf, std::vector< int >& partOf )
{
	std::vector< geo::Shape > shapes;
	const int per = geo::BodiesPerDie( r.die );
	for( int d = 0; d < r.count; ++d )
		for( int p = 0; p < per; ++p )
		{
			shapes.push_back( geo::ShapeOf( r.die ) );
			dieOf.push_back( d );
			partOf.push_back( p );
		}
	return shapes;
}
} // namespace

physics::World MakeWorld( const Request& r )
{
	physics::World world;
	world.settings.restitution = r.restitution;
	world.settings.energyGuard = !r.noEnergyGuard;

	physics::Plane table;
	table.normal   = { 0, 1, 0 };
	table.offset   = 0.0;
	table.friction = world.settings.tableFriction;
	table.restitution = r.restitution;
	table.table    = true;
	world.planes.push_back( table );

	const V3 centre = Centre( r.arena );
	for( int i = 0; i < 4; ++i )
	{
		const V3 a = r.arena.corner[ i ], b = r.arena.corner[ ( i + 1 ) % 4 ];
		V3 n       = Normalise( Cross( { 0, 1, 0 }, b - a ) );
		if( Dot( n, centre - a ) < 0.0 )
			n = -n;
		physics::Plane wall;
		wall.normal   = n;
		wall.offset   = Dot( n, a );
		wall.friction = world.settings.wallFriction;
		wall.restitution = world.settings.wallRestitution;
		world.planes.push_back( wall );
	}
	return world;
}

std::vector< int > DrawResults( const Request& request )
{
	return Draw( request, std::max( 1, request.count ), geo::Sides( request.die ) );
}

Plan MakePlan( const Request& r, const std::atomic< bool >* cancel )
{
	const auto start = std::chrono::steady_clock::now();
	Plan plan;
	plan.duration  = std::max( 0.05, r.rollTime );
	plan.results   = DrawResults( r );
	for( int v : plan.results )
		plan.total += v;

	std::vector< int > dieOf, partOf;
	const std::vector< geo::Shape > shapes = ShapesFor( r, dieOf, partOf );

	Throw from = r.from;
	if( from == Throw::Any || from == Throw::Count )
		from = static_cast< Throw >( Hash( r.seed, r.roll, 0xa11u ) % 5u );

	double speed = FirstSpeed( plan.duration );
	Attempt best;
	double bestError = 1e30;
	//In a close-up (walls beyond the frame) the dice must also come to rest
	//close enough together to be in shot: a throw that fits beats one that
	//does not, whatever its timing; among those that do not, the tightest.
	const double room = r.arena.enlarged && r.arena.view > 0.0 ? 0.45 * r.arena.view : 1e30;
	for( int trial = 0; trial < kMaxTrials; ++trial )
	{
		++plan.trials;
		const uint32_t sub = Hash( r.seed, r.roll, static_cast< uint32_t >( trial ), 0x7417u );
		Attempt attempt    = Simulate( r, shapes, from, speed, sub, cancel );
		if( attempt.cancelled )
			return Plan {};
		if( attempt.valid && attempt.natural > 0.05 )
		{
			const bool fits    = attempt.spread <= room;
			const double error = std::fabs( std::log( attempt.natural / plan.duration ) ) + ( fits ? 0.0 : 1000.0 + attempt.spread );
			if( error < bestError )
			{
				bestError  = error;
				best       = attempt;
				plan.speed = speed;
			}
			const double warp = attempt.natural / plan.duration;
			if( fits && warp > kWarpLow && warp < kWarpHigh )
				break;
			if( !fits )
				continue;
			//Already throwing as hard (or as soft) as it will: another throw
			//cannot get closer, only luckier.
			if( ( warp < 1.0 && speed >= kSpeedHigh ) || ( warp > 1.0 && speed <= kSpeedLow ) )
				break;
			//Natural time grows roughly in proportion to the throw speed here;
			//damped, so one unlucky throw does not swing the next too far.
			speed = std::clamp( speed * std::pow( plan.duration / attempt.natural, 0.8 ), kSpeedLow, kSpeedHigh );
		}
	}

	if( !best.valid )
	{
		//Nothing came to rest flat in eight throws -- in practice only with
		//more dice than the arena holds. Fall back to the resting layout, so
		//the result still shows.
		Plan rest   = MakeRest( r );
		rest.trials = plan.trials;
		rest.ms     = std::chrono::duration< double, std::milli >( std::chrono::steady_clock::now() - start ).count();
		return rest;
	}

	//A close-up: move the whole throw, walls and all, so the dice's resting
	//centroid is the arena's centre -- along the enlarged axes only, whose
	//walls are off-screen. A translation of everything is exact: the table is
	//infinite. Along an axis that was not enlarged the walls ARE the frame's
	//edges and stay where they are.
	if( r.arena.enlarged && !best.tracks.empty() )
	{
		V3 rest {};
		for( const Track& t : best.tracks )
			rest += t.keys.back().x * ( 1.0 / static_cast< double >( best.tracks.size() ) );
		const V3 centre = Centre( r.arena );
		plan.shift      = { r.arena.enlargedX ? centre.x - rest.x : 0.0, 0.0, r.arena.enlargedZ ? centre.z - rest.z : 0.0 };
		for( Track& t : best.tracks )
			for( Pose& k : t.keys )
				k.x += plan.shift;
	}

	plan.natural = best.natural;
	plan.settled = true;
	plan.physics = best.stats;
	plan.warp    = r.noWarp ? 1.0 : plan.natural / plan.duration;
	if( r.noWarp || plan.warp >= 1.0 )
		plan.rateStart = plan.rateEnd = plan.warp;
	else
	{
		//rate( t ) = c ( 1 - k t / R ): the mean is c ( 1 - k / 2 ), which must
		//be the warp. k grows as the throw falls short of Roll Time, so a
		//near-miss plays at real speed throughout and a long Roll Time starts
		//near real speed and slows into the reveal.
		const double k = std::clamp( 2.0 * ( 1.0 - plan.warp ), 0.0, 0.75 );
		plan.rateStart = plan.warp / ( 1.0 - 0.5 * k );
		plan.rateEnd   = plan.rateStart * ( 1.0 - k );
	}
	plan.bodies  = best.tracks;
	for( size_t i = 0; i < plan.bodies.size(); ++i )
	{
		plan.bodies[ i ].shape = shapes[ i ];
		plan.bodies[ i ].die   = dieOf[ i ];
		plan.bodies[ i ].part  = partOf[ i ];
	}
	ChooseSymmetries( r, plan, plan.results );
	plan.ms = std::chrono::duration< double, std::milli >( std::chrono::steady_clock::now() - start ).count();
	return plan;
}

Plan MakeRest( const Request& r )
{
	Plan plan;
	plan.idle     = true;
	plan.settled  = true;
	plan.duration = 0.0;
	plan.results  = DrawResults( r );
	for( int v : plan.results )
		plan.total += v;

	std::vector< int > dieOf, partOf;
	const std::vector< geo::Shape > shapes = ShapesFor( r, dieOf, partOf );
	const V3 centre                        = Centre( r.arena );
	double size                            = 0.0;
	for( geo::Shape shape : shapes )
		size = std::max( size, geo::GetSolid( shape ).circumradius );

	const size_t n    = shapes.size();
	const int columns = std::max( 1, std::min( static_cast< int >( n ), 6 ) );
	const double gap  = 2.4 * size;
	for( size_t i = 0; i < n; ++i )
	{
		Track track;
		track.shape         = shapes[ i ];
		track.die           = dieOf[ i ];
		track.part          = partOf[ i ];
		const geo::Solid& s = geo::GetSolid( track.shape );
		const auto& labels  = geo::GetLabelling( r.die, track.part );
		track.result        = plan.results[ static_cast< size_t >( track.die ) ];
		track.target        = geo::ItemForResult( r.die, track.part, track.result );
		track.landed        = track.target;

		//Turn the wanted item to the top, then a seeded turn about the vertical.
		const V3 item = labels.atVertex ? Normalise( s.vertices[ static_cast< size_t >( track.target ) ] )
		                                : s.faces[ static_cast< size_t >( track.target ) ].normal;
		Stream st( Hash( r.seed, r.roll, static_cast< uint32_t >( i ), 0x1d1eu ) );
		const M3 rotation = AxisAngle( { 0, 1, 0 }, st.Range( 0.0, 2.0 * kPi ) ) * Align( item, { 0, 1, 0 } );
		const int row     = static_cast< int >( i ) / columns;
		const int col     = static_cast< int >( i ) % columns;
		const int inRow   = std::min( columns, static_cast< int >( n ) - row * columns );
		const int rows    = ( static_cast< int >( n ) + columns - 1 ) / columns;
		Pose pose;
		pose.q = FromMatrix( rotation );
		pose.x = centre + V3 { ( col - 0.5 * ( inRow - 1 ) ) * gap, 0.0, ( row - 0.5 * ( rows - 1 ) ) * gap };
		//Resting on the face opposite the top: every face of these solids is
		//at the inradius, and the d4's bottom is the face opposite its top vertex.
		pose.x.y = s.inradius;
		track.keys.push_back( pose );
		track.S = M3 {};
		plan.bodies.push_back( track );
	}
	return plan;
}

double Plan::SimTime( double seconds ) const
{
	const double t = std::max( 0.0, std::min( seconds, duration ) );
	if( duration <= 0.0 )
		return 0.0;
	//The integral of a linear rate from rateStart to rateEnd over [0, t].
	return t * ( rateStart + 0.5 * ( rateEnd - rateStart ) * t / duration );
}

Pose Plan::PoseAt( size_t i, double seconds ) const
{
	const Track& track = bodies[ i ];
	if( track.keys.empty() )
		return {};
	const double simSeconds = SimTime( seconds );
	const double f          = simSeconds * kKeyRate;
	const size_t last       = track.keys.size() - 1;
	if( f >= static_cast< double >( last ) )
		return track.keys[ last ];
	const size_t k = static_cast< size_t >( std::floor( f ) );
	const double t = f - static_cast< double >( k );
	Pose p;
	p.x = track.keys[ k ].x + ( track.keys[ k + 1 ].x - track.keys[ k ].x ) * t;
	p.q = Slerp( track.keys[ k ].q, track.keys[ k + 1 ].q, t );
	return p;
}

M3 Plan::RenderRotation( size_t i, double seconds ) const
{
	return ToMatrix( PoseAt( i, seconds ).q ) * bodies[ i ].S;
}

} // namespace dice::roll
