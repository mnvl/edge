// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include <cstddef>
#include <ostream>
#include <stdexcept>
#include <string>
#include "pixel.h"

namespace edge
{

std::string to_string(PixelFormat format)
{
	return std::to_string(static_cast<unsigned>(format));
}

std::ostream &operator<<(std::ostream &out, PixelFormat format)
{
	return out << static_cast<unsigned>(format);
}

int bits_per_pixel(PixelFormat format)
{
	switch (format)
	{
	case PixelFormat::X8R8G8B8: return 32;
	case PixelFormat::A8R8G8B8: return 32;
	case PixelFormat::R16G16B16A16F: return 64;
	case PixelFormat::R32G32B32A32F: return 128;
	case PixelFormat::R32F: return 32;
	case PixelFormat::R8UI: return 8;
	case PixelFormat::BC1: return 4;         // 8 bytes per 4x4 block
	case PixelFormat::BC3: return 8;         // 16 bytes per 4x4 block
	case PixelFormat::BC5: return 8;
	case PixelFormat::BC7: return 8;
	case PixelFormat::ASTC_4x4: return 8;    // 16 bytes per block
	case PixelFormat::ASTC_6x6: return 4;    // 3.56, rounded; sizes come from pixel_data_size
	case PixelFormat::ASTC_8x8: return 2;
	}
	throw std::runtime_error("unknown pixel format " + to_string(format));
}

std::size_t pixel_data_size(int width, int height, PixelFormat format)
{
	if (is_block_compressed(format))
	{
		int const extent = block_extent(format);
		std::size_t const blocks_x = static_cast<std::size_t>((width + extent - 1) / extent);
		std::size_t const blocks_y = static_cast<std::size_t>((height + extent - 1) / extent);
		std::size_t const block_bytes = format == PixelFormat::BC1 ? 8 : 16;
		return blocks_x * blocks_y * block_bytes;
	}
	return static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * bits_per_pixel(format) / 8;
}

bool is_block_compressed(PixelFormat format)
{
	return format == PixelFormat::BC1 || format == PixelFormat::BC3 || format == PixelFormat::BC5 || format == PixelFormat::BC7
		|| is_astc(format);
}

bool is_astc(PixelFormat format)
{
	return format == PixelFormat::ASTC_4x4 || format == PixelFormat::ASTC_6x6 || format == PixelFormat::ASTC_8x8;
}

int block_extent(PixelFormat format)
{
	switch (format)
	{
	case PixelFormat::ASTC_6x6: return 6;
	case PixelFormat::ASTC_8x8: return 8;
	default: return is_block_compressed(format) ? 4 : 1;
	}
}

}
