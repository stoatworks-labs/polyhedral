#pragma once

#include "Controls.h"
#include "Geometry.h"
#include "Labels.h"
#include "Picture.h"
#include "Roll.h"
#include "Typeface.h"

#include <FFGLSDK.h>

// After FFGLSDK.h, which is where FFUInt32 comes from.
#include "StoatworksAboutParams.h"

#include <atomic>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace dice
{
/// The data texture's rows: mirrored by the ROW_ constants in Shaders.cpp,
/// and `ditest --data` reads them back through the GPU to prove it.
enum DataRow : int
{
	ROW_PLANES  = 0,
	ROW_VERTS   = 1,
	ROW_EDGE_A  = 2,
	ROW_EDGE_B  = 3,
	ROW_SLOTS   = 4,///< five rows per numbering, two numberings
	ROW_GLYPH   = 14,
	ROW_CELL    = 15,
	ROW_FACE    = 16,
	ROW_FACE_UP = 17,
	kDataRows   = 18,
	kDataWidth  = 64
};

/// What the camera sees, in doubles: for the shader and for the harness's
/// projection of a face onto pixels.
struct Camera
{
	V3 position, right, up, forward;
	double tanHalf     = 0.0;
	double frameHeight = 0.0;///< metres of table spanned by the frame's height at the target
	double aspect      = 16.0 / 9.0;

	/// The world ray through pixel (px, py), y up from the bottom, of a w x h frame.
	V3 Ray( double px, double py, int w, int h ) const;
	/// The pixel a world point lands on.
	bool Project( V3 p, int w, int h, double& px, double& py ) const;
};

/**
    The plugin: the source (dice on a table, or on nothing) and, with
    `isEffect`, the Over effect (the same, over the clip, which can also be the
    faces' texture). One class, two registrations.
*/
class DicePlugin : public CFFGLPlugin
{
public:
	explicit DicePlugin( bool isEffect );
	~DicePlugin() override;

	FFResult InitGL( const FFGLViewportStruct* viewport ) override;
	FFResult ProcessOpenGL( ProcessOpenGLStruct* input ) override;
	FFResult DeInitGL() override;

	FFResult SetFloatParameter( unsigned int index, float value ) override;
	float GetFloatParameter( unsigned int index ) override;
	char* GetTextParameter( unsigned int index ) override;
	/// LOAD-BEARING: the base class's stub fails, and a failed default deletes
	/// the instance -- so without this no real host can load the plugin.
	FFResult SetTextParameter( unsigned int index, const char* value ) override;
	FFResult SetTime( double time ) override;

	bool IsEffect() const
	{
		return isEffect;
	}
	unsigned int ParamCount() const
	{
		return isEffect ? PT_COUNT_ALL : PT_SOURCE_COUNT;
	}

	//-------------------------------------------------------------------
	// For the harness. Nothing in the plugin's own operation calls these.
	//-------------------------------------------------------------------
	/// Plan on the render thread, so a roll starts on the frame it is asked
	/// for and every run is the same run.
	void SetSynchronousForTest( bool on )
	{
		synchronous = on;
	}
	void SetClockScaleForTest( double scale )
	{
		clockScale = scale;
	}
	/// The negative controls' wrong models, copied into every request.
	roll::Request& TestFlags()
	{
		return testFlags;
	}
	/// Draw dice as flat white and the table as nothing: silhouettes only.
	void SetFlatForTest( bool on )
	{
		flat = on;
	}
	/// Show this plan from now on, as if it had just been planned.
	void AdoptPlanForTest( const roll::Plan& plan );
	const roll::Plan& CurrentPlan() const
	{
		return plan;
	}
	double PlaybackSeconds() const
	{
		return clock - rollStart;
	}
	const Camera& CurrentCamera() const
	{
		return camera;
	}
	const roll::Arena& CurrentArena() const
	{
		return arena;
	}
	const DigitAtlas& CurrentAtlas() const
	{
		return atlas;
	}
	const std::vector< SlotLayout >& Layouts( int part ) const
	{
		return layouts[ part & 1 ];
	}
	int RollsStarted() const
	{
		return rollsStarted;
	}
	geo::DieType Die() const;
	int Count() const;
	/// Lay out a candidate number in a slot exactly as the plugin would.
	SlotLayout LayoutCandidate( const geo::Slot& slot, const std::string& text, int value, bool marked ) const;
	std::string FontFamily() const;
	const Picture& CurrentPicture() const
	{
		return picture;
	}
	/// Upload the data texture now and read it back (`ditest --data`).
	std::vector< float > DataForTest() const
	{
		return data;
	}
	GLuint DataTextureForTest() const
	{
		return dataTexture;
	}
	/// The camera and the arena for a frame of this size, without GL.
	void UpdateViewForTest( int width, int height )
	{
		BuildCamera( width, height );
	}
	/// The request the next roll would make, as it would make it.
	roll::Request RequestForTest() const
	{
		return MakeRequest();
	}

private:
	void UpdateClock();
	void Tick( double dt );
	roll::Request MakeRequest() const;
	void StartRoll();
	void Relayout();
	void ResolveFont();
	void BuildData();
	void UploadAtlas();
	void UploadPicture();
	void BuildCamera( int width, int height );
	void ReapAbandoned( bool wait );

	const bool isEffect;
	float params[ PT_COUNT_ALL ] = {};

	ffglex::FFGLShader shader;
	ffglex::FFGLScreenQuad quad;
	GLuint dataTexture = 0, atlasTexture = 0, pictureTexture = 0, blankTexture = 0;

	//Time. Resolume has sent both seconds and milliseconds (see boreal).
	double hostTime = -1.0, lastRawTime = -1.0, lastWallTime = -1.0, wallStart = -1.0;
	double clockScale = 0.0;
	int secondsVotes = 0, millisVotes = 0;
	double now = 0.0, lastNow = -1.0;
	double clock = 0.0;///< seconds of plugin time, monotonic, frame-relative

	//Rolling.
	roll::Plan plan;
	double rollStart        = 0.0;
	double lastRollRequest  = -1e30;
	uint32_t rollIndex      = 0;
	int rollPresses         = 0;
	bool rollHeld           = false;
	bool autoWas            = false;
	int rollsStarted        = 0;
	bool synchronous        = false;
	bool laidOut            = false;
	int layoutDie = -1, layoutCount = -1;
	std::future< roll::Plan > pending;
	std::shared_ptr< std::atomic< bool > > pendingCancel;
	std::vector< std::future< roll::Plan > > abandoned;
	std::vector< std::shared_ptr< std::atomic< bool > > > abandonedCancel;
	roll::Request testFlags;
	bool flat = false;

	//The view and the walls it implies.
	Camera camera;
	roll::Arena arena;
	int lastWidth = 0, lastHeight = 0;

	//Numbers.
	Typeface typeface;
	DigitAtlas atlas;
	std::vector< SlotLayout > layouts[ 2 ];
	std::vector< float > data;
	bool fontDirty = true, dataDirty = true, atlasUploadDirty = true;
	bool fontIndexMoved = false, fontNameSet = false;
	std::string fontFilePath, fontName, resolvedFamily;

	//The Texture File.
	std::string pictureFilePath, loadedPicturePath;
	Picture picture;
	bool pictureDirty = false;

	mutable std::mutex textMutex;
	char textReturn[ 1024 ] = {};
};

} // namespace dice
