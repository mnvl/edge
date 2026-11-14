// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include <atomic>
#include <cstring>
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
	// the cameras gone have nothing to tell
	std::lock_guard const lock(camera_motion_mutex_);
	std::erase_if(camera_motion_, [](auto const &entry) { return entry.second.camera.expired(); });
}

void Renderer::finish_frame()
{
	if (!in_frame())
		throw std::logic_error("finish_frame() outside a frame");
	if (in_pass())
		throw std::logic_error("finish_frame() inside a pass: end_pass() first");
	do_finish_frame();
	state_ = State::IDLE;
	++frames_ended_;
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
	++frames_ended_;
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

void Renderer::end_pass()
{
	if (!in_pass())
		throw std::logic_error("end_pass() outside a pass");
	do_end_pass();
	state_ = State::FRAME;
	pass_type_ = 0;
	pass_target_.reset();
}

void Renderer::set_viewport(int x, int y, int width, int height)
{
	if (!in_pass())
		throw std::logic_error("set_viewport() outside a pass");
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
	do_draw(call);
	frame_resources_.push_back(call.vertex_buffer());
	if (call.index_buffer())
		frame_resources_.push_back(call.index_buffer());
	frame_resources_.push_back(call.material());
}

Renderer::CameraMotion const *Renderer::camera_motion(CameraPtr const &camera) const
{
	auto const at = camera_motion_.find(camera.get());
	return at != camera_motion_.end() && at->second.camera.lock() == camera ? &at->second : nullptr;
}

namespace
{

std::atomic<std::uint64_t> next_renderer_serial{1};

/// What this thread last asked the renderer about one camera in one frame.  Within a frame the answers do not change -- a camera's
/// frame before, and the view-projection it had in it -- once the frame's first commit through the camera has recorded it, so a
/// thread committing many calls through one camera (an arena building a view) asks the shared record, and takes its lock, once.
struct CameraAnswers
{
	std::uint64_t renderer = 0;
	Camera const *address = nullptr;
	std::weak_ptr<Camera const> camera;
	std::uint64_t frame = 0;
	bool frame_known = false;
	std::optional<std::uint64_t> previous_frame;
	bool view_projection_known = false;
	std::uint64_t cuts = 0;
	rove::matrix<4,4> current;
	std::optional<rove::matrix<4,4>> before;

	/// Whether these are the answers about @p camera in @p frame of renderer @p serial; made so (empty) when they are not.
	bool about(std::uint64_t serial, CameraPtr const &of, std::uint64_t in_frame)
	{
		if (renderer == serial && address == of.get() && frame == in_frame && camera.lock() == of)
			return true;
		*this = CameraAnswers{serial, of.get(), of, in_frame};
		return false;
	}
};

thread_local CameraAnswers answers;

}

Renderer::Renderer():
	serial_(next_renderer_serial.fetch_add(1))
{
}

std::optional<std::uint64_t> Renderer::previous_frame(CameraPtr const &camera) const
{
	if (answers.about(serial_, camera, frames_ended_) && answers.frame_known)
		return answers.previous_frame;
	std::optional<std::uint64_t> result;
	{
		std::lock_guard const lock(camera_motion_mutex_);
		CameraMotion const *const motion = camera_motion(camera);
		if (motion)
			result = motion->frame == frames_ended_ ? motion->before_frame : std::optional<std::uint64_t>(motion->frame);
	}
	answers.frame_known = true;
	answers.previous_frame = result;
	return result;
}

std::optional<rove::matrix<4,4>> Renderer::previous_view_projection(CameraPtr const &camera, rove::matrix<4,4> const &current)
{
	std::uint64_t const frame = frames_ended_;
	if (answers.about(serial_, camera, frame) && answers.view_projection_known && answers.cuts == camera->cuts()
		&& std::memcmp(&answers.current, &current, sizeof(current)) == 0)
		return answers.before;
	std::optional<rove::matrix<4,4>> const before = record_view_projection(camera, current, frame);
	answers.view_projection_known = true;
	answers.cuts = camera->cuts();
	answers.current = current;
	answers.before = before;
	return before;
}

std::optional<rove::matrix<4,4>> Renderer::record_view_projection(CameraPtr const &camera, rove::matrix<4,4> const &current,
	std::uint64_t frame)
{
	std::lock_guard const lock(camera_motion_mutex_);
	bool const known = camera_motion(camera) != nullptr;
	CameraMotion &motion = camera_motion_[camera.get()];
	if (!known)
	{
		motion = CameraMotion{camera, camera->cuts(), frame, current, std::nullopt, std::nullopt};
		return std::nullopt;
	}
	if (motion.frame != frame)
	{
		// the camera's last frame is now the frame before
		motion.before_frame = motion.frame;
		motion.before = motion.last;
		motion.frame = frame;
	}
	if (motion.cuts != camera->cuts())
	{
		motion.before.reset();
		motion.cuts = camera->cuts();
	}
	motion.last = current;
	return motion.before;
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
