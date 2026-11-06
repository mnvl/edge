// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include "null_render_target.h"

namespace edge
{

NullRenderTarget::NullRenderTarget(NullRenderer const *renderer, int width, int height, std::vector<TexturePtr> const &color_textures,
	TexturePtr const &depth_texture, TexturePtr const &stencil_texture, int layer):
	RenderTarget(width, height, color_textures, depth_texture, stencil_texture, layer),
	renderer_(renderer)
{
	if (layer < 0)
		for (int i = 0; i < layers(); ++i)
			layer_targets_.push_back(std::make_shared<NullRenderTarget>(renderer, width, height, color_textures, depth_texture,
				stencil_texture, i));
}

NullRenderTarget::~NullRenderTarget() = default;

}
