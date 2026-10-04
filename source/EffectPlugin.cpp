#include "Dice.h"

/**
    The effect: the same dice over the clip -- and the clip itself can be what
    the faces are made of (Texture: Clip).

    See SourcePlugin.cpp for why this file is listed in its own target.
*/
namespace
{
class DiceEffect : public dice::DicePlugin
{
public:
	DiceEffect() :
		DicePlugin( true )
	{
	}
};
} // namespace

static CFFGLPluginInfo PluginInfo(
	PluginFactory< DiceEffect >,  // Create method
	"DI02",                       // Plugin unique ID of maximum length 4
	"SW Dice Over",               // Plugin name
	2,                            // API major version number
	1,                            // API minor version number
	0,                            // Plugin major version number
	1,                            // Plugin minor version number
	FF_EFFECT,                    // Plugin type
	"Polyhedral dice thrown over the clip, landing on a random result or the one you set at exactly Roll Time. "
	"Texture: Clip puts the clip on every face. Table: None keeps the clip as the table, with the dice's shadows on it.",
	"Dice FFGL effect"            // About
);

extern "C" const char* DiceEffectBuildStamp()
{
	return "dice " DICE_VERSION " effect, built " __DATE__ " " __TIME__;
}
