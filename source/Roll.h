#pragma once

#include "Geometry.h"
#include "Maths.h"
#include "Physics.h"

#include <atomic>
#include <vector>

/**
    A roll, planned ahead of time.

    `Plan()` draws the results, throws the dice into a world of table and walls,
    integrates to rest, and returns keyframes plus, per body, the symmetry `S`
    that makes the die show its result. The renderer draws body `i` at
    `rotation(t) * S_i`. See Geometry.h for why `S` changes nothing a viewer can
    see but the number.

    ## Meeting Roll Time

    A throw's natural duration depends on how hard it is thrown, and only
    noisily (dice are chaotic). So the planner throws, measures when the last die
    came to rest, rescales the throw speed toward the wanted time and throws again
    with a fresh sub-seed, keeping the closest throw that ended with every die
    flat on the table. Playback then maps playback time onto simulated time so
    the dice stop EXACTLY at Roll Time.

    Real dice settle fast: a 20 mm die thrown across a frame-sized table is
    still in 0.3 to 0.7 s however hard it is thrown (most of the energy goes
    in the first few impacts, and it is fast motion until nine tenths of the
    way). So a Roll Time longer than that is played slower than real time --
    slow motion, which is how a tabletop close-up is filmed anyway -- easing a
    little further toward the end, so the reveal lingers (`SimTime()`). A Roll
    Time shorter than the throw plays it uniformly fast.
    `Plan::warp` is the average rate, simulated seconds per second.

    ## Rejected throws

    A die resting against a wall or on another die has no top face. Such a throw
    is thrown again (`Plan::trials` counts them); a die that the sleeping test
    leaves a degree or two off flat is snapped flat over its last 0.15 s.
*/
namespace dice::roll
{
enum class Throw
{
	Left = 0,
	Right,
	Bottom,
	Top,
	Drop,
	Any,
	Count
};

/// The walls' footprint on the table, in metres: four corners in SCREEN order --
/// bottom-left, bottom-right, top-right, top-left of the frame -- so a throw
/// from the Left comes in over edge 3-0 whatever way the camera faces. The
/// plugin derives it from what the camera sees.
struct Arena
{
	V3 corner[ 4 ];
	double ceiling = 1.0;///< m: no die is dropped from higher (the camera is above it)
	/// The walls stand beyond the frame (a close-up): the throw is moved so the
	/// dice come to rest round the arena's centre, which is the frame's.
	bool enlarged = false;
	bool enlargedX = false, enlargedZ = false;///< which way: the throw moves only along those
	double viewX  = 0.0;///< m: half the frame's footprint across (the narrower end of it), before any enlarging
	double viewZ  = 0.0;///< m: half its depth
	V3 middle;          ///< the table point at the middle of the frame
};

struct Request
{
	geo::DieType die  = geo::DieType::D20;
	int count         = 1;    ///< dice (pairs, for the d100)
	bool fixed        = false;
	int fixedTotal    = 20;
	uint32_t seed     = 0;
	uint32_t roll     = 0;    ///< which roll this is: a new one draws new results
	Throw from        = Throw::Left;
	double spin       = 0.5;  ///< 0..1
	double restitution = 0.4;
	double rollTime   = 2.0;  ///< seconds of playback from release to rest
	Arena arena;

	//---------------------------------------------------------------
	// Wrong models, for ditest's negative controls only.
	//---------------------------------------------------------------
	bool identitySymmetry = false;///< S = I: the physics' own face shows
	bool noWarp           = false;///< play at 1x: the dice stop when they stop
	bool biasedDraw       = false;///< draw with % (n - 1): the top value never comes up
	bool reflectSymmetry  = false;///< S = -I (a reflection, not a rotation)
	bool noEnergyGuard    = false;
};

struct Pose
{
	V3 x;
	Quat q;
};

struct Track
{
	geo::Shape shape = geo::Shape::Cube;
	int part         = 0;  ///< the d100's tens (0) or units (1)
	int die          = 0;  ///< which die of Count this body belongs to
	int result       = 1;  ///< the die's result (the pair's, on the d100)
	int target       = 0;  ///< the item that must show
	int landed       = 0;  ///< the item the physics left on top
	int symmetry     = 0;  ///< index into the solid's group
	M3 S;                  ///< that element (or a deliberately wrong one)
	double settle    = 0.0;///< sim seconds: when this die stopped moving
	std::vector< Pose > keys;
};

struct Plan
{
	std::vector< Track > bodies;
	std::vector< int > results;///< per die (per pair on the d100)
	int total        = 0;
	double natural   = 0.0;    ///< sim seconds from release to the last die at rest
	double duration  = 0.0;    ///< playback seconds: Roll Time
	double warp      = 1.0;    ///< average sim seconds per playback second (natural / duration)
	double rateStart = 1.0;    ///< sim seconds per playback second at release
	double rateEnd   = 1.0;    ///< ... and at rest
	int trials       = 0;
	int cocked       = 0;      ///< throws rejected for a die resting on something (rerolled, as at a table)
	int unsettled    = 0;      ///< ... for dice still moving, or ending into each other
	bool settled     = false;  ///< every die came to rest flat on the table
	bool idle        = false;  ///< a resting layout, not a throw
	double speed     = 0.0;    ///< m/s, the throw that was kept
	double ms        = 0.0;    ///< wall time the planning took
	V3 shift;                  ///< m: how far a close-up moved the whole throw (and its walls)
	physics::Stats physics;    ///< the kept throw's integrator counts

	static constexpr double kKeyRate = 240.0;

	/// Simulated seconds after release at `seconds` of playback: the rate falls
	/// linearly from rateStart to rateEnd, clamped to the roll.
	double SimTime( double seconds ) const;
	/// The simulated pose of body `i`, `seconds` of playback after release.
	Pose PoseAt( size_t i, double seconds ) const;
	/// What is drawn: the simulated rotation times the body's symmetry.
	M3 RenderRotation( size_t i, double seconds ) const;
	bool Finished( double seconds ) const
	{
		return seconds >= duration;
	}
};

/// The results a roll will show: uniform per die (Random), or a uniformly
/// chosen tuple summing to the Fixed total (clamped to what the dice can make).
std::vector< int > DrawResults( const Request& request );

/// Throw, simulate, choose the symmetries. `cancel`, if given, is polled; a
/// cancelled plan comes back empty.
Plan MakePlan( const Request& request, const std::atomic< bool >* cancel = nullptr );

/// The dice at rest in a row in the middle of the arena, showing the request's
/// results, without a throw: what is on the table before the first roll.
Plan MakeRest( const Request& request );

/// The world a request throws into: table, walls, restitution. Shared with the
/// harness's physics checks.
physics::World MakeWorld( const Request& request );

} // namespace dice::roll
