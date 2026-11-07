// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include <algorithm>
#include <stdexcept>
#include <string>
#include "null_texture.h"
#include "pixel.h"

namespace edge
{

NullTexture::NullTexture(int width, int height, PixelFormat pixel_format, TextureUsage const &usage):
	Texture(width, height, pixel_format, usage)
{
	// What a GPU's limits say on Vulkan (maxImageDimension2D, maxImageArrayLayers), at the largest any desktop's are, so that code that fits
	// nothing does not pass here.
	constexpr int MAX_DIMENSION = 32768, MAX_LAYERS = 2048;
	if (width > MAX_DIMENSION || height > MAX_DIMENSION)
		throw std::invalid_argument("a texture of " + std::to_string(width) + " x " + std::to_string(height) + ": at most " +
			std::to_string(MAX_DIMENSION) + " on a side");
	if (usage.array_layers > MAX_LAYERS)
		throw std::invalid_argument(std::to_string(usage.array_layers) + " array layers: at most " + std::to_string(MAX_LAYERS));
	buffer_.resize(cpu_size());
}

NullTexture::~NullTexture() = default;

void NullTexture::mark_layered(bool cube)
{
	if (array_layers() <= 0 || usage().keep_cpu_copy)
		throw std::logic_error("a layered target's texture is made with its layers and no CPU copy");
	cube_ = cube;
	std::vector<char>().swap(buffer_);
}

const void *NullTexture::do_lock() const
{
	if (buffer_.empty())
		throw std::logic_error("a texture without its CPU copy (TextureUsage::keep_cpu_copy) cannot be read");
	return buffer_.data();
}

void NullTexture::do_unlock() const {}

void *NullTexture::do_lock(bool)
{
	if (buffer_.empty())
		throw std::logic_error("a texture without its CPU copy (TextureUsage::keep_cpu_copy) cannot be written");
	return buffer_.data();
}
void NullTexture::do_unlock()
{
	if (!usage().keep_cpu_copy)
		std::vector<char>().swap(buffer_);
}

void NullTexture::download()
{
	if (in_a_frame_ && *in_a_frame_)
		throw std::runtime_error("a texture download requires an idle renderer; call after finish_frame()");
	Texture::download();
	if (!usage().keep_cpu_copy)
		throw std::logic_error("a texture without its CPU copy (TextureUsage::keep_cpu_copy) cannot be downloaded");
}

}
