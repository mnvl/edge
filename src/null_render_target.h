// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

#include "render_target.h"

namespace edge
{

class NullRenderer;

class NullRenderTarget: public RenderTarget
{
public:
	/// A target of @p renderer of these textures (none: the renderer's default target, which has none), or of their layer @p layer; a
	/// target of layers makes the target of each.
	NullRenderTarget(NullRenderer const *renderer, int width, int height, std::vector<std::shared_ptr<Texture>> const &color_textures = {},
		std::shared_ptr<Texture> const &depth_texture = std::shared_ptr<Texture>(), std::shared_ptr<Texture> const &stencil_texture = std::shared_ptr<Texture>(), int layer = -1);
	~NullRenderTarget() override;

	/// The renderer that made it, which alone draws into it (a name only: the target does not keep it alive, or reach it).
	NullRenderer const *renderer() const { return renderer_; }

private:
	NullRenderer const *renderer_;
};

}
