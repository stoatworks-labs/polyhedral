#include "Picture.h"

#include <cstdio>
#include <cstring>

//STBI_NO_STDIO: the file is read here, so a path never reaches stb's own
//fopen (which on Windows is the ANSI one and fails on non-ASCII paths in a
//different way from ours). STRINGS is left on: stbi_failure_reason() is the
//only explanation an operator gets for a picture that will not load.
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBI_NO_STDIO
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STBI_ONLY_TGA
#define STBI_ONLY_GIF
#include "stb_image.h"

namespace dice
{
namespace
{
constexpr int kLargest = 4096;
} // namespace

Picture LoadPicture( const std::string& path )
{
	Picture picture;
	if( path.empty() )
		return picture;

	std::FILE* file = std::fopen( path.c_str(), "rb" );
	if( file == nullptr )
	{
		picture.error = "will not open";
		return picture;
	}
	std::fseek( file, 0, SEEK_END );
	const long size = std::ftell( file );
	std::fseek( file, 0, SEEK_SET );
	if( size <= 0 || size > 256L * 1024L * 1024L )
	{
		std::fclose( file );
		picture.error = "implausible size";
		return picture;
	}
	std::vector< unsigned char > bytes( static_cast< size_t >( size ) );
	const size_t read = std::fread( bytes.data(), 1, bytes.size(), file );
	std::fclose( file );
	if( read != bytes.size() )
	{
		picture.error = "short read";
		return picture;
	}

	int w = 0, h = 0, channels = 0;
	if( !stbi_info_from_memory( bytes.data(), static_cast< int >( bytes.size() ), &w, &h, &channels ) )
	{
		const char* reason = stbi_failure_reason();
		picture.error      = reason ? reason : "not a picture";
		return picture;
	}
	if( w > kLargest || h > kLargest )
	{
		picture.error = "larger than 4096 on a side";
		return picture;
	}

	stbi_uc* pixels = stbi_load_from_memory( bytes.data(), static_cast< int >( bytes.size() ), &w, &h, &channels, 4 );
	if( pixels == nullptr )
	{
		const char* reason = stbi_failure_reason();
		picture.error      = reason ? reason : "would not decode";
		return picture;
	}
	picture.width  = w;
	picture.height = h;
	picture.rgba.resize( static_cast< size_t >( w ) * h * 4 );
	const size_t row = static_cast< size_t >( w ) * 4;
	for( int y = 0; y < h; ++y )
		std::memcpy( picture.rgba.data() + static_cast< size_t >( h - 1 - y ) * row, pixels + static_cast< size_t >( y ) * row, row );
	stbi_image_free( pixels );
	return picture;
}

} // namespace dice
