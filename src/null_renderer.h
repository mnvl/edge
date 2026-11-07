// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

#include <memory>

#include "renderer.h"

namespace edge
{

class NullRenderer: public Renderer {
public:
	NullRenderer(int width = 800, int height = 600);
	~NullRenderer() override;


	int screen_width() const override;
	int screen_height() const override;
	void resize(int width, int height) override;


	VertexBufferPtr create_vertex_buffer(VertexTraits const *vertex_traits, size_t size,
		BufferUsage usage = BufferUsage::STATIC) override;
	IndexBufferPtr create_index_buffer(IndexFormat index_format, size_t size, BufferUsage usage = BufferUsage::STATIC) override;

	TexturePtr create_texture(int width, int height, PixelFormat pixel_format, TextureUsage const &usage = TextureUsage()) override;
	RenderTargetPtr default_render_target() override;
	/// What every device edge draws on promises (Vulkan, OpenGL ES 3.0, WebGL 2): 4 colour attachments, 256 layers, cube faces of 2048
	/// pixels.  A target past them is refused here as a phone would refuse it.
	static constexpr unsigned MAX_COLOR_ATTACHMENTS = 4;
	static constexpr int MAX_LAYERS = 256, MAX_CUBE_SIZE = 2048;
	unsigned max_color_attachments() const override { return MAX_COLOR_ATTACHMENTS; }
	MaterialPtr create_material() override;

	uint64_t frame_serial() const override { return frame_serial_; }

protected:
	void do_begin_frame() override;
	void do_finish_frame() override;
	void do_begin_pass(RenderTargetPtr const &target, RenderPassClearState const &clear_state, uint32_t pass_type) override;
	void do_end_pass() override;
	void do_set_viewport(int x, int y, int width, int height) override;
	void do_set_frame_constants(void const *data, size_t size) override;
	void do_set_global_texture(unsigned slot, TexturePtr const &texture, bool compare) override;
	void do_draw(DrawCall const &call) override;
	RenderTargetPtr do_create_render_target(int width, int height, RenderTargetFormat const &format) override;
	/// One that is not available(): there is no GPU to time.
	GpuTimer &gpu_timer() const override { return *gpu_timer_; }

private:
	std::shared_ptr<bool> in_a_frame_ = std::make_shared<bool>(false);     // a texture may outlive the renderer, and asks this

	int width_, height_;
	RenderTargetPtr default_render_target_;
	uint64_t frame_serial_ = 0;
	std::unique_ptr<GpuTimer> gpu_timer_;
};

}
