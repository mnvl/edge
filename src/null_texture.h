// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

#include <vector>
#include "texture.h"

namespace edge
{

class NullTexture: public Texture {
public:
	NullTexture(int width, int height, PixelFormat pixel_format, TextureUsage const &usage = TextureUsage());
	~NullTexture() override;

	/// Without TextureUsage::keep_cpu_copy the texels are let go, as a GPU texture lets go of its CPU copy once it is uploaded.
	/// Its CPU copy is what it has: std::logic_error without one.
	void download() override;
	bool is_depth() const override { return depth_; }
	/// Makes it a render target's readable depth (NullRenderer::create_render_target).
	void mark_depth() { depth_ = true; }
	/// Makes it a texture of a target of layers, which has no CPU copy (read(), write() and download() throw std::logic_error, as on the GPUs),
	/// and a cube if @p cube.  It was made with its layers and without TextureUsage::keep_cpu_copy.
	void mark_layered(bool cube);

protected:
	const void *do_lock() const override;
	void do_unlock() const override;
	void *do_lock(bool discard) override;
	void do_unlock() override;

private:
	mutable std::vector<char> buffer_;
	bool depth_ = false;
};

}
