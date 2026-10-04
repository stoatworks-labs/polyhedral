#include "Dice.h"

/**
    The source: dice on a table -- or on nothing, with only their shadows, so
    the layer goes over whatever is under it.

    Listed directly in the PolyhedralSource target, not in polyhedral_core: both plugins
    share the class and not the `CFFGLPluginInfo` below, and putting either
    registration in the shared library would register both plugins into both
    bundles. The core is an OBJECT library because this registers itself from a
    file-scope constructor nothing references (see CMakeLists.txt).

    `SW Polyhedral` is thirteen characters; the FFGL name field is char[ 16 ] and not
    null-terminated. `oxbow probe` reads it back the way a host does.
*/
namespace
{
class PolyhedralSource : public dice::DicePlugin
{
public:
	PolyhedralSource() :
		DicePlugin( false )
	{
	}
};
} // namespace

static CFFGLPluginInfo PluginInfo(
	PluginFactory< PolyhedralSource >,// Create method
	"PY01",                       // Plugin unique ID of maximum length 4
	"SW Polyhedral",              // Plugin name
	2,                            // API major version number
	1,                            // API minor version number
	0,                            // Plugin major version number
	1,                            // Plugin minor version number
	FF_SOURCE,                    // Plugin type
	"Polyhedral dice -- d4, d6, d8, d10, d12, d20 and d100 -- thrown onto the table with real physics. Press Roll; "
	"the result is random, or the Fixed Total you set, and lands face up at exactly Roll Time. The faces can be "
	"marble, pearl, metal, gem, stone, wood, galaxy, a picture or a wireframe; the numbers in any installed font.",
	"Polyhedral FFGL source"      // About
);

extern "C" const char* PolyhedralSourceBuildStamp()
{
	return "polyhedral " POLYHEDRAL_VERSION " source, built " __DATE__ " " __TIME__;
}
