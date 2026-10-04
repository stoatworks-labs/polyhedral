/*
    The four GL entry points emscripten's WebGL2 library cannot take from the
    plugin as they are. Every other GL call the plugin and the FFGL SDK make
    goes straight to emscripten's implementation, on the page's own WebGL2
    context.

    glShaderSource
        The plugin hands GL desktop GLSL 4.10 (`#version 410 core`); WebGL2
        compiles GLSL ES 3.00. The page's `polyhedralShaderSource` first
        REQUIRES the text to be byte-for-byte the page's own copy of the
        plugin's shaders (demo/shaders.js, which demo/tools/check_shaders.py
        holds to source/Shaders.cpp), then applies the demo kit's `port()`:
        the version line and the ES precision defaults, nothing else. A
        shader the plugin assembled differently from the page's copy is a
        thrown error, not a quiet compile.

    glEnable / glDisable / glIsEnabled
        GL_PROGRAM_POINT_SIZE is desktop-only state (in ES a point is always
        sized by the shader, and WebGL2 rejects the enum with INVALID_ENUM).
        The plugin's GLState.h saves and restores it on every frame, as a
        plugin inside Resolume must. Here it reads as off and enabling it is
        ignored; the plugin draws no points. Every other capability is passed
        through unchanged.
*/
#include <GLES3/gl3.h>
#include <emscripten/emscripten.h>

#include <cstring>
#include <string>

namespace
{
constexpr GLenum kProgramPointSize = 0x8642;///< GL_PROGRAM_POINT_SIZE, desktop GL 3.2

// clang-format off
EM_JS( void, polyShaderSource, ( GLuint shader, const char* source ), {
	const object = GL.shaders[ shader ];
	const stage  = GLctx.getShaderParameter( object, 0x8B4F /* GL_SHADER_TYPE */ ) === 0x8B31 ? 'vertex' : 'fragment';
	GLctx.shaderSource( object, Module[ 'polyhedralShaderSource' ]( stage, UTF8ToString( source ) ) );
} );
EM_JS( void, polyEnable, ( GLenum cap ), { GLctx.enable( cap ); } );
EM_JS( void, polyDisable, ( GLenum cap ), { GLctx.disable( cap ); } );
EM_JS( int, polyIsEnabled, ( GLenum cap ), { return GLctx.isEnabled( cap ) ? 1 : 0; } );
// clang-format on
} // namespace

extern "C"
{
void glShaderSource( GLuint shader, GLsizei count, const GLchar* const* string, const GLint* length )
{
	std::string source;
	for( GLsizei i = 0; i < count; ++i )
	{
		if( string[ i ] == nullptr )
			continue;
		const size_t n = ( length != nullptr && length[ i ] >= 0 ) ? static_cast< size_t >( length[ i ] ) : std::strlen( string[ i ] );
		source.append( string[ i ], n );
	}
	polyShaderSource( shader, source.c_str() );
}

void glEnable( GLenum cap )
{
	if( cap != kProgramPointSize )
		polyEnable( cap );
}

void glDisable( GLenum cap )
{
	if( cap != kProgramPointSize )
		polyDisable( cap );
}

GLboolean glIsEnabled( GLenum cap )
{
	if( cap == kProgramPointSize )
		return GL_FALSE;
	return polyIsEnabled( cap ) ? GL_TRUE : GL_FALSE;
}
} // extern "C"
