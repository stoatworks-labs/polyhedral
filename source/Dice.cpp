#include "Dice.h"

#include "Diag.h"
#include "GLState.h"
#include "Shaders.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <string>

using namespace ffglex;

namespace dice
{
namespace
{
std::string glStringOrUnknown( GLenum name )
{
	const GLubyte* value = glGetString( name );
	return value ? reinterpret_cast< const char* >( value ) : "unknown";
}

double wallSeconds()
{
	using namespace std::chrono;
	static const steady_clock::time_point start = steady_clock::now();
	return duration_cast< duration< double > >( steady_clock::now() - start ).count();
}

constexpr int kClockVotes       = 4;
constexpr double kMaxFrameDelta = 0.25;///< host seconds; a bigger step is a jump
constexpr double kHalfFov       = 15.0;///< degrees: a 30-degree lens, a tabletop close-up
constexpr double kLightElevation = 55.0;

const char* const kDieNames[]     = { "D4", "D6", "D8", "D10", "D12", "D20", "D100" };
const char* const kResultNames[]  = { "Random", "Fixed" };
const char* const kThrowNames[]   = { "Left", "Right", "Bottom", "Top", "Drop", "Any" };
const char* const kTextureNames[] = { "Plastic", "Marble", "Pearl", "Metal", "Gem", "Stone",
	                                  "Wood", "Galaxy", "Image", "Wireframe", "Clip" };
const char* const kStyleNames[]   = { "Painted", "Engraved" };
const char* const kMarkNames[]    = { "None", "Dot", "Underline" };
const char* const kD6Names[]      = { "Numbers", "Pips" };
const char* const kTableNames[]   = { "None", "Felt", "Wood" };

/// Colours arrive as display values; the shader lights in linear.
void linearColour( const float* p, float out[ 3 ] )
{
	for( int i = 0; i < 3; ++i )
		out[ i ] = std::pow( std::clamp( p[ i ], 0.0f, 1.0f ), 2.2f );
}

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

static_assert( PT_SOURCE_COUNT - PT_ABOUT_TEXT == stoatworks::about::kParamCount,
               "the About run no longer matches StoatworksAbout.h -- add or remove a PT_ABOUT_BUTTON_n to match" );

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

//---------------------------------------------------------------------------
DicePlugin::DicePlugin( bool effect ) : isEffect( effect )
{
	SetMinInputs( isEffect ? 1 : 0 );
	SetMaxInputs( isEffect ? 1 : 0 );
	SetTimeSupported( true );

	//-------------------------------------------------------------------
	// Defaults: a red marble d20 with gold numbers, thrown from anywhere.
	//-------------------------------------------------------------------
	params[ PT_DIE ]          = static_cast< float >( geo::DieType::D20 );
	params[ PT_COUNT ]        = 1.0f;
	params[ PT_ROLL_TIME ]    = ParamFromRollTime( 1.2 );
	params[ PT_RESULT ]       = static_cast< float >( Result::Random );
	params[ PT_FIXED_TOTAL ]  = 20.0f;
	params[ PT_SEED ]         = 0.0f;
	params[ PT_AUTO_ROLL ]    = 0.0f;
	params[ PT_INTERVAL ]     = ParamFromInterval( 6.0 );
	params[ PT_THROW ]        = static_cast< float >( roll::Throw::Any );
	params[ PT_SPIN ]         = 0.5f;
	params[ PT_BOUNCE ]       = ParamFromBounce( 0.5 );
	params[ PT_TEXTURE ]      = static_cast< float >( Texture::Marble );
	params[ PT_COLOUR_R ]     = 0.55f;
	params[ PT_COLOUR_G ]     = 0.05f;
	params[ PT_COLOUR_B ]     = 0.08f;
	params[ PT_SECOND_R ]     = 0.95f;
	params[ PT_SECOND_G ]     = 0.86f;
	params[ PT_SECOND_B ]     = 0.70f;
	params[ PT_GLOSS ]        = 0.7f;
	params[ PT_EDGE ]         = 0.4f;
	params[ PT_LINE_WIDTH ]   = 0.3f;
	params[ PT_FONT ]         = 0.0f;
	params[ PT_NUMBER_SIZE ]  = 0.55f;
	params[ PT_WEIGHT ]       = 0.5f;
	params[ PT_INK_R ]        = 1.0f;
	params[ PT_INK_G ]        = 0.80f;
	params[ PT_INK_B ]        = 0.32f;
	params[ PT_NUMBER_STYLE ] = static_cast< float >( NumberStyle::Painted );
	params[ PT_MARK ]         = static_cast< float >( Mark::Underline );
	params[ PT_D6_FACES ]     = static_cast< float >( D6Faces::Numbers );
	params[ PT_SIZE ]         = ParamFromSize( 0.22 );
	params[ PT_CAMERA_ANGLE ] = 0.6667f;//70 degrees
	params[ PT_LIGHT_ANGLE ]  = 0.5f;   //0
	params[ PT_SHADOW ]       = 0.6f;
	params[ PT_TABLE ]        = static_cast< float >( Table::None );
	params[ PT_TABLE_R ]      = 0.05f;
	params[ PT_TABLE_G ]      = 0.26f;
	params[ PT_TABLE_B ]      = 0.14f;
	params[ PT_MIX ]          = 1.0f;

	auto standard = [ this ]( unsigned int id, const char* name ) { SetParamInfo( id, name, FF_TYPE_STANDARD, params[ id ] ); };
	auto option   = [ this ]( unsigned int id, const char* name, const char* const* names, int count ) {
		SetOptionParamInfo( id, name, static_cast< unsigned int >( count ), params[ id ] );
		for( int i = 0; i < count; ++i )
			SetParamElementInfo( id, static_cast< unsigned int >( i ), names[ i ], static_cast< float >( i ) );
	};
	auto integer = [ this ]( unsigned int id, const char* name, float lo, float hi ) {
		//Only FF_TYPE_STANDARD has its default clamped into 0..1, so an integer
		//is declared with its real default and range.
		SetParamInfo( id, name, FF_TYPE_INTEGER, params[ id ] );
		SetParamRange( id, lo, hi );
	};
	auto colour = [ this ]( unsigned int id, const char* red, const char* green, const char* blue ) {
		//Consecutive red/green/blue parameters are what a host needs to show a
		//swatch rather than three sliders.
		SetParamInfo( id, red, FF_TYPE_RED, params[ id ] );
		SetParamInfo( id + 1, green, FF_TYPE_GREEN, params[ id + 1 ] );
		SetParamInfo( id + 2, blue, FF_TYPE_BLUE, params[ id + 2 ] );
	};

	option( PT_DIE, "Die", kDieNames, static_cast< int >( geo::DieType::Count ) );
	integer( PT_COUNT, "Count", 1.0f, static_cast< float >( kMaxCount ) );
	SetParamInfo( PT_ROLL, "Roll", FF_TYPE_EVENT, false );
	standard( PT_ROLL_TIME, "Roll Time" );
	option( PT_RESULT, "Result", kResultNames, static_cast< int >( Result::Count ) );
	integer( PT_FIXED_TOTAL, "Fixed Total", 1.0f, static_cast< float >( kMaxTotal ) );
	integer( PT_SEED, "Seed", 0.0f, 9999.0f );
	SetParamInfo( PT_AUTO_ROLL, "Auto Roll", FF_TYPE_BOOLEAN, false );
	standard( PT_INTERVAL, "Interval" );
	option( PT_THROW, "Throw", kThrowNames, static_cast< int >( roll::Throw::Count ) );
	standard( PT_SPIN, "Spin" );
	standard( PT_BOUNCE, "Bounce" );

	const int textures = isEffect ? static_cast< int >( Texture::Count ) : static_cast< int >( Texture::Count ) - 1;
	option( PT_TEXTURE, "Texture", kTextureNames, textures );
	SetFileParamInfo( PT_TEXTURE_FILE, "Texture File", { "png", "jpg", "jpeg", "bmp", "tga", "gif" }, "" );
	colour( PT_COLOUR_R, "Colour", "Colour_Green", "Colour_Blue" );
	colour( PT_SECOND_R, "Second Colour", "Second_Green", "Second_Blue" );
	standard( PT_GLOSS, "Gloss" );
	standard( PT_EDGE, "Edge" );
	standard( PT_LINE_WIDTH, "Line Width" );

	//-------------------------------------------------------------------
	// The font list is scanned here, once per process. It has to be declared
	// in the constructor -- SetParamElementInfo is how a host learns a
	// dropdown's contents (downpour's note). The scan reads only name tables.
	//-------------------------------------------------------------------
	const std::vector< FontFile >& fonts = InstalledFonts();
	SetOptionParamInfo( PT_FONT, "Font", static_cast< unsigned int >( fonts.size() + 1 ), 0.0f );
	SetParamElementInfo( PT_FONT, 0, "Built-in", 0.0f );
	for( size_t i = 0; i < fonts.size(); ++i )
		SetParamElementInfo( PT_FONT, static_cast< unsigned int >( i + 1 ), fonts[ i ].family.c_str(), static_cast< float >( i + 1 ) );
	SetFileParamInfo( PT_FONT_FILE, "Font File", { "ttf", "otf", "ttc", "otc" }, "" );
	SetParamInfo( PT_FONT_NAME, "Font Name", FF_TYPE_TEXT, "" );
	standard( PT_NUMBER_SIZE, "Number Size" );
	standard( PT_WEIGHT, "Weight" );
	colour( PT_INK_R, "Ink", "Ink_Green", "Ink_Blue" );
	option( PT_NUMBER_STYLE, "Number Style", kStyleNames, static_cast< int >( NumberStyle::Count ) );
	option( PT_MARK, "Mark 6 and 9", kMarkNames, static_cast< int >( Mark::Count ) );
	option( PT_D6_FACES, "D6 Faces", kD6Names, static_cast< int >( D6Faces::Count ) );

	standard( PT_SIZE, "Size" );
	standard( PT_CAMERA_ANGLE, "Camera Angle" );
	standard( PT_LIGHT_ANGLE, "Light Angle" );
	standard( PT_SHADOW, "Shadow" );
	option( PT_TABLE, "Table", kTableNames, static_cast< int >( Table::Count ) );
	colour( PT_TABLE_R, "Table Colour", "Table_Green", "Table_Blue" );

	for( unsigned int id = PT_DIE; id <= PT_BOUNCE; ++id )
		SetParamGroup( id, "Roll" );
	for( unsigned int id = PT_TEXTURE; id <= PT_LINE_WIDTH; ++id )
		SetParamGroup( id, "Dice" );
	for( unsigned int id = PT_FONT; id <= PT_D6_FACES; ++id )
		SetParamGroup( id, "Numbers" );
	for( unsigned int id = PT_SIZE; id <= PT_TABLE_B; ++id )
		SetParamGroup( id, "Scene" );

	SetParamInfo( PT_ABOUT_TEXT, "About", FF_TYPE_TEXT, stoatworks::about::defaultText() );
	{
		FFUInt32 aboutId = PT_ABOUT_TEXT + 1;
		for( const auto& b : stoatworks::about::buttons() )
			SetParamInfo( aboutId++, b.label, FF_TYPE_EVENT, false );
	}
	for( unsigned int id = PT_ABOUT_TEXT; id < PT_SOURCE_COUNT; ++id )
		SetParamGroup( id, "About" );

	if( isEffect )
	{
		standard( PT_MIX, "Mix" );
		SetParamGroup( PT_MIX, "Over" );
	}
}

DicePlugin::~DicePlugin()
{
	if( pendingCancel )
		pendingCancel->store( true );
	if( pending.valid() )
		pending.wait();
	ReapAbandoned( true );
}

//---------------------------------------------------------------------------
FFResult DicePlugin::InitGL( const FFGLViewportStruct* vp )
{
	diag::init();
	diag::info( std::string( "GL vendor=" ) + glStringOrUnknown( GL_VENDOR ) + " renderer="
	            + glStringOrUnknown( GL_RENDERER ) + " version=" + glStringOrUnknown( GL_VERSION ) );

	using namespace shaders;
	const std::string vertex   = std::string( kVersion ) + kQuadVertex;
	const std::string fragment = Assemble( kDice, kMaterial, kFragment );
	if( !shader.Compile( vertex.c_str(), fragment.c_str() ) )
	{
		diag::error( "the dice shader failed to compile - the plugin will do nothing" );
		FFGLLog::LogToHost( "Polyhedral: shader failed to compile" );
		DeInitGL();
		return FF_FAIL;
	}
	if( !quad.Initialise() )
	{
		diag::error( "quad geometry failed to initialise" );
		DeInitGL();
		return FF_FAIL;
	}

	//Every texture made and filled before anything is bound to draw.
	GLuint textures[ 4 ];
	glGenTextures( 4, textures );
	dataTexture    = textures[ 0 ];
	atlasTexture   = textures[ 1 ];
	pictureTexture = textures[ 2 ];
	blankTexture   = textures[ 3 ];
	const unsigned char grey[ 4 ] = { 200, 200, 200, 255 };
	for( GLuint t : { pictureTexture, blankTexture } )
	{
		glBindTexture( GL_TEXTURE_2D, t );
		glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, grey );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
	}
	glBindTexture( GL_TEXTURE_2D, 0 );
	fontDirty = dataDirty = atlasUploadDirty = true;
	pictureDirty = !pictureFilePath.empty();
	loadedPicturePath.clear();

	diag::info( isEffect ? "initialised (Over)" : "initialised (source)" );
	return CFFGLPlugin::InitGL( vp );
}

FFResult DicePlugin::DeInitGL()
{
	shader.FreeGLResources();
	quad.Release();
	GLuint textures[ 4 ] = { dataTexture, atlasTexture, pictureTexture, blankTexture };
	for( GLuint t : textures )
		if( t != 0 )
			glDeleteTextures( 1, &t );
	dataTexture = atlasTexture = pictureTexture = blankTexture = 0;
	return FF_SUCCESS;
}

//---------------------------------------------------------------------------
void DicePlugin::UpdateClock()
{
	const double wallNow = wallSeconds();
	if( wallStart < 0.0 )
		wallStart = wallNow;
	const double raw = hostTime;

	//Resolume has been seen sending seconds and milliseconds through SetTime:
	//vote on the unit against the wall clock (rosette's code, via boreal).
	if( clockScale == 0.0 && raw >= 0.0 && lastRawTime >= 0.0 && lastWallTime >= 0.0 )
	{
		const double hostDelta = raw - lastRawTime;
		const double wallDelta = wallNow - lastWallTime;
		if( hostDelta > 0.0 && wallDelta >= 0.0005 )
		{
			const double ratio = hostDelta / wallDelta;
			if( ratio > 0.1 && ratio < 10.0 )
				++secondsVotes;
			else if( ratio > 100.0 && ratio < 10000.0 )
				++millisVotes;
			if( secondsVotes >= kClockVotes || millisVotes >= kClockVotes )
			{
				clockScale = millisVotes > secondsVotes ? 0.001 : 1.0;
				lastNow    = -1.0;//the unit changed under us: no step across it
			}
		}
	}
	if( raw >= 0.0 )
		lastRawTime = raw;
	lastWallTime = wallNow;
	now          = ( raw >= 0.0 && clockScale != 0.0 ) ? raw * clockScale : wallNow - wallStart;
}

roll::Request DicePlugin::MakeRequest() const
{
	roll::Request r  = testFlags;
	r.die            = static_cast< geo::DieType >( OptionIndex( params[ PT_DIE ], static_cast< int >( geo::DieType::Count ) ) );
	r.count          = Count();
	r.fixed          = OptionIndex( params[ PT_RESULT ], static_cast< int >( Result::Count ) ) == static_cast< int >( Result::Fixed );
	r.fixedTotal     = static_cast< int >( std::lround( params[ PT_FIXED_TOTAL ] ) );
	r.seed           = static_cast< uint32_t >( std::clamp( std::lround( params[ PT_SEED ] ), 0L, 9999L ) );
	r.roll           = rollIndex;
	r.from           = static_cast< roll::Throw >( OptionIndex( params[ PT_THROW ], static_cast< int >( roll::Throw::Count ) ) );
	r.spin           = SpinFromParam( params[ PT_SPIN ] );
	r.restitution    = BounceFromParam( params[ PT_BOUNCE ] );
	r.rollTime       = RollTimeFromParam( params[ PT_ROLL_TIME ] );
	r.arena          = arena;
	return r;
}

geo::DieType DicePlugin::Die() const
{
	return static_cast< geo::DieType >( OptionIndex( params[ PT_DIE ], static_cast< int >( geo::DieType::Count ) ) );
}

int DicePlugin::Count() const
{
	return std::clamp( static_cast< int >( std::lround( params[ PT_COUNT ] ) ), 1, kMaxCount );
}

void DicePlugin::ReapAbandoned( bool wait )
{
	for( size_t i = 0; i < abandoned.size(); )
	{
		if( wait || abandoned[ i ].wait_for( std::chrono::seconds( 0 ) ) == std::future_status::ready )
		{
			if( wait )
				abandoned[ i ].wait();
			abandoned.erase( abandoned.begin() + static_cast< long >( i ) );
			abandonedCancel.erase( abandonedCancel.begin() + static_cast< long >( i ) );
		}
		else
			++i;
	}
}

void DicePlugin::StartRoll()
{
	++rollIndex;
	lastRollRequest = clock;
	const roll::Request request = MakeRequest();
	if( pending.valid() )
	{
		//A press while a throw is still being planned: that throw is dropped,
		//and its worker told to stop. Its future is kept until it does --
		//a std::async future's destructor would block the render thread.
		pendingCancel->store( true );
		abandoned.push_back( std::move( pending ) );
		abandonedCancel.push_back( pendingCancel );
	}
	if( synchronous )
	{
		plan      = roll::MakePlan( request );
		rollStart = clock;
		++rollsStarted;
		return;
	}
	pendingCancel = std::make_shared< std::atomic< bool > >( false );
	auto cancel   = pendingCancel;
	pending       = std::async( std::launch::async, [ request, cancel ]() { return roll::MakePlan( request, cancel.get() ); } );
}

void DicePlugin::Relayout()
{
	if( pending.valid() )
	{
		pendingCancel->store( true );
		abandoned.push_back( std::move( pending ) );
		abandonedCancel.push_back( pendingCancel );
	}
	plan        = roll::MakeRest( MakeRequest() );
	rollStart   = clock;
	laidOut     = true;
	layoutDie   = static_cast< int >( Die() );
	layoutCount = Count();
	dataDirty   = true;
}

void DicePlugin::Tick( double dt )
{
	clock += dt;

	if( !laidOut || static_cast< int >( Die() ) != layoutDie || Count() != layoutCount )
		Relayout();

	if( rollPresses > 0 )
	{
		rollPresses = 0;
		StartRoll();
	}

	const bool autoOn = params[ PT_AUTO_ROLL ] > 0.5f;
	if( autoOn && !autoWas )
		lastRollRequest = -1e30;//switching it on rolls now
	autoWas = autoOn;
	if( autoOn && !pending.valid() && clock - lastRollRequest >= IntervalFromParam( params[ PT_INTERVAL ] ) )
		StartRoll();

	if( pending.valid() && pending.wait_for( std::chrono::seconds( 0 ) ) == std::future_status::ready )
	{
		roll::Plan next = pending.get();
		pendingCancel.reset();
		if( !next.bodies.empty() )
		{
			plan      = std::move( next );
			rollStart = clock;
			++rollsStarted;
			std::string values;
			for( int v : plan.results )
				values += ( values.empty() ? "" : " " ) + std::to_string( v );
			diag::info( std::string( "roll " ) + std::to_string( rollIndex ) + ": " + geo::DieName( Die() ) + " x"
			            + std::to_string( Count() ) + " = " + values + " (total " + std::to_string( plan.total ) + "), "
			            + std::to_string( plan.trials ) + " throws, natural " + std::to_string( plan.natural ) + " s, warp "
			            + std::to_string( plan.warp ) + ", planned in " + std::to_string( plan.ms ) + " ms" );
		}
	}
	ReapAbandoned( false );
}

void DicePlugin::AdoptPlanForTest( const roll::Plan& p )
{
	plan      = p;
	rollStart = clock;
	laidOut   = true;
	layoutDie = static_cast< int >( Die() );
	layoutCount = Count();
}

//---------------------------------------------------------------------------
// The camera, and the walls: the frame's footprint on the table, pulled in
// so a die touching a wall is still wholly in shot.
//---------------------------------------------------------------------------
void DicePlugin::BuildCamera( int width, int height )
{
	const geo::Solid& solid = geo::GetSolid( geo::ShapeOf( Die() ) );
	const double nominal    = 2.0 * solid.midradius;
	const double elevation  = CameraAngleFromParam( params[ PT_CAMERA_ANGLE ] ) * kPi / 180.0;

	camera.tanHalf     = std::tan( kHalfFov * kPi / 180.0 );
	camera.aspect      = static_cast< double >( width ) / std::max( height, 1 );
	camera.frameHeight = nominal / SizeFromParam( params[ PT_SIZE ] );
	//Several dice must all fit: a frame too tight for Count of them is widened
	//until the table in shot holds a square of them with room to land. One die
	//keeps any close-up Size asks for (its walls move out instead, below).
	{
		const int dice = Count() * geo::BodiesPerDie( Die() );
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
	const geo::Solid& thrown = geo::GetSolid( geo::ShapeOf( Die() ) );
	const int bodies         = Count() * geo::BodiesPerDie( Die() );
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
	arena.middle  = { 0.0, 0.0, 0.0 };//the camera looks at the origin
	lastWidth     = width;
	lastHeight    = height;
}

//---------------------------------------------------------------------------
// The font, and the data texture the numbers are read from.
//---------------------------------------------------------------------------
void DicePlugin::ResolveFont()
{
	std::string file, name;
	bool nameSet = false, indexMoved = false;
	{
		std::lock_guard< std::mutex > lock( textMutex );
		file           = fontFilePath;
		name           = fontName;
		nameSet        = fontNameSet;
		indexMoved     = fontIndexMoved;
		fontNameSet    = false;
		fontIndexMoved = false;
	}

	const std::vector< FontFile >& fonts = InstalledFonts();
	bool fromName = false;
	bool loaded   = false;
	//An explicit file beats everything: the escape hatch for a font that is
	//not installed, and someone who picked one meant it.
	if( !file.empty() && typeface.Load( file, 0 ) )
		loaded = true;
	else
	{
		//The dropdown stores an INDEX into this machine's font list; the
		//Font Name stores the family. A composition restores both before the
		//next frame, and then the name wins -- index 47 is a different font
		//on the next rig. Only the dropdown moving on its own picks by index.
		int chosen = -1;
		if( nameSet )
		{
			fromName = true;
			chosen   = name.empty() ? -1 : FindFontByFamily( name );
			if( !name.empty() && chosen < 0 )
				diag::warn( "font '" + name + "' is not installed here; using the built-in face" );
		}
		else
			chosen = OptionIndex( params[ PT_FONT ], static_cast< int >( fonts.size() ) + 1 ) - 1;
		(void)indexMoved;
		if( chosen >= 0 && typeface.Load( fonts[ static_cast< size_t >( chosen ) ].path, fonts[ static_cast< size_t >( chosen ) ].collectionIndex ) )
			loaded = true;
		else
			typeface.UseBuiltin();
	}
	if( !loaded )
		typeface.UseBuiltin();

	atlas          = typeface.Build();
	resolvedFamily = atlas.family;
	const int index = resolvedFamily.empty() ? 0 : FindFontByFamily( resolvedFamily ) + 1;
	if( index > 0 || resolvedFamily.empty() )
		params[ PT_FONT ] = static_cast< float >( std::max( index, 0 ) );
	{
		//The field shows the family in use, so a saved composition carries it
		//-- except when a typed name was not found: kept as typed, so the same
		//composition finds it on a machine that has it.
		std::lock_guard< std::mutex > lock( textMutex );
		if( !( fromName && !name.empty() && resolvedFamily.empty() ) )
			fontName = resolvedFamily;
	}
	diag::info( "numbers in " + ( resolvedFamily.empty() ? std::string( "the built-in face" ) : resolvedFamily ) );
	fontDirty        = false;
	atlasUploadDirty = true;
	dataDirty        = true;
}

SlotLayout DicePlugin::LayoutCandidate( const geo::Slot& slot, const std::string& text, int value, bool marked ) const
{
	const Mark mark = static_cast< Mark >( OptionIndex( params[ PT_MARK ], static_cast< int >( Mark::Count ) ) );
	return LayoutSlot( slot, text, value, marked, mark, atlas, NumberSizeFromParam( params[ PT_NUMBER_SIZE ] ) );
}

std::string DicePlugin::FontFamily() const
{
	return resolvedFamily;
}

void DicePlugin::BuildData()
{
	const geo::DieType die  = Die();
	const geo::Shape shape  = geo::ShapeOf( die );
	const geo::Solid& solid = geo::GetSolid( shape );
	const auto& slots       = geo::GetSlots( shape );

	data.assign( static_cast< size_t >( kDataWidth ) * kDataRows * 4, 0.0f );
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

	const Mark mark   = static_cast< Mark >( OptionIndex( params[ PT_MARK ], static_cast< int >( Mark::Count ) ) );
	const float size  = NumberSizeFromParam( params[ PT_NUMBER_SIZE ] );
	for( int part = 0; part < 2; ++part )
	{
		const geo::Labelling& labels = geo::GetLabelling( die, die == geo::DieType::D100 ? part : 0 );
		layouts[ part ].clear();
		const int base = ROW_SLOTS + 5 * part;
		for( size_t s = 0; s < slots.size(); ++s )
		{
			const geo::Slot& slot = slots[ s ];
			const size_t item     = static_cast< size_t >( slot.item );
			const SlotLayout l    = LayoutSlot( slot, labels.text[ item ], labels.digit[ item ], labels.marked[ item ], mark, atlas, size );
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

	glBindTexture( GL_TEXTURE_2D, dataTexture );
	glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA32F, kDataWidth, kDataRows, 0, GL_RGBA, GL_FLOAT, data.data() );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
	glBindTexture( GL_TEXTURE_2D, 0 );
	dataDirty = false;
}

void DicePlugin::UploadAtlas()
{
	GLint alignment = 4;
	glGetIntegerv( GL_UNPACK_ALIGNMENT, &alignment );
	glPixelStorei( GL_UNPACK_ALIGNMENT, 1 );
	glBindTexture( GL_TEXTURE_2D, atlasTexture );
	glTexImage2D( GL_TEXTURE_2D, 0, GL_R8, DigitAtlas::kWidth, DigitAtlas::kHeight, 0, GL_RED, GL_UNSIGNED_BYTE, atlas.pixels.data() );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
	glBindTexture( GL_TEXTURE_2D, 0 );
	glPixelStorei( GL_UNPACK_ALIGNMENT, alignment );
	atlasUploadDirty = false;
}

void DicePlugin::UploadPicture()
{
	std::string path;
	{
		std::lock_guard< std::mutex > lock( textMutex );
		path = pictureFilePath;
	}
	pictureDirty = false;
	if( path == loadedPicturePath )
		return;
	loadedPicturePath = path;
	picture           = LoadPicture( path );
	glBindTexture( GL_TEXTURE_2D, pictureTexture );
	if( picture.Valid() )
	{
		GLint alignment = 4;
		glGetIntegerv( GL_UNPACK_ALIGNMENT, &alignment );
		glPixelStorei( GL_UNPACK_ALIGNMENT, 1 );
		glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8, picture.width, picture.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, picture.rgba.data() );
		glPixelStorei( GL_UNPACK_ALIGNMENT, alignment );
		glGenerateMipmap( GL_TEXTURE_2D );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR );
		diag::info( "texture file loaded: " + path + " (" + std::to_string( picture.width ) + "x" + std::to_string( picture.height ) + ")" );
	}
	else
	{
		const unsigned char grey[ 4 ] = { 200, 200, 200, 255 };
		glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, grey );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
		if( !path.empty() )
			diag::warn( "texture file will not load (" + picture.error + "): " + path );
	}
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
	glBindTexture( GL_TEXTURE_2D, 0 );
}

//---------------------------------------------------------------------------
FFResult DicePlugin::ProcessOpenGL( ProcessOpenGLStruct* pgl )
{
	if( pgl == nullptr || shader.GetGLID() == 0 )
		return FF_FAIL;
	const FFGLTextureStruct* input = nullptr;
	if( isEffect )
	{
		if( pgl->numInputTextures < 1 || pgl->inputTextures[ 0 ] == nullptr )
			return FF_FAIL;
		input = pgl->inputTextures[ 0 ];
	}

	ScopedGLState restore;
	const GLint* hostViewport = restore.saved.viewport;
	const int width           = input ? static_cast< int >( input->Width ) : hostViewport[ 2 ];
	const int height          = input ? static_cast< int >( input->Height ) : hostViewport[ 3 ];
	if( width <= 0 || height <= 0 )
		return FF_FAIL;
	glDisable( GL_BLEND );

	//-------------------------------------------------------------------
	// Time: frame-relative, so Resolume's huge float clock never reaches the
	// shader; a backwards or large step (a clip trigger, a scrub) passes no
	// time at all.
	//-------------------------------------------------------------------
	UpdateClock();
	double dt = 0.0;
	if( lastNow >= 0.0 )
	{
		const double step = now - lastNow;
		if( step > 0.0 && step <= kMaxFrameDelta )
			dt = step;
	}
	lastNow = now;

	BuildCamera( width, height );
	Tick( dt );

	//-------------------------------------------------------------------
	// Everything uploaded before anything is bound to draw.
	//-------------------------------------------------------------------
	if( fontDirty )
		ResolveFont();
	if( dataDirty )
		BuildData();
	if( atlasUploadDirty )
		UploadAtlas();
	if( pictureDirty )
		UploadPicture();

	//-------------------------------------------------------------------
	// The bodies as they are this frame: simulated rotation times symmetry.
	//-------------------------------------------------------------------
	const double t        = clock - rollStart;
	const size_t bodies   = std::min< size_t >( plan.bodies.size(), static_cast< size_t >( kMaxBodies ) );
	float rotations[ kMaxBodies * 9 ] = {};
	float positions[ kMaxBodies * 3 ] = {};
	int parts[ kMaxBodies ]           = {};
	for( size_t i = 0; i < bodies; ++i )
	{
		const roll::Pose pose = plan.PoseAt( i, t );
		const M3 r            = ToMatrix( pose.q ) * plan.bodies[ i ].S;
		for( int row = 0; row < 3; ++row )
			for( int col = 0; col < 3; ++col )
				rotations[ i * 9 + static_cast< size_t >( row * 3 + col ) ] = static_cast< float >( r.m[ row ][ col ] );
		positions[ i * 3 + 0 ] = static_cast< float >( pose.x.x );
		positions[ i * 3 + 1 ] = static_cast< float >( pose.x.y );
		positions[ i * 3 + 2 ] = static_cast< float >( pose.x.z );
		parts[ i ]             = plan.bodies[ i ].part;
	}

	const geo::DieType die  = Die();
	const geo::Shape shape  = geo::ShapeOf( die );
	const geo::Solid& solid = geo::GetSolid( shape );
	const int texture       = OptionIndex( params[ PT_TEXTURE ], isEffect ? static_cast< int >( Texture::Count )
	                                                                     : static_cast< int >( Texture::Count ) - 1 );
	float colour[ 3 ], second[ 3 ], ink[ 3 ], table[ 3 ];
	linearColour( &params[ PT_COLOUR_R ], colour );
	linearColour( &params[ PT_SECOND_R ], second );
	linearColour( &params[ PT_INK_R ], ink );
	linearColour( &params[ PT_TABLE_R ], table );
	const double azimuth   = ( LightAngleFromParam( params[ PT_LIGHT_ANGLE ] ) + 135.0 ) * kPi / 180.0;
	const double elevation = kLightElevation * kPi / 180.0;
	const V3 light = { std::cos( elevation ) * std::cos( azimuth ), std::sin( elevation ), std::cos( elevation ) * std::sin( azimuth ) };

	if( flat )
	{
		//Silhouettes for the harness: white, unlit, no ink, no table.
		colour[ 0 ] = colour[ 1 ] = colour[ 2 ] = 1.0f;
	}

	ScopedShaderBinding shaderBinding( shader.GetGLID() );
	shader.Set( "Resolution", static_cast< float >( width ), static_cast< float >( height ) );
	shader.Set( "CamPos", static_cast< float >( camera.position.x ), static_cast< float >( camera.position.y ), static_cast< float >( camera.position.z ) );
	shader.Set( "CamRight", static_cast< float >( camera.right.x ), static_cast< float >( camera.right.y ), static_cast< float >( camera.right.z ) );
	shader.Set( "CamUp", static_cast< float >( camera.up.x ), static_cast< float >( camera.up.y ), static_cast< float >( camera.up.z ) );
	shader.Set( "CamForward", static_cast< float >( camera.forward.x ), static_cast< float >( camera.forward.y ), static_cast< float >( camera.forward.z ) );
	shader.Set( "TanHalf", static_cast< float >( camera.tanHalf ) );
	shader.Set( "PixelAngle", static_cast< float >( 2.0 * camera.tanHalf / height ) );
	shader.Set( "Bodies", static_cast< int >( bodies ) );
	if( bodies > 0 )
	{
		glUniformMatrix3fv( shader.FindUniform( "Rot" ), static_cast< GLsizei >( bodies ), GL_TRUE, rotations );
		glUniform3fv( shader.FindUniform( "Pos" ), static_cast< GLsizei >( bodies ), positions );
		glUniform1iv( shader.FindUniform( "Part" ), static_cast< GLsizei >( bodies ), parts );
	}
	shader.Set( "FaceCount", static_cast< int >( solid.faces.size() ) );
	shader.Set( "EdgeCount", static_cast< int >( solid.edges.size() ) );
	shader.Set( "SlotsPerFace", static_cast< int >( geo::GetSlots( shape ).size() / solid.faces.size() ) );
	shader.Set( "Inradius", static_cast< float >( solid.inradius ) );
	shader.Set( "Circumradius", static_cast< float >( solid.circumradius ) );
	shader.Set( "Material", flat ? 0 : texture );
	shader.Set( "Colour", colour[ 0 ], colour[ 1 ], colour[ 2 ] );
	shader.Set( "Second", second[ 0 ], second[ 1 ], second[ 2 ] );
	shader.Set( "Ink", ink[ 0 ], ink[ 1 ], ink[ 2 ] );
	shader.Set( "Gloss", GlossFromParam( params[ PT_GLOSS ] ) );
	shader.Set( "Bevel", static_cast< float >( EdgeFromParam( params[ PT_EDGE ] ) * solid.inradius ) );
	shader.Set( "LineWidth", static_cast< float >( LineWidthFromParam( params[ PT_LINE_WIDTH ] ) * 2.0 * solid.inradius ) );
	shader.Set( "Weight", WeightFromParam( params[ PT_WEIGHT ] ) );
	shader.Set( "Engraved", OptionIndex( params[ PT_NUMBER_STYLE ], 2 ) == static_cast< int >( NumberStyle::Engraved ) ? 1 : 0 );
	shader.Set( "Pips", die == geo::DieType::D6 && OptionIndex( params[ PT_D6_FACES ], 2 ) == static_cast< int >( D6Faces::Pips ) ? 1 : 0 );
	shader.Set( "AtlasPx", atlas.pxPerUnit );
	shader.Set( "AtlasSize", static_cast< float >( DigitAtlas::kWidth ), static_cast< float >( DigitAtlas::kHeight ) );
	shader.Set( "LightDir", static_cast< float >( light.x ), static_cast< float >( light.y ), static_cast< float >( light.z ) );
	shader.Set( "ShadowAmount", flat ? 0.0f : std::clamp( params[ PT_SHADOW ], 0.0f, 1.0f ) );
	shader.Set( "TableKind", flat ? 0 : OptionIndex( params[ PT_TABLE ], static_cast< int >( Table::Count ) ) );
	shader.Set( "TableColour", table[ 0 ], table[ 1 ], table[ 2 ] );
	shader.Set( "IsEffect", isEffect && !flat ? 1 : 0 );
	shader.Set( "Mix", std::clamp( params[ PT_MIX ], 0.0f, 1.0f ) );
	shader.Set( "TestFlat", flat ? 1 : 0 );
	if( input != nullptr )
	{
		const FFGLTexCoords maxCoords = GetMaxGLTexCoords( *input );
		shader.Set( "ClipScale", maxCoords.s, maxCoords.t );
	}
	else
		shader.Set( "ClipScale", 1.0f, 1.0f );

	//Units bound by hand: four is past what the scoped bindings can unwind
	//(boreal's note in GLState.h).
	shader.Set( "Data", 0 );
	shader.Set( "Atlas", 1 );
	shader.Set( "Clip", 2 );
	shader.Set( "Picture", 3 );
	bindUnit( 0, dataTexture );
	bindUnit( 1, atlasTexture );
	bindUnit( 2, input != nullptr ? input->Handle : blankTexture );
	bindUnit( 3, pictureTexture );
	quad.Draw();
	unbindTextureUnits( 4 );
	glActiveTexture( GL_TEXTURE0 );
	return FF_SUCCESS;
}

//---------------------------------------------------------------------------
FFResult DicePlugin::SetTime( double time )
{
	hostTime = time;
	return FF_SUCCESS;
}

FFResult DicePlugin::SetFloatParameter( unsigned int index, float value )
{
	if( index >= ParamCount() )
		return FF_FAIL;
	if( index >= PT_ABOUT_TEXT && index < PT_SOURCE_COUNT )
		return stoatworks::about::handleParam( index - PT_ABOUT_TEXT, value ) ? FF_SUCCESS : FF_FAIL;

	//Events act on the rising edge, once.
	if( index == PT_ROLL )
	{
		const bool down = value >= 0.5f;
		if( down && !rollHeld )
			++rollPresses;
		rollHeld = down;
	}
	const float previous = params[ index ];
	params[ index ]      = value;
	if( value == previous )
		return FF_SUCCESS;

	switch( index )
	{
	case PT_FONT:
		fontIndexMoved = true;
		fontDirty      = true;
		break;
	case PT_NUMBER_SIZE:
	case PT_MARK:
		dataDirty = true;
		break;
	default:
		break;
	}
	return FF_SUCCESS;
}

float DicePlugin::GetFloatParameter( unsigned int index )
{
	return index < ParamCount() ? params[ index ] : 0.0f;
}

FFResult DicePlugin::SetTextParameter( unsigned int index, const char* value )
{
	//Display-only, and it MUST still succeed: instantiateGL pushes every
	//declared default back through the setters and deletes the instance if
	//one fails.
	if( index == PT_ABOUT_TEXT )
		return FF_SUCCESS;

	const std::string incoming = value != nullptr ? value : "";
	std::lock_guard< std::mutex > lock( textMutex );
	switch( index )
	{
	case PT_TEXTURE_FILE:
		if( incoming != pictureFilePath )
		{
			pictureFilePath = incoming;
			pictureDirty    = true;
		}
		return FF_SUCCESS;
	case PT_FONT_FILE:
		if( incoming != fontFilePath )
		{
			fontFilePath = incoming;
			fontDirty    = true;
		}
		return FF_SUCCESS;
	case PT_FONT_NAME:
		if( incoming != fontName )
		{
			fontName    = incoming;
			fontNameSet = true;
			fontDirty   = true;
		}
		return FF_SUCCESS;
	default:
		return FF_FAIL;
	}
}

char* DicePlugin::GetTextParameter( unsigned int index )
{
	if( index == PT_ABOUT_TEXT )
	{
		static const std::string aboutLine = stoatworks::about::textParam( 0 );
		return const_cast< char* >( aboutLine.c_str() );
	}

	std::lock_guard< std::mutex > lock( textMutex );
	const std::string* source = nullptr;
	switch( index )
	{
	case PT_TEXTURE_FILE: source = &pictureFilePath; break;
	case PT_FONT_FILE: source = &fontFilePath; break;
	case PT_FONT_NAME: source = &fontName; break;
	default: break;
	}
	textReturn[ 0 ] = '\0';
	if( source != nullptr )
	{
		const size_t length = std::min( source->size(), sizeof( textReturn ) - 1 );
		std::memcpy( textReturn, source->data(), length );
		textReturn[ length ] = '\0';
	}
	return textReturn;
}

} // namespace dice
