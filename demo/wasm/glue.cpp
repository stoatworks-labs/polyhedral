/*
    The page's way in to the plugin: what an FFGL host does to a plugin
    instance, as C functions JavaScript can call.

    Nothing here is the plugin, and nothing here does the plugin's work. The
    plugin is `source/Dice.cpp` and everything it calls, compiled UNMODIFIED
    into the same WebAssembly module as this file (see
    demo/tools/build-wasm.sh for the list). This file only plays the host:

      - it constructs a `DicePlugin`, as SourcePlugin.cpp / EffectPlugin.cpp's
        registrations would, and calls InitGL / DeInitGL on it;
      - it reads the parameter declarations back through the FFGL SDK's own
        host-facing getters (CFFGLPluginManager::GetParamName and the rest,
        the SDK compiled unmodified too), so the page's panel is built from
        the plugin's constructor rather than from a copy of it;
      - it forwards the host's calls: SetFloatParameter, SetTextParameter,
        SetTime, ProcessOpenGL with a viewport (the source) or one input
        texture (the effect), exactly as `tools/polytest`'s Rig does.

    Two choices here are the page's and not the plugin's, and the page says so:

      - `SetSynchronousForTest( true )`: the plugin plans a throw on a worker
        thread (`std::async`). This module is built without threads, so it
        plans on the page's thread, as the plugin's own harness does. A roll
        of many dice can hold up one frame.
      - The clock is the page's: SetTime is handed the kit's seconds, and the
        plugin's own vote decides the unit (it settles on seconds within four
        frames, as it would in Arena).

    GL calls go to the page's WebGL2 context through emscripten's GL library;
    demo/wasm/gl_shim.cpp replaces the four entry points that cannot be passed
    straight through.
*/
#include "Controls.h"
#include "Diag.h"
#include "Dice.h"

#include <emscripten/emscripten.h>

#include <sys/stat.h>

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>

using namespace dice;

namespace
{
/// One plugin instance and the host-side structs it is handed.
struct Instance
{
	explicit Instance( bool effect ) : plugin( effect )
	{
		plugin.SetSynchronousForTest( true );
	}

	DicePlugin plugin;
	bool initialised = false;

	FFGLTextureStruct input {};
	FFGLTextureStruct* inputs[ 1 ] = { &input };
	ProcessOpenGLStruct process {};

	std::string scratch;///< a string handed back to JS lives here until the next call
};

float asFloat( FFMixed mixed )
{
	float value = 0.0f;
	static_assert( sizeof( value ) == sizeof( mixed.UIntValue ), "FFMixed carries a float in its bits" );
	std::memcpy( &value, &mixed.UIntValue, sizeof( value ) );
	return value;
}

const char* hold( Instance* instance, std::string text )
{
	instance->scratch = std::move( text );
	return instance->scratch.c_str();
}
} // namespace

extern "C"
{
//---------------------------------------------------------------------------
// Lifetime.
//---------------------------------------------------------------------------
EMSCRIPTEN_KEEPALIVE Instance* poly_new( int effect )
{
	return new Instance( effect != 0 );
}

EMSCRIPTEN_KEEPALIVE void poly_delete( Instance* instance )
{
	if( instance == nullptr )
		return;
	if( instance->initialised )
		instance->plugin.DeInitGL();
	delete instance;
}

/// InitGL, with the viewport a host would pass. 1 on success.
EMSCRIPTEN_KEEPALIVE int poly_init_gl( Instance* instance, int width, int height )
{
	FFGLViewportStruct viewport {};
	viewport.width  = static_cast< GLuint >( width );
	viewport.height = static_cast< GLuint >( height );
	instance->initialised = instance->plugin.InitGL( &viewport ) == FF_SUCCESS;
	return instance->initialised ? 1 : 0;
}

//---------------------------------------------------------------------------
// The declarations, read as a host reads them.
//---------------------------------------------------------------------------
EMSCRIPTEN_KEEPALIVE int poly_param_count( Instance* instance )
{
	return static_cast< int >( instance->plugin.GetNumParams() );
}

EMSCRIPTEN_KEEPALIVE const char* poly_param_name( Instance* instance, int index )
{
	const char* name = instance->plugin.GetParamName( static_cast< unsigned int >( index ) );
	return name ? name : "";
}

EMSCRIPTEN_KEEPALIVE int poly_param_type( Instance* instance, int index )
{
	return static_cast< int >( instance->plugin.GetParamType( static_cast< unsigned int >( index ) ) );
}

EMSCRIPTEN_KEEPALIVE const char* poly_param_group( Instance* instance, int index )
{
	return hold( instance, instance->plugin.GetParamGroup( static_cast< unsigned int >( index ) ) );
}

/// The declared default of a numeric parameter (FF_TYPE_STANDARD already
/// clamped into 0..1 by the SDK, as a host sees it).
EMSCRIPTEN_KEEPALIVE float poly_param_default( Instance* instance, int index )
{
	return asFloat( instance->plugin.GetParamDefault( static_cast< unsigned int >( index ) ) );
}

/// The declared default of a text or file parameter.
EMSCRIPTEN_KEEPALIVE const char* poly_param_default_text( Instance* instance, int index )
{
	const FFMixed mixed = instance->plugin.GetParamDefault( static_cast< unsigned int >( index ) );
	return mixed.PointerValue ? static_cast< const char* >( mixed.PointerValue ) : "";
}

EMSCRIPTEN_KEEPALIVE int poly_param_element_count( Instance* instance, int index )
{
	return static_cast< int >( instance->plugin.GetNumParamElements( static_cast< unsigned int >( index ) ) );
}

EMSCRIPTEN_KEEPALIVE const char* poly_param_element_name( Instance* instance, int index, int element )
{
	const char* name = instance->plugin.GetParamElementName( static_cast< unsigned int >( index ), static_cast< unsigned int >( element ) );
	return name ? name : "";
}

EMSCRIPTEN_KEEPALIVE float poly_param_element_value( Instance* instance, int index, int element )
{
	return asFloat( instance->plugin.GetParamElementDefault( static_cast< unsigned int >( index ), static_cast< unsigned int >( element ) ) );
}

EMSCRIPTEN_KEEPALIVE float poly_param_range_min( Instance* instance, int index )
{
	return instance->plugin.GetParamRange( static_cast< unsigned int >( index ) ).min;
}

EMSCRIPTEN_KEEPALIVE float poly_param_range_max( Instance* instance, int index )
{
	return instance->plugin.GetParamRange( static_cast< unsigned int >( index ) ).max;
}

EMSCRIPTEN_KEEPALIVE int poly_param_extension_count( Instance* instance, int index )
{
	return static_cast< int >( instance->plugin.GetNumFileParamExtensions( static_cast< unsigned int >( index ) ) );
}

EMSCRIPTEN_KEEPALIVE const char* poly_param_extension( Instance* instance, int index, int extension )
{
	const char* text = instance->plugin.GetFileParamExtension( static_cast< unsigned int >( index ), static_cast< unsigned int >( extension ) );
	return text ? text : "";
}

EMSCRIPTEN_KEEPALIVE int poly_max_inputs( Instance* instance )
{
	return static_cast< int >( instance->plugin.GetMaxInputs() );
}

//---------------------------------------------------------------------------
// The host's calls.
//---------------------------------------------------------------------------
EMSCRIPTEN_KEEPALIVE int poly_set_float( Instance* instance, int index, float value )
{
	return instance->plugin.SetFloatParameter( static_cast< unsigned int >( index ), value ) == FF_SUCCESS ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE float poly_get_float( Instance* instance, int index )
{
	return instance->plugin.GetFloatParameter( static_cast< unsigned int >( index ) );
}

EMSCRIPTEN_KEEPALIVE int poly_set_text( Instance* instance, int index, const char* value )
{
	return instance->plugin.SetTextParameter( static_cast< unsigned int >( index ), value ) == FF_SUCCESS ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE const char* poly_get_text( Instance* instance, int index )
{
	const char* text = instance->plugin.GetTextParameter( static_cast< unsigned int >( index ) );
	return text ? text : "";
}

/// One frame: SetTime, then ProcessOpenGL into whatever the page has bound.
/// The source reads its size from the viewport the page set; the effect is
/// handed one input of clipWidth x clipHeight whose GL name is `clip` (a
/// texture the page registered with emscripten's GL tables). 1 on success.
EMSCRIPTEN_KEEPALIVE int poly_process( Instance* instance, double seconds, int clip, int clipWidth, int clipHeight )
{
	instance->plugin.SetTime( seconds );
	if( instance->plugin.IsEffect() )
	{
		instance->input.Width = instance->input.HardwareWidth = static_cast< FFUInt32 >( clipWidth );
		instance->input.Height = instance->input.HardwareHeight = static_cast< FFUInt32 >( clipHeight );
		instance->input.Handle                                 = static_cast< GLuint >( clip );
		instance->process.numInputTextures                     = 1;
		instance->process.inputTextures                        = instance->inputs;
	}
	else
	{
		instance->process.numInputTextures = 0;
		instance->process.inputTextures    = nullptr;
	}
	instance->process.HostFBO = 0;
	return instance->plugin.ProcessOpenGL( &instance->process ) == FF_SUCCESS ? 1 : 0;
}

//---------------------------------------------------------------------------
// For the panel's read-outs: Controls.cpp's conversion for a parameter, the
// one the plugin applies to it (Dice.cpp's MakeRequest, BuildCamera and
// ProcessOpenGL are where each pairing is made). NaN where the plugin uses
// the host's 0..1 as it is.
//---------------------------------------------------------------------------
EMSCRIPTEN_KEEPALIVE double poly_convert( int index, float v )
{
	switch( index )
	{
	case PT_ROLL_TIME: return RollTimeFromParam( v );
	case PT_INTERVAL: return IntervalFromParam( v );
	case PT_SPIN: return SpinFromParam( v );
	case PT_BOUNCE: return BounceFromParam( v );
	case PT_GLOSS: return GlossFromParam( v );
	case PT_EDGE: return EdgeFromParam( v );
	case PT_LINE_WIDTH: return LineWidthFromParam( v );
	case PT_NUMBER_SIZE: return NumberSizeFromParam( v );
	case PT_WEIGHT: return WeightFromParam( v );
	case PT_SIZE: return SizeFromParam( v );
	case PT_CAMERA_ANGLE: return CameraAngleFromParam( v );
	case PT_LIGHT_ANGLE: return LightAngleFromParam( v );
	default: return std::numeric_limits< double >::quiet_NaN();
	}
}

/// Give the plugin's log (Diag.cpp) somewhere to go: a directory in the
/// page's in-memory file system, named through the override Diag.cpp reads.
/// Its own `mkdir -p` goes through system(), which a browser does not have.
/// Call before the first InitGL, which is when the log opens.
EMSCRIPTEN_KEEPALIVE void poly_prepare_log()
{
	setenv( "POLYHEDRAL_LOG_DIR", "/polyhedral/logs", 1 );
	mkdir( "/polyhedral", 0755 );
	mkdir( "/polyhedral/logs", 0755 );
}

/// The plugin's log file (Diag.cpp), in the page's in-memory file system.
EMSCRIPTEN_KEEPALIVE const char* poly_log_path()
{
	static std::string path;
	path = diag::logPath();
	return path.c_str();
}

/// How many throws have started playing: the harness's own counter, so the
/// page can tell a press that has been honoured from one still pending.
EMSCRIPTEN_KEEPALIVE int poly_rolls_started( Instance* instance )
{
	return instance->plugin.RollsStarted();
}

//---------------------------------------------------------------------------
// The plan being played, through the harness's own accessors (CurrentPlan,
// PlaybackSeconds): the same fields the plugin's log line reports for a roll
// planned on its worker. Planned synchronously, as here, it logs nothing.
//---------------------------------------------------------------------------
EMSCRIPTEN_KEEPALIVE int poly_plan_dice( Instance* instance )
{
	return static_cast< int >( instance->plugin.CurrentPlan().results.size() );
}

EMSCRIPTEN_KEEPALIVE int poly_plan_result( Instance* instance, int die )
{
	const auto& results = instance->plugin.CurrentPlan().results;
	return die >= 0 && static_cast< size_t >( die ) < results.size() ? results[ static_cast< size_t >( die ) ] : 0;
}

EMSCRIPTEN_KEEPALIVE int poly_plan_total( Instance* instance )
{
	return instance->plugin.CurrentPlan().total;
}

EMSCRIPTEN_KEEPALIVE int poly_plan_trials( Instance* instance )
{
	return instance->plugin.CurrentPlan().trials;
}

EMSCRIPTEN_KEEPALIVE int poly_plan_idle( Instance* instance )
{
	return instance->plugin.CurrentPlan().idle ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE double poly_plan_natural( Instance* instance )
{
	return instance->plugin.CurrentPlan().natural;
}

EMSCRIPTEN_KEEPALIVE double poly_plan_warp( Instance* instance )
{
	return instance->plugin.CurrentPlan().warp;
}

EMSCRIPTEN_KEEPALIVE double poly_plan_ms( Instance* instance )
{
	return instance->plugin.CurrentPlan().ms;
}

/// 1 once the dice have come to rest (playback has reached Roll Time).
EMSCRIPTEN_KEEPALIVE int poly_plan_finished( Instance* instance )
{
	return instance->plugin.CurrentPlan().Finished( instance->plugin.PlaybackSeconds() ) ? 1 : 0;
}

} // extern "C"
