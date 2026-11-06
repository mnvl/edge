// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

#include <vector>

#include <GLES3/gl3.h>

#include "gles_context.h"
#include "texture.h"

namespace edge
{

/// GLESTexture::Layers.
struct GLESTextureLayers
{
	int count = 0;
	bool cube = false;
};

/// A GL texture and, until it is uploaded (always with TextureUsage::keep_cpu_copy), its texels on the CPU as the Vulkan texture's:
/// write() writes them, the end of its mapping sends them, download() reads the GPU's over them.  X8R8G8B8, A8R8G8B8, R32G32B32A32F and R32F only
/// so far (std::logic_error for the rest).  Texels of the 8-bit formats lie B, G, R, A in memory and in GL as R, G, B, A: the copies in
/// and out swap the channels.
class GLESTexture: public Texture
{
public:
	GLESTexture(GLESContextPtr const &context, int width, int height, PixelFormat pixel_format, TextureUsage const &usage);
	/// The layers of a render target's texture (RenderTargetFormat): none, an array of @p count, or a cube (@p count 6).
	using Layers = GLESTextureLayers;
	/// A colour texture of a render target of layers: a GL_TEXTURE_2D_ARRAY of @p layers layers, or a GL_TEXTURE_CUBE_MAP.  The GPU
	/// writes it and shaders read it: no CPU copy to lock or download.
	GLESTexture(GLESContextPtr const &context, int width, int height, PixelFormat pixel_format, Layers layers);
	/// A render target's readable depth (Renderer::create_render_target): 24-bit depth, sampled as one value in red, 0 near to 1 far,
	/// with nearest filtering (OpenGL ES 3.0 filters no depth texture), of @p layers layers.  The GPU writes it: no CPU copy to lock or
	/// download.
	struct Depth {};
	GLESTexture(GLESContextPtr const &context, int width, int height, Depth, Layers layers = Layers());
	~GLESTexture() override;

	GLuint get() const { return id_; }
	/// What it is bound as: GL_TEXTURE_2D, GL_TEXTURE_2D_ARRAY (a target of layers) or GL_TEXTURE_CUBE_MAP (a cube's).
	GLenum target() const { return target_; }
	/// Whether this is a render target's depth (Depth), which is sampled with nearest filtering.
	bool is_depth() const override { return depth_; }
	/// The context the texture is in: a texture is its renderer's own.
	GLESContextPtr const &context() const { return context_; }

	void download() override;

protected:
	const void *do_lock() const override;
	void do_unlock() const override;
	void *do_lock(bool discard) override;
	void do_unlock() override;

private:
	GLESContextPtr context_;
	GLuint id_ = 0;
	GLenum target_ = GL_TEXTURE_2D;
	mutable std::vector<unsigned char> cpu_;
	bool uploaded_ = false;    // the CPU copy has been sent once
	bool depth_ = false;
};

}
