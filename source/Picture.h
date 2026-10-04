#pragma once

#include <cstdint>
#include <string>
#include <vector>

/**
    The Texture File: a still picture for the faces, through stb_image (PNG,
    JPEG, BMP, TGA, GIF's first frame), as flipbook loads its sheets.

    Rows are stored bottom first, GL's order, so v = 1 is the top of the
    picture. A picture larger than 4096 on a side is refused rather than
    uploaded: it would be a quarter of a gigabyte of texture to put a few
    hundred pixels on each face.
*/
namespace dice
{
struct Picture
{
	int width  = 0;
	int height = 0;
	std::vector< uint8_t > rgba;
	std::string error;

	bool Valid() const
	{
		return width > 0 && height > 0 && rgba.size() == static_cast< size_t >( width ) * height * 4;
	}
};

Picture LoadPicture( const std::string& path );

} // namespace dice
