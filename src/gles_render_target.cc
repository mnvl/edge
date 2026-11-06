// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include <stdexcept>
#include <string>

#include "gles_render_target.h"

namespace edge
{

namespace
{

std::vector<TexturePtr> textures(std::vector<std::shared_ptr<GLESTexture>> const &colors)
{
	return std::vector<TexturePtr>(colors.begin(), colors.end());
}

/// Attaches layer @p layer of @p texture (-1: the texture as it is) at @p attachment of the bound framebuffer.
void attach(GLenum attachment, GLESTexture const &texture, int layer)
{
	if (texture.target() == GL_TEXTURE_2D)
		glFramebufferTexture2D(GL_FRAMEBUFFER, attachment, GL_TEXTURE_2D, texture.get(), 0);
	else if (texture.target() == GL_TEXTURE_CUBE_MAP)
		glFramebufferTexture2D(GL_FRAMEBUFFER, attachment, GL_TEXTURE_CUBE_MAP_POSITIVE_X + layer, texture.get(), 0);
	else
		glFramebufferTextureLayer(GL_FRAMEBUFFER, attachment, texture.get(), 0, layer);
}

}

struct GLESRenderTarget::DepthRenderbuffer
{
	GLESContextPtr context;
	GLuint id = 0;

	DepthRenderbuffer(GLESContextPtr const &gl, int width, int height):
		context(gl)
	{
		context->make_current();
		glGenRenderbuffers(1, &id);
		glBindRenderbuffer(GL_RENDERBUFFER, id);
		glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
	}

	~DepthRenderbuffer()
	{
		context->make_current();
		glDeleteRenderbuffers(1, &id);
	}
};

GLESRenderTarget::GLESRenderTarget(GLESContextPtr const &context, int width, int height,
	std::vector<std::shared_ptr<GLESTexture>> const &colors, std::shared_ptr<GLESTexture> const &depth, int layer):
	GLESRenderTarget(context, width, height, colors, depth, layer, nullptr)
{
}

GLuint GLESRenderTarget::depth_renderbuffer() const
{
	return depth_ ? depth_->id : 0;
}

GLESRenderTarget::GLESRenderTarget(GLESContextPtr const &context, int width, int height,
	std::vector<std::shared_ptr<GLESTexture>> const &colors, std::shared_ptr<GLESTexture> const &depth, int layer,
	std::shared_ptr<DepthRenderbuffer> const &depth_renderbuffer):
	RenderTarget(width, height, textures(colors), depth, TexturePtr(), layer),
	context_(context),
	depth_(depth_renderbuffer)
{
	if (depth && (!depth->is_depth() || depth->width() != width || depth->height() != height))
		throw std::logic_error("a GLES render target's readable depth must be a depth GLESTexture of its size");
	for (auto const &color : colors)
		if (color->is_depth() || color->width() != width || color->height() != height)
			throw std::logic_error("a GLES render target's colours must be colour GLESTextures of its size");
	if (layers() > 0 && layer < 0)
	{
		// a target of layers: its layers' targets draw them, with one depth where it is thrown away (a pass draws one layer, and clears
		// it first)
		std::shared_ptr<DepthRenderbuffer> const shared = depth ? nullptr : std::make_shared<DepthRenderbuffer>(context, width, height);
		for (int i = 0; i < layers(); ++i)
			layer_targets_.push_back(RenderTargetPtr(new GLESRenderTarget(context, width, height, colors, depth, i, shared)));
		return;
	}
	if (!depth && !depth_)
		depth_ = std::make_shared<DepthRenderbuffer>(context, width, height);
	context_->make_current();
	glGenFramebuffers(1, &framebuffer_);
	glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
	std::vector<GLenum> buffers;
	for (size_t i = 0; i < colors.size(); ++i)
	{
		attach(GL_COLOR_ATTACHMENT0 + static_cast<GLenum>(i), *colors[i], layer);
		buffers.push_back(GL_COLOR_ATTACHMENT0 + static_cast<GLenum>(i));
	}
	// fragment output i to attachment i (the framebuffer keeps it); none drawn or read for a target of depth alone
	if (buffers.empty())
		buffers.push_back(GL_NONE);
	glDrawBuffers(static_cast<GLsizei>(buffers.size()), buffers.data());
	glReadBuffer(colors.empty() ? GL_NONE : GL_COLOR_ATTACHMENT0);
	if (depth)
		attach(GL_DEPTH_ATTACHMENT, *depth, layer);
	else
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth_->id);
	GLenum const status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	if (status != GL_FRAMEBUFFER_COMPLETE)
	{
		glDeleteFramebuffers(1, &framebuffer_);
		std::string formats;
		for (auto const &color : colors)
			formats += (formats.empty() ? "" : ", ") + to_string(color->pixel_format());
		throw std::runtime_error("GLES backend: a render target of colour pixel formats [" + formats
			+ "] is not a complete framebuffer (status " + std::to_string(status) + "): a format this device cannot draw into");
	}
	check_gl("creating a render target");
}

GLESRenderTarget::~GLESRenderTarget()
{
	context_->make_current();
	if (framebuffer_)
		glDeleteFramebuffers(1, &framebuffer_);
}

}
