// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

#include <memory>
#include <vector>

#include "metal_context.h"
#include "texture.h"

namespace edge
{

/// MetalTexture::Layers.
struct MetalTextureLayers
{
	int count = 0;
	bool cube = false;
};

/// A Metal texture (shared storage, so that the CPU writes and reads it without a command buffer) and, until it is uploaded (always with
/// TextureUsage::keep_cpu_copy), its texels on the CPU as the Vulkan texture's: write() writes them, the end of its mapping sends them, download()
/// reads the GPU's over them.  Texels of the 8-bit formats lie B, G, R, A in memory and in Metal's BGRA8Unorm alike: no copy swaps them.
/// X8R8G8B8, A8R8G8B8, R16G16B16A16F, R32G32B32A32F, R32F, and block-compressed BC (where the device has them) and ASTC; std::logic_error
/// for the rest, as the GLES texture says.  A texture a command buffer still to run draws with is not written in place: the end of a write() makes
/// the Metal texture again, and the draws already encoded keep the old one.
class MetalTexture: public Texture
{
public:
	MetalTexture(MetalContextPtr const &context, int width, int height, PixelFormat pixel_format, TextureUsage const &usage);
	using Layers = MetalTextureLayers;
	/// The colour of a render target of layers (RenderTargetFormat): an array of @p layers.count or a cube.  The GPU writes it and shaders
	/// read it: no CPU copy.
	MetalTexture(MetalContextPtr const &context, int width, int height, PixelFormat pixel_format, Layers layers);
	/// A render target's readable depth, or the depth it draws with when it keeps none: 32-bit float depth, sampled as one value in red
	/// (0 near to 1 far); no CPU copy.  @p readable: later passes sample it.
	struct Depth {};
	MetalTexture(MetalContextPtr const &context, int width, int height, Depth, Layers layers = Layers(), bool readable = true);
	~MetalTexture() override;

	bool is_depth() const override { return depth_; }
	void download() override;

	MetalContextPtr const &context() const { return context_; }
	/// A draw or pass of the open frame uses it.
	void used();

	struct Impl;
	Impl &impl() const { return *impl_; }

protected:
	const void *do_lock() const override;
	void do_unlock() const override;
	void *do_lock(bool discard) override;
	void do_unlock() override;

private:
	MetalContextPtr context_;
	std::unique_ptr<Impl> impl_;
	mutable std::vector<unsigned char> cpu_;
	bool depth_ = false;
	bool readable_depth_ = true;

	void make_texture(int array_length);
};

}
