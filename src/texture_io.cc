// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include "texture_io.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "log.h"
#include "pixel.h"
#include "stb_dxt.h"
#include "stb_image.h"
#include "texture.h"

namespace edge
{

namespace
{

bool has_meaningful_alpha(unsigned char const *rgba, int width, int height)
{
	size_t const count = static_cast<size_t>(width) * static_cast<size_t>(height);
	for (size_t i = 0; i < count; ++i)
	{
		if (rgba[i * 4 + 3] < 250)
		{
			return true;
		}
	}
	return false;
}

// Compress RGBA data to BC1 or BC3 block-compressed format.
// Input: RGBA pixels (4 bytes/pixel). Output: compressed blocks.
std::vector<std::uint8_t> compress_rgba_to_bc(unsigned char const *rgba, int width, int height, bool use_bc3)
{
	int const blocks_x = (width + 3) / 4;
	int const blocks_y = (height + 3) / 4;
	size_t const block_bytes = use_bc3 ? 16 : 8;
	std::vector<std::uint8_t> compressed(size_t(blocks_x) * size_t(blocks_y) * block_bytes);

	for (int by = 0; by < blocks_y; ++by)
	{
		for (int bx = 0; bx < blocks_x; ++bx)
		{
			unsigned char block[4 * 4 * 4];
			std::memset(block, 0, sizeof(block));

			for (int py = 0; py < 4; ++py)
			{
				for (int px = 0; px < 4; ++px)
				{
					int const sx = bx * 4 + px;
					int const sy = by * 4 + py;
					int const dst_offset = (py * 4 + px) * 4;

					if (sx < width && sy < height)
					{
						int const src_offset = (sy * width + sx) * 4;
						block[dst_offset + 0] = rgba[src_offset + 0];
						block[dst_offset + 1] = rgba[src_offset + 1];
						block[dst_offset + 2] = rgba[src_offset + 2];
						block[dst_offset + 3] = rgba[src_offset + 3];
					}
					else
					{
						block[dst_offset + 3] = 255;
					}
				}
			}

			size_t const out_offset = (size_t(by) * size_t(blocks_x) + size_t(bx)) * block_bytes;
			stb_compress_dxt_block(compressed.data() + out_offset, block, use_bc3 ? 1 : 0, STB_DXT_HIGHQUAL);
		}
	}

	return compressed;
}

/// The R, G, B, A texels stb_image decoded, as a texture of them as @p how says.
SerializedTexture picture_texture(unsigned char *rgba, int width, int height, TextureDecoding const &how)
{
	SerializedTexture result;
	result.width = width;
	result.height = height;
	result.srgb = how.srgb;
	result.mipmaps = how.mipmaps;
	result.provided_mip_levels = 0;
	if (how.compress && ((width % 4) != 0 || (height % 4) != 0 || width < 4 || height < 4))
	{
		EDGE_LOG_WARN("texture {}x{} not compressed: dimensions must be multiples of 4", width, height);
	}
	else if (how.compress)
	{
		bool const use_bc3 = has_meaningful_alpha(rgba, width, height);
		result.pixel_format = use_bc3 ? PixelFormat::BC3 : PixelFormat::BC1;
		result.pixels = compress_rgba_to_bc(rgba, width, height, use_bc3);
		EDGE_LOG_INFO("texture {}x{} compressed to {} ({:.1f} KB)", width, height, use_bc3 ? "BC3" : "BC1", result.pixels.size() / 1024.0);
		return result;
	}

	// stb_image returns R, G, B, A; edge's 8-bit textures are B, G, R, A
	size_t const count = size_t(width) * size_t(height);
	result.pixels.assign(rgba, rgba + count * 4);
	for (size_t i = 0; i < count; ++i)
		std::swap(result.pixels[i * 4], result.pixels[i * 4 + 2]);
	// (X8R8G8B8 says the fourth byte is nothing: never for a picture whose alpha says something)
	result.pixel_format = how.srgb || how.mipmaps || !how.keep_cpu_copy || has_meaningful_alpha(rgba, width, height) ? DEFAULT_ALPHA_PIXEL_FORMAT
		: DEFAULT_PIXEL_FORMAT;
	return result;
}

}

std::size_t DdsHeader::data_size() const
{
	return Texture::levels_size(width, height, format, levels) * size_t(layers);
}

DdsHeader read_dds(void const *data, std::size_t size)
{
	auto const *bytes = static_cast<unsigned char const *>(data);
	auto u32 = [&](size_t at) {
		if (at + 4 > size)
			throw std::runtime_error("DDS: truncated header");
		uint32_t v;
		std::memcpy(&v, bytes + at, 4);
		return v;
	};
	if (!Texture::is_dds(data, size) || u32(4) != 124)
		throw std::runtime_error("not a DDS file");
	// six faces, which no texture here holds
	if ((u32(112) & 0x200) != 0)                                  // DDSCAPS2_CUBEMAP
		throw std::runtime_error("DDS: a cube map");
	DdsHeader d{int(u32(16)), int(u32(12)), std::max(1, int(u32(28))), 1, PixelFormat{}, false, 128};
	uint32_t const four_cc = u32(84);
	auto cc = [](char const *s) { return uint32_t(s[0]) | uint32_t(s[1]) << 8 | uint32_t(s[2]) << 16 | uint32_t(s[3]) << 24; };
	if (four_cc == cc("DX10"))
	{
		uint32_t const dxgi = u32(128), dimension = u32(132), array = u32(140);
		if ((u32(136) & 0x4) != 0)                                // RESOURCE_MISC_TEXTURECUBE
			throw std::runtime_error("DDS: a cube map");
		if (dimension != 3 || array < 1)
			throw std::runtime_error("DDS: only 2D textures and arrays of them");
		// (as many as any GPU has: more wraps an int, and the size of the texture with it)
		if (array > 65536)
			throw std::runtime_error("DDS: an array of " + std::to_string(array) + " layers");
		d.layers = int(array);
		switch (dxgi)
		{
		case 71: d.format = PixelFormat::BC1; break;
		case 72: d.format = PixelFormat::BC1; d.srgb = true; break;
		case 77: d.format = PixelFormat::BC3; break;
		case 78: d.format = PixelFormat::BC3; d.srgb = true; break;
		case 83: d.format = PixelFormat::BC5; break;
		case 98: d.format = PixelFormat::BC7; break;
		case 99: d.format = PixelFormat::BC7; d.srgb = true; break;
		// DXGI's ASTC blocks, one triple (typeless, UNORM, UNORM_SRGB) and a gap each: 4x4 from 133, 6x6 from 149, 8x8 from 161
		case 134: d.format = PixelFormat::ASTC_4x4; break;
		case 135: d.format = PixelFormat::ASTC_4x4; d.srgb = true; break;
		case 150: d.format = PixelFormat::ASTC_6x6; break;
		case 151: d.format = PixelFormat::ASTC_6x6; d.srgb = true; break;
		case 162: d.format = PixelFormat::ASTC_8x8; break;
		case 163: d.format = PixelFormat::ASTC_8x8; d.srgb = true; break;
		default: throw std::runtime_error("DDS: DXGI format " + std::to_string(dxgi) + " is not BC1, BC3, BC5, BC7 or ASTC 4x4, 6x6 or 8x8");
		}
		d.offset = 148;
	}
	else if (four_cc == cc("DXT1"))
		d.format = PixelFormat::BC1;
	else if (four_cc == cc("DXT5"))
		d.format = PixelFormat::BC3;
	else if (four_cc == cc("ATI2") || four_cc == cc("BC5U"))
		d.format = PixelFormat::BC5;
	else
		throw std::runtime_error("DDS: unsupported pixel format");
	if (d.width <= 0 || d.height <= 0)
		throw std::runtime_error("DDS: empty texture");
	// (these go through shifts and products: past them the size that is checked below is not the real one)
	if (d.width > 65536 || d.height > 65536)
		throw std::runtime_error("DDS: a texture of " + std::to_string(d.width) + " x " + std::to_string(d.height));
	if (d.levels < 1 || d.levels > 17)
		throw std::runtime_error("DDS: " + std::to_string(d.levels) + " mip levels");
	if (d.offset + d.data_size() != size)
		throw std::runtime_error("DDS: " + std::to_string(size - d.offset) + " bytes of data for "
			+ std::to_string(d.data_size()) + " in " + std::to_string(d.levels) + " levels"
			+ (d.layers > 1 ? " of " + std::to_string(d.layers) + " layers" : ""));
	return d;
}

namespace
{

/// A picture's R, G, B, A texels as stb_image decodes them, rows top first.
struct Picture
{
	int width = 0, height = 0;
	std::unique_ptr<unsigned char, void (*)(void *)> rgba{nullptr, stbi_image_free};

	/// Channel @p channel at the centre of texel (x, y) of a picture of @p width x @p height over the same texture coordinates: bilinear
	/// between this one's texel centres, repeating at its edges, as a GPU samples it.  This one's own texels where the sizes are the same.
	unsigned char sample(int x, int y, int width_, int height_, int channel) const
	{
		double const u = (x + 0.5) * width / width_ - 0.5, v = (y + 0.5) * height / height_ - 0.5;
		int const x0 = int(std::floor(u)), y0 = int(std::floor(v));
		double const fx = u - x0, fy = v - y0;
		auto const texel = [&](int i, int j) {
			i = (i % width + width) % width;
			j = (j % height + height) % height;
			return double(rgba.get()[(std::size_t(j) * width + i) * 4 + channel]);
		};
		double const value = (1 - fy) * ((1 - fx) * texel(x0, y0) + fx * texel(x0 + 1, y0))
			+ fy * ((1 - fx) * texel(x0, y0 + 1) + fx * texel(x0 + 1, y0 + 1));
		return static_cast<unsigned char>(std::lround(value));
	}
};

Picture picture(void const *data, std::size_t size, char const *what)
{
	if (!data || size == 0)
		throw std::runtime_error(std::string(what) + ": no bytes");
	if (Texture::is_dds(data, size))
		throw std::runtime_error(std::string(what) + " is a DDS: its blocks have no texels to pack (a picture's would)");
	Picture result;
	int channels = 0;
	result.rgba.reset(stbi_load_from_memory(static_cast<unsigned char const *>(data), static_cast<int>(size), &result.width,
		&result.height, &channels, 4));
	if (!result.rgba)
		throw std::runtime_error(std::string(what) + ": failed to load image from memory (" + stbi_failure_reason() + ")");
	return result;
}

}

SerializedTexture pack_occlusion(void const *metallic_roughness, std::size_t metallic_roughness_size, void const *occlusion,
	std::size_t occlusion_size, TextureDecoding const &how)
{
	if (how.compress && how.mipmaps)
		throw std::invalid_argument("a compressed texture cannot be mipmapped at load: load it uncompressed");
	Picture const ao = picture(occlusion, occlusion_size, "the occlusion image");
	std::optional<Picture> mr;
	if (metallic_roughness)
		mr = picture(metallic_roughness, metallic_roughness_size, "the metallic-roughness image");
	int const width = mr ? std::max(ao.width, mr->width) : ao.width, height = mr ? std::max(ao.height, mr->height) : ao.height;
	std::vector<unsigned char> rgba(std::size_t(width) * std::size_t(height) * 4);
	for (int y = 0; y < height; ++y)
	{
		for (int x = 0; x < width; ++x)
		{
			unsigned char *const texel = rgba.data() + (std::size_t(y) * width + x) * 4;
			texel[0] = ao.sample(x, y, width, height, 0);
			for (int c = 1; c < 4; ++c)
				texel[c] = mr ? mr->sample(x, y, width, height, c) : 255;
		}
	}
	return picture_texture(rgba.data(), width, height, how);
}

SerializedTexture decode_texture(void const *data, std::size_t size, TextureDecoding const &how)
{
	if (how.compress && how.mipmaps)
		throw std::invalid_argument("a compressed texture cannot be mipmapped at load: load it uncompressed");
	if (!data || size == 0)
		throw std::runtime_error("a texture of no bytes");
	if (Texture::is_dds(data, size))
	{
		DdsHeader const d = read_dds(data, size);
		if (d.layers > 1)
			throw std::runtime_error("DDS: an array of " + std::to_string(d.layers) + " layers, not one texture");
		SerializedTexture result;
		result.width = d.width;
		result.height = d.height;
		result.pixel_format = d.format;
		result.srgb = d.format != PixelFormat::BC5 && (d.srgb || how.srgb);
		result.mipmaps = false;
		result.provided_mip_levels = d.levels;
		auto const *bytes = static_cast<std::uint8_t const *>(data);
		result.pixels.assign(bytes + d.offset, bytes + d.offset + d.data_size());
		return result;
	}
	int w = 0, h = 0, channels = 0;
	unsigned char *rgba = stbi_load_from_memory(static_cast<unsigned char const *>(data), static_cast<int>(size), &w, &h, &channels, 4);
	if (!rgba)
		throw std::runtime_error(std::string("failed to load image from memory (") + stbi_failure_reason() + ")");
	std::unique_ptr<unsigned char, void (*)(void *)> const owned(rgba, stbi_image_free);
	return picture_texture(rgba, w, h, how);
}

}
