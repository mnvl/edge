// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

#include <cstdint>
#include <vector>
#include "forward.h"
#include "pixel.h"

namespace edge
{

struct RenderPassClearState
{
	RenderPassClearState():
		clear_color(true),
		clear_color_r(0.0f),
		clear_color_g(0.0f),
		clear_color_b(0.0f),
		clear_color_a(1.0f),
		clear_depth(true),
		clear_depth_value(1.0f),
		clear_stencil(0u)
	{
	}

	bool clear_color;
	float clear_color_r;
	float clear_color_g;
	float clear_color_b;
	float clear_color_a;
	bool clear_depth;
	float clear_depth_value;
	uint32_t clear_stencil;
};

/// What a render target is made of besides its size (Renderer::create_render_target).
struct RenderTargetFormat
{
	/// The colour attachments, a pixel format each, in order: a fragment shader writes attachment i at its output location i (an
	/// attachment it writes nothing to holds what the GPU leaves there).  Every one is cleared to the pass's clear colour, and a material's
	/// blending is the same for all.  None: a target of depth alone, which needs a readable depth.
	std::vector<PixelFormat> color_formats;
	/// The depth is kept after each pass, a texture the passes after it sample (RenderTarget::depth_texture); else it is thrown away.
	bool readable_depth = false;
	/// 0: an image of each attachment.  N >= 1: N layers of each, array textures (a sampler2DArray in the shader; a readable depth's a
	/// sampler2DArrayShadow as a shadow texture), each layer drawn by a pass of its own into RenderTarget::layer(i).  An array of one layer
	/// is an array still.  A depth thrown away is one buffer of the target's size that the layers share, cleared by each pass (a pass that
	/// does not clear it, which OpenGL ES alone allows, sees the last layer's).
	int layers = 0;
	/// The six square faces of a cube (layers stays 0): cube textures (samplerCube, samplerCubeShadow) whose layer i is face i, +x, -x, +y,
	/// -y, +z, -z, each drawn by a pass into RenderTarget::layer(i) with Camera::set_cube_face(eye, i, ...)'s camera and sampled with
	/// edge_cube.glsl's edge_cube_direction().
	bool cube = false;
};

class RenderTarget: public std::enable_shared_from_this<RenderTarget>
{
public:
	RenderTarget(RenderTarget const &) = delete;
	RenderTarget &operator=(RenderTarget const &) = delete;
	virtual ~RenderTarget();

	int width() const { return width_; }
	int height() const { return height_; }

	/// Colour attachment 0: the colour of a target of one; null for a target of depth alone.
	TexturePtr color_texture() const;
	/// Colour attachment @p attachment, what a fragment shader writes at that output location; std::out_of_range past the last.  Each is a
	/// texture of the target's size and its own format that later passes sample and, but for a target of layers, a download reads.
	TexturePtr const &color_texture(size_t attachment) const;
	size_t color_textures_count() const { return color_textures_.size(); }
	std::vector<TexturePtr> const &color_textures() const { return color_textures_; }
	/// The depth of the last pass drawn into this target, for passes after it to sample; null unless the target was made with a readable
	/// depth (Renderer::create_render_target).  It has no CPU copy: it cannot be locked or downloaded.
	TexturePtr depth_texture() const { return depth_texture_; }
	/// The stencil of the last pass drawn into this target (R8UI: a usampler2D, read with texelFetch), for passes after it to read; null
	/// unless the target was made with a readable depth whose format has a stencil, and on OpenGL ES (3.0 cannot sample a stencil), and
	/// for a target of layers.  The Vulkan one has no CPU copy either.
	TexturePtr stencil_texture() const { return stencil_texture_; }

	/// Layers of its textures: 0 for a plain target, N for an array of N, 6 for a cube.
	int layers() const { return layers_; }
	bool is_cube() const { return cube_; }
	/// The layer this target draws into: i for RenderTarget::layer(i), -1 for a target itself.
	int layer_index() const { return layer_; }
	/// The target that draws into layer (or face) @p i of this one: the same size and textures, all their layers, a pass into it
	/// (Renderer::begin_pass) drawing layer i alone and leaving the others as they are.  A target of layers is drawn only through
	/// these.  Made with the target, the same each time; it keeps the textures, not the target.  std::out_of_range for a layer the target
	/// has not (any of a plain target), std::logic_error for a layer's.
	RenderTargetPtr const &layer(int i) const;

protected:
	/// A target of these textures, drawing into all of them, or, @p layer >= 0, into their layer @p layer.  Its layers and whether it is a
	/// cube are its textures'.
	RenderTarget(int width, int height, std::vector<TexturePtr> const &color_textures, TexturePtr const &depth_texture,
		TexturePtr const &stencil_texture, int layer = -1);

	int width_;
	int height_;
	/// The targets of a target of layers, layer() of each: the backend's target makes them when it is made.
	std::vector<RenderTargetPtr> layer_targets_;

private:
	std::vector<TexturePtr> color_textures_;
	TexturePtr depth_texture_;
	TexturePtr stencil_texture_;
	int layers_;
	bool cube_;
	int layer_;
};

}
