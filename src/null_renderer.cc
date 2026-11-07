// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include <memory>
#include <stdexcept>
#include <string>

#include "gpu_timer.h"
#include "null_renderer.h"
#include "null_texture.h"
#include "null_vertex_buffer.h"
#include "null_index_buffer.h"
#include "null_render_target.h"
#include "material.h"
#include "shader_program.h"

namespace edge
{

namespace
{

class NullGpuTimer: public GpuTimer
{
public:
	NullGpuTimer(): GpuTimer("the null renderer has no GPU") {}

protected:
	// (never: it is not available)
	void start() override { throw std::logic_error("the null renderer's GPU timer started"); }
	void stop() override {}
};

}

NullRenderer::NullRenderer(int width, int height):
	width_(width),
	height_(height),
	default_render_target_(new NullRenderTarget(this, width, height)),
	gpu_timer_(new NullGpuTimer())
{
	// (as on Vulkan: the same mistake is the same error on every renderer)
	if (width <= 0 || height <= 0)
		throw std::invalid_argument("a renderer of " + std::to_string(width) + "x" + std::to_string(height) + " pixels");
}

NullRenderer::~NullRenderer() = default;

int NullRenderer::screen_width() const { return width_; }
int NullRenderer::screen_height() const { return height_; }

void NullRenderer::resize(int width, int height)
{
	if (width <= 0 || height <= 0)
		throw std::invalid_argument("a renderer resized to " + std::to_string(width) + "x" + std::to_string(height) + " pixels");
	width_ = width;
	height_ = height;
	default_render_target_ = std::make_shared<NullRenderTarget>(this, width_, height_);
}


void NullRenderer::do_begin_frame()
{
	++frame_serial_;
	*in_a_frame_ = true;
}
void NullRenderer::do_finish_frame()
{
	*in_a_frame_ = false;
}
void NullRenderer::do_begin_pass(RenderTargetPtr const &target, RenderPassClearState const &, uint32_t)
{
	NullRenderTarget const *const null_target = dynamic_cast<NullRenderTarget const *>(target.get());
	if (!null_target || null_target->renderer() != this)
		throw std::invalid_argument("a null render pass into a target of another renderer");
}
void NullRenderer::do_end_pass() {}
void NullRenderer::do_set_viewport(int, int, int, int) {}
void NullRenderer::do_set_frame_constants(void const *, size_t) {}
void NullRenderer::do_set_global_texture(unsigned, TexturePtr const &, bool) {}
void NullRenderer::do_draw(DrawCall const &) {}

VertexBufferPtr NullRenderer::create_vertex_buffer(VertexTraits const *vertex_traits, size_t size, BufferUsage)
{
	auto buffer = std::make_shared<NullVertexBuffer>(vertex_traits, size);
	buffer->set_owner(this);
	return buffer;
}

IndexBufferPtr NullRenderer::create_index_buffer(IndexFormat index_format, size_t size, BufferUsage)
{
	auto buffer = std::make_shared<NullIndexBuffer>(index_format, size);
	buffer->set_owner(this);
	return buffer;
}

TexturePtr NullRenderer::create_texture(int width, int height, PixelFormat pixel_format,
	TextureUsage const &usage)
{
	auto texture = std::make_shared<NullTexture>(width, height, pixel_format, usage);
	texture->set_owner(this);
	texture->set_in_a_frame(in_a_frame_);
	return texture;
}

RenderTargetPtr NullRenderer::default_render_target()
{
	return default_render_target_;
}

RenderTargetPtr NullRenderer::do_create_render_target(int width, int height, RenderTargetFormat const &format)
{
	if (format.layers > MAX_LAYERS)
		throw std::invalid_argument("a render target of " + std::to_string(format.layers) + " layers: at most " + std::to_string(MAX_LAYERS)
			+ " layers on every device");
	if (format.cube && width > MAX_CUBE_SIZE)
		throw std::invalid_argument("a cube render target of " + std::to_string(width) + " pixel faces: at most "
			+ std::to_string(MAX_CUBE_SIZE) + " on every device");
	int const layers = format.cube ? 6 : format.layers;
	auto const texture = [&](PixelFormat pixel_format)
	{
		if (!layers)
			return create_texture(width, height, pixel_format);
		TextureUsage usage;
		usage.keep_cpu_copy = false;
		usage.array_layers = layers;
		auto layered = std::make_shared<NullTexture>(width, height, pixel_format, usage);
		layered->mark_layered(format.cube);
		return TexturePtr(layered);
	};
	std::vector<TexturePtr> color_textures;
	for (PixelFormat pixel_format : format.color_formats)
		color_textures.push_back(texture(pixel_format));
	TexturePtr depth_texture = format.readable_depth ? texture(PixelFormat::R32F) : TexturePtr();
	if (depth_texture)
		static_cast<NullTexture &>(*depth_texture).mark_depth();
	TexturePtr stencil_texture = format.readable_depth && !layers ? create_texture(width, height, PixelFormat::R8UI) : TexturePtr();
	return std::make_shared<NullRenderTarget>(this, width, height, color_textures, depth_texture, stencil_texture);
}

MaterialPtr NullRenderer::create_material()
{
	return std::make_shared<Material>(shared_from_this());
}

}
