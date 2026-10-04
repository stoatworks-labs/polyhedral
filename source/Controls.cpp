#include "Controls.h"

#include <algorithm>
#include <cmath>

namespace dice
{
namespace
{
float clamp01( float v )
{
	return std::clamp( v, 0.0f, 1.0f );
}
double geometric( float v, double low, double high )
{
	return low * std::pow( high / low, static_cast< double >( clamp01( v ) ) );
}
float inverseGeometric( double value, double low, double high )
{
	return static_cast< float >( std::log( std::clamp( value, low, high ) / low ) / std::log( high / low ) );
}
float linear( float v, float low, float high )
{
	return low + ( high - low ) * clamp01( v );
}

constexpr double kRollTimeLow  = 0.4;
constexpr double kRollTimeHigh = 8.0;
constexpr double kIntervalLow  = 1.0;
constexpr double kIntervalHigh = 60.0;
constexpr double kBounceLow    = 0.1;
constexpr double kBounceHigh   = 0.8;
constexpr double kSizeLow      = 0.06;
constexpr double kSizeHigh     = 0.6;
} // namespace

double RollTimeFromParam( float v )
{
	return geometric( v, kRollTimeLow, kRollTimeHigh );
}
float ParamFromRollTime( double seconds )
{
	return inverseGeometric( seconds, kRollTimeLow, kRollTimeHigh );
}
double IntervalFromParam( float v )
{
	return geometric( v, kIntervalLow, kIntervalHigh );
}
float ParamFromInterval( double seconds )
{
	return inverseGeometric( seconds, kIntervalLow, kIntervalHigh );
}
double BounceFromParam( float v )
{
	return kBounceLow + ( kBounceHigh - kBounceLow ) * clamp01( v );
}
float ParamFromBounce( double e )
{
	return static_cast< float >( ( std::clamp( e, kBounceLow, kBounceHigh ) - kBounceLow ) / ( kBounceHigh - kBounceLow ) );
}
double SpinFromParam( float v )
{
	return clamp01( v );
}

float GlossFromParam( float v )
{
	return clamp01( v );
}
float EdgeFromParam( float v )
{
	return linear( v, 0.0f, 0.3f );
}
float LineWidthFromParam( float v )
{
	return linear( v, 0.01f, 0.15f );
}

float NumberSizeFromParam( float v )
{
	return linear( v, 0.3f, 1.1f );
}
float WeightFromParam( float v )
{
	return linear( v, -0.06f, 0.06f );
}

double SizeFromParam( float v )
{
	return geometric( v, kSizeLow, kSizeHigh );
}
float ParamFromSize( double fraction )
{
	return inverseGeometric( fraction, kSizeLow, kSizeHigh );
}
double CameraAngleFromParam( float v )
{
	return linear( v, 30.0f, 90.0f );
}
double LightAngleFromParam( float v )
{
	return linear( v, -180.0f, 180.0f );
}

int OptionIndex( float value, int count )
{
	return std::clamp( static_cast< int >( std::lround( value ) ), 0, count - 1 );
}

} // namespace dice
