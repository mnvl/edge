// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include <algorithm>
#include <stdexcept>
#include "null_texture.h"
#include "pixel.h"

namespace edge
{

NullTexture::NullTexture(int width, int height, PixelFormat pixel_format, TextureUsage const &usage):
	Texture(width, height, pixel_format, usage)
{
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
	Texture::download();
	if (!usage().keep_cpu_copy)
		throw std::logic_error("a texture without its CPU copy (TextureUsage::keep_cpu_copy) cannot be downloaded");
}

}
