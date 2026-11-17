// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include <algorithm>
#include <stdexcept>
#include <string>

#include "renderer.h"

#include "draw_call.h"
#include "index_buffer.h"
#include "material.h"
#include "pixel.h"
#include "gpu_timer.h"
#include "render_target.h"
#include "shader_program.h"
#include "texture.h"
#include "uber_draw_call.h"
#include "vertex_buffer.h"
#include "vertex_traits.h"

namespace edge
{

Renderer::Renderer() = default;

Renderer::~Renderer() = default;

DrawCallPtr Renderer::create_draw_call()
{
	return std::make_shared<DrawCall>(shared_from_this());
}

UberDrawCallPtr Renderer::create_uber_draw_call()
{
	return std::make_shared<UberDrawCall>(shared_from_this());
}

ShaderProgramPtr Renderer::create_shader_program(std::string const &vertex_shader_filename, std::string const &fragment_shader_filename)
{
	return std::make_shared<ShaderProgram>(vertex_shader_filename, fragment_shader_filename);
}

ShaderProgramPtr Renderer::load_shader_program(std::filesystem::path const &dir, std::string const &name)
{
	std::filesystem::path const vertex = dir / (name + ".vert.spv"), fragment = dir / (name + ".frag.spv");
	for (std::filesystem::path const &file : {vertex, fragment})
		if (!std::filesystem::is_regular_file(file))
			throw std::runtime_error("shader program '" + name + "': " + file.string() + " is not there");
	return create_shader_program(vertex.string(), fragment.string());
}

RenderTargetPtr Renderer::create_render_target(int width, int height, PixelFormat pixel_format, bool readable_depth)
{
	RenderTargetFormat format;
	format.color_formats = {pixel_format};
	format.readable_depth = readable_depth;
	return create_render_target(width, height, format);
}

RenderTargetPtr Renderer::create_render_target(int width, int height, RenderTargetFormat const &format)
{
	if (width <= 0 || height <= 0)
		throw std::invalid_argument("a render target of " + std::to_string(width) + " x " + std::to_string(height) + " pixels");
	if (format.layers < 0)
		throw std::invalid_argument("a render target of " + std::to_string(format.layers) + " layers");
	if (format.cube && format.layers != 0)
		throw std::invalid_argument("a cube render target of layers: a cube is its six faces (layers stays 0)");
	if (format.cube && width != height)
		throw std::invalid_argument("a cube render target of " + std::to_string(width) + " x " + std::to_string(height)
			+ " pixels: a cube's faces are square");
	if (format.color_formats.empty() && !format.readable_depth)
		throw std::invalid_argument("a render target of no colour and no readable depth: nothing a pass draws is kept");
	for (PixelFormat pixel_format : format.color_formats)
		if (is_block_compressed(pixel_format))
			throw std::invalid_argument("a render target's colour of pixel format " + to_string(pixel_format)
				+ ": a block-compressed format is not drawn into");
	if (format.color_formats.size() > max_color_attachments())
		throw std::invalid_argument("a render target of " + std::to_string(format.color_formats.size()) + " colour attachments: this "
			"renderer draws at most " + std::to_string(max_color_attachments()));
	return do_create_render_target(width, height, format);
}

bool Renderer::has_gpu_timer() const
{
	return gpu_timer().available();
}

void Renderer::set_gpu_trace_enabled(bool enabled)
{
	// a frame is timed or not from its beginning: a Vulkan frame's queries are reset when it begins
	if (in_frame())
		throw std::logic_error("GPU tracing is switched between frames");
	gpu_timer().set_enabled(enabled);
}

bool Renderer::gpu_trace_enabled() const
{
	return gpu_timer().enabled();
}

std::vector<double> Renderer::last_pass_times_us() const
{
	return gpu_timer().last_pass_times_us();
}

uint64_t Renderer::last_pass_times_frame() const
{
	return gpu_timer().last_frame();
}

std::optional<std::vector<double>> Renderer::pass_times_us(uint64_t frame) const
{
	return gpu_timer().pass_times_us(frame);
}

bool Renderer::is_gpu_time_disjoint(uint64_t frame) const
{
	return gpu_timer().disjoint(frame);
}

void Renderer::begin_frame()
{
	if (in_frame())
		throw std::logic_error("begin_frame() inside a frame");
	do_begin_frame();
	state_ = State::FRAME;
}

void Renderer::finish_frame()
{
	if (!in_frame())
		throw std::logic_error("finish_frame() outside a frame");
	if (in_pass())
		throw std::logic_error("finish_frame() inside a pass: end_pass() first");
	do_finish_frame();
	state_ = State::IDLE;
	frame_resources_.clear();
}

void Renderer::submit_frame()
{
	if (!in_frame())
		throw std::logic_error("submit_frame() outside a frame");
	if (in_pass())
		throw std::logic_error("submit_frame() inside a pass: end_pass() first");
	do_submit_frame();
	state_ = State::IDLE;
	frame_resources_.clear();
}

void Renderer::begin_pass(RenderTargetPtr const &target, RenderPassClearState const &clear_state, uint32_t pass_type)
{
	if (!in_frame())
		throw std::logic_error("begin_pass() outside a frame");
	if (in_pass())
		throw std::logic_error("begin_pass() inside a pass");
	if (!target)
		throw std::invalid_argument("begin_pass() of no target");
	if (target->layers() > 0 && target->layer_index() < 0)
		throw std::invalid_argument("begin_pass() of a target of " + std::to_string(target->layers())
			+ " layers: a pass draws one, begin_pass(target->layer(i))");
	do_begin_pass(target, clear_state, pass_type);
	frame_resources_.push_back(target);
	state_ = State::PASS;
	pass_type_ = pass_type;
	pass_target_ = target;
}

DrawList::DrawList(Renderer &renderer, RenderTargetPtr const &target):
	renderer_(renderer),
	target_width_(target->width()),
	target_height_(target->height())
{
}

DrawList::~DrawList() = default;

void DrawList::set_viewport(int x, int y, int width, int height)
{
	if (x < 0 || y < 0 || width <= 0 || height <= 0 || x + width > target_width_ || y + height > target_height_)
	{
		throw std::out_of_range("viewport " + std::to_string(x) + ", " + std::to_string(y) + ", " + std::to_string(width) + " x " +
			std::to_string(height) + " is not inside the target of " + std::to_string(target_width_) + " x " +
			std::to_string(target_height_));
	}
	do_set_viewport(x, y, width, height);
}

void DrawList::draw(DrawCall const &call)
{
	renderer_.check_draw(call);
	do_draw(call);
	hold(call.vertex_buffer());
	if (call.index_buffer())
		hold(call.index_buffer());
	hold(call.material());
}

std::vector<DrawList *> Renderer::begin_split_pass(RenderTargetPtr const &target, RenderPassClearState const &clear_state,
	uint32_t pass_type, size_t lists)
{
	if (!can_split_pass())
		throw std::logic_error("begin_split_pass() on a renderer that records a pass on one thread");
	if (!in_frame())
		throw std::logic_error("begin_split_pass() outside a frame");
	if (in_pass())
		throw std::logic_error("begin_split_pass() inside a pass");
	if (!target)
		throw std::invalid_argument("begin_split_pass() of no target");
	if (target->layers() > 0 && target->layer_index() < 0)
		throw std::invalid_argument("begin_split_pass() of a target of " + std::to_string(target->layers())
			+ " layers: a pass draws one, begin_split_pass(target->layer(i))");
	if (lists == 0)
		throw std::invalid_argument("begin_split_pass() of no lists");
	// (the pass's type and target are what the backend's lists read: set before it makes them, taken back if it throws)
	pass_type_ = pass_type;
	pass_target_ = target;
	try
	{
		split_lists_ = do_begin_split_pass(target, clear_state, pass_type, lists);
	}
	catch (...)
	{
		pass_type_ = 0;
		pass_target_.reset();
		throw;
	}
	frame_resources_.push_back(target);
	state_ = State::PASS;
	split_pass_ = true;
	return split_lists_;
}

std::vector<DrawList *> Renderer::do_begin_split_pass(RenderTargetPtr const &, RenderPassClearState const &, uint32_t, size_t)
{
	throw std::logic_error("this renderer records a pass on one thread");
}

void Renderer::wait_frame(uint64_t serial)
{
	if (serial == 0 || serial > frame_serial() || (in_frame() && serial == frame_serial()))
		throw std::logic_error("wait_frame(" + std::to_string(serial) + "): not a frame submitted (the last begun is "
			+ std::to_string(frame_serial()) + (in_frame() ? ", open)" : ")"));
	do_wait_frame(serial);
}

bool Renderer::frame_drawn(uint64_t serial)
{
	if (serial == 0 || serial > frame_serial() || (in_frame() && serial == frame_serial()))
		throw std::logic_error("frame_drawn(" + std::to_string(serial) + "): not a frame submitted (the last begun is "
			+ std::to_string(frame_serial()) + (in_frame() ? ", open)" : ")"));
	return do_frame_drawn(serial);
}

void Renderer::end_pass()
{
	if (!in_pass())
		throw std::logic_error("end_pass() outside a pass");
	if (split_pass_)
	{
		// what the lists drew with is the frame's to hold, as a draw through the renderer leaves it
		for (DrawList *list : split_lists_)
			frame_resources_.insert(frame_resources_.end(), list->resources_.begin(), list->resources_.end());
		split_lists_.clear();
	}
	do_end_pass();
	split_pass_ = false;
	state_ = State::FRAME;
	pass_type_ = 0;
	pass_target_.reset();
}

void Renderer::set_viewport(int x, int y, int width, int height)
{
	if (!in_pass())
		throw std::logic_error("set_viewport() outside a pass");
	if (split_pass_)
		throw std::logic_error("set_viewport() in a split pass: its lists set their own (DrawList::set_viewport)");
	if (x < 0 || y < 0 || width <= 0 || height <= 0 || x + width > pass_target_->width() || y + height > pass_target_->height())
	{
		throw std::out_of_range("viewport " + std::to_string(x) + ", " + std::to_string(y) + ", " + std::to_string(width) + " x " +
			std::to_string(height) + " is not inside the target of " + std::to_string(pass_target_->width()) + " x " +
			std::to_string(pass_target_->height()));
	}
	do_set_viewport(x, y, width, height);
}

void Renderer::set_frame_constants(void const *data, size_t size)
{
	if (in_frame())
		throw std::logic_error("set_frame_constants() inside a frame");
	if (size > FRAME_CONSTANTS_SIZE)
		throw std::invalid_argument("frame constants of " + std::to_string(size) + " bytes, at most " + std::to_string(FRAME_CONSTANTS_SIZE));
	if (!data && size)
		throw std::invalid_argument("frame constants of " + std::to_string(size) + " bytes at no address");
	do_set_frame_constants(data, size);
}

void Renderer::set_global_texture(unsigned slot, TexturePtr const &texture)
{
	if (in_frame())
		throw std::logic_error("set_global_texture() inside a frame");
	if (slot >= GLOBAL_TEXTURES_COUNT)
		throw std::out_of_range("global texture slot " + std::to_string(slot) + ", at most " + std::to_string(GLOBAL_TEXTURES_COUNT - 1));
	do_set_global_texture(slot, texture, false);
}

void Renderer::set_global_shadow_texture(unsigned slot, TexturePtr const &depth)
{
	if (in_frame())
		throw std::logic_error("set_global_shadow_texture() inside a frame");
	if (slot >= GLOBAL_TEXTURES_COUNT)
		throw std::out_of_range("global texture slot " + std::to_string(slot) + ", at most " + std::to_string(GLOBAL_TEXTURES_COUNT - 1));
	if (!depth || !depth->is_depth())
		throw std::invalid_argument("a shadow texture that is not a render target's readable depth");
	do_set_global_texture(slot, depth, true);
}

void Renderer::check_ranges(DrawCall const &call) const
{
	// (an owner of null is one the backend did not record; Vulkan's checks its own, by device, in do_draw)
	VertexBuffer const &vertices = *call.vertex_buffer();
	if (vertices.owner() && vertices.owner() != this)
		throw std::invalid_argument("a draw of a vertex buffer of another renderer");
	IndexBuffer const *const indices = call.index_buffer().get();
	if (indices && indices->owner() && indices->owner() != this)
		throw std::invalid_argument("a draw of an index buffer of another renderer");

	// (each on its own side of the comparison: these come from a caller, and the product can wrap)
	VertexTraits const *const traits = vertices.vertex_traits();
	size_t const vertex_size = traits ? traits->size() : 0;
	size_t const vertices_count = vertex_size ? vertices.size() / vertex_size : 0;
	size_t const triangles = call.triangles_count(), minimal = call.minimal_vertex_index();
	bool const known = vertex_size != 0;
	if (known && minimal > vertices_count)
		throw std::invalid_argument("a draw from vertex " + std::to_string(minimal) + " of " + std::to_string(vertices_count));
	if (indices)
	{
		size_t const indices_count = indices->size() / indices->index_size(), first = call.first_index_offset();
		if (triangles > indices_count / 3 || first > indices_count - triangles * 3)
			throw std::invalid_argument("a draw of " + std::to_string(triangles) + " triangles from index " + std::to_string(first) + " of " +
				std::to_string(indices_count));
		if (known && triangles > 0)
		{
			size_t const highest = indices->highest_index(first, triangles * 3);
			if (highest >= vertices_count)
				throw std::invalid_argument("a draw that reaches vertex " + std::to_string(highest) + " of " + std::to_string(vertices_count));
		}
	}
	else if (known && (triangles > vertices_count / 3 || minimal > vertices_count - triangles * 3))
	{
		throw std::invalid_argument("a draw of " + std::to_string(triangles) + " triangles from vertex " + std::to_string(minimal) + " of " +
			std::to_string(vertices_count));
	}

	// the textures the material brings: a descriptor of another device's image is a crash there
	Material const &material = *call.material();
	auto const check_texture = [&](TexturePtr const &texture) {
		if (texture && texture->owner() && texture->owner() != this)
			throw std::invalid_argument("a draw of texture '" + texture->filename() + "' of another renderer");
	};
	for (size_t i = 0; i < material.diffuse_textures_count(); ++i)
		check_texture(material.diffuse_texture(i));
	check_texture(material.texture_blending_weights_texture());
	check_texture(material.normal_map_texture());
	check_texture(material.metallic_roughness_texture());
	check_texture(material.emissive_texture());
}

void Renderer::draw(DrawCall const &call)
{
	if (split_pass_ && in_pass())
		throw std::logic_error("a draw through the renderer in a split pass: its lists draw (DrawCall::execute(DrawList &))");
	check_draw(call);
	do_draw(call);
	frame_resources_.push_back(call.vertex_buffer());
	if (call.index_buffer())
		frame_resources_.push_back(call.index_buffer());
	frame_resources_.push_back(call.material());
}

void Renderer::check_draw(DrawCall const &call) const
{
	if (!in_pass())
		throw std::logic_error("a draw outside a pass");
	if (!call.vertex_buffer())
		throw std::logic_error("a draw with no vertex buffer");
	if (!call.material())
		throw std::logic_error("a draw with no material");
	check_ranges(call);
	if (call.is_instanced() && call.visible_instances().empty())
		throw std::invalid_argument("an instanced draw of no instances");
	if (call.material()->occlusion() && !call.material()->metallic_roughness_texture())
		throw std::logic_error("material '" + call.material()->name() + "' has occlusion and no metallic-roughness texture to hold it");
}

namespace
{

void refuse_null(std::vector<TexturePtr> const &textures, char const *what)
{
	if (std::find(textures.begin(), textures.end(), TexturePtr()) != textures.end())
		throw std::invalid_argument(std::string(what) + ": no texture");
}

}

void Renderer::batch_download_textures(std::vector<TexturePtr> const &textures)
{
	if (in_frame())
		throw std::logic_error("batch_download_textures() inside a frame: after finish_frame()");
	refuse_null(textures, "batch_download_textures");
	do_batch_download_textures(textures);
}

namespace
{

void check_rects(std::vector<TexturePtr> const &textures, std::vector<TextureRect> const &rects, char const *what)
{
	for (TextureRect const &r : rects)
		for (TexturePtr const &texture : textures)
			if (r.x < 0 || r.y < 0 || r.width <= 0 || r.height <= 0 || r.x + r.width > texture->width()
				|| r.y + r.height > texture->height())
				throw std::out_of_range(std::string(what) + ": rectangle " + std::to_string(r.x) + ", " + std::to_string(r.y) + ", "
					+ std::to_string(r.width) + " x " + std::to_string(r.height) + " is not inside a texture of "
					+ std::to_string(texture->width()) + " x " + std::to_string(texture->height()));
}

}

void Renderer::download_in_frame(std::vector<TexturePtr> const &textures, std::vector<TextureRect> const &rects)
{
	if (!can_download_in_frame())
		throw std::logic_error("download_in_frame() on a renderer that downloads between frames (batch_download_textures)");
	if (!in_frame())
		throw std::logic_error("download_in_frame() outside a frame");
	if (in_pass())
		throw std::logic_error("download_in_frame() inside a pass");
	refuse_null(textures, "download_in_frame");
	check_rects(textures, rects, "download_in_frame");
	do_download_in_frame(textures, rects);
	for (TexturePtr const &texture : textures)
		frame_resources_.push_back(texture);
}

void Renderer::finish_downloads(std::vector<TexturePtr> const &textures, std::vector<TextureRect> const &rects)
{
	if (!can_download_in_frame())
		throw std::logic_error("finish_downloads() on a renderer that downloads between frames (batch_download_textures)");
	refuse_null(textures, "finish_downloads");
	check_rects(textures, rects, "finish_downloads");
	do_finish_downloads(textures, rects);
}

void Renderer::do_download_in_frame(std::vector<TexturePtr> const &, std::vector<TextureRect> const &)
{
	throw std::logic_error("this renderer downloads between frames");
}

void Renderer::do_finish_downloads(std::vector<TexturePtr> const &, std::vector<TextureRect> const &)
{
	throw std::logic_error("this renderer downloads between frames");
}

void Renderer::batch_upload_textures(std::vector<TexturePtr> const &textures)
{
	refuse_null(textures, "batch_upload_textures");
	for (size_t i = 0; i < textures.size(); ++i)
		if (std::find(textures.begin() + i + 1, textures.end(), textures[i]) != textures.end())
			throw std::invalid_argument("batch_upload_textures: a texture named twice");
	do_batch_upload_textures(textures);
}

void Renderer::stream_textures(std::vector<TexturePtr> const &textures)
{
	if (in_pass())
		throw std::logic_error("stream_textures() inside a pass");
	refuse_null(textures, "stream_textures");
	for (size_t i = 0; i < textures.size(); ++i)
		if (std::find(textures.begin() + i + 1, textures.end(), textures[i]) != textures.end())
			throw std::invalid_argument("stream_textures: a texture named twice");
	do_stream_textures(textures);
	if (in_frame())
		frame_resources_.insert(frame_resources_.end(), textures.begin(), textures.end());
}

void Renderer::do_batch_download_textures(std::vector<TexturePtr> const &textures)
{
	for (auto const &texture : textures)
		texture->download();
}

void Renderer::do_batch_upload_textures(std::vector<TexturePtr> const &textures)
{
	for (auto const &texture : textures)
		texture->do_unlock();
}

void Renderer::do_stream_textures(std::vector<TexturePtr> const &textures)
{
	do_batch_upload_textures(textures);
}

}
