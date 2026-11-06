// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

#include <memory>
#include <vector>

#include <GLES3/gl3.h>

#include "gles_context.h"
#include "gles_texture.h"
#include "render_target.h"

namespace edge
{

/// A framebuffer of colour textures (GL_COLOR_ATTACHMENT0 + i, drawn by glDrawBuffers in their order; none for a target of depth alone)
/// and a 24-bit depth: a renderbuffer, or a depth texture that later passes sample (a readable depth).  OpenGL ES 3.0 and WebGL 2 cannot
/// sample a depth texture's stencil: a readable depth has no stencil texture here.  Textures of layers make a target of layers, which has
/// no framebuffer and makes the target of each layer: one attaching that layer of each (glFramebufferTextureLayer, or the cube's face).
class GLESRenderTarget: public RenderTarget
{
public:
	/// @p depth: the readable depth (a GLESTexture::Depth of the target's size), or null for a renderbuffer no one reads.  @p layer >= 0:
	/// the target of that layer of the textures.  std::runtime_error naming the formats if GL finds the framebuffer incomplete (a colour
	/// format the device cannot draw into).
	GLESRenderTarget(GLESContextPtr const &context, int width, int height, std::vector<std::shared_ptr<GLESTexture>> const &colors,
		std::shared_ptr<GLESTexture> const &depth = nullptr, int layer = -1);
	~GLESRenderTarget() override;

	/// The framebuffer drawn into (none for a target of layers).
	GLuint framebuffer() const { return framebuffer_; }
	/// The context of the renderer that made it.
	GLESContextPtr const &context() const { return context_; }
	/// The renderbuffer its passes draw their depth into when it keeps none (0 with a readable depth), which the layers of a target of
	/// layers share.
	GLuint depth_renderbuffer() const;

private:
	/// A depth renderbuffer no one reads: a plain target's own, or the one the layers of a target of layers share (a pass draws one layer
	/// and clears it).
	struct DepthRenderbuffer;
	/// The target of layer @p layer, drawing its depth into @p depth when it keeps none (null: a renderbuffer of its own).
	GLESRenderTarget(GLESContextPtr const &context, int width, int height, std::vector<std::shared_ptr<GLESTexture>> const &colors,
		std::shared_ptr<GLESTexture> const &depth, int layer, std::shared_ptr<DepthRenderbuffer> const &depth_renderbuffer);

	GLESContextPtr context_;
	GLuint framebuffer_ = 0;
	std::shared_ptr<DepthRenderbuffer> depth_;
};

}
