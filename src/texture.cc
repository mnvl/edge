// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "log.h"
#include "stb_image_write.h"

#include "renderer.h"
#include "serialized_model.h"
#include "texture.h"
#include "texture_io.h"

namespace edge
{

namespace
{

// stb_image returns RGBA [R,G,B,A]; edge textures are BGRA [B,G,R,A].
void swizzle_rgba_to_bgra(unsigned char *data, int width, int height)
{
	size_t const count = static_cast<size_t>(width) * static_cast<size_t>(height);
	for (size_t i = 0; i < count; ++i)
	{
		unsigned char *p = data + i * 4;
		std::swap(p[0], p[2]);
	}
}

void swizzle_bgra_to_rgba(unsigned char *data, int width, int height)
{
	swizzle_rgba_to_bgra(data, width, height);
}

bool ends_with_ci(std::string const &s, char const *suffix)
{
	size_t const suffix_len = std::strlen(suffix);
	if (s.size() < suffix_len)
	{
		return false;
	}
	for (size_t i = 0; i < suffix_len; ++i)
	{
		char a = s[s.size() - suffix_len + i];
		char b = suffix[i];
		if (a >= 'A' && a <= 'Z') a += 32;
		if (b >= 'A' && b <= 'Z') b += 32;
		if (a != b) return false;
	}
	return true;
}

}

Texture::Texture(int width, int height, PixelFormat pixel_format, TextureUsage const &usage):
	width_(width),
	height_(height),
	pixel_format_(pixel_format),
	usage_(usage),
	mip_levels_(1)
{
	if (usage.array_layers < 0)
		throw std::invalid_argument(std::to_string(usage.array_layers) + " array layers");
	if (usage.provided_mip_levels > 0)
	{
		if (usage.mipmaps)
			throw std::invalid_argument("a texture is mipmapped or brings its mips, not both");
		int full = 1;
		for (int size = std::max(width, height); size > 1; size /= 2)
			++full;
		if (usage.provided_mip_levels > full)
			throw std::invalid_argument(std::to_string(usage.provided_mip_levels) + " mip levels for "
				+ std::to_string(width) + " x " + std::to_string(height));
		mip_levels_ = usage.provided_mip_levels;
	}
	if (usage.mipmaps)
	{
		if (is_block_compressed(pixel_format))
		{
			throw std::invalid_argument("block-compressed textures are not mipmapped from level 0");
		}
		for (int size = std::max(width, height); size > 1; size /= 2)
		{
			++mip_levels_;
		}
	}
}

Texture::~Texture() = default;

void Texture::download()
{
	if (usage_.array_layers > 0)
		throw std::logic_error("an array texture cannot be downloaded");
}

size_t Texture::cpu_size() const
{
	return levels_size(width_, height_, pixel_format_, std::max(1, usage_.provided_mip_levels)) * size_t(std::max(1, usage_.array_layers));
}

void const *Texture::cpu_copy(PixelFormat const *pixel_formats, size_t count) const
{
	if (usage_.array_layers > 0)
		throw std::logic_error("an array texture is not one image: its pixels cannot be viewed as one");
	if (std::find(pixel_formats, pixel_formats + count, pixel_format_) == pixel_formats + count)
	{
		throw std::logic_error("a texture of pixel format " + to_string(pixel_format_)
			+ " does not keep its texels as the pixels asked for");
	}

	// the CPU copy outlives the mapping: the view of it is good until the texture changes (pixels())
	return read().data();
}

TexturePtr Texture::load_from_file(RendererPtr const &renderer, std::string const &filename,
	bool compress, TextureUsage const &usage) {
	std::ifstream in(filename, std::ios::binary);
	if (!in)
	{
		throw std::runtime_error("failed to load image: " + filename + " (no such file)");
	}
	std::vector<char> const bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	TexturePtr texture;
	try
	{
		texture = load_from_memory(renderer, bytes.data(), bytes.size(), compress, usage);
	}
	catch (std::runtime_error const &e)
	{
		throw std::runtime_error("failed to load image: " + filename + " (" + e.what() + ")");
	}
	texture->filename_ = filename;
	return texture;
}

TexturePtr Texture::load_from_memory(RendererPtr const &renderer, void const *data, size_t size,
	bool compress, TextureUsage const &usage) {
	TextureDecoding how;
	how.compress = compress;
	how.srgb = usage.srgb;
	how.mipmaps = usage.mipmaps;
	how.keep_cpu_copy = usage.keep_cpu_copy;
	return import_texture(decode_texture(data, size, how), renderer, usage.keep_cpu_copy);
}

size_t Texture::level_size(int width, int height, PixelFormat pixel_format, int level)
{
	return pixel_data_size(std::max(1, width >> level), std::max(1, height >> level), pixel_format);
}

size_t Texture::levels_size(int width, int height, PixelFormat pixel_format, int levels)
{
	size_t total = 0;
	for (int i = 0; i < levels; ++i)
		total += level_size(width, height, pixel_format, i);
	return total;
}

bool Texture::is_dds(void const *data, size_t size)
{
	return size >= 4 && std::memcmp(data, "DDS ", 4) == 0;
}

namespace
{

/// The texture a DDS of `d` makes: its levels as they are, sRGB as the file or `usage` says (never BC5), `layers` layers (0: not an array).
TexturePtr dds_texture(RendererPtr const &renderer, DdsHeader const &d, int layers, TextureUsage const &usage)
{
	TextureUsage u = usage;
	u.mipmaps = false;
	u.provided_mip_levels = d.levels;
	u.srgb = d.format != PixelFormat::BC5 && (d.srgb || usage.srgb);
	u.array_layers = layers;
	return renderer->create_texture(d.width, d.height, d.format, u);
}

}

TexturePtr Texture::load_dds(RendererPtr const &renderer, void const *data, size_t size, TextureUsage const &usage)
{
	DdsHeader const d = read_dds(data, size);
	TexturePtr texture = dds_texture(renderer, d, d.layers > 1 ? d.layers : 0, usage);
	auto texels = texture->write(Contents::DISCARD);
	std::memcpy(texels.data(), static_cast<char const *>(data) + d.offset, d.data_size());
	texels.unmap();
	return texture;
}

TexturePtr Texture::load_dds_array(RendererPtr const &renderer, std::vector<std::span<std::byte const>> const &files, TextureUsage const &usage)
{
	if (files.empty())
		throw std::runtime_error("DDS array: no files");
	std::vector<DdsHeader> dds;
	for (size_t k = 0; k < files.size(); ++k)
	{
		auto fail = [k](std::string const &why) { return std::runtime_error("DDS array: file " + std::to_string(k) + ": " + why); };
		DdsHeader d;
		try
		{
			d = read_dds(files[k].data(), files[k].size());
		}
		catch (std::runtime_error const &e)
		{
			throw fail(e.what());
		}
		if (d.layers != 1)
			throw fail("an array of " + std::to_string(d.layers) + " layers, not one 2D texture");
		DdsHeader const &first = dds.empty() ? d : dds.front();
		if (d.width != first.width || d.height != first.height)
			throw fail(std::to_string(d.width) + " x " + std::to_string(d.height) + ", file 0 is " + std::to_string(first.width) + " x " + std::to_string(first.height));
		if (d.format != first.format)
			throw fail("pixel format " + to_string(d.format) + ", file 0 has " + to_string(first.format));
		if (d.srgb != first.srgb)
			throw fail(std::string(d.srgb ? "sRGB" : "linear") + ", file 0 is " + (first.srgb ? "sRGB" : "linear"));
		if (d.levels != first.levels)
			throw fail(std::to_string(d.levels) + " levels, file 0 has " + std::to_string(first.levels));
		dds.push_back(d);
	}
	TexturePtr texture = dds_texture(renderer, dds.front(), int(files.size()), usage);
	auto texels = texture->write(Contents::DISCARD);
	for (size_t k = 0; k < files.size(); ++k)
		std::memcpy(texels.data() + k * dds[k].data_size(), files[k].data() + dds[k].offset, dds[k].data_size());
	texels.unmap();
	return texture;
}

void Texture::save_to_file(TexturePtr const &texture, std::string const &filename) {
	int const w = texture->width();
	int const h = texture->height();
	size_t const pixel_count = static_cast<size_t>(w) * static_cast<size_t>(h);

	std::vector<unsigned char> rgba(pixel_count * 4);
	{
		auto const texels = std::as_const(*texture).read();
		std::memcpy(rgba.data(), texels.data(), pixel_count * 4);
	}

	swizzle_bgra_to_rgba(rgba.data(), w, h);

	int result = 0;
	if (ends_with_ci(filename, ".png"))
	{
		result = stbi_write_png(filename.c_str(), w, h, 4, rgba.data(), w * 4);
	}
	else
	{
		result = stbi_write_bmp(filename.c_str(), w, h, 4, rgba.data());
	}

	if (!result)
	{
		throw std::runtime_error("failed to write image: " + filename);
	}
}

}
