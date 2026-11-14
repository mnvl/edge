// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
#include <memory>
#include <mutex>
#include <rove/matrix.h>
#include "buffer_usage.h"
#include "forward.h"
#include "camera.h"
#include "render_target.h"
#include "texture.h"
#include "vertex_traits.h"
#include "vertex_array.h"
#include "index_array.h"

namespace edge
{

class GpuTimer;

class Renderer: public std::enable_shared_from_this<Renderer> {
public:
	Renderer(Renderer const &) = delete;
	Renderer &operator=(Renderer const &) = delete;
	Renderer();
	virtual ~Renderer();

	virtual int screen_width() const = 0;
	virtual int screen_height() const = 0;
	virtual void resize(int width, int height) = 0;

	// The frame and its passes, the same on every renderer: begin_frame, then passes (begin_pass ... draws ... end_pass), then
	// finish_frame or submit_frame.  Out of that order is a std::logic_error, and the renderer is left as it was.

	/// std::logic_error inside a frame.
	void begin_frame();
	/// Ends the frame and waits until the GPU has drawn it: what is read after (a download, the GPU's times) is this frame's.
	/// std::logic_error outside a frame or inside a pass.
	void finish_frame();
	/// Ends the frame and hands it to the GPU without waiting for it, where the renderer can: the host builds the next frame
	/// meanwhile.  A download still sees what the frame drew (it waits).  As finish_frame() where the renderer cannot.
	void submit_frame();
	/// A pass into @p target, cleared as @p clear_state says (every colour attachment to the clear colour); @p pass_type picks each
	/// material's shader program for it.  A target of layers is drawn a layer at a time: @p target is RenderTarget::layer(i).
	/// std::logic_error outside a frame or inside a pass, std::invalid_argument for no target, one of another renderer, or a target of
	/// layers itself.
	void begin_pass(RenderTargetPtr const &target,
		RenderPassClearState const &clear_state = RenderPassClearState(),
		uint32_t pass_type = 0);
	/// std::logic_error outside a pass.
	void end_pass();
	/// Draws of the active pass from now on go to the rectangle of the target
	/// x, y (from its top left corner), w x h pixels, and nowhere else
	/// (scissored): several views in one target, a shadow atlas.  begin_pass
	/// starts with the whole target.  std::logic_error outside a pass,
	/// std::out_of_range for a rectangle not inside the target.
	void set_viewport(int x, int y, int width, int height);

	/// Frames begun since the renderer was made: the number of the frame open, or of the last one (0 before the first).
	virtual uint64_t frame_serial() const = 0;

	// GPU timing of the passes, the same on every renderer (gpu_timer.h): a frame's durations come once the GPU has drawn it, frames
	// after it is ended where the renderer keeps frames in flight or the GPU answers late (WebGL 2), and are asked for by its number.

	/// Whether the renderer can time its passes on the GPU: Vulkan where its queue writes timestamps, OpenGL ES and WebGL 2 with
	/// GL_EXT_disjoint_timer_query (_webgl2); never the null renderer.
	bool has_gpu_timer() const;
	/// Times the passes of the frames begun from now on (off until then); turning it off drops the durations.  Between frames only
	/// (std::logic_error inside one).  Turned on where the renderer cannot time (has_gpu_timer()), it is std::runtime_error saying why.
	void set_gpu_trace_enabled(bool enabled);
	bool gpu_trace_enabled() const;
	/// The durations (microseconds, in begin_pass order, one for each of the first GpuTimer::MAX_PASSES passes) of the latest timed
	/// frame read back: after finish_frame() on Vulkan that frame's, otherwise maybe an earlier one's (last_pass_times_frame()
	/// names it).  Empty while tracing is off and before.
	std::vector<double> last_pass_times_us() const;
	/// The frame (frame_serial()) last_pass_times_us() is of; 0 for none.
	uint64_t last_pass_times_frame() const;
	/// The durations of `frame` once the GPU has drawn it and they are read back, while it is one of the last GpuTimer::HISTORY frames
	/// read back; none before, after, for a frame begun untimed, or a disjoint one.  An application that renders several frames for
	/// each it shows asks for the one it wants.
	std::optional<std::vector<double>> pass_times_us(uint64_t frame) const;
	/// Whether `frame` (one of the last GpuTimer::HISTORY read back) was timed and the GPU could not measure it (OpenGL ES's disjoint:
	/// its clock changed, or another context took it): its durations never come.
	bool is_gpu_time_disjoint(uint64_t frame) const;

	/// The frame a draw call committed now is drawn in, counted from 0: the open frame, or between frames the next one; the frames
	/// ended (finish_frame(), submit_frame()) before it.  What a draw call's previous frame is (DrawCall::commit()).
	std::uint64_t frame_index() const { return frames_ended_; }
	/// The frame before this one for what is seen through @p camera (a draw call's previous frame, DrawCall::commit()): the last frame
	/// before frame_index() in which a draw call was committed through the camera; none for a camera not committed before this
	/// frame.  A cut (Camera::cut()) keeps it.  An application that draws a scene in one frame and the screen it is shown on in the next
	/// has the scene's frame before two frames back.
	std::optional<std::uint64_t> previous_frame(CameraPtr const &camera) const;

	bool in_frame() const { return state_ != State::IDLE; }
	bool in_pass() const { return state_ == State::PASS; }
	/// The active pass's type and target (none outside a pass).
	uint32_t pass_type() const { return pass_type_; }
	RenderTargetPtr const &pass_target() const { return pass_target_; }

	/// A vertex buffer of @p size bytes of vertices of @p vertex_traits, written once or often (@p usage).
	virtual VertexBufferPtr create_vertex_buffer(VertexTraits const *vertex_traits, size_t size,
		BufferUsage usage = BufferUsage::STATIC) = 0;
	virtual IndexBufferPtr create_index_buffer(IndexFormat index_format, size_t size, BufferUsage usage = BufferUsage::STATIC) = 0;

	template <vertex_layout V> std::shared_ptr<VertexArray<V>>
	create_vertex_array(size_t count, BufferUsage usage = BufferUsage::STATIC)
	{
		return std::make_shared<VertexArray<V>>(create_vertex_buffer(VertexTraits::of<V>(), sizeof(V) * count, usage));
	}

	template <class I> std::shared_ptr<IndexArray<I>>
	create_index_array(size_t count, BufferUsage usage = BufferUsage::STATIC)
	{
		return std::make_shared<IndexArray<I>>(create_index_buffer(sizeof(I) == 2 ? IndexFormat::U16 : IndexFormat::U32,
			sizeof(I) * count, usage));
	}

	/// A texture of @p pixel_format made as @p usage says (sRGB, mipmaps, CPU copy, layers).
	virtual TexturePtr create_texture(int width, int height, PixelFormat pixel_format, TextureUsage const &usage = TextureUsage()) = 0;

	/// Bytes the frame constants block (binding 9 in every shader) holds at most: 16 KB, the uniform block Vulkan, OpenGL ES 3.0 and
	/// WebGL 2 all promise (it was 4 KB).
	static constexpr size_t FRAME_CONSTANTS_SIZE = 16384;
	/// The instances one batch of an instanced draw holds (EDGE_MAX_INSTANCES): 256 matrices, 16 KB, a uniform block every backend
	/// has.  A draw of more instances is drawn in batches of these.
	static constexpr size_t MAX_INSTANCES = 256;
	/// Global texture slots (bindings 12 to 19), sampled clamped to edge.
	static constexpr unsigned GLOBAL_TEXTURES_COUNT = 8;

	/// Sets the frame constants: up to FRAME_CONSTANTS_SIZE bytes every
	/// shader reads at binding 9 (std140 layout is the caller's business).
	/// Between frames only (std::logic_error inside one), so every draw of a
	/// frame sees the same values; they persist until the next call.
	/// std::invalid_argument for more than FRAME_CONSTANTS_SIZE bytes.
	void set_frame_constants(void const *data, size_t size);

	/// Sets global texture @p slot (binding 12 + slot) for every draw: a
	/// shadow map, a reflection, the frame so far.  A null texture means a
	/// 1 x 1 white one.  Between frames only (std::logic_error inside one);
	/// std::out_of_range for a slot >= GLOBAL_TEXTURES_COUNT.  A render
	/// target's colour texture may be set: it can be sampled in passes after
	/// the one that drew into it.
	void set_global_texture(unsigned slot, TexturePtr const &texture);
	/// Sets global texture @p slot to a render target's readable depth (RenderTarget::depth_texture) sampled with a comparison: a
	/// shader declares it a sampler2DShadow, and texture(shadow, vec3(uv, reference)) is 1 where the reference is at most the depth
	/// stored and 0 where it is past it, filtered linearly between texels where the device can (four lookups, compared and blended).
	/// A reference outside 0 to 1 means nothing (OpenGL ES clamps it for its 24-bit depth).
	/// The rules of set_global_texture(), and std::invalid_argument for anything but a readable depth of this renderer.
	void set_global_shadow_texture(unsigned slot, TexturePtr const &depth);
	virtual RenderTargetPtr default_render_target() = 0;
	/// A target of @p pixel_format colour and a depth buffer.  With @p readable_depth its depth is kept after each pass and is a texture
	/// (RenderTarget::depth_texture) that passes after it sample, as they do its colour: the depth the depth test saw, 0 near to 1 far
	/// as the projection put it, one value in red; without, the depth is thrown away when the pass ends.  The target of format
	/// {{pixel_format}, readable_depth}.
	RenderTargetPtr create_render_target(int width, int height, PixelFormat pixel_format, bool readable_depth = false);
	/// A target of @p format: colour attachments of their own formats, a depth thrown away or kept, layers or a cube (RenderTargetFormat).
	/// std::invalid_argument for what cannot be made, saying why: a size that is not positive, layers below 0, a cube that is not square
	/// or has layers, no colour and no readable depth, a block-compressed colour format, more colour attachments than
	/// max_color_attachments(), and on a device: a format it cannot draw into, more layers or a larger cube than it has.
	RenderTargetPtr create_render_target(int width, int height, RenderTargetFormat const &format);
	/// The colour attachments a target can have on this renderer: the device's (Vulkan maxColorAttachments, OpenGL ES GL_MAX_DRAW_BUFFERS,
	/// the smaller of them and of what a fragment shader writes), at least 4 everywhere; the null renderer's are those 4, so that what a
	/// test makes on it the phones make too.
	virtual unsigned max_color_attachments() const = 0;
	virtual MaterialPtr create_material() = 0;
	/// The shaders (their .spv names; each renderer finds its translation) a material draws with instead of the built-in ones.
	ShaderProgramPtr create_shader_program(std::string const &vertex_shader_filename, std::string const &fragment_shader_filename);
	/// The program of <dir>/<name>.vert.spv and <dir>/<name>.frag.spv, as edge_add_shaders makes them; std::runtime_error naming a file
	/// that is not there.
	ShaderProgramPtr load_shader_program(std::filesystem::path const &dir, std::string const &name);

	DrawCallPtr create_draw_call();
	UberDrawCallPtr create_uber_draw_call();

	/// Download multiple textures' contents from GPU to CPU in a single
	/// command-buffer submission.
	///
	/// Equivalent to calling @ref Texture::download() on every entry in
	/// @p textures, refusing what it refuses before anything is downloaded
	/// (and std::invalid_argument for a null entry), but on backends that pay
	/// a per-submit cost (NVIDIA Vulkan does a full pipeline drain via
	/// vkQueueWaitIdle for each command-buffer submit) this coalesces all
	/// readbacks into a single submit + wait.
	///
	/// Must be called when the renderer is idle (no active frame/pass) —
	/// same precondition as @ref Texture::download().
	///
	/// Default implementation in the base class falls back to a per-
	/// texture download loop; VulkanRenderer overrides for the batched
	/// path.
	void batch_download_textures(std::vector<TexturePtr> const &textures);

	/// Uploads multiple textures to the GPU in a single command-buffer submission: those written through
	/// @ref Texture::write() and not sent (Mapping::release()), each as the end of its mapping would send it.
	///
	/// Equivalent to ending a mapping of every entry, refusing what it refuses before anything is uploaded
	/// (std::logic_error for a texture without its CPU copy; std::invalid_argument for a null entry or one named
	/// twice), but on backends that pay a per-submit cost (a full queue wait for each upload) it coalesces all of
	/// them into a single submit + wait.  An empty batch does nothing.
	///
	/// Default implementation in the base class falls back to sending each in turn; VulkanRenderer overrides it.
	void batch_upload_textures(std::vector<TexturePtr> const &textures);

	/// Uploads textures written through @ref Texture::write() and released without waiting for the GPU: inside a frame (and not in
	/// a pass) the new texels ride in the frame's own command buffer, copied before anything the frame draws, so the frame
	/// sees them; the texture's CPU copy is free to be written again at once.  Outside a frame there is nothing to ride in,
	/// and they are sent as batch_upload_textures() sends them.  For a texture rewritten every frame (a sea).
	///
	/// std::invalid_argument for a null entry or one named twice, std::logic_error for a texture without its CPU copy, with
	/// levels (mipmaps) or layers (an array), or any in a pass: all refused before anything is sent.
	///
	/// Default implementation in the base class falls back to batch_upload_textures(); VulkanRenderer overrides it.
	void stream_textures(std::vector<TexturePtr> const &textures);

protected:
	// What a renderer does at each step, called when the step is in order (the state is the one before the step; the renderer's
	// state changes only when the call returns).
	virtual void do_begin_frame() = 0;
	virtual void do_finish_frame() = 0;
	virtual void do_submit_frame() { do_finish_frame(); }
	/// @p target is not null.
	virtual void do_begin_pass(RenderTargetPtr const &target, RenderPassClearState const &clear_state, uint32_t pass_type) = 0;
	virtual void do_end_pass() = 0;
	/// The rectangle is inside the pass's target.
	virtual void do_set_viewport(int x, int y, int width, int height) = 0;
	/// Outside a frame, @p size bytes at most FRAME_CONSTANTS_SIZE.
	virtual void do_set_frame_constants(void const *data, size_t size) = 0;
	/// Outside a frame, @p slot below GLOBAL_TEXTURES_COUNT; @p compare: a readable depth, sampled with a comparison.
	virtual void do_set_global_texture(unsigned slot, TexturePtr const &texture, bool compare) = 0;
	/// In a pass, @p call committed.
	virtual void do_draw(DrawCall const &call) = 0;
	/// A target of @p format as create_render_target() has checked it (the size positive, layers 0 or more, a cube square and of no
	/// layers, something to draw into, no block-compressed colour, no more colours than max_color_attachments()); the backend
	/// refuses what its device cannot make (std::invalid_argument naming the limit) and makes the layers' targets.
	virtual RenderTargetPtr do_create_render_target(int width, int height, RenderTargetFormat const &format) = 0;
	/// No entry null (downloads: outside a frame; uploads: none twice).  The defaults download, unlock, one texture at a time.
	virtual void do_batch_download_textures(std::vector<TexturePtr> const &textures);
	virtual void do_batch_upload_textures(std::vector<TexturePtr> const &textures);
	/// Not in a pass, no entry null or twice.  The default uploads them as batch_upload_textures(), outside a frame or not.
	virtual void do_stream_textures(std::vector<TexturePtr> const &textures);
	/// The renderer's timer: one that is not available() where it cannot time.
	virtual GpuTimer &gpu_timer() const = 0;

private:
	/// What a draw of @p call reads is all there, and the renderer's own: std::invalid_argument otherwise.
	void check_ranges(DrawCall const &call) const;

	friend class DrawCall;
	/// Draws @p call in the active pass (std::logic_error outside one): DrawCall::execute() gives it here once committed.
	void draw(DrawCall const &call);
	/// The view-projection @p camera had at its last commit (any draw call's) in its frame before (previous_frame()), for a commit now
	/// with @p current: none where there is no frame before or the camera was cut since.  Records @p current as the camera's in this
	/// frame.
	std::optional<rove::matrix<4,4>> previous_view_projection(CameraPtr const &camera, rove::matrix<4,4> const &current);

	/// Each camera's frames and view-projections, as previous_view_projection() keeps them: by the camera's address, while it lives.
	struct CameraMotion
	{
		std::weak_ptr<Camera const> camera;
		std::uint64_t cuts = 0;                       // Camera::cuts() when recorded
		std::uint64_t frame = 0;                      // of the last commit
		rove::matrix<4,4> last;                       // the view-projection at it
		std::optional<std::uint64_t> before_frame;    // the frame of a commit before frame (none: none)
		std::optional<rove::matrix<4,4>> before;     // the view-projection in it (none: none, or cut since)
	};
	/// The record of @p camera, null for none (or one of a camera gone whose address another has now).
	CameraMotion const *camera_motion(CameraPtr const &camera) const;
	std::unordered_map<Camera const *, CameraMotion> camera_motion_;
	/// Guards camera_motion_: draw calls are committed on several threads at once (an arena building its worlds' views in
	/// parallel), each through cameras of its own, and every commit reads and records its camera here.
	mutable std::mutex camera_motion_mutex_;
	std::uint64_t frames_ended_ = 0;

	enum class State { IDLE, FRAME, PASS };
	State state_ = State::IDLE;
	uint32_t pass_type_ = 0;
	RenderTargetPtr pass_target_;
	/// What the open frame draws with or into (targets, materials and their textures, buffers, streamed textures): held until the
	/// frame is submitted, from when a resource's destructor waits for the GPU to be done with it.  Its last owner may let it go
	/// inside the frame.
	std::vector<std::shared_ptr<void const>> frame_resources_;
};

}
