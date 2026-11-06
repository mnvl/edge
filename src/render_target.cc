// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include <stdexcept>
#include <string>

#include "render_target.h"
#include "texture.h"

namespace edge
{

namespace
{

/// The texture a target's layers are read from: its first colour, or its depth; null for neither (the null renderer's default target).
Texture const *first_texture(std::vector<TexturePtr> const &color_textures, TexturePtr const &depth_texture)
{
	return color_textures.empty() ? depth_texture.get() : color_textures.front().get();
}

}

RenderTarget::RenderTarget(int width, int height, std::vector<TexturePtr> const &color_textures, TexturePtr const &depth_texture,
	TexturePtr const &stencil_texture, int layer):
	width_(width),
	height_(height),
	color_textures_(color_textures),
	depth_texture_(depth_texture),
	stencil_texture_(stencil_texture),
	layers_(first_texture(color_textures, depth_texture) ? first_texture(color_textures, depth_texture)->array_layers() : 0),
	cube_(first_texture(color_textures, depth_texture) && first_texture(color_textures, depth_texture)->is_cube()),
	layer_(layer)
{
	for (TexturePtr const &texture : color_textures_)
		if (!texture || texture->array_layers() != layers_ || texture->is_cube() != cube_)
			throw std::logic_error("a render target's colour textures are of as many layers as each other");
	if (depth_texture_ && (depth_texture_->array_layers() != layers_ || depth_texture_->is_cube() != cube_))
		throw std::logic_error("a render target's depth is of as many layers as its colour");
	if (layer_ < -1 || layer_ >= layers_)
		throw std::logic_error("a render target of layer " + std::to_string(layer_) + " of " + std::to_string(layers_));
}

RenderTarget::~RenderTarget() = default;

TexturePtr RenderTarget::color_texture() const
{
	return color_textures_.empty() ? TexturePtr() : color_textures_.front();
}

TexturePtr const &RenderTarget::color_texture(size_t attachment) const
{
	if (attachment >= color_textures_.size())
		throw std::out_of_range("colour attachment " + std::to_string(attachment) + " of a target of " + std::to_string(color_textures_.size()));
	return color_textures_[attachment];
}

RenderTargetPtr const &RenderTarget::layer(int i) const
{
	if (layer_ >= 0)
		throw std::logic_error("layer() of a layer: the layers are the target's");
	if (i < 0 || static_cast<size_t>(i) >= layer_targets_.size())
		throw std::out_of_range("layer " + std::to_string(i) + " of a target of " + std::to_string(layers_) + " layers");
	return layer_targets_[static_cast<size_t>(i)];
}

}
