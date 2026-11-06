// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

#include <memory>
#include <string>

#include "metal_context.h"
#include "renderer.h"

namespace edge
{

class MetalGpuTimer;
class MetalRenderer;
using MetalRendererPtr = std::shared_ptr<MetalRenderer>;

/// edge's Renderer on Metal (Apple silicon: macOS 13 and iOS 15 on).  Offscreen as VulkanRenderer and GLESRenderer
/// are: it draws into render targets, which the host shows (show_frame).  What is not implemented is a std::logic_error saying so, as on
/// GLES: array textures and stencil writes, and there is no stencil texture.
///
/// Shaders are the Metal translations of edge's SPIR-V (tools/shader_cross --msl), named to create_shader_program() by their .spv names
/// relative to the shader directory: for x.spv the renderer reads x.msl.map (what it binds: uniform blocks at buffer(binding), the
/// textures of Vulkan's bindings at texture(binding) with the stage's sampler states numbered from 0, the push constants at buffer(30),
/// the vertex buffer at buffer(16)) and x.<sdk>.metallib (sdk: macosx, iphoneos or iphonesimulator, the one it is built for) beside it.
/// The clip space is the SPIR-V's own (y up, z from 0 to w): Metal's, with row 0 of a target its top as in Vulkan.
///
/// Two frames are in flight at most: begin_frame() waits for the GPU when it has two not done.  submit_frame() hands a frame over, and
/// finish_frame() waits for it.
class MetalRenderer: public Renderer {
public:
	MetalRendererPtr shared_from_this()
	{
		return std::static_pointer_cast<MetalRenderer>(Renderer::shared_from_this());
	}

	/// In @p context, which the host made and keeps (hosted(): show_frame() shows frames in its window layer; otherwise it is a
	/// std::logic_error).  std::invalid_argument for no context or a size that is not positive.
	MetalRenderer(int width, int height, std::string const &shader_dir, MetalContextPtr const &context);
	~MetalRenderer() override;

	int screen_width() const override;
	int screen_height() const override;
	void resize(int width, int height) override;

	VertexBufferPtr create_vertex_buffer(VertexTraits const *vertex_traits, size_t size,
		BufferUsage usage = BufferUsage::STATIC) override;
	IndexBufferPtr create_index_buffer(IndexFormat index_format, size_t size, BufferUsage usage = BufferUsage::STATIC) override;
	TexturePtr create_texture(int width, int height, PixelFormat pixel_format, TextureUsage const &usage = TextureUsage()) override;

	RenderTargetPtr default_render_target() override;
	/// Eight, what an Apple GPU of family 7 draws into.
	unsigned max_color_attachments() const override { return 8; }
	MaterialPtr create_material() override;

	/// Draws a frame of the host (B, G, R, X bytes, rows tight, row 0 the top, width x height) into the window layer's next drawable,
	/// stretched to window_width x window_height pixels, the window's size; swap_window() shows it.  std::logic_error inside a frame or
	/// without a host window; std::invalid_argument for a size that is not positive.
	void show_frame(void const *frame, int width, int height, int window_width, int window_height);
	/// The same of a texture of this renderer's (a render target's colour texture: the frame drawn stays on the GPU).
	/// std::invalid_argument for no texture, one of another renderer, or a size that is not positive; std::logic_error as above.
	void show_frame(TexturePtr const &frame, int window_width, int window_height);
	/// Presents what show_frame() drew, no sooner than `min_interval` seconds after the last present (0: at the next refresh).  std::logic_error without a host window or without a frame shown since the last.
	void swap_window(double min_interval = 0);

	MetalContextPtr const &context() const;
	uint64_t frame_serial() const override;
	/// Joint matrices of the skeleton a draw is skinned by, at most this many (std::length_error for more).
	static constexpr size_t MAX_JOINT_MATRICES = 256;
	/// The blocks of the previous frame's instances and joint matrices (bindings 21 and 22, edge_motion.glsl) written since the renderer
	/// was made: one for each batch of an instanced draw and each skinned draw whose program reads them.
	uint64_t previous_block_writes() const;

	/// What the Metal translations bind (shader_cross.cc's map): the buffer the push constants are at, and where the vertex buffer and
	/// the attributes a vertex buffer lacks are.
	static constexpr unsigned PUSH_BUFFER = 30;
	static constexpr unsigned VERTEX_BUFFER = 16;

	struct Impl;

protected:
	void do_begin_frame() override;
	/// Commits the frame and waits until the GPU has drawn it.
	void do_finish_frame() override;
	void do_submit_frame() override;
	void do_begin_pass(RenderTargetPtr const &target, RenderPassClearState const &clear_state, uint32_t pass_type) override;
	void do_end_pass() override;
	void do_set_viewport(int x, int y, int width, int height) override;
	void do_set_frame_constants(void const *data, size_t size) override;
	void do_set_global_texture(unsigned slot, TexturePtr const &texture, bool compare) override;
	/// std::invalid_argument for a vertex or index buffer that is not this renderer's.
	void do_draw(DrawCall const &call) override;
	/// std::invalid_argument for more layers or a larger cube than the device has.
	RenderTargetPtr do_create_render_target(int width, int height, RenderTargetFormat const &format) override;
	GpuTimer &gpu_timer() const override;

private:
	std::unique_ptr<Impl> impl_;
	int width_, height_;
	RenderTargetPtr default_render_target_;
};

}
