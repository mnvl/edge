// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include "metal_impl.h"

#include <stdexcept>
#include <string>

namespace edge
{

namespace
{

std::vector<TexturePtr> textures(std::vector<std::shared_ptr<MetalTexture>> const &colors)
{
	return std::vector<TexturePtr>(colors.begin(), colors.end());
}

}

MetalRenderTarget::MetalRenderTarget(MetalContextPtr const &context, int width, int height,
	std::vector<std::shared_ptr<MetalTexture>> const &colors, std::shared_ptr<MetalTexture> const &depth, int layer):
	MetalRenderTarget(context, width, height, colors, depth, layer, nullptr)
{
}

MetalRenderTarget::MetalRenderTarget(MetalContextPtr const &context, int width, int height,
	std::vector<std::shared_ptr<MetalTexture>> const &colors, std::shared_ptr<MetalTexture> const &depth, int layer,
	std::shared_ptr<MetalTexture> const &own_depth):
	RenderTarget(width, height, textures(colors), depth, TexturePtr(), layer),
	context_(context),
	colors_(colors),
	pass_depth_(depth ? depth : own_depth)
{
	if (depth && (!depth->is_depth() || depth->width() != width || depth->height() != height))
		throw std::logic_error("a Metal render target's readable depth must be a depth MetalTexture of its size");
	for (auto const &color : colors)
		if (color->is_depth() || color->width() != width || color->height() != height)
			throw std::logic_error("a Metal render target's colours must be colour MetalTextures of its size");
	if (layers() > 0 && layer < 0)
	{
		// a target of layers: its layers' targets draw them; where no depth is readable they share one of their own, which each pass clears
		std::shared_ptr<MetalTexture> const shared = depth ? nullptr
			: std::make_shared<MetalTexture>(context, width, height, MetalTexture::Depth(), MetalTexture::Layers(), false);
		for (int i = 0; i < layers(); ++i)
			layer_targets_.push_back(RenderTargetPtr(new MetalRenderTarget(context, width, height, colors, depth, i, shared)));
		pass_depth_ = depth ? depth : shared;
		return;
	}
	if (!pass_depth_)
		pass_depth_ = std::make_shared<MetalTexture>(context, width, height, MetalTexture::Depth(), MetalTexture::Layers(), false);
}

MetalRenderTarget::~MetalRenderTarget() = default;

}
