// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

#include <memory>
#include <vector>

#include "metal_context.h"
#include "metal_texture.h"
#include "render_target.h"

namespace edge
{

/// The colour textures (attachments 0, 1, ... in order; none for a target of depth alone) and a 32-bit depth: the readable depth texture
/// that later passes sample, or one of the target's own that nobody reads.  A target of layers makes the target of each layer (colour
/// slice i, or face i of a cube) when it is made; those share the textures, and one own depth of the target's size where none is
/// readable (a pass draws one layer and clears it).
class MetalRenderTarget: public RenderTarget
{
public:
	/// @p depth: the readable depth (a MetalTexture::Depth of the target's size and layers), or null.  @p layer >= 0: the target of that
	/// layer of the textures.
	MetalRenderTarget(MetalContextPtr const &context, int width, int height, std::vector<std::shared_ptr<MetalTexture>> const &colors,
		std::shared_ptr<MetalTexture> const &depth = nullptr, int layer = -1);
	~MetalRenderTarget() override;

	MetalContextPtr const &context() const { return context_; }
	std::vector<std::shared_ptr<MetalTexture>> const &colors() const { return colors_; }
	/// The depth a pass draws with: the readable one, or the target's own.
	std::shared_ptr<MetalTexture> const &depth() const { return pass_depth_; }

private:
	MetalRenderTarget(MetalContextPtr const &context, int width, int height, std::vector<std::shared_ptr<MetalTexture>> const &colors,
		std::shared_ptr<MetalTexture> const &depth, int layer, std::shared_ptr<MetalTexture> const &own_depth);

	MetalContextPtr context_;
	std::vector<std::shared_ptr<MetalTexture>> colors_;
	std::shared_ptr<MetalTexture> pass_depth_;
};

}
