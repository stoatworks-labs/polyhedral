/**
    polytest -- render Polyhedral offline, and measure what its dice are doing.

    It drives the REAL plugin class, through the same ProcessOpenGL a host
    calls, on a synthetic 60 fps clock, in a headless CGL context, with the
    planner run on the render thread (`SetSynchronousForTest`) so a roll starts
    on the frame it is asked for and every run is the same run. The physics and
    outcome checks read the plan the plugin is drawing; the picture checks read
    the pixels the shipped shader drew.

        polytest --out /tmp/polyhedral.png           the source, after a roll
        polytest --over --out /tmp/o.png       the Over effect on the harness's card
        polytest --list                        every parameter and its default
        polytest --film N                      N frames, raw RGBA on stdout
        polytest --pipe                        raw frames in (Over), raw frames out
        polytest --offline                     the checks that need no GL context (CI)

    `--script` is the fleet's cue format: `frame  Parameter Name  value` lines,
    held before the first key and after the last, linearly interpolated
    between. A button press is three keys (0, 1, 0).

    The claims, one flag each -- see README "Building and testing".
*/

#include "Controls.h"
#include "Dice.h"
#include "Geometry.h"
#include "Labels.h"
#include "Physics.h"
#include "Roll.h"
#include "Shaders.h"
#include "Typeface.h"

#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>
#include <zlib.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>

using namespace dice;

namespace
{
using Floats = std::vector< float >;
using Bytes  = std::vector< unsigned char >;

//---------------------------------------------------------------------------
// Reporting.
//---------------------------------------------------------------------------
int g_failures = 0;

std::string fmt( const char* format, ... )
{
	char buffer[ 2048 ];
	va_list args;
	va_start( args, format );
	std::vsnprintf( buffer, sizeof( buffer ), format, args );
	va_end( args );
	return buffer;
}

void Check( bool condition, const std::string& message )
{
	std::printf( "  %s  %s\n", condition ? "ok  " : "FAIL", message.c_str() );
	if( !condition )
		++g_failures;
}

int Verdict()
{
	std::printf( "\n  %s\n", g_failures == 0 ? "PASS" : "FAIL" );
	return g_failures == 0 ? 0 : 1;
}

//---------------------------------------------------------------------------
// PNG and context: boreal's harness, unchanged.
//---------------------------------------------------------------------------
void putU32( Bytes& out, uint32_t value )
{
	out.push_back( static_cast< unsigned char >( value >> 24 ) );
	out.push_back( static_cast< unsigned char >( value >> 16 ) );
	out.push_back( static_cast< unsigned char >( value >> 8 ) );
	out.push_back( static_cast< unsigned char >( value ) );
}

void putChunk( Bytes& out, const char* type, const Bytes& data )
{
	putU32( out, static_cast< uint32_t >( data.size() ) );
	const size_t start = out.size();
	out.insert( out.end(), type, type + 4 );
	out.insert( out.end(), data.begin(), data.end() );
	uLong crc = crc32( 0L, Z_NULL, 0 );
	crc       = crc32( crc, out.data() + start, static_cast< uInt >( 4 + data.size() ) );
	putU32( out, static_cast< uint32_t >( crc ) );
}

/// `rgba` is floats, row 0 at the BOTTOM (GL's order); the file is written top
/// row first, which is the only place anything here flips.
bool writePng( const std::string& path, int width, int height, const Floats& rgba )
{
	Bytes raw;
	raw.reserve( static_cast< size_t >( height ) * ( 1 + static_cast< size_t >( width ) * 4 ) );
	for( int y = height - 1; y >= 0; --y )
	{
		raw.push_back( 0 );
		for( int x = 0; x < width; ++x )
			for( int c = 0; c < 4; ++c )
			{
				const float v = rgba[ ( static_cast< size_t >( y ) * width + x ) * 4 + c ];
				raw.push_back( static_cast< unsigned char >( std::lround( std::clamp( v, 0.0f, 1.0f ) * 255.0f ) ) );
			}
	}

	uLongf compressedSize = compressBound( static_cast< uLong >( raw.size() ) );
	Bytes compressed( compressedSize );
	if( compress2( compressed.data(), &compressedSize, raw.data(), static_cast< uLong >( raw.size() ), 6 ) != Z_OK )
		return false;
	compressed.resize( compressedSize );

	Bytes png = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
	Bytes ihdr;
	putU32( ihdr, static_cast< uint32_t >( width ) );
	putU32( ihdr, static_cast< uint32_t >( height ) );
	ihdr.insert( ihdr.end(), { 8, 6, 0, 0, 0 } );
	putChunk( png, "IHDR", ihdr );
	putChunk( png, "IDAT", compressed );
	putChunk( png, "IEND", {} );

	FILE* file = fopen( path.c_str(), "wb" );
	if( file == nullptr )
		return false;
	const size_t written = fwrite( png.data(), 1, png.size(), file );
	fclose( file );
	return written == png.size();
}

CGLContextObj createContext()
{
	//Accelerated first; fall back so the harness still runs somewhere without
	//a GPU, where it will at least prove the shaders compile.
	const CGLPixelFormatAttribute accelerated[] = {
		kCGLPFAOpenGLProfile, static_cast< CGLPixelFormatAttribute >( kCGLOGLPVersion_GL4_Core ),
		kCGLPFAAccelerated,
		kCGLPFAColorSize, static_cast< CGLPixelFormatAttribute >( 24 ),
		kCGLPFAAlphaSize, static_cast< CGLPixelFormatAttribute >( 8 ),
		static_cast< CGLPixelFormatAttribute >( 0 )
	};
	const CGLPixelFormatAttribute software[] = {
		kCGLPFAOpenGLProfile, static_cast< CGLPixelFormatAttribute >( kCGLOGLPVersion_GL4_Core ),
		kCGLPFAColorSize, static_cast< CGLPixelFormatAttribute >( 24 ),
		kCGLPFAAlphaSize, static_cast< CGLPixelFormatAttribute >( 8 ),
		static_cast< CGLPixelFormatAttribute >( 0 )
	};

	CGLPixelFormatObj format = nullptr;
	GLint formatCount        = 0;
	if( CGLChoosePixelFormat( accelerated, &format, &formatCount ) != kCGLNoError || format == nullptr )
	{
		if( CGLChoosePixelFormat( software, &format, &formatCount ) != kCGLNoError || format == nullptr )
			return nullptr;
	}

	CGLContextObj context = nullptr;
	const CGLError error  = CGLCreateContext( format, nullptr, &context );
	CGLDestroyPixelFormat( format );
	if( error != kCGLNoError )
		return nullptr;

	CGLSetCurrentContext( context );
	return context;
}

const char* kindName( unsigned int type )
{
	switch( type )
	{
	case FF_TYPE_BOOLEAN: return "bool";
	case FF_TYPE_EVENT: return "event";
	case FF_TYPE_RED: return "red";
	case FF_TYPE_GREEN: return "green";
	case FF_TYPE_BLUE: return "blue";
	case FF_TYPE_OPTION: return "option";
	case FF_TYPE_BUFFER: return "buffer";
	case FF_TYPE_STANDARD: return "standard";
	case FF_TYPE_TEXT: return "text";
	case FF_TYPE_FILE: return "file";
	case FF_TYPE_INTEGER: return "integer";
	default: return "other";
	}
}

using Track = std::vector< std::pair< int, float > >;

std::map< std::string, Track > loadScript( const std::string& path, std::string& error )
{
	std::map< std::string, Track > tracks;
	std::ifstream file( path );
	if( !file )
	{
		error = "cannot open " + path;
		return tracks;
	}

	std::string line;
	int lineNumber = 0;
	while( std::getline( file, line ) )
	{
		++lineNumber;
		const size_t hash = line.find( '#' );
		if( hash != std::string::npos )
			line.erase( hash );
		std::istringstream in( line );

		int frame = 0;
		if( !( in >> frame ) )
			continue;

		//The name is everything up to the last token: parameters have spaces
		//in them ("Roll Time") and the value never does.
		std::vector< std::string > words;
		std::string word;
		while( in >> word )
			words.push_back( word );
		if( words.size() < 2 )
		{
			error = path + ":" + std::to_string( lineNumber ) + ": expected `frame Parameter Name value`";
			return {};
		}

		const float value = std::strtof( words.back().c_str(), nullptr );
		words.pop_back();
		std::string name = words.front();
		for( size_t i = 1; i < words.size(); ++i )
			name += " " + words[ i ];

		tracks[ name ].emplace_back( frame, value );
	}

	for( auto& entry : tracks )
		std::sort( entry.second.begin(), entry.second.end() );
	return tracks;
}

float valueAt( const Track& track, int frame )
{
	if( track.empty() )
		return 0.0f;
	if( frame <= track.front().first )
		return track.front().second;
	if( frame >= track.back().first )
		return track.back().second;
	for( size_t i = 0; i + 1 < track.size(); ++i )
	{
		const auto& a = track[ i ];
		const auto& b = track[ i + 1 ];
		if( frame >= a.first && frame <= b.first )
		{
			if( b.first == a.first )
				return b.second;
			const float t = static_cast< float >( frame - a.first ) / static_cast< float >( b.first - a.first );
			return a.second + ( b.second - a.second ) * t;
		}
	}
	return track.back().second;
}

//---------------------------------------------------------------------------
// The card, for the Over effect: a dusk gradient with a few bright stripes,
// so a die textured with the clip has something to show.
//---------------------------------------------------------------------------
Floats buildCard( int width, int height )
{
	Floats card( static_cast< size_t >( width ) * height * 4 );
	for( int y = 0; y < height; ++y )
		for( int x = 0; x < width; ++x )
		{
			const double u = ( x + 0.5 ) / width, v = ( y + 0.5 ) / height;
			float* o       = &card[ ( static_cast< size_t >( y ) * width + x ) * 4 ];
			const bool stripe = static_cast< int >( std::floor( u * 12.0 + v * 3.0 ) ) % 3 == 0;
			o[ 0 ] = static_cast< float >( 0.15 + 0.5 * v + ( stripe ? 0.25 : 0.0 ) );
			o[ 1 ] = static_cast< float >( 0.10 + 0.25 * u );
			o[ 2 ] = static_cast< float >( 0.35 + 0.4 * ( 1.0 - v ) );
			o[ 3 ] = 1.0f;
		}
	return card;
}

GLuint makeTexture( int width, int height, const float* pixels )
{
	GLuint texture = 0;
	glGenTextures( 1, &texture );
	glBindTexture( GL_TEXTURE_2D, texture );
	glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA32F, width, height, 0, GL_RGBA, GL_FLOAT, pixels );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
	glBindTexture( GL_TEXTURE_2D, 0 );
	return texture;
}

//---------------------------------------------------------------------------
// A rig: the real plugin, a float output framebuffer, a synthetic 60 fps clock.
//---------------------------------------------------------------------------
struct Rig
{
	DicePlugin plugin;
	int width = 0, height = 0;
	GLuint sourceTexture = 0, outputTexture = 0, outputFBO = 0;
	int frame            = 0;
	double fps           = 60.0;
	double clockOffset   = 0.0;

	ProcessOpenGLStruct process    = {};
	FFGLTextureStruct inputStruct  = {};
	FFGLTextureStruct* inputs[ 1 ] = { nullptr };

	explicit Rig( bool effect = false ) : plugin( effect )
	{
		plugin.SetSynchronousForTest( true );
	}

	~Rig()
	{
		plugin.DeInitGL();
		if( outputFBO )
			glDeleteFramebuffers( 1, &outputFBO );
		if( outputTexture )
			glDeleteTextures( 1, &outputTexture );
		if( sourceTexture )
			glDeleteTextures( 1, &sourceTexture );
	}

	bool Init( int w, int h, const Floats* picture = nullptr )
	{
		width  = w;
		height = h;
		FFGLViewportStruct viewport = {};
		viewport.width              = static_cast< FFUInt32 >( width );
		viewport.height             = static_cast< FFUInt32 >( height );
		if( plugin.InitGL( &viewport ) != FF_SUCCESS )
		{
			std::fprintf( stderr, "InitGL failed -- see ~/Library/Logs/polyhedral for why\n" );
			return false;
		}
		plugin.SetClockScaleForTest( 1.0 );

		outputTexture = makeTexture( width, height, nullptr );
		glGenFramebuffers( 1, &outputFBO );
		glBindFramebuffer( GL_FRAMEBUFFER, outputFBO );
		glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, outputTexture, 0 );
		if( glCheckFramebufferStatus( GL_FRAMEBUFFER ) != GL_FRAMEBUFFER_COMPLETE )
			return false;

		process.HostFBO = outputFBO;
		if( plugin.IsEffect() )
		{
			const Floats card = picture ? *picture : buildCard( width, height );
			sourceTexture     = makeTexture( width, height, card.data() );
			inputStruct.Width = inputStruct.HardwareWidth = static_cast< FFUInt32 >( width );
			inputStruct.Height = inputStruct.HardwareHeight = static_cast< FFUInt32 >( height );
			inputStruct.Handle                              = sourceTexture;
			inputs[ 0 ]                                     = &inputStruct;
			process.numInputTextures                        = 1;
			process.inputTextures                           = inputs;
		}
		return true;
	}

	void Upload( const Floats& picture )
	{
		glBindTexture( GL_TEXTURE_2D, sourceTexture );
		glTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_FLOAT, picture.data() );
		glBindTexture( GL_TEXTURE_2D, 0 );
	}

	void Set( unsigned int id, float value )
	{
		plugin.SetFloatParameter( id, value );
	}

	void Press( unsigned int id )
	{
		plugin.SetFloatParameter( id, 1.0f );
		plugin.SetFloatParameter( id, 0.0f );
	}

	bool Render( int frames = 1 )
	{
		for( int i = 0; i < frames; ++i )
		{
			const double seconds = clockOffset + static_cast< double >( frame ) / fps;
			plugin.SetTime( seconds );
			++frame;
			glBindFramebuffer( GL_FRAMEBUFFER, outputFBO );
			glViewport( 0, 0, width, height );
			glClearColor( 0.0f, 0.0f, 0.0f, 0.0f );
			glClear( GL_COLOR_BUFFER_BIT );
			if( plugin.ProcessOpenGL( &process ) != FF_SUCCESS )
			{
				std::fprintf( stderr, "ProcessOpenGL failed\n" );
				return false;
			}
		}
		return true;
	}

	/// Press Roll and play the throw through to rest, plus `after` frames.
	bool RollToRest( int after = 2 )
	{
		Press( PT_ROLL );
		if( !Render( 1 ) )
			return false;
		const int frames = static_cast< int >( std::ceil( plugin.CurrentPlan().duration * fps ) ) + after;
		return Render( frames );
	}

	Floats Output() const
	{
		Floats pixels( static_cast< size_t >( width ) * height * 4 );
		glBindFramebuffer( GL_FRAMEBUFFER, outputFBO );
		glPixelStorei( GL_PACK_ALIGNMENT, 1 );
		glReadPixels( 0, 0, width, height, GL_RGBA, GL_FLOAT, pixels.data() );
		return pixels;
	}
};

//---------------------------------------------------------------------------
// Parameters by display name.
//---------------------------------------------------------------------------
struct NamedParameter
{
	std::string name;
	unsigned int index;
	float value;
	std::string kind;
};

std::vector< NamedParameter > listParameters( DicePlugin& plugin )
{
	std::vector< NamedParameter > list;
	for( unsigned int i = 0; i < plugin.ParamCount(); ++i )
	{
		const char* const name = plugin.GetParamName( i );
		list.push_back( NamedParameter { name ? name : "?", i, plugin.GetFloatParameter( i ),
		                                 kindName( plugin.GetParamType( i ) ) } );
	}
	return list;
}

bool applySetting( DicePlugin& plugin, const std::string& assignment, std::string& error )
{
	const size_t equals = assignment.find( '=' );
	if( equals == std::string::npos )
	{
		error = "expected Name=Value";
		return false;
	}
	const std::string name  = assignment.substr( 0, equals );
	const std::string value = assignment.substr( equals + 1 );
	for( const NamedParameter& parameter : listParameters( plugin ) )
		if( parameter.name == name )
		{
			if( parameter.kind == "text" || parameter.kind == "file" )
				plugin.SetTextParameter( parameter.index, value.c_str() );
			else
				plugin.SetFloatParameter( parameter.index, std::strtof( value.c_str(), nullptr ) );
			return true;
		}
	error = "no parameter called '" + name + "'";
	return false;
}

/// --pipe and --film. Raw RGBA, top row first, on the synthetic 60 fps clock.
int runPipe( bool effect, int width, int height, const std::string& scriptPath, int filmFrames,
             const std::vector< std::string >& settings, const std::vector< int >& rolls )
{
	Rig rig( effect );
	if( !rig.Init( width, height ) )
		return 1;
	for( const std::string& setting : settings )
	{
		std::string error;
		if( !applySetting( rig.plugin, setting, error ) )
		{
			std::fprintf( stderr, "--set %s: %s\n", setting.c_str(), error.c_str() );
			return 2;
		}
	}

	//A misspelt cue that silently did nothing would film a take that looks
	//deliberate and is wrong: refuse any name that is not a parameter.
	std::map< unsigned int, Track > automation;
	if( !scriptPath.empty() )
	{
		std::string error;
		const std::map< std::string, Track > tracks = loadScript( scriptPath, error );
		if( !error.empty() )
		{
			std::fprintf( stderr, "%s\n", error.c_str() );
			return 2;
		}
		const std::vector< NamedParameter > known = listParameters( rig.plugin );
		for( const auto& entry : tracks )
		{
			bool found = false;
			for( const NamedParameter& parameter : known )
				if( parameter.name == entry.first )
				{
					automation[ parameter.index ] = entry.second;
					found                         = true;
				}
			if( !found )
			{
				std::fprintf( stderr, "script names '%s', which is not a parameter (try --list)\n", entry.first.c_str() );
				return 2;
			}
		}
	}

	std::vector< unsigned char > in( static_cast< size_t >( width ) * height * 4 );
	Floats picture( in.size() );
	for( int index = 0; filmFrames < 0 || index < filmFrames; ++index )
	{
		if( filmFrames < 0 )
		{
			size_t filled = 0;
			while( filled < in.size() )
			{
				const ssize_t got = read( STDIN_FILENO, in.data() + filled, in.size() - filled );
				if( got <= 0 )
					break;
				filled += static_cast< size_t >( got );
			}
			if( filled < in.size() )
				break;
			for( int y = 0; y < height; ++y )
				for( int x = 0; x < width * 4; ++x )
					picture[ static_cast< size_t >( height - 1 - y ) * width * 4 + x ] =
						in[ static_cast< size_t >( y ) * width * 4 + x ] / 255.0f;
			if( effect )
				rig.Upload( picture );
		}

		for( const auto& track : automation )
			rig.plugin.SetFloatParameter( track.first, valueAt( track.second, index ) );
		if( std::find( rolls.begin(), rolls.end(), index ) != rolls.end() )
			rig.Press( PT_ROLL );
		if( !rig.Render( 1 ) )
			return 1;

		const Floats out = rig.Output();
		std::vector< unsigned char > bytes( in.size() );
		for( int y = 0; y < height; ++y )
			for( int x = 0; x < width * 4; ++x )
				bytes[ static_cast< size_t >( y ) * width * 4 + x ] = static_cast< unsigned char >( std::lround(
					std::clamp( out[ static_cast< size_t >( height - 1 - y ) * width * 4 + x ], 0.0f, 1.0f ) * 255.0f ) );
		size_t written = 0;
		while( written < bytes.size() )
		{
			const ssize_t put = write( STDOUT_FILENO, bytes.data() + written, bytes.size() - written );
			//The reader has gone. SIGPIPE is ignored in main(), so this is EPIPE
			//and not a silent 141: say so and stop.
			if( put <= 0 )
			{
				std::fprintf( stderr, "stdout closed at frame %d\n", index );
				return 1;
			}
			written += static_cast< size_t >( put );
		}
	}
	return 0;
}

//===========================================================================
// The checks.
//
// Each takes a Perturb. With every field at its default the check scores the
// plugin; `--negative` sets one field at a time to a deliberately wrong model
// and requires the check to FAIL.
//===========================================================================
struct Perturb
{
	bool identitySymmetry = false;///< --outcome, --readback: S = I
	bool reflectSymmetry  = false;///< --symmetry: S = -I (a reflection)
	bool biasedDraw       = false;///< --uniform: % (n - 1)
	bool noWarp           = false;///< --duration: play at 1x
	bool superBounce      = false;///< --physics: restitution 1.3, no energy guard
	bool wrongLabels      = false;///< --labels: expect opposite faces to sum to n
	bool defaultsShifted  = false;///< --defaults: expect a d6
	bool determinismSeeds = false;///< --determinism: expect seeds 3 and 4 to agree
};

using CheckFn = int ( * )( const Perturb& );
struct CheckEntry
{
	const char* flag;
	CheckFn run;
};

//---------------------------------------------------------------------------
// Shared helpers.
//---------------------------------------------------------------------------
constexpr geo::DieType kAllDice[] = { geo::DieType::D4,  geo::DieType::D6,  geo::DieType::D8, geo::DieType::D10,
	                                  geo::DieType::D12, geo::DieType::D20, geo::DieType::D100 };

/// The request a default plugin would make, at 1280x720, for this die.
roll::Request baseRequest( geo::DieType die, int count = 1 )
{
	DicePlugin plugin( false );
	plugin.SetFloatParameter( PT_DIE, static_cast< float >( die ) );
	plugin.SetFloatParameter( PT_COUNT, static_cast< float >( count ) );
	plugin.UpdateViewForTest( 1280, 720 );
	return plugin.RequestForTest();
}

/// The number on top of body i at rest, read through the rendered rotation:
/// the printed digit of the item that faces up.
int shownDigit( const roll::Plan& plan, geo::DieType die, size_t i )
{
	const roll::Track& t        = plan.bodies[ i ];
	const geo::Solid& s         = geo::GetSolid( t.shape );
	const geo::Labelling& label = geo::GetLabelling( die, t.part );
	const M3 r                  = plan.RenderRotation( i, plan.duration + 1.0 );
	return label.digit[ static_cast< size_t >( geo::TopItem( s, label.atVertex, r ) ) ];
}

/// What a die's (or a d100 pair's) shown digits add up to as a roll.
int shownValue( const roll::Plan& plan, geo::DieType die, int which )
{
	if( die == geo::DieType::D100 )
	{
		int tens = 0, units = 0;
		for( size_t i = 0; i < plan.bodies.size(); ++i )
			if( plan.bodies[ i ].die == which )
				( plan.bodies[ i ].part == 0 ? tens : units ) = shownDigit( plan, die, i );
		const int v = tens * 10 + units;
		return v == 0 ? 100 : v;
	}
	for( size_t i = 0; i < plan.bodies.size(); ++i )
		if( plan.bodies[ i ].die == which )
		{
			const int d = shownDigit( plan, die, i );
			return die == geo::DieType::D10 && d == 0 ? 10 : d;
		}
	return -1;
}

void applyPerturb( roll::Request& r, const Perturb& p )
{
	r.identitySymmetry = p.identitySymmetry;
	r.reflectSymmetry  = p.reflectSymmetry;
	r.biasedDraw       = p.biasedDraw;
	r.noWarp           = p.noWarp;
	if( p.superBounce )
	{
		r.restitution   = 1.3;
		r.noEnergyGuard = true;
	}
}

//===========================================================================
// --geometry: the solids, measured independently of how they were built.
//===========================================================================
int runGeometry( const Perturb& )
{
	std::printf( "\n=== geometry: hull, Euler, volume and inertia against a Monte Carlo of the planes\n" );
	const char* names[] = { "tetrahedron", "cube", "octahedron", "trapezohedron", "dodecahedron", "icosahedron" };
	const int faces[]   = { 4, 6, 8, 10, 12, 20 };
	for( int k = 0; k < static_cast< int >( geo::Shape::Count ); ++k )
	{
		const geo::Solid& s = geo::GetSolid( static_cast< geo::Shape >( k ) );
		const long v = static_cast< long >( s.vertices.size() ), e = static_cast< long >( s.edges.size() ),
		           f = static_cast< long >( s.faces.size() );

		//A million points in the bounding cube, kept where every plane says
		//inside: the volume and the second moment come from the PLANES, a
		//route that shares nothing with the tetrahedron fan that built them.
		const double c = s.circumradius;
		double inside = 0.0;
		double m[ 3 ][ 3 ] = {};
		const int samples = 1000000;
		for( int i = 0; i < samples; ++i )
		{
			const V3 p = { ( 2.0 * Unit( Hash( 11u, static_cast< uint32_t >( i ), 1u ) ) - 1.0 ) * c,
			               ( 2.0 * Unit( Hash( 11u, static_cast< uint32_t >( i ), 2u ) ) - 1.0 ) * c,
			               ( 2.0 * Unit( Hash( 11u, static_cast< uint32_t >( i ), 3u ) ) - 1.0 ) * c };
			bool in = true;
			for( const geo::Face& face : s.faces )
				in = in && Dot( face.normal, p ) <= face.offset;
			if( !in )
				continue;
			inside += 1.0;
			const double q[ 3 ] = { p.x, p.y, p.z };
			for( int a = 0; a < 3; ++a )
				for( int b = 0; b < 3; ++b )
					m[ a ][ b ] += q[ a ] * q[ b ];
		}
		const double box    = 8.0 * c * c * c;
		const double volume = box * inside / samples;
		const double trace  = m[ 0 ][ 0 ] + m[ 1 ][ 1 ] + m[ 2 ][ 2 ];
		double worstInertia = 0.0;
		for( int a = 0; a < 3; ++a )
			for( int b = 0; b < 3; ++b )
			{
				const double mc = ( ( a == b ? trace : 0.0 ) - m[ a ][ b ] ) / inside * s.mass;
				worstInertia    = std::max( worstInertia, std::fabs( mc - s.inertia.m[ a ][ b ] ) / s.inertia.m[ 0 ][ 0 ] );
			}
		//Tolerances from the sampling: about 1/sqrt(N kept) relative, times
		//a few for the second moment's heavier tail.
		const double relV = std::fabs( volume - s.volume ) / s.volume;
		const double kept = inside;
		const double tolV = 4.0 / std::sqrt( kept ) * std::sqrt( box / s.volume );
		Check( f == faces[ k ] && v - e + f == 2, fmt( "%-14s V %ld, E %ld, F %ld: V - E + F = %ld", names[ k ], v, e, f, v - e + f ) );
		Check( relV < tolV, fmt( "%-14s volume %.4g mm^3, Monte Carlo of the planes %.4g (%.2g, tolerance %.2g)", names[ k ],
		                          s.volume * 1e9, volume * 1e9, relV, tolV ) );
		Check( worstInertia < 3.0 * tolV, fmt( "%-14s inertia tensor matches the Monte Carlo to %.2g of I_xx (tolerance %.2g)",
		                                        names[ k ], worstInertia, 3.0 * tolV ) );
		bool flat = true;
		for( const geo::Face& face : s.faces )
			for( int index : face.loop )
				flat = flat && std::fabs( Dot( face.normal, s.vertices[ static_cast< size_t >( index ) ] ) - face.offset ) < 1e-12;
		Check( flat, fmt( "%-14s every face's vertices lie in its plane to 1e-12 m (the kites too)", names[ k ] ) );
	}
	return Verdict();
}

//===========================================================================
// --symmetry: the rotation groups, and that S changes no pixel of the shape.
//===========================================================================
int runSymmetryGroups( const Perturb& perturb )
{
	std::printf( "\n=== symmetry: each die's rotation group, closed, proper, transitive\n" );
	const char* names[] = { "d4", "d6", "d8", "d10", "d12", "d20" };
	const size_t order[] = { 12, 24, 24, 10, 60, 60 };
	for( int k = 0; k < static_cast< int >( geo::Shape::Count ); ++k )
	{
		const geo::Solid& s = geo::GetSolid( static_cast< geo::Shape >( k ) );
		int improper = 0, notClosed = 0, offVertices = 0;
		for( const M3& g : s.group )
		{
			improper += std::fabs( Determinant( g ) - 1.0 ) > 1e-9;
			for( const V3& p : s.vertices )
			{
				bool hit = false;
				for( const V3& o : s.vertices )
					hit = hit || Length( g * p - o ) < 1e-9 * s.circumradius;
				offVertices += !hit;
			}
			for( const M3& h : s.group )
			{
				bool in = false;
				for( const M3& x : s.group )
					in = in || MaxDifference( g * h, x ) < 1e-9;
				notClosed += !in;
			}
		}
		//Transitive on what is read: faces, or the d4's vertices.
		const bool atVertex = k == 0;
		const size_t items  = atVertex ? s.vertices.size() : s.faces.size();
		size_t reached      = 0;
		for( size_t to = 0; to < items; ++to )
			reached += !geo::SymmetriesTaking( s, atVertex, 0, static_cast< int >( to ) ).empty();
		Check( s.group.size() == order[ k ] && improper == 0 && notClosed == 0 && offVertices == 0 && reached == items,
		       fmt( "%-4s %2zu rotations (want %zu), all det +1, closed under composition, each carries the vertices onto "
		            "themselves; every %s reached from the first",
		            names[ k ], s.group.size(), order[ k ], atVertex ? "vertex" : "face" ) );
	}

	//The S a plan actually uses must be in the group and proper.
	int badS = 0, plans = 0;
	for( geo::DieType die : kAllDice )
		for( uint32_t seed = 1; seed <= 3; ++seed )
		{
			roll::Request r = baseRequest( die );
			applyPerturb( r, perturb );
			r.seed = seed;
			const roll::Plan plan = roll::MakePlan( r );
			++plans;
			for( const roll::Track& t : plan.bodies )
			{
				const geo::Solid& s = geo::GetSolid( t.shape );
				bool member         = false;
				for( const M3& g : s.group )
					member = member || MaxDifference( g, t.S ) < 1e-12;
				badS += !member || std::fabs( Determinant( t.S ) - 1.0 ) > 1e-9;
			}
		}
	Check( badS == 0, fmt( "%d plans: every body's S is an element of its group, det +1 (%d not)", plans, badS ) );
	return Verdict();
}

//===========================================================================
// --labels: how a real set numbers its dice.
//===========================================================================
int runLabels( const Perturb& perturb )
{
	std::printf( "\n=== labels: every value once, opposite faces summing as a real set's do\n" );
	for( geo::DieType die : kAllDice )
		for( int part = 0; part < geo::BodiesPerDie( die ); ++part )
		{
			const geo::Solid& s         = geo::GetSolid( geo::ShapeOf( die ) );
			const geo::Labelling& label = geo::GetLabelling( die, part );
			std::multiset< int > seen( label.digit.begin(), label.digit.end() );
			const bool zeroBased = die == geo::DieType::D10 || die == geo::DieType::D100;
			const int n          = static_cast< int >( label.digit.size() );
			bool once            = true;
			for( int v = 0; v < n; ++v )
				once = once && seen.count( zeroBased ? v : v + 1 ) == 1;
			std::string name = std::string( geo::DieName( die ) ) + ( die == geo::DieType::D100 ? ( part == 0 ? " tens" : " units" ) : "" );
			if( label.atVertex )
			{
				//Each face prints its three vertices' numbers, each near its
				//vertex: the number at the top is the same on all three faces.
				const auto& slots = geo::GetSlots( geo::ShapeOf( die ) );
				int wrong         = 0;
				for( const geo::Slot& slot : slots )
				{
					const auto& loop = s.faces[ static_cast< size_t >( slot.face ) ].loop;
					wrong += std::find( loop.begin(), loop.end(), slot.item ) == loop.end();
					const V3 toward = Normalise( s.vertices[ static_cast< size_t >( slot.item ) ] - slot.centre );
					wrong += Dot( toward, slot.up ) < 0.999;
				}
				Check( once && slots.size() == 12 && wrong == 0,
				       fmt( "%-9s 1-4 once each, at the vertices; 12 printings, each on a face that has the vertex, top toward it (%d wrong)",
				            name.c_str(), wrong ) );
				continue;
			}
			const int sum = ( zeroBased ? n - 1 : n + 1 ) + ( perturb.wrongLabels ? -1 : 0 );
			int pairsWrong = 0;
			for( size_t f = 0; f < s.faces.size(); ++f )
				pairsWrong += label.digit[ f ] + label.digit[ static_cast< size_t >( s.opposite[ f ] ) ] != sum;
			Check( once && pairsWrong == 0, fmt( "%-9s every value once; opposite faces sum to %d (%d faces do not)", name.c_str(), sum, pairsWrong ) );
		}

	//The western d6: 1, 2, 3 counter-clockwise round their shared corner.
	{
		const geo::Solid& s         = geo::GetSolid( geo::Shape::Cube );
		const geo::Labelling& label = geo::GetLabelling( geo::DieType::D6, 0 );
		V3 n[ 4 ];
		for( size_t f = 0; f < s.faces.size(); ++f )
			if( label.digit[ f ] <= 3 )
				n[ label.digit[ f ] ] = s.faces[ f ].normal;
		const V3 corner = Normalise( n[ 1 ] + n[ 2 ] + n[ 3 ] );
		Check( Dot( Cross( n[ 1 ], n[ 2 ] ), corner ) > 0.0 && Dot( Cross( n[ 2 ], n[ 3 ] ), corner ) > 0.0,
		       "d6: 1, 2 and 3 meet at a corner and run counter-clockwise round it, seen from outside (western)" );
	}

	//Balanced: how far the sum round each vertex strays from the mean.
	for( geo::DieType die : { geo::DieType::D8, geo::DieType::D12, geo::DieType::D20 } )
	{
		const geo::Solid& s         = geo::GetSolid( geo::ShapeOf( die ) );
		const geo::Labelling& label = geo::GetLabelling( die, 0 );
		double worst                = 0.0;
		for( size_t v = 0; v < s.vertices.size(); ++v )
		{
			double sum = 0.0;
			int around = 0;
			for( size_t f = 0; f < s.faces.size(); ++f )
				for( int index : s.faces[ f ].loop )
					if( index == static_cast< int >( v ) )
					{
						sum += label.digit[ f ];
						++around;
					}
			const double mean = 0.5 * ( s.faces.size() + 1 ) * around;
			worst             = std::max( worst, std::fabs( sum - mean ) / mean );
		}
		std::printf( "  info  %-4s the sum round any vertex is within %.0f%% of the mean\n", geo::DieName( die ), worst * 100.0 );
	}
	return Verdict();
}

//===========================================================================
// --outcome: the number on top is the number asked for.
//===========================================================================
int runOutcome( const Perturb& perturb )
{
	std::printf( "\n=== outcome: Fixed, every value of every die, at rest the top reads what was asked\n" );
	for( geo::DieType die : kAllDice )
	{
		const int sides = geo::Sides( die );
		int wrong = 0, plans = 0, idle = 0;
		for( int value = 1; value <= sides; ++value )
			for( uint32_t seed : { 1u, 2u } )
			{
				//The d100's hundred values, on one seed: 100 throws is plenty.
				if( die == geo::DieType::D100 && seed == 2u )
					continue;
				roll::Request r = baseRequest( die );
				applyPerturb( r, perturb );
				r.fixed      = true;
				r.fixedTotal = value;
				r.seed       = seed;
				r.roll       = static_cast< uint32_t >( value );
				const roll::Plan plan = roll::MakePlan( r );
				++plans;
				idle += plan.idle;
				wrong += shownValue( plan, die, 0 ) != value;
			}
		Check( wrong == 0, fmt( "%-4s %3d throws, every value: %d showed something else (%d fell back to the resting layout)",
		                        geo::DieName( die ), plans, wrong, idle ) );
	}

	std::printf( "\n  Random: every throw shows what was drawn, and the draw is a face the die has\n" );
	for( geo::DieType die : kAllDice )
	{
		int wrong = 0, outOfRange = 0, plans = 0;
		for( uint32_t roll = 1; roll <= 12; ++roll )
		{
			roll::Request r = baseRequest( die, 2 );
			applyPerturb( r, perturb );
			r.seed = 21;
			r.roll = roll;
			const roll::Plan plan = roll::MakePlan( r );
			++plans;
			for( int d = 0; d < 2; ++d )
			{
				const int drawn = plan.results[ static_cast< size_t >( d ) ];
				outOfRange += drawn < 1 || drawn > geo::Sides( die );
				wrong += shownValue( plan, die, d ) != drawn;
			}
		}
		Check( wrong == 0 && outOfRange == 0, fmt( "%-4s %d throws of two: %d dice showed something other than their draw, %d draws "
		                                            "outside 1..%d", geo::DieName( die ), plans, wrong, outOfRange, geo::Sides( die ) ) );
	}

	std::printf( "\n  totals over several dice: the dice's top values add up to the Fixed Total\n" );
	for( geo::DieType die : kAllDice )
	{
		int wrong = 0, plans = 0;
		for( int count = 2; count <= kMaxCount; count += 2 )
			for( int k = 0; k < 3; ++k )
			{
				roll::Request r = baseRequest( die, count );
				applyPerturb( r, perturb );
				r.fixed      = true;
				const int lo = count, hi = count * geo::Sides( die );
				r.fixedTotal = lo + ( hi - lo ) * ( k + 1 ) / 4;
				r.seed       = static_cast< uint32_t >( 10 + k );
				r.roll       = static_cast< uint32_t >( count );
				const roll::Plan plan = roll::MakePlan( r );
				++plans;
				int total = 0;
				for( int d = 0; d < count; ++d )
					total += shownValue( plan, die, d );
				wrong += total != r.fixedTotal || plan.total != r.fixedTotal;
			}
		Check( wrong == 0, fmt( "%-4s %d throws of 2, 4 and 6 dice: %d totals wrong", geo::DieName( die ), plans, wrong ) );
	}
	return Verdict();
}

//===========================================================================
// --uniform: Random is uniform.
//===========================================================================
/// The chi-square value that p = 0.001 leaves above it, by Wilson-Hilferty:
/// good to well under 1% for these degrees of freedom.
double chiSquareCritical( int dof )
{
	const double z = 3.0902;//one-sided 0.001
	const double k = static_cast< double >( dof );
	const double a = 1.0 - 2.0 / ( 9.0 * k ) + z * std::sqrt( 2.0 / ( 9.0 * k ) );
	return k * a * a * a;
}

int runUniform( const Perturb& perturb )
{
	std::printf( "\n=== uniform: Random's draws, chi-square at p = 0.001\n" );
	for( geo::DieType die : kAllDice )
	{
		const int sides = geo::Sides( die );
		const int rolls = 400 * sides;
		std::vector< double > counts( static_cast< size_t >( sides ), 0.0 );
		roll::Request r = baseRequest( die );
		applyPerturb( r, perturb );
		r.seed = 7;
		for( int i = 0; i < rolls; ++i )
		{
			r.roll = static_cast< uint32_t >( i );
			const int v = roll::DrawResults( r )[ 0 ];
			if( v >= 1 && v <= sides )
				counts[ static_cast< size_t >( v - 1 ) ] += 1.0;
		}
		double chi = 0.0;
		const double expected = static_cast< double >( rolls ) / sides;
		for( double c : counts )
			chi += ( c - expected ) * ( c - expected ) / expected;
		const double critical = chiSquareCritical( sides - 1 );
		Check( chi < critical, fmt( "%-4s %6d draws: chi-square %.1f, critical %.1f (%d dof)", geo::DieName( die ), rolls, chi,
		                            critical, sides - 1 ) );
	}

	//A Fixed total over two d6 is split uniformly over the tuples that make it.
	{
		roll::Request r = baseRequest( geo::DieType::D6, 2 );
		applyPerturb( r, perturb );
		r.fixed      = true;
		r.fixedTotal = 7;
		std::vector< double > counts( 6, 0.0 );
		const int rolls = 6000;
		for( int i = 0; i < rolls; ++i )
		{
			r.roll = static_cast< uint32_t >( i );
			const std::vector< int > v = roll::DrawResults( r );
			if( v.size() == 2 && v[ 0 ] + v[ 1 ] == 7 )
				counts[ static_cast< size_t >( v[ 0 ] - 1 ) ] += 1.0;
		}
		double chi = 0.0, sum = 0.0;
		for( double c : counts )
			sum += c;
		for( double c : counts )
			chi += ( c - sum / 6.0 ) * ( c - sum / 6.0 ) / ( sum / 6.0 );
		Check( sum == rolls && chi < chiSquareCritical( 5 ),
		       fmt( "2d6 Fixed 7: every split sums to 7, the six (a, 7 - a) equally likely: chi-square %.1f, critical %.1f", chi,
		            chiSquareCritical( 5 ) ) );
	}
	return Verdict();
}

//===========================================================================
// --rest: at the end, flat, still, in the arena, apart.
//===========================================================================
/// Do two convex bodies overlap? The separating-axis test over face normals
/// and edge-edge cross products, with a small allowance for contact.
bool overlapping( const geo::Solid& a, const M3& ra, V3 xa, const geo::Solid& b, const M3& rb, V3 xb, double slack )
{
	std::vector< V3 > pa, pb;
	for( const V3& v : a.vertices )
		pa.push_back( xa + ra * v );
	for( const V3& v : b.vertices )
		pb.push_back( xb + rb * v );
	std::vector< V3 > axes;
	for( const geo::Face& f : a.faces )
		axes.push_back( ra * f.normal );
	for( const geo::Face& f : b.faces )
		axes.push_back( rb * f.normal );
	for( const auto& ea : a.edges )
		for( const auto& eb : b.edges )
		{
			const V3 da = ra * ( a.vertices[ static_cast< size_t >( ea[ 1 ] ) ] - a.vertices[ static_cast< size_t >( ea[ 0 ] ) ] );
			const V3 db = rb * ( b.vertices[ static_cast< size_t >( eb[ 1 ] ) ] - b.vertices[ static_cast< size_t >( eb[ 0 ] ) ] );
			const V3 axis = Cross( da, db );
			if( Length( axis ) > 1e-12 )
				axes.push_back( Normalise( axis ) );
		}
	for( const V3& axis : axes )
	{
		double minA = 1e30, maxA = -1e30, minB = 1e30, maxB = -1e30;
		for( const V3& p : pa )
		{
			minA = std::min( minA, Dot( p, axis ) );
			maxA = std::max( maxA, Dot( p, axis ) );
		}
		for( const V3& p : pb )
		{
			minB = std::min( minB, Dot( p, axis ) );
			maxB = std::max( maxB, Dot( p, axis ) );
		}
		if( maxA < minB + slack || maxB < minA + slack )
			return false;
	}
	return true;
}

int runRest( const Perturb& perturb )
{
	std::printf( "\n=== rest: every die ends flat on the table, still, inside the walls, apart\n" );
	for( geo::DieType die : kAllDice )
	{
		int plans = 0, notFlat = 0, notDown = 0, moving = 0, outside = 0, overlaps = 0, idle = 0;
		double worstTilt = 0.0;
		for( int count : { 1, 3, 6 } )
			for( uint32_t seed = 1; seed <= 4; ++seed )
			{
				roll::Request r = baseRequest( die, count );
				applyPerturb( r, perturb );
				r.seed = seed;
				r.roll = seed * 7u;
				const roll::Plan plan = roll::MakePlan( r );
				++plans;
				idle += plan.idle;
				physics::World walls = roll::MakeWorld( r );
				std::vector< roll::Pose > end;
				for( size_t i = 0; i < plan.bodies.size(); ++i )
				{
					const roll::Pose pose = plan.PoseAt( i, plan.duration );
					end.push_back( pose );
					const geo::Solid& s = geo::GetSolid( plan.bodies[ i ].shape );
					const M3 rot        = ToMatrix( pose.q );
					double lowest = 1e30, flatness = -2.0;
					for( const geo::Face& f : s.faces )
						flatness = std::max( flatness, -( rot * f.normal ).y );
					for( const V3& v : s.vertices )
					{
						const V3 p = pose.x + rot * v;
						lowest     = std::min( lowest, p.y );
						for( size_t w = 1; w < walls.planes.size(); ++w )
							outside += Dot( walls.planes[ w ].normal, p - plan.shift ) - walls.planes[ w ].offset < -1e-4;
					}
					const double tilt = std::acos( std::clamp( flatness, -1.0, 1.0 ) ) * 180.0 / kPi;
					worstTilt         = std::max( worstTilt, tilt );
					notFlat += tilt > 0.01;
					notDown += std::fabs( lowest ) > 1e-9;
					const roll::Pose later = plan.PoseAt( i, plan.duration + 5.0 );
					moving += Length( later.x - pose.x ) > 0.0 || std::fabs( later.q.w - pose.q.w ) > 0.0;
				}
				for( size_t i = 0; i < end.size(); ++i )
					for( size_t j = i + 1; j < end.size(); ++j )
						overlaps += overlapping( geo::GetSolid( plan.bodies[ i ].shape ), ToMatrix( end[ i ].q ), end[ i ].x,
						                         geo::GetSolid( plan.bodies[ j ].shape ), ToMatrix( end[ j ].q ), end[ j ].x, 2e-4 );
			}
		Check( notFlat + notDown + moving + outside + overlaps == 0,
		       fmt( "%-4s %d throws of 1, 3, 6: tilted %d (worst %.4f deg), off the table %d, moving %d, vertices through a wall %d, "
		            "overlapping pairs %d; resting layouts %d",
		            geo::DieName( die ), plans, notFlat, worstTilt, notDown, moving, outside, overlaps, idle ) );
	}
	return Verdict();
}

//===========================================================================
// --duration: the dice come to rest at Roll Time.
//===========================================================================
int runDuration( const Perturb& perturb )
{
	std::printf( "\n=== duration: the last die stops at exactly Roll Time, and not a frame sooner\n" );
	const double frame = 1.0 / 60.0;
	for( geo::DieType die : { geo::DieType::D4, geo::DieType::D6, geo::DieType::D20, geo::DieType::D100 } )
		for( double rollTime : { 0.5, 1.0, 2.0, 4.0, 8.0 } )
		{
			roll::Request r = baseRequest( die, 2 );
			applyPerturb( r, perturb );
			r.rollTime = rollTime;
			r.seed     = 5;
			const roll::Plan plan = roll::MakePlan( r );
			//At Roll Time the simulation has reached the moment the last die
			//settled; a frame earlier at least one die is still moving.
			const double atEnd = plan.SimTime( rollTime );
			bool stillMoving   = false;
			for( size_t i = 0; i < plan.bodies.size(); ++i )
			{
				const roll::Pose a = plan.PoseAt( i, rollTime - frame );
				const roll::Pose b = plan.PoseAt( i, rollTime );
				stillMoving        = stillMoving || Length( a.x - b.x ) > 1e-9
				              || std::fabs( a.q.w - b.q.w ) + std::fabs( a.q.x - b.q.x ) + std::fabs( a.q.y - b.q.y ) > 1e-9;
			}
			bool frozenAfter = true;
			for( size_t i = 0; i < plan.bodies.size(); ++i )
			{
				const roll::Pose a = plan.PoseAt( i, rollTime );
				const roll::Pose b = plan.PoseAt( i, rollTime + 10.0 );
				frozenAfter        = frozenAfter && Length( a.x - b.x ) == 0.0 && a.q.w == b.q.w && a.q.x == b.q.x;
			}
			//The rate never stops or runs backwards, and starts at real speed
			//whenever the throw was at least 5/8 of Roll Time.
			const bool monotone = plan.rateStart > 0.0 && plan.rateEnd > 0.0;
			Check( std::fabs( atEnd - plan.natural ) < 1e-9 && stillMoving && frozenAfter && monotone,
			       fmt( "%-4s x2, Roll Time %.1f s: the throw settles in %.3f s; at Roll Time sim %.3f s; moving a frame before: "
			            "%s; still after: %s; rate %.2f -> %.2f",
			            geo::DieName( die ), rollTime, plan.natural, atEnd, stillMoving ? "yes" : "NO", frozenAfter ? "yes" : "NO",
			            plan.rateStart, plan.rateEnd ) );
		}
	return Verdict();
}

//===========================================================================
// --physics: the integrator and the contacts.
//===========================================================================
int runPhysics( const Perturb& perturb )
{
	std::printf( "\n=== physics: free flight is the parabola; contacts never make energy; a drop bounces at Bounce\n" );

	//Free flight, far above everything: x = x0 + v0 t - g t^2 / 2 exactly.
	{
		physics::World world;
		physics::Body body;
		body.solid = &geo::GetSolid( geo::Shape::Icosahedron );
		body.x     = { 0.01, 1.0, -0.02 };
		body.v     = { 0.3, 1.5, -0.2 };
		body.w     = { 3.0, -7.0, 11.0 };
		world.bodies.push_back( body );
		const physics::Body start = body;
		const int steps           = 480;
		double worst              = 0.0;
		for( int i = 1; i <= steps; ++i )
		{
			world.Step();
			const double t = i * world.settings.dt;
			const V3 want  = start.x + start.v * t + V3 { 0.0, -0.5 * world.settings.gravity * t * t, 0.0 };
			worst          = std::max( worst, Length( world.bodies[ 0 ].x - want ) );
		}
		const double e0 = 0.5 * body.solid->mass * Dot( start.v, start.v ) + body.solid->mass * world.settings.gravity * start.x.y;
		const physics::Body& b = world.bodies[ 0 ];
		const double e1 = 0.5 * body.solid->mass * Dot( b.v, b.v ) + body.solid->mass * world.settings.gravity * b.x.y;
		Check( worst < 1e-12, fmt( "free flight, 0.5 s: furthest from the exact parabola %.2g m (one rounding is ~1e-16 m)", worst ) );
		Check( std::fabs( e1 - e0 ) < 1e-12 * std::fabs( e0 ),
		       fmt( "free flight: translational energy conserved to %.2g of itself", std::fabs( e1 - e0 ) / std::fabs( e0 ) ) );
	}

	//Every die, thrown hard into the walls and each other for three seconds,
	//straight through the world (not through the planner, which would hide a
	//world that never settles behind its resting fallback): no velocity solve
	//that was KEPT added kinetic energy.
	{
		double worstKept = 0.0, worstRejected = 0.0;
		long retries = 0, steps = 0, contacts = 0;
		for( geo::DieType die : kAllDice )
			for( int count : { 1, 4 } )
			{
				roll::Request r = baseRequest( die, count );
				applyPerturb( r, perturb );
				physics::World world = roll::MakeWorld( r );
				const int bodies     = count * geo::BodiesPerDie( die );
				for( int b = 0; b < bodies; ++b )
				{
					physics::Body body;
					body.solid = &geo::GetSolid( geo::ShapeOf( die ) );
					body.x     = { -0.04 + 0.025 * b, 0.03 + 0.01 * b, 0.0 };
					body.v     = { 1.5 - 0.4 * b, 0.2, 0.3 * ( b % 2 ? 1.0 : -1.0 ) };
					body.w     = { 20.0, -35.0 + 7.0 * b, 12.0 };
					body.q     = Normalise( Quat { 0.9, 0.1 * b, -0.3, 0.2 } );
					world.bodies.push_back( body );
				}
				for( int i = 0; i < 3 * 960; ++i )
					world.Step();
				worstKept     = std::max( worstKept, world.stats.worstKeptGain );
				worstRejected = std::max( worstRejected, world.stats.worstRejectedGain );
				retries += world.stats.inelasticRetries;
				steps += world.stats.steps;
				contacts += world.stats.contacts;
			}
		//1e-12 of the energy is a few roundings of the sum it is made of.
		Check( steps > 0 && contacts > 0 && worstKept <= 1e-12,
		       fmt( "14 worlds, %ld steps, %ld contacts: no kept contact solve added kinetic energy (worst %.2g of it); "
		            "%ld elastic solves (worst +%.2g J) were redone inelastically to keep it so",
		            steps, contacts, worstKept, retries, worstRejected ) );
	}

	//A cube dropped flat, no spin: the rebound height is e^2 times the drop.
	for( double e : { 0.2, 0.5, 0.8 } )
	{
		roll::Request r = baseRequest( geo::DieType::D6 );
		r.restitution   = perturb.superBounce ? 1.3 : e;
		r.noEnergyGuard = perturb.superBounce;
		physics::World world = roll::MakeWorld( r );
		physics::Body body;
		body.solid = &geo::GetSolid( geo::Shape::Cube );
		const double h0 = 0.05;
		body.x     = { 0.0, body.solid->inradius + h0, 0.0 };
		world.bodies.push_back( body );
		double apex = 0.0;
		bool bounced = false;
		for( int i = 0; i < 960; ++i )
		{
			world.Step();
			const physics::Body& b = world.bodies[ 0 ];
			if( b.v.y > 0.0 )
				bounced = true;
			if( bounced )
				apex = std::max( apex, b.x.y - body.solid->inradius );
			if( bounced && b.v.y < 0.0 )
				break;
		}
		const double measured = std::sqrt( std::max( apex, 0.0 ) / h0 );
		//One step's travel at impact (v dt, ~1 mm from 5 cm) against the
		//drop: the contact is found up to a step late, which shifts the
		//apex by that much of h0.
		const double v      = std::sqrt( 2.0 * world.settings.gravity * h0 );
		const double tol    = v * world.settings.dt / h0 + 0.005;
		Check( std::fabs( measured - e ) < tol, fmt( "a flat drop from 5 cm at Bounce e = %.1f rebounds with e = %.3f (tolerance %.3f)", e,
		                                              measured, tol ) );
	}
	return Verdict();
}

//===========================================================================
// --determinism
//===========================================================================
int runDeterminism( const Perturb& perturb )
{
	std::printf( "\n=== determinism: the same seed and roll are the same throw, bit for bit\n" );
	roll::Request a = baseRequest( geo::DieType::D20, 3 );
	a.seed          = 3;
	roll::Request b = a;
	b.seed          = perturb.determinismSeeds ? 4 : 3;
	const roll::Plan pa = roll::MakePlan( a ), pb = roll::MakePlan( b );
	bool same = pa.bodies.size() == pb.bodies.size() && pa.results == pb.results;
	for( size_t i = 0; same && i < pa.bodies.size(); ++i )
	{
		same = same && pa.bodies[ i ].keys.size() == pb.bodies[ i ].keys.size();
		for( size_t k = 0; same && k < pa.bodies[ i ].keys.size(); ++k )
			same = same && std::memcmp( &pa.bodies[ i ].keys[ k ], &pb.bodies[ i ].keys[ k ], sizeof( roll::Pose ) ) == 0;
	}
	Check( same, "3 d20, seed 3, twice: identical results and identical keyframes, byte for byte" );
	roll::Request c = a;
	c.seed          = 4;
	const roll::Plan pc = roll::MakePlan( c );
	bool differs = pc.bodies.size() != pa.bodies.size() || pc.results != pa.results;
	for( size_t i = 0; !differs && i < pa.bodies.size(); ++i )
		differs = Length( pa.bodies[ i ].keys.back().x - pc.bodies[ i ].keys.back().x ) > 1e-6;
	Check( differs, "seed 4 is a different throw" );
	roll::Request d = a;
	d.roll          = a.roll + 1;
	Check( roll::MakePlan( d ).results != pa.results || roll::MakePlan( d ).bodies[ 0 ].keys.back().x.x != pa.bodies[ 0 ].keys.back().x.x,
	       "the next roll on the same seed is a different throw" );
	return Verdict();
}

//===========================================================================
// --defaults and --names
//===========================================================================
int runDefaults( const Perturb& perturb )
{
	std::printf( "\n=== defaults: what the README says a fresh instance is\n" );
	DicePlugin plugin( false );
	const geo::DieType want = perturb.defaultsShifted ? geo::DieType::D6 : geo::DieType::D20;
	Check( plugin.Die() == want, fmt( "the die is a %s", geo::DieName( want ) ) );
	Check( plugin.Count() == 1, "one of it" );
	Check( std::fabs( RollTimeFromParam( plugin.GetFloatParameter( PT_ROLL_TIME ) ) - 1.2 ) < 1e-5, "Roll Time 1.2 s" );
	Check( std::fabs( BounceFromParam( plugin.GetFloatParameter( PT_BOUNCE ) ) - 0.5 ) < 1e-6, "Bounce e = 0.5" );
	Check( std::fabs( SizeFromParam( plugin.GetFloatParameter( PT_SIZE ) ) - 0.22 ) < 1e-6, "Size 0.22 of the frame" );
	Check( OptionIndex( plugin.GetFloatParameter( PT_RESULT ), 2 ) == 0, "Result Random" );
	Check( OptionIndex( plugin.GetFloatParameter( PT_TABLE ), 3 ) == 0, "Table None: transparent but for the shadows" );
	return Verdict();
}

int runNames( const Perturb& )
{
	std::printf( "\n=== names: unique as Arena addresses them (lower case, no spaces), within 16 characters\n" );
	for( bool effect : { false, true } )
	{
		DicePlugin plugin( effect );
		std::map< std::string, int > seen;
		int longNames = 0, clashes = 0;
		for( unsigned int i = 0; i < plugin.ParamCount(); ++i )
		{
			const std::string name = plugin.GetParamName( i ) ? plugin.GetParamName( i ) : "";
			if( name.size() > 16 )
			{
				std::printf( "    too long: %s\n", name.c_str() );
				++longNames;
			}
			std::string address;
			for( char c : name )
				if( c != ' ' )
					address += static_cast< char >( std::tolower( static_cast< unsigned char >( c ) ) );
			if( seen[ address ]++ > 0 )
			{
				std::printf( "    clash: %s\n", name.c_str() );
				++clashes;
			}
			for( char c : name )
				clashes += c == '/' || c == '#' || c == '*' || c == '?' || c == '[' || c == ']' || c == '{' || c == '}' || c == ',';
		}
		Check( longNames == 0 && clashes == 0, fmt( "%s: %u parameters, %d too long, %d clash or carry an OSC-reserved character",
		                                            effect ? "SW Dice Over" : "SW Polyhedral", plugin.ParamCount(), longNames, clashes ) );
	}
	return Verdict();
}

//===========================================================================
// The picture checks (GL).
//===========================================================================
void quietScene( Rig& rig )
{
	//White plastic, black ink, no shadow, no table, straight down: the number
	//is the only dark thing on the die.
	rig.Set( PT_CAMERA_ANGLE, 1.0f );
	rig.Set( PT_TEXTURE, static_cast< float >( Texture::Plastic ) );
	rig.Set( PT_COLOUR_R, 1.0f );
	rig.Set( PT_COLOUR_G, 1.0f );
	rig.Set( PT_COLOUR_B, 1.0f );
	rig.Set( PT_INK_R, 0.0f );
	rig.Set( PT_INK_G, 0.0f );
	rig.Set( PT_INK_B, 0.0f );
	rig.Set( PT_GLOSS, 0.0f );
	rig.Set( PT_SHADOW, 0.0f );
	rig.Set( PT_TABLE, 0.0f );
}

float luminance( const Floats& img, int w, int x, int y )
{
	const float* p = &img[ ( static_cast< size_t >( y ) * w + x ) * 4 ];
	return 0.2126f * p[ 0 ] + 0.7152f * p[ 1 ] + 0.0722f * p[ 2 ];
}

struct ReadResult
{
	std::string best;          ///< the candidate whose ink fits best (intersection over union)
	double bestIoU   = 0.0;
	double targetIoU = 0.0;
	/// Against every rival number: of the pixels where the wanted number and
	/// that rival disagree about ink, the share where the picture sides with
	/// the wanted one. The worst rival's share. Shared strokes ("30" and "80"
	/// both end in 0) are not evidence either way, so they are not counted.
	double worstDispute = 0.0;
	std::string rival;
	int pixels = 0;
};

/// Read the number printed in `slot` of body `i` out of the picture: the ink
/// inside the slot's circle, against every candidate number of that die laid
/// out in the same slot.
ReadResult readSlot( Rig& rig, const Floats& img, size_t i, const geo::Slot& slot, const std::string& target )
{
	ReadResult out;
	const roll::Plan& plan      = rig.plugin.CurrentPlan();
	const geo::DieType die      = rig.plugin.Die();
	const roll::Track& track    = plan.bodies[ i ];
	const geo::Solid& solid     = geo::GetSolid( track.shape );
	const geo::Labelling& label = geo::GetLabelling( die, track.part );
	const roll::Pose pose       = plan.PoseAt( i, plan.duration );
	const M3 r                  = ToMatrix( pose.q ) * track.S;
	const M3 rT                 = Transpose( r );
	const Camera& cam           = rig.plugin.CurrentCamera();
	const geo::Face& face       = solid.faces[ static_cast< size_t >( slot.face ) ];
	const V3 nWorld             = r * face.normal;
	const double offsetWorld    = Dot( nWorld, pose.x ) + face.offset;
	const V3 right              = Cross( slot.up, face.normal );

	struct Candidate
	{
		std::string text;
		SlotLayout layout;
		double both = 0, either = 0;
		std::vector< bool > mask;
		std::vector< float > edge;///< pixels from this number's outline
	};
	std::vector< Candidate > candidates;
	for( size_t k = 0; k < label.text.size(); ++k )
	{
		bool known = false;
		for( const Candidate& c : candidates )
			known = known || c.text == label.text[ k ];
		if( !known )
			candidates.push_back( { label.text[ k ], rig.plugin.LayoutCandidate( slot, label.text[ k ], label.digit[ k ], label.marked[ k ] ), 0, 0, {} } );
	}

	double cx = 0, cy = 0, ex = 0, ey = 0;
	cam.Project( pose.x + r * slot.centre, rig.width, rig.height, cx, cy );
	cam.Project( pose.x + r * ( slot.centre + right * slot.room ), rig.width, rig.height, ex, ey );
	const double radius = std::hypot( ex - cx, ey - cy ) + 2.0;
	//Metres of face per pixel, near enough: the room over its radius in pixels.
	const double perPixel = slot.room / std::max( radius - 2.0, 1.0 );
	//Ink as COVERAGE, not a threshold: a serif's hairline is a pixel wide and
	//mostly lands as grey. The face's own brightness is the paper (it varies
	//with the light), estimated as the 90th percentile inside the circle.
	std::vector< float > lums;
	std::vector< std::array< int, 2 > > where;
	for( int y = static_cast< int >( cy - radius ); y <= static_cast< int >( cy + radius ); ++y )
		for( int x = static_cast< int >( cx - radius ); x <= static_cast< int >( cx + radius ); ++x )
		{
			if( x < 0 || y < 0 || x >= rig.width || y >= rig.height )
				continue;
			const V3 ray     = cam.Ray( x + 0.5, y + 0.5, rig.width, rig.height );
			const double den = Dot( nWorld, ray );
			if( den > -1e-6 )
				continue;
			const V3 hit  = cam.position + ray * ( ( offsetWorld - Dot( nWorld, cam.position ) ) / den );
			const V3 body = rT * ( hit - pose.x );
			const V3 rel  = body - slot.centre;
			if( Length( rel ) > 0.9 * slot.room )
				continue;
			++out.pixels;
			lums.push_back( luminance( img, rig.width, x, y ) );
			where.push_back( { x, y } );
			for( Candidate& c : candidates )
			{
				const float qx   = static_cast< float >( Dot( rel, right ) / c.layout.height );
				const float qy   = static_cast< float >( Dot( rel, slot.up ) / c.layout.height );
				const float d    = LayoutDistance( c.layout, rig.plugin.CurrentAtlas(), qx, qy );
				c.mask.push_back( d > 0.0f );
				c.edge.push_back( static_cast< float >( std::fabs( d ) * c.layout.height / perPixel ) );
			}
		}
	std::vector< float > sorted = lums;
	std::sort( sorted.begin(), sorted.end() );
	const float paper = sorted.empty() ? 1.0f : std::max( sorted[ sorted.size() * 9 / 10 ], 1e-3f );
	std::vector< double > observed( lums.size() );
	//Within 15% of the paper is paper (the light falls off across a face);
	//from there to a quarter of it, ink rising linearly.
	for( size_t k = 0; k < lums.size(); ++k )
		observed[ k ] = std::clamp( ( 0.85 * paper - lums[ k ] ) / ( 0.6 * paper ), 0.0, 1.0 );
	for( Candidate& c : candidates )
		for( size_t k = 0; k < observed.size(); ++k )
		{
			const double want = c.mask[ k ] ? 1.0 : 0.0;
			c.both += std::min( want, observed[ k ] );
			c.either += std::max( want, observed[ k ] );
		}
	const Candidate* wanted = nullptr;
	for( const Candidate& c : candidates )
	{
		const double iou = c.either > 0.0 ? c.both / c.either : 0.0;
		if( iou > out.bestIoU )
		{
			out.bestIoU = iou;
			out.best    = c.text;
		}
		if( c.text == target )
		{
			out.targetIoU = iou;
			wanted        = &c;
		}
	}
	//DITEST_DUMP=file.png (DITEST_DUMP_WANT=text to pick one): the slot's
	//pixels as red = ink seen, green = the wanted number, blue = the rival it
	//disputes worst (written below).
	out.worstDispute = wanted != nullptr ? 1.0 : 0.0;
	if( wanted != nullptr )
		for( const Candidate& c : candidates )
		{
			if( &c == wanted )
				continue;
			//Only where the two disagree by more than a pixel: within a pixel
			//of either outline the picture is antialiased, half ink whichever
			//number it is, and two layouts a pixel apart would dispute there.
			double disputed = 0.0, sided = 0.0;
			for( size_t k = 0; k < observed.size(); ++k )
				if( wanted->mask[ k ] != c.mask[ k ] && wanted->edge[ k ] > 1.0f && c.edge[ k ] > 1.0f )
				{
					disputed += 1.0;
					sided += wanted->mask[ k ] ? observed[ k ] : 1.0 - observed[ k ];
				}
			const double share = disputed > 0.0 ? sided / disputed : 0.0;
			if( share < out.worstDispute )
			{
				out.worstDispute = share;
				out.rival        = c.text;
			}
		}
	const char* dumpWant = std::getenv( "DITEST_DUMP_WANT" );
	if( const char* dump = std::getenv( "DITEST_DUMP" ); dump != nullptr && ( dumpWant == nullptr || target == dumpWant ) )
	{
		const Candidate* rival = nullptr;
		for( const Candidate& c : candidates )
			if( c.text == out.rival )
				rival = &c;
		Floats picture( static_cast< size_t >( rig.width ) * rig.height * 4, 0.0f );
		size_t k = 0;
		for( size_t p = 0; p < where.size(); ++p, ++k )
		{
			float* o = &picture[ ( static_cast< size_t >( where[ p ][ 1 ] ) * rig.width + where[ p ][ 0 ] ) * 4 ];
			o[ 0 ]   = static_cast< float >( observed[ k ] );
			o[ 1 ]   = wanted != nullptr && wanted->mask[ k ] ? 1.0f : 0.0f;
			o[ 2 ]   = rival != nullptr && rival->mask[ k ] ? 1.0f : 0.0f;
			o[ 3 ]   = 1.0f;
		}
		writePng( dump, rig.width, rig.height, picture );
	}
	return out;
}

int runReadback( const Perturb& perturb )
{
	std::printf( "\n=== readback: the number on top, read out of the picture, is the one asked for\n" );
	struct Case
	{
		geo::DieType die;
		std::vector< int > values;
	};
	const std::vector< Case > cases = {
		{ geo::DieType::D4, { 1, 2, 3, 4 } },          { geo::DieType::D6, { 1, 3, 6 } },
		{ geo::DieType::D8, { 2, 6, 8 } },             { geo::DieType::D10, { 1, 6, 9, 10 } },
		{ geo::DieType::D12, { 5, 9, 11, 12 } },       { geo::DieType::D20, { 1, 6, 9, 17, 20 } },
		{ geo::DieType::D100, { 1, 37, 60, 99, 100 } },
	};
	//Two faces: the built-in strokes, and an installed font, end to end.
	std::vector< std::string > fonts = { "" };
	for( const char* family : { "Georgia", "Helvetica", "Arial" } )
		if( FindFontByFamily( family ) >= 0 )
		{
			fonts.push_back( family );
			break;
		}
	for( const std::string& font : fonts )
		for( const Case& c : cases )
		{
			int wrong = 0, slots = 0;
			double worstIoU = 1.0, worstMargin = 1.0;
			std::string detail;
			for( int value : c.values )
			{
				Rig rig;
				//The d4's numbers are small and seen at 70 degrees, and the d100's
				//are two digits on a kite: more pixels, so a serif face's
				//hairlines are wider than the antialiasing.
				const int side = c.die == geo::DieType::D4 || c.die == geo::DieType::D100 ? 1280 : 640;
				if( !rig.Init( side, side ) )
					return 1;
				quietScene( rig );
				rig.Set( PT_DIE, static_cast< float >( c.die ) );
				//A close-up (the die about 40% of the frame): big numbers, and
				//the arena reaching past the frame so it is a real throw. The
				//d100 is a pair, which a close-up cannot keep in shot: wider.
				rig.Set( PT_SIZE, c.die == geo::DieType::D100 ? 0.6f : 0.85f );
				rig.Set( PT_RESULT, 1.0f );
				rig.Set( PT_FIXED_TOTAL, static_cast< float >( value ) );
				rig.Set( PT_SEED, static_cast< float >( value + 3 ) );
				rig.plugin.SetTextParameter( PT_FONT_NAME, font.c_str() );
				rig.plugin.TestFlags().identitySymmetry = perturb.identitySymmetry;
				if( !rig.RollToRest() )
					return 1;
				const Floats img       = rig.Output();
				const roll::Plan& plan = rig.plugin.CurrentPlan();
				//A resting layout would pass by construction: it must be a throw.
				if( plan.idle )
				{
					++wrong;
					detail += fmt( " [%d: no throw, the resting layout]", value );
				}
				for( size_t i = 0; i < plan.bodies.size(); ++i )
				{
					const roll::Track& track    = plan.bodies[ i ];
					const geo::Solid& solid     = geo::GetSolid( track.shape );
					const geo::Labelling& label = geo::GetLabelling( c.die, track.part );
					const int target            = geo::ItemForResult( c.die, track.part, value );
					const std::string want      = label.text[ static_cast< size_t >( target ) ];
					const M3 r                  = ToMatrix( plan.PoseAt( i, plan.duration ).q ) * track.S;
					const int top               = geo::TopItem( solid, label.atVertex, r );
					for( const geo::Slot& slot : geo::GetSlots( track.shape ) )
					{
						//The d4: its top vertex's number on each face that
						//shows; any other die: the top face's number.
						if( label.atVertex ? ( slot.item != top || ( r * solid.faces[ static_cast< size_t >( slot.face ) ].normal ).y < 0.3 )
						                   : slot.face != top )
							continue;
						const ReadResult got = readSlot( rig, img, i, slot, want );
						++slots;
						worstIoU    = std::min( worstIoU, got.targetIoU );
						worstMargin = std::min( worstMargin, got.worstDispute );
						//Read, and read unambiguously: the wanted number fits
						//the ink best, and wherever it and any rival number
						//disagree, the picture sides with it three times in
						//four -- in a slot that was in shot.
						if( got.pixels < 100 || got.best != want || got.worstDispute < 0.75 )
						{
							++wrong;
							detail += fmt( " [%d: wanted %s, read %s %.2f; against %s only %.2f; %d px]", value, want.c_str(),
							               got.best.c_str(), got.bestIoU, got.rival.c_str(), got.worstDispute, got.pixels );
						}
					}
				}
			}
			Check( wrong == 0 && slots > 0,
			       fmt( "%-4s %-9s %zu values, %d printings read: %d wrong; worst fit %.2f; where the wanted number and a "
			            "rival disagree the picture sides with it at worst %.0f%%%s",
			            geo::DieName( c.die ), font.empty() ? "built-in" : font.c_str(), c.values.size(), slots, wrong, worstIoU,
			            100.0 * worstMargin, detail.c_str() ) );
		}
	return Verdict();
}

/// R and R * S for S the plan's own symmetry (and every element besides):
/// the same silhouette, because S maps the solid onto itself.
int runSilhouette( const Perturb& perturb )
{
	std::printf( "\n=== silhouette: a die drawn at R and at R S covers the same pixels, at two rasters\n" );
	for( const int scale : { 1, 2 } )
	for( geo::DieType die : { geo::DieType::D4, geo::DieType::D6, geo::DieType::D8, geo::DieType::D10, geo::DieType::D12,
	                          geo::DieType::D20 } )
	{
		Rig rig;
		if( !rig.Init( 320 * scale, 180 * scale ) )
			return 1;
		rig.Set( PT_DIE, static_cast< float >( die ) );
		rig.Set( PT_SIZE, 0.9f );
		rig.plugin.SetFlatForTest( true );
		if( !rig.Render( 1 ) )
			return 1;

		roll::Request request = baseRequest( die );
		applyPerturb( request, perturb );
		request.seed = 9;
		const roll::Plan thrown = roll::MakePlan( request );
		const geo::Solid& solid = geo::GetSolid( geo::ShapeOf( die ) );

		std::vector< M3 > tests = { thrown.bodies[ 0 ].S };
		if( !perturb.reflectSymmetry )
			for( size_t g = 1; g < solid.group.size(); g += 3 )
				tests.push_back( solid.group[ g ] );

		auto draw = [ & ]( const M3& s ) {
			roll::Plan p;
			p.idle    = true;
			p.settled = true;
			roll::Track t;
			t.shape = geo::ShapeOf( die );
			t.S     = s;
			//In the air, turned so no face is square to the camera.
			t.keys.push_back( { V3 { 0.0, 0.012, 0.0 }, Normalise( Quat { 0.83, 0.31, -0.22, 0.4 } ) } );
			p.bodies.push_back( t );
			rig.plugin.AdoptPlanForTest( p );
			rig.Render( 1 );
			return rig.Output();
		};
		const Floats plain = draw( M3 {} );
		long covered       = 0;
		for( size_t k = 3; k < plain.size(); k += 4 )
			covered += plain[ k ] > 0.5f;
		int worstPixels = 0;
		for( const M3& s : tests )
		{
			const Floats turned = draw( s );
			int differ          = 0;
			for( size_t k = 3; k < plain.size(); k += 4 )
				differ += std::fabs( turned[ k ] - plain[ k ] ) > 0.01f;
			worstPixels = std::max( worstPixels, differ );
		}
		//A supersample landing within float rounding of an edge can flip:
		//that is a pixel or two, not a shape.
		Check( covered > 500 && worstPixels <= 2,
		       fmt( "%-4s %dx%d, %zu symmetries (the plan's first): at most %d of %ld covered pixels differ", geo::DieName( die ),
		            rig.width, rig.height, tests.size(), worstPixels, covered ) );
	}
	return Verdict();
}

/// The data texture as the SHADER reads it, through the shipped ROW_ constants.
int runData( const Perturb& )
{
	std::printf( "\n=== data: the shader's ROW_ constants and the C++ DataRow agree, and the texture reads back exact\n" );
	Rig rig;
	if( !rig.Init( 320, 180 ) || !rig.Render( 1 ) )
		return 1;
	const std::string fragment = shaders::Assemble( R"(
out vec4 fragColour;
void main()
{
	ivec2 p = ivec2( gl_FragCoord.xy );
	if( p.y == 0 )
	{
		if( p.x == 0 ) fragColour = vec4( ROW_PLANES, ROW_VERTS, ROW_EDGE_A, ROW_EDGE_B );
		else if( p.x == 1 ) fragColour = vec4( ROW_SLOTS, ROW_GLYPH, ROW_CELL, ROW_FACE );
		else fragColour = vec4( ROW_FACE_UP, 0.0, 0.0, 0.0 );
		return;
	}
	fragColour = dataAt( p.y - 1, p.x );
}
)" );
	ffglex::FFGLShader probe;
	const std::string vertex = std::string( shaders::kVersion ) + shaders::kQuadVertex;
	if( !probe.Compile( vertex.c_str(), fragment.c_str() ) )
	{
		Check( false, "the probe shader compiles" );
		return Verdict();
	}
	const int w = kDataWidth, h = kDataRows + 1;
	const GLuint target = makeTexture( w, h, nullptr );
	GLuint fbo          = 0;
	glGenFramebuffers( 1, &fbo );
	glBindFramebuffer( GL_FRAMEBUFFER, fbo );
	glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target, 0 );
	glViewport( 0, 0, w, h );
	ffglex::FFGLScreenQuad quad;
	quad.Initialise();
	glUseProgram( probe.GetGLID() );
	probe.Set( "Data", 0 );
	glActiveTexture( GL_TEXTURE0 );
	glBindTexture( GL_TEXTURE_2D, rig.plugin.DataTextureForTest() );
	quad.Draw();
	Floats got( static_cast< size_t >( w ) * h * 4 );
	glReadPixels( 0, 0, w, h, GL_RGBA, GL_FLOAT, got.data() );
	glBindTexture( GL_TEXTURE_2D, 0 );
	glUseProgram( 0 );
	quad.Release();
	probe.FreeGLResources();
	glDeleteFramebuffers( 1, &fbo );
	glDeleteTextures( 1, &target );

	const float constants[ 12 ] = { ROW_PLANES, ROW_VERTS, ROW_EDGE_A, ROW_EDGE_B, ROW_SLOTS, ROW_GLYPH,
		                            ROW_CELL,   ROW_FACE,  ROW_FACE_UP, 0, 0, 0 };
	int constantsWrong = 0;
	for( int k = 0; k < 9; ++k )
		constantsWrong += got[ static_cast< size_t >( k ) ] != constants[ k ];
	const std::vector< float > cpu = rig.plugin.DataForTest();
	long dataWrong = 0;
	for( int row = 0; row < kDataRows; ++row )
		for( int x = 0; x < w; ++x )
			for( int c = 0; c < 4; ++c )
				dataWrong += got[ ( static_cast< size_t >( row + 1 ) * w + x ) * 4 + c ]
				             != cpu[ ( static_cast< size_t >( row ) * w + x ) * 4 + c ];
	Check( constantsWrong == 0, fmt( "the nine ROW_ constants in the shipped GLSL equal DataRow (%d differ)", constantsWrong ) );
	Check( dataWrong == 0, fmt( "all %d x %d texels read back through dataAt() bit for bit (%ld differ)", w, kDataRows, dataWrong ) );
	return Verdict();
}

int runFonts( const Perturb& )
{
	std::printf( "\n=== fonts: the built-in strokes, an installed face, a file, and a name that is not installed\n" );
	{
		Typeface builtin;
		const DigitAtlas atlas = builtin.Build();
		//On a stroke's centreline the field is the half-width, to a texel.
		double worst = 0.0;
		for( int d = 0; d < 10; ++d )
			for( const auto& stroke : BuiltinStrokes( d ) )
				for( size_t k = 0; k + 1 < stroke.size(); ++k )
				{
					const float x = 0.5f * ( stroke[ k ].first + stroke[ k + 1 ].first );
					const float y = 0.5f * ( stroke[ k ].second + stroke[ k + 1 ].second );
					worst         = std::max( worst, static_cast< double >( std::fabs( atlas.Distance( d, x, y ) - kBuiltinHalfWidth ) ) );
				}
		Check( atlas.builtin && worst < 1.0 / atlas.pxPerUnit, fmt( "built-in: on every stroke's centreline the distance is the "
		                                                           "half-width to %.4f H (one texel is %.4f)", worst, 1.0 / atlas.pxPerUnit ) );
	}

	const std::vector< FontFile >& fonts = InstalledFonts();
	std::printf( "  info  %zu font families installed\n", fonts.size() );
	if( fonts.empty() )
	{
		std::printf( "  skip  no installed fonts to load\n" );
		return Verdict();
	}
	int pick = -1;
	for( const char* family : { "Georgia", "Helvetica", "Arial", "Times New Roman" } )
		if( pick < 0 )
			pick = FindFontByFamily( family );
	if( pick < 0 )
		pick = 0;
	const FontFile& font = fonts[ static_cast< size_t >( pick ) ];

	{
		Typeface face;
		Check( face.Load( font.path, font.collectionIndex ), fmt( "%s loads from %s", font.family.c_str(), font.path.c_str() ) );
		const DigitAtlas atlas = face.Build();
		//The ten digits' union of ink spans 0..1 H by construction; each
		//digit's own ink must sit inside it, to a texel.
		double low = 1e9, high = -1e9;
		for( int d = 0; d < 10; ++d )
			for( float y = -0.3f; y <= 1.3f; y += 0.005f )
				for( float x = -0.2f; x <= 1.2f; x += 0.01f )
					if( atlas.Distance( d, x, y ) > 0.0f )
					{
						low  = std::min( low, static_cast< double >( y ) );
						high = std::max( high, static_cast< double >( y ) );
					}
		const double texel = 1.0 / atlas.pxPerUnit;
		Check( !atlas.builtin && low > -2.0 * texel && high < 1.0 + 2.0 * texel,
		       fmt( "%s: ten digits from the font, their ink between %.3f and %.3f H (0 and 1, to two texels)", font.family.c_str(), low, high ) );
	}

	auto resolve = [ & ]( const std::function< void( Rig& ) >& setup, std::string& name ) {
		Rig rig;
		if( !rig.Init( 160, 90 ) )
			return std::string( "!" );
		setup( rig );
		rig.Render( 1 );
		name = rig.plugin.GetTextParameter( PT_FONT_NAME );
		return rig.plugin.FontFamily();
	};
	std::string name;
	std::string got = resolve( [ & ]( Rig& rig ) { rig.plugin.SetTextParameter( PT_FONT_NAME, font.family.c_str() ); }, name );
	Check( got == font.family, fmt( "Font Name \"%s\" resolves to that family (got \"%s\")", font.family.c_str(), got.c_str() ) );
	got = resolve( [ & ]( Rig& rig ) { rig.Set( PT_FONT, static_cast< float >( pick + 1 ) ); }, name );
	Check( got == font.family && name == font.family,
	       fmt( "the dropdown at its index picks %s and writes its name into Font Name (\"%s\")", font.family.c_str(), name.c_str() ) );
	got = resolve( [ & ]( Rig& rig ) {
		rig.Set( PT_FONT, static_cast< float >( pick + 1 ) );
		rig.plugin.SetTextParameter( PT_FONT_NAME, "Built In Nowhere 7" );
	}, name );
	Check( got.empty() && name == "Built In Nowhere 7",
	       "a composition naming a font this machine lacks: built-in face, and the name kept for the next machine" );
	got = resolve( [ & ]( Rig& rig ) { rig.plugin.SetTextParameter( PT_FONT_FILE, font.path.c_str() ); }, name );
	Check( !got.empty(), fmt( "Font File %s loads (\"%s\")", font.path.c_str(), got.c_str() ) );
	got = resolve( [ & ]( Rig& rig ) { rig.plugin.SetTextParameter( PT_FONT_FILE, "/nonexistent/font.ttf" ); }, name );
	Check( got.empty(), "a Font File that will not open falls back to the built-in face" );
	return Verdict();
}

int runOver( const Perturb& )
{
	std::printf( "\n=== over-check: away from the dice the clip is untouched; Mix 0 is the clip; two rasters\n" );
	for( const int scale : { 1, 2 } )
	{
	const int W = 320 * scale, H = 180 * scale;
	Rig rig( true );
	if( !rig.Init( W, H ) )
		return 1;
	rig.Set( PT_SIZE, 0.2f );
	if( !rig.RollToRest() )
		return 1;
	const Floats card = buildCard( W, H );
	const Floats out  = rig.Output();
	const Camera& cam = rig.plugin.CurrentCamera();
	const roll::Plan& plan = rig.plugin.CurrentPlan();
	long far = 0, changed = 0;
	for( int y = 0; y < H; ++y )
		for( int x = 0; x < W; ++x )
		{
			bool close = false;
			for( size_t i = 0; i < plan.bodies.size(); ++i )
			{
				double px = 0, py = 0, ex = 0, ey = 0;
				const roll::Pose pose = plan.PoseAt( i, plan.duration );
				const double radius   = geo::GetSolid( plan.bodies[ i ].shape ).circumradius;
				cam.Project( pose.x, W, H, px, py );
				cam.Project( pose.x + V3 { radius, 0, 0 }, W, H, ex, ey );
				close = close || std::hypot( x + 0.5 - px, y + 0.5 - py ) < 5.0 * std::hypot( ex - px, ey - py );
			}
			if( close )
				continue;
			++far;
			for( int c = 0; c < 4; ++c )
				changed += std::fabs( out[ ( static_cast< size_t >( y ) * W + x ) * 4 + c ] - card[ ( static_cast< size_t >( y ) * W + x ) * 4 + c ] ) > 1e-6f;
		}
	Check( far > 10000 && changed == 0, fmt( "%dx%d: %ld pixels more than five die radii from any die: %ld channels changed", W, H, far, changed ) );
	rig.Set( PT_MIX, 0.0f );
	rig.Render( 1 );
	const Floats dry = rig.Output();
	long differ      = 0;
	for( size_t k = 0; k < dry.size(); ++k )
		differ += std::fabs( dry[ k ] - card[ k ] ) > 1e-6f;
	Check( differ == 0, fmt( "%dx%d: Mix 0, the output is the clip (%ld channels differ)", W, H, differ ) );
	}
	return Verdict();
}

int runState( const Perturb& )
{
	std::printf( "\n=== state: the GL state the host hands over is the state it gets back\n" );
	for( bool effect : { false, true } )
	{
		Rig rig( effect );
		if( !rig.Init( 320, 180 ) )
			return 1;
		GLuint hostArray = 0, hostBuffer = 0;
		glGenVertexArrays( 1, &hostArray );
		glGenBuffers( 1, &hostBuffer );
		int problems = 0;
		std::string what;
		for( int frame = 0; frame < 3; ++frame )
		{
			if( frame == 1 )
				rig.Press( PT_ROLL );
			glBindFramebuffer( GL_FRAMEBUFFER, rig.outputFBO );
			glViewport( 7, 5, 300, 170 );
			glBindVertexArray( hostArray );
			glBindBuffer( GL_ARRAY_BUFFER, hostBuffer );
			glEnable( GL_BLEND );
			glBlendFuncSeparate( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ZERO );
			glClearColor( 0.2f, 0.3f, 0.4f, 0.5f );
			glEnable( GL_SCISSOR_TEST );
			glScissor( 0, 0, 320, 180 );
			glActiveTexture( GL_TEXTURE0 );
			glUseProgram( 0 );
			rig.plugin.SetTime( frame / 60.0 );
			if( rig.plugin.ProcessOpenGL( &rig.process ) != FF_SUCCESS )
				return 1;
			GLint viewport[ 4 ] = {}, array = 0, buffer = 0, program = 0, unit = 0, fbo = 0, src = 0, dst = 0, unpack = 0;
			GLfloat clear[ 4 ] = {};
			glGetIntegerv( GL_VIEWPORT, viewport );
			glGetIntegerv( GL_VERTEX_ARRAY_BINDING, &array );
			glGetIntegerv( GL_ARRAY_BUFFER_BINDING, &buffer );
			glGetIntegerv( GL_CURRENT_PROGRAM, &program );
			glGetIntegerv( GL_ACTIVE_TEXTURE, &unit );
			glGetIntegerv( GL_FRAMEBUFFER_BINDING, &fbo );
			glGetIntegerv( GL_BLEND_SRC_RGB, &src );
			glGetIntegerv( GL_BLEND_DST_RGB, &dst );
			glGetIntegerv( GL_UNPACK_ALIGNMENT, &unpack );
			glGetFloatv( GL_COLOR_CLEAR_VALUE, clear );
			auto expect = [ & ]( bool ok, const char* name ) {
				if( !ok )
				{
					++problems;
					what += std::string( " " ) + name;
				}
			};
			expect( viewport[ 0 ] == 7 && viewport[ 1 ] == 5 && viewport[ 2 ] == 300 && viewport[ 3 ] == 170, "viewport" );
			expect( array == static_cast< GLint >( hostArray ), "vertex-array" );
			expect( buffer == static_cast< GLint >( hostBuffer ), "array-buffer" );
			expect( program == 0, "program" );
			expect( unit == GL_TEXTURE0, "active-unit" );
			expect( fbo == static_cast< GLint >( rig.outputFBO ), "framebuffer" );
			expect( glIsEnabled( GL_BLEND ) && src == GL_SRC_ALPHA && dst == GL_ONE_MINUS_SRC_ALPHA, "blend" );
			expect( glIsEnabled( GL_SCISSOR_TEST ), "scissor" );
			expect( unpack == 4, "unpack-alignment" );
			expect( clear[ 0 ] == 0.2f && clear[ 1 ] == 0.3f && clear[ 2 ] == 0.4f && clear[ 3 ] == 0.5f, "clear-colour" );
			for( int u = 0; u < 10; ++u )
			{
				GLint bound = 0;
				glActiveTexture( static_cast< GLenum >( GL_TEXTURE0 + u ) );
				glGetIntegerv( GL_TEXTURE_BINDING_2D, &bound );
				expect( bound == 0, "texture-unit" );
			}
			glActiveTexture( GL_TEXTURE0 );
		}
		glDisable( GL_SCISSOR_TEST );
		glDisable( GL_BLEND );
		glBindVertexArray( 0 );
		glBindBuffer( GL_ARRAY_BUFFER, 0 );
		glDeleteVertexArrays( 1, &hostArray );
		glDeleteBuffers( 1, &hostBuffer );
		Check( problems == 0, fmt( "%s, three frames and a roll: viewport, vertex array, array buffer, program, active unit, "
		                           "framebuffer, blend, scissor, unpack alignment, clear colour, ten texture units (%d wrong:%s)",
		                           effect ? "Over" : "source", problems, what.empty() ? " none" : what.c_str() ) );
	}
	return Verdict();
}

int runResize( const Perturb& )
{
	std::printf( "\n=== resize: a new frame size mid-throw carries on with the same throw\n" );
	Rig rig;
	if( !rig.Init( 640, 360 ) )
		return 1;
	rig.Set( PT_ROLL_TIME, ParamFromRollTime( 3.0 ) );
	rig.Set( PT_COUNT, 3.0f );
	rig.Press( PT_ROLL );
	rig.Render( 30 );
	const std::vector< int > before = rig.plugin.CurrentPlan().results;
	const double t0 = rig.plugin.PlaybackSeconds();
	const roll::Pose p0 = rig.plugin.CurrentPlan().PoseAt( 0, t0 );
	//The host resizes: a new framebuffer at a new size.
	glDeleteFramebuffers( 1, &rig.outputFBO );
	glDeleteTextures( 1, &rig.outputTexture );
	rig.width = 320;
	rig.height = 180;
	rig.outputTexture = makeTexture( 320, 180, nullptr );
	glGenFramebuffers( 1, &rig.outputFBO );
	glBindFramebuffer( GL_FRAMEBUFFER, rig.outputFBO );
	glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, rig.outputTexture, 0 );
	rig.process.HostFBO = rig.outputFBO;
	const bool rendered = rig.Render( 1 );
	const double t1 = rig.plugin.PlaybackSeconds();
	const roll::Pose p1 = rig.plugin.CurrentPlan().PoseAt( 0, t1 );
	Check( rendered && rig.plugin.CurrentPlan().results == before && std::fabs( t1 - t0 - 1.0 / 60.0 ) < 1e-9,
	       fmt( "same results, playback advanced by one frame (%.4f s)", t1 - t0 ) );
	Check( Length( p1.x - p0.x ) < 0.05, fmt( "the first die moved %.1f mm across the resize: a step, not a jump", Length( p1.x - p0.x ) * 1000.0 ) );
	return Verdict();
}

//===========================================================================
// --bench
//===========================================================================
int runBench()
{
	struct Size
	{
		int w, h;
		const char* name;
	};
	const Size sizes[] = { { 1280, 720, "720p" }, { 1920, 1080, "1080p" }, { 3840, 2160, "4K" } };
	struct Load
	{
		geo::DieType die;
		int count;
		int texture;
		const char* name;
	};
	const Load loads[] = { { geo::DieType::D20, 1, 1, "1 d20, marble" },
		                   { geo::DieType::D20, 6, 4, "6 d20, gem" },
		                   { geo::DieType::D100, 6, 1, "6 d100 (12 dice), marble" } };
	std::printf( "\n=== bench: ms/frame mid-throw (Felt table, shadows), median of 60 frames\n" );
	for( const Load& load : loads )
		for( const Size& size : sizes )
		{
			Rig rig;
			if( !rig.Init( size.w, size.h ) )
				return 1;
			rig.Set( PT_DIE, static_cast< float >( load.die ) );
			rig.Set( PT_COUNT, static_cast< float >( load.count ) );
			rig.Set( PT_TEXTURE, static_cast< float >( load.texture ) );
			rig.Set( PT_TABLE, 1.0f );
			rig.Set( PT_ROLL_TIME, ParamFromRollTime( 8.0 ) );
			rig.Press( PT_ROLL );
			rig.Render( 20 );
			glFinish();
			std::vector< double > times;
			for( int f = 0; f < 60; ++f )
			{
				const auto start = std::chrono::steady_clock::now();
				rig.Render( 1 );
				glFinish();
				times.push_back( std::chrono::duration< double, std::milli >( std::chrono::steady_clock::now() - start ).count() );
			}
			std::sort( times.begin(), times.end() );
			std::printf( "  %-26s %-6s median %6.2f ms/frame, worst %6.2f  (%4.1f%% of 60 fps)\n", load.name, size.name,
			             times[ 30 ], times.back(), 100.0 * times[ 30 ] / ( 1000.0 / 60.0 ) );
		}
	std::printf( "\n=== bench: planning a throw on the worker (CPU), median of 9\n" );
	for( const Load& load : loads )
	{
		std::vector< double > ms;
		int trials = 0;
		for( uint32_t seed = 1; seed <= 9; ++seed )
		{
			roll::Request r = baseRequest( load.die, load.count );
			r.seed          = seed;
			const roll::Plan p = roll::MakePlan( r );
			ms.push_back( p.ms );
			trials += p.trials;
		}
		std::sort( ms.begin(), ms.end() );
		std::printf( "  %-26s median %6.2f ms, worst %6.2f ms, %.1f throws per roll\n", load.name, ms[ 4 ], ms.back(), trials / 9.0 );
	}
	return 0;
}

//===========================================================================
// The registry, --offline and --negative.
//===========================================================================
const std::vector< CheckEntry >& checks()
{
	static const std::vector< CheckEntry > list = {
		{ "geometry", runGeometry },     { "symmetry", runSymmetryGroups }, { "labels", runLabels },
		{ "outcome", runOutcome },       { "readback", runReadback },       { "silhouette", runSilhouette },
		{ "uniform", runUniform },       { "rest", runRest },               { "duration", runDuration },
		{ "physics", runPhysics },       { "determinism", runDeterminism }, { "fonts", runFonts },
		{ "defaults", runDefaults },     { "names", runNames },             { "data", runData },
		{ "over-check", runOver },       { "state", runState },             { "resize", runResize },
	};
	return list;
}

/// The checks that need no GL context: the solids, the numbering, the planner
/// and the physics. The one place that knows which they are -- `--offline`
/// (what CI runs) is everything else's complement.
bool isOffline( const std::string& flag )
{
	static const char* const offline[] = { "geometry", "symmetry", "labels",      "outcome",  "uniform", "rest",
		                                   "duration", "physics",  "determinism", "defaults", "names" };
	for( const char* name : offline )
		if( flag == name )
			return true;
	return false;
}

int runNegative( bool offlineOnly = false )
{
	struct Case
	{
		const char* name;
		CheckFn check;
		Perturb perturb;
		const char* what;
	};
	std::vector< Case > cases;
	auto add = [ & ]( const char* name, CheckFn fn, const char* what, std::function< void( Perturb& ) > set ) {
		Perturb p;
		set( p );
		cases.push_back( { name, fn, p, what } );
	};
	add( "outcome", runOutcome, "S = identity: the physics' own face shows", []( Perturb& p ) { p.identitySymmetry = true; } );
	add( "readback", runReadback, "S = identity, read out of the picture", []( Perturb& p ) { p.identitySymmetry = true; } );
	add( "symmetry", runSymmetryGroups, "S = -I, a reflection and not a rotation", []( Perturb& p ) { p.reflectSymmetry = true; } );
	add( "silhouette", runSilhouette, "S = -I: the d4 is not centrally symmetric", []( Perturb& p ) { p.reflectSymmetry = true; } );
	add( "uniform", runUniform, "draw with % (n - 1): the top value never comes up", []( Perturb& p ) { p.biasedDraw = true; } );
	add( "duration", runDuration, "play the throw at 1x: the dice stop when they stop", []( Perturb& p ) { p.noWarp = true; } );
	add( "physics", runPhysics, "restitution 1.3 and no energy guard", []( Perturb& p ) { p.superBounce = true; } );
	add( "labels", runLabels, "expect opposite faces to sum to n", []( Perturb& p ) { p.wrongLabels = true; } );
	add( "determinism", runDeterminism, "expect seeds 3 and 4 to agree", []( Perturb& p ) { p.determinismSeeds = true; } );
	add( "defaults", runDefaults, "expect the default die to be a d6", []( Perturb& p ) { p.defaultsShifted = true; } );

	if( offlineOnly )
		cases.erase( std::remove_if( cases.begin(), cases.end(), []( const Case& c ) { return !isOffline( c.name ); } ),
		             cases.end() );

	int unfalsifiable = 0;
	for( const Case& c : cases )
	{
		std::printf( "\n=== negative control: %s -- %s\n", c.name, c.what );
		const int before = g_failures;
		g_failures       = 0;
		c.check( c.perturb );
		const int observed = g_failures;
		g_failures         = before;
		if( observed > 0 )
			std::printf( "  ok    %s failed %d check%s, as it must\n", c.name, observed, observed == 1 ? "" : "s" );
		else
		{
			std::printf( "  FAIL  %s PASSED against a wrong model -- it cannot fail, so it is not a check\n", c.name );
			++unfalsifiable;
		}
	}
	std::printf( "\nnegative controls: %zu wrong models, %d of them undetected\n", cases.size(), unfalsifiable );
	std::printf( "\n  %s\n", unfalsifiable == 0 ? "PASS" : "FAIL" );
	return unfalsifiable == 0 ? 0 : 1;
}
} // namespace

//---------------------------------------------------------------------------
int main( int argc, char** argv )
{
	std::string outPath = "/tmp/polyhedral.png";
	std::vector< std::string > settings;
	int width = 1280, height = 720, frames = -1;
	std::vector< int > rolls;
	bool effect = false;
	std::string mode, scriptPath;
	int filmFrames = -1;

	for( int i = 1; i < argc; ++i )
	{
		const std::string argument = argv[ i ];
		const bool hasNext         = i + 1 < argc;
		if( argument == "--help" || argument == "-h" )
		{
			std::printf( "polytest -- render Polyhedral offline and measure its dice\n\n"
			             "  --out PATH        render and write a PNG (default /tmp/polyhedral.png)\n"
			             "  --over            the Over effect, on the harness's card\n"
			             "  --size WxH        render size (default 1280x720)\n"
			             "  --frames N        frames of 60 fps before reading back (default: one roll to rest)\n"
			             "  --roll N          press Roll on frame N. Repeatable. (default: frame 0)\n"
			             "  --set \"Name=V\"    set a parameter by its display name. Repeatable.\n"
			             "  --list            every parameter and its default\n"
			             "  --pipe            raw RGBA frames in (Over) and out\n"
			             "  --film N          N frames, raw RGBA on stdout\n"
			             "  --script PATH     cues for --pipe/--film: 'frame Name value'\n\n"
			             "  checks: --geometry --symmetry --labels --outcome --readback --uniform --rest\n"
			             "          --duration --physics --determinism --fonts --defaults --names --data\n"
			             "          --over-check --state --resize --negative --bench\n"
			             "  --offline         the checks and negative controls that need no GL context\n" );
			return 0;
		}
		else if( argument == "--out" && hasNext )
			outPath = argv[ ++i ];
		else if( argument == "--set" && hasNext )
			settings.push_back( argv[ ++i ] );
		else if( argument == "--frames" && hasNext )
			frames = std::atoi( argv[ ++i ] );
		else if( argument == "--roll" && hasNext )
			rolls.push_back( std::atoi( argv[ ++i ] ) );
		else if( argument == "--over" )
			effect = true;
		else if( argument == "--pipe" )
			mode = "pipe";
		else if( argument == "--film" && hasNext )
		{
			mode       = "pipe";
			filmFrames = std::max( 1, std::atoi( argv[ ++i ] ) );
		}
		else if( argument == "--script" && hasNext )
			scriptPath = argv[ ++i ];
		else if( argument == "--list" )
			mode = "list";
		else if( argument == "--size" && hasNext )
		{
			const std::string value = argv[ ++i ];
			const size_t cross      = value.find( 'x' );
			if( cross != std::string::npos )
			{
				width  = std::atoi( value.substr( 0, cross ).c_str() );
				height = std::atoi( value.substr( cross + 1 ).c_str() );
			}
		}
		else if( argument.rfind( "--", 0 ) == 0 )
			mode = argument.substr( 2 );
		else
		{
			std::fprintf( stderr, "unknown argument '%s' (try --help)\n", argument.c_str() );
			return 2;
		}
	}

	if( mode == "list" )
	{
		DicePlugin plugin( effect );
		std::printf( "%-3s %-18s %-9s %s\n", "id", "name", "kind", "default" );
		for( const NamedParameter& parameter : listParameters( plugin ) )
			std::printf( "%-3u %-18s %-9s %.4f\n", parameter.index, parameter.name.c_str(), parameter.kind.c_str(),
			             parameter.value );
		return 0;
	}

	//A reader that hangs up must end --pipe/--film with exit 1 and a message,
	//not SIGPIPE's silent 141: ignored here, the write fails with EPIPE.
	std::signal( SIGPIPE, SIG_IGN );

	if( mode == "offline" )
	{
		//No context at all: this is what a runner with no accelerated GL can
		//run. The skip is loud, so a green run is not read as one that
		//checked the shaders against a driver.
		int failed = 0;
		for( const CheckEntry& check : checks() )
			if( isOffline( check.flag ) )
			{
				//Each check's verdict is its own.
				g_failures = 0;
				failed |= check.run( Perturb {} );
			}
		failed |= runNegative( true );
		std::printf( "\n  offline: the checks that need no GL context. The shader and pixel checks were NOT run --\n"
		             "  tools/verify.sh runs them against a real driver, at 320x180 and above.\n"
		             "\n  %s\n", failed == 0 ? "PASS" : "FAIL" );
		return failed == 0 ? 0 : 1;
	}

	CGLContextObj context = createContext();
	if( context == nullptr )
	{
		std::fprintf( stderr, "could not create an OpenGL 4.1 core context\n" );
		return 1;
	}

	int result = 0;
	bool ran   = false;
	for( const CheckEntry& check : checks() )
		if( mode == check.flag )
		{
			result = check.run( Perturb {} );
			ran    = true;
		}

	if( ran )
		;
	else if( mode == "pipe" )
		result = runPipe( effect, width, height, scriptPath, filmFrames, settings, rolls );
	else if( mode == "negative" )
		result = runNegative();
	else if( mode == "bench" )
		result = runBench();
	else if( !mode.empty() )
	{
		std::fprintf( stderr, "unknown mode --%s (try --help)\n", mode.c_str() );
		result = 2;
	}
	else
	{
		Rig rig( effect );
		if( !rig.Init( width, height ) )
			result = 1;
		else
		{
			for( const std::string& setting : settings )
			{
				std::string error;
				if( !applySetting( rig.plugin, setting, error ) )
				{
					std::fprintf( stderr, "--set %s: %s\n", setting.c_str(), error.c_str() );
					return 2;
				}
			}
			if( rolls.empty() )
				rolls.push_back( 0 );
			int total = frames;
			if( total < 0 )
			{
				//One roll to rest: render a frame to plan it, then its length.
				total = rolls.back() + 1;
			}
			for( int f = 0; f < std::max( total, 1 ) && result == 0; ++f )
			{
				if( std::find( rolls.begin(), rolls.end(), f ) != rolls.end() )
					rig.Press( PT_ROLL );
				if( !rig.Render( 1 ) )
					result = 1;
			}
			if( frames < 0 && result == 0 )
				result = rig.Render( static_cast< int >( std::ceil( rig.plugin.CurrentPlan().duration * 60.0 ) ) + 2 ) ? 0 : 1;
			if( result == 0 )
			{
				const roll::Plan& plan = rig.plugin.CurrentPlan();
				std::string values;
				for( int v : plan.results )
					values += ( values.empty() ? "" : " " ) + std::to_string( v );
				if( writePng( outPath, width, height, rig.Output() ) )
					std::printf( "wrote %s -- %dx%d, %d frames; showing %s (total %d), %d throws, natural %.2f s, warp %.2f\n",
					             outPath.c_str(), width, height, rig.frame, values.c_str(), plan.total, plan.trials,
					             plan.natural, plan.warp );
				else
					result = 1;
			}
		}
	}

	CGLSetCurrentContext( nullptr );
	CGLDestroyContext( context );
	return result;
}
