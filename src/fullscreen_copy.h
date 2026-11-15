// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

#include "forward.h"

namespace edge
{

/// Draws a texture over the whole target of the pass it is drawn in (a scene's frame onto a window's, before what goes over it):
/// stretched to the target's size, sampled linearly but never across the texture's edges, row 0 at the top, opaque (alpha 1).
/// edge's built-in copy shaders (copy_vertex.spv, copy_fragment.spv), on any renderer.
class FullscreenCopy
{
public:
	/// std::invalid_argument for no renderer.
	explicit FullscreenCopy(std::shared_ptr<Renderer> const &renderer);

	/// In a pass (std::logic_error outside one, as any draw); std::invalid_argument for no texture.
	void draw(std::shared_ptr<Texture> const &source);

private:
	std::shared_ptr<Renderer> renderer_;
	std::shared_ptr<VertexBuffer> triangle_;
	std::shared_ptr<Material> material_;
	std::shared_ptr<DrawCall> draw_;
};

}
