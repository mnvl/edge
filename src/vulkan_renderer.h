// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

#include <vulkan/vulkan.h>
#include <cstdint>
#include <optional>
#include <vector>
#include <memory>
#include <string>
#include "renderer.h"

namespace edge
{

// The renderer's own parts, edge's business: their headers are not installed, and nothing here needs more than their names.
class VulkanDeviceContext;
using VulkanDeviceContextPtr = std::shared_ptr<VulkanDeviceContext>;
class VulkanDescriptorCache;
class VulkanDynamicUniformBuffer;
class VulkanFrameRing;
class VulkanGpuTimer;
class VulkanPipelineBuilder;
class VulkanPresenter;
class VulkanRenderPassCache;
class VulkanUploader;

// Always-offscreen Vulkan renderer. No window or display surface is created,
// enabling multiple independent Renderer instances per process: each has its own device, made from the process's one Vulkan
// instance, which they share (vulkan_instance.h).
class VulkanRenderer: public Renderer {
public:
	VulkanRendererPtr shared_from_this()
	{
		return std::static_pointer_cast<VulkanRenderer>(Renderer::shared_from_this());
	}

	// device_uuid selects which physical device to run on; empty keeps the
	// historical behaviour of taking the first suitable one.  See
	// vulkan_device_info.h for why the uuid rather than a name or an index.
	//
	// instance_extensions are enabled on the instance on top of the renderer's own: what a window system needs to make a
	// surface (SDL_Vulkan_GetInstanceExtensions).  One the driver lacks is a std::runtime_error naming it.  The instance is the
	// process's, made by the first renderer (VulkanInstance): one the live instance was made without is a std::logic_error naming it
	// (the renderer that needs it is made first).
	// width and height: positive (std::invalid_argument otherwise).
	//
	// frames_in_flight (1 to MAX_FRAMES_IN_FLIGHT, std::invalid_argument otherwise): how many frames submit_frame() lets the GPU
	// draw while the host builds the next.  What the host writes for a frame (frame and draw constants, lights, staged uploads)
	// is kept apart for each of them, so the capacity of the draw constants and joint matrices (EDGE_VK_DRAW_CONSTANTS_BLOCKS,
	// joint_matrices) is shared out among them.  An application that renders two frames for each it shows (a
	// scene, then the screen it is put on) wants three.
	//
	// joint_matrices: how many joint matrices the frames in flight have room for together (64 bytes of host-visible memory each), a
	// skinned draw taking as many as its palette has, rounded up to the device's uniform offset alignment (at most 4); a draw past
	// them in a frame is a std::runtime_error.  At least frames_in_flight (std::invalid_argument otherwise).  The default is 1024 draws
	// of MAX_JOINT_MATRICES (16 MB).  $EDGE_VK_JOINT_MATRICES, when set, is taken instead; $EDGE_VK_JOINT_MATRIX_BLOCKS, its name when
	// it counted blocks of 256, is std::invalid_argument.
	static constexpr int DEFAULT_JOINT_MATRICES = 1024 * 256;
	VulkanRenderer(int width, int height, std::string const &device_uuid = std::string(),
		std::vector<std::string> const &instance_extensions = std::vector<std::string>(), int frames_in_flight = 1,
		int joint_matrices = DEFAULT_JOINT_MATRICES);
	~VulkanRenderer() override;

	int screen_width() const override;
	int screen_height() const override;
	void resize(int width, int height) override;


	// UUID of the device actually in use, canonical 8-4-4-4-12 hex.
	std::string const &device_uuid() const { return device_uuid_; }

	/// The instance, for the application to make a window's surface from: the process's, which its other renderers share.
	VkInstance instance() const { return instance_; }

	/// Takes over a surface made from instance() (destroyed with the renderer once this returns; when it throws the
	/// caller still owns it; one lost already is taken, and lost: surface_lost()) so that present() can show frames in
	/// its window.  vsync: the presentation waits for the display's refresh, otherwise it does not wait (tearing) when the
	/// display allows that.  std::invalid_argument for a null surface; std::logic_error with a surface attached already
	/// or inside a frame; std::runtime_error when the device cannot present (no swapchain extension) or its queue cannot
	/// present to this surface.
	void attach_surface(VkSurfaceKHR surface, bool vsync);

	/// Takes the surface back down, swapchain first, so that its window can be destroyed while the renderer lives on.
	/// std::logic_error with no surface or inside a frame.  A surface can be attached again after.
	void detach_surface();

	/// Switches the presentation's wait for the display's refresh (see attach_surface); takes effect with the next present().
	/// std::logic_error with no surface or inside a frame.
	void set_vsync(bool vsync);
	bool vsync() const;

	/// Shows a rendered frame in the attached surface's window, scaled to width x height pixels, the window's size in
	/// pixels.  It does not wait for the copy: the next present() does (the frame can be rendered into again at once, the GPU
	/// copies it first).  The frame is as it was left, layout included.  Returns false, showing nothing, when the window has
	/// no area (minimised), and when the surface is lost (surface_lost()).
	/// std::logic_error without a surface, on a lost one, inside a frame, or for an image in a layout it cannot be copied from (never
	/// rendered into); std::invalid_argument for a texture that is not this renderer's or a negative size.
	bool present(TexturePtr const &frame, int width, int height);
	/// Whether the surface attached was lost presenting: its window was taken back (a phone's is when its app goes to the background, and
	/// a pause and a resume close together can take it between a frame and its present).  Nothing is presented on it: it is to be
	/// detached (detach_surface()) and the window's next attached.  False without a surface.
	bool surface_lost() const;

	static constexpr int MAX_FRAMES_IN_FLIGHT = 4;
	int frames_in_flight() const { return static_cast<int>(frames_in_flight_); }

	/// Waits for every frame submitted and for the last copy present() made.  std::logic_error inside a frame (it is not submitted).
	void wait_frames();
	/// As wait_frames(), inside a frame too (the open one is not waited for): for a resource about to be destroyed.
	void wait_submitted();
	uint64_t frame_serial() const override { return frame_serial_; }
	/// Milliseconds the host has spent waiting for frames the GPU had not drawn yet, since the renderer was made.
	double gpu_wait_ms() const;

	VertexBufferPtr create_vertex_buffer(VertexTraits const *vertex_traits, size_t size,
		BufferUsage usage = BufferUsage::STATIC) override;
	IndexBufferPtr create_index_buffer(IndexFormat index_format, size_t size, BufferUsage usage = BufferUsage::STATIC) override;

	TexturePtr create_texture(int width, int height, PixelFormat pixel_format, TextureUsage const &usage = TextureUsage()) override;
	RenderTargetPtr default_render_target() override;
	/// The device's maxColorAttachments, or its maxFragmentOutputAttachments where that is fewer.
	unsigned max_color_attachments() const override;
	MaterialPtr create_material() override;



	VkPhysicalDevice physical_device() const { return physical_device_; }
	VkDevice device() const { return device_; }
	/// Shared instance/device owner; every resource created here keeps a
	/// reference so the device outlives it whatever the destruction order.
	VulkanDeviceContextPtr const &device_context() const { return device_context_; }
	uint32_t graphics_queue_family() const { return graphics_queue_family_; }
	VkCommandBuffer active_command_buffer() const { return active_command_buffer_; }
	VkPipelineLayout pipeline_layout() const;
	/// See VulkanRenderPassCache.
	VkRenderPass offscreen_render_pass(VkFormat color_format, bool keep_depth = false);
	VkRenderPass offscreen_render_pass(std::vector<VkFormat> const &color_formats, bool keep_depth);
	VkFormat depth_format() const;


	/// Copy `size` bytes into device-local buffer `destination`.  Vertex and index buffers live in device-local
	/// memory, so the bytes a caller writes through write() have to be transferred, staged by VulkanUploader.
	/// Where the copy is recorded depends on renderer state:
	///   * frame open, outside a pass: into the frame's command buffer at once, ordered after the draws already
	///     recorded and before the ones that follow, at no extra submit;
	///   * inside a render pass: refused (std::logic_error), as copies are illegal there and one sent on its own would
	///     land before the uploads the frame already holds;
	///   * renderer idle: deferred and flushed into the next frame's command buffer by begin_frame(), which keeps
	///     model loading and per-frame animation updates down to zero extra submits.
	void upload_to_buffer(VkBuffer destination, void const *data, size_t size);
	/// Drop any deferred upload targeting `destination`; call before destroying
	/// a buffer so no pending copy references a dead handle.
	void cancel_buffer_uploads(VkBuffer destination);
	/// Destroy a buffer and its memory, waiting until the current frame has
	/// been submitted and completed if one is open.  An upload recorded into
	/// the frame's command buffer still names the handle, so releasing it
	/// straight away would leave the submit reading freed objects.
	void destroy_buffer(VkBuffer buffer, VkDeviceMemory memory);
	/// The bytes of staging space the uploads hold: what a frame uses, kept from one frame to the next, and what was
	/// written since the last frame ended (see VulkanUploader::end_frame()).
	VkDeviceSize staging_arena_size() const;
	VkCommandBuffer begin_single_time_commands() const;
	/// Submits and waits for the queue: every frame submitted is drawn after it.
	void end_single_time_commands(VkCommandBuffer command_buffer);
	/// Submits since creation: those of end_single_time_commands(), each followed by a vkQueueWaitIdle, and one a frame.
	/// What a test asserts to prove a call does, or does not, stall on the GPU.
	uint64_t single_time_submit_count() const { return single_time_submit_count_; }
	void transition_image_layout(VkImage image, VkImageLayout old_layout, VkImageLayout new_layout);


	uint32_t update_joint_matrices(std::vector<rove::matrix<4,4>> const &joint_matrices);
	uint32_t update_draw_constants(rove::matrix<4,4> const &world_view_projection,
		rove::matrix<4,4> const &world_matrix, rove::matrix<4,4> const &previous_world_view_projection,
		rove::matrix<4,4> const &previous_world_matrix);
	VkPipeline pipeline(VertexTraits const *traits, MaterialPtr const &material);
	VkDescriptorSet material_descriptor_set(MaterialPtr const &material);
	/// A draw's block of a buffer and its block of the frame before's (the same where they are the same).
	struct Blocks
	{
		uint32_t offset, previous_offset;
	};
	/// Binds a material's set for a draw: its blocks of the joint matrices (none: not skinned) and of the instances (none: not
	/// instanced), of the draw constants and of the material parameters, and the frame's region of the frame constants.
	void bind_descriptor_set(VkCommandBuffer command_buffer, VkDescriptorSet set, std::optional<Blocks> joint_matrices,
		std::optional<Blocks> instances, uint32_t draw_constants_offset, uint32_t material_parameters_offset);
	/// Descriptor sets currently cached: one per live material that has been
	/// drawn, plus those of materials that died since the last frame boundary
	/// (reclaimed by the next begin_frame).  Exposed so tests can prove dead
	/// materials are reclaimed.
	size_t material_descriptor_set_count() const;
	/// Descriptor pools made: one until more materials lived at once than a pool holds.
	size_t material_descriptor_pool_count() const;

protected:
	/// Waits until the frame that last used the share of the host's memory this one takes is drawn (with one frame in flight:
	/// the frame before), then opens the frame.
	void do_begin_frame() override;
	/// do_submit_frame(), then waits for every frame submitted: the GPU's times are this frame's.
	void do_finish_frame() override;
	/// Ends the frame and submits it with no wait: the GPU draws it while the host builds the next frames, each in a share of
	/// the host's memory of its own.  A download, a texture written, or wait_frames() waits for it.  One submit, as finish_frame().
	void do_submit_frame() override;
	void do_begin_pass(RenderTargetPtr const &target, RenderPassClearState const &clear_state, uint32_t pass_type) override;
	void do_end_pass() override;
	void do_set_viewport(int x, int y, int width, int height) override;
	void do_set_frame_constants(void const *data, size_t size) override;
	void do_set_global_texture(unsigned slot, TexturePtr const &texture, bool compare) override;
	void do_draw(DrawCall const &call) override;
	/// std::invalid_argument for a colour format the device cannot draw into, more layers than its maxImageArrayLayers, a cube larger than
	/// its maxImageDimensionCube.
	RenderTargetPtr do_create_render_target(int width, int height, RenderTargetFormat const &format) override;
	/// Every texture's VulkanTexture::record_download() into one command buffer, submitted with one wait instead of
	/// one per texture: each wait drains the pipeline (and a PCIe round trip on a discrete GPU).
	void do_batch_download_textures(std::vector<TexturePtr> const &textures) override;
	void do_batch_upload_textures(std::vector<TexturePtr> const &textures) override;
	void do_stream_textures(std::vector<TexturePtr> const &textures) override;
	/// A VulkanGpuTimer: a frame's durations are read back when the renderer waits for the frame (finish_frame(), wait_frames(), or
	/// the begin_frame() that takes its share back).
	GpuTimer &gpu_timer() const override;

private:
	int screen_width_, screen_height_;

	VulkanDeviceContextPtr device_context_;
	VkInstance instance_;
	VkPhysicalDevice physical_device_;
	// Requested before pick_physical_device(), the chosen device's after it.
	std::string device_uuid_;
	VkDevice device_;
	VkQueue graphics_queue_;
	uint32_t graphics_queue_family_;
	VkCommandPool command_pool_;

	std::vector<std::string> instance_extensions_;     // the application's, on top of the renderer's own
	bool swapchain_extension_enabled_ = false;
	bool surface_instance_extension_ = false;  // VK_KHR_surface on the instance: a swapchain can be had
	std::unique_ptr<VulkanPresenter> presenter_;

	// (made by the constructor, never null)
	std::unique_ptr<VulkanPipelineBuilder> pipeline_builder_;

	TexturePtr default_texture_;
	std::unique_ptr<VulkanDynamicUniformBuffer> frame_constants_ubo_;
	TexturePtr global_textures_[GLOBAL_TEXTURES_COUNT];
	bool global_compare_[GLOBAL_TEXTURES_COUNT] = {};      // global_shadow_texture: sampled with a comparison
	bool anisotropy_enabled_ = false;
	std::unique_ptr<VulkanDynamicUniformBuffer> joint_matrices_ubo_;
	std::unique_ptr<VulkanDynamicUniformBuffer> instances_ubo_;
	std::unique_ptr<VulkanDynamicUniformBuffer> material_parameters_ubo_;
	uint32_t material_zero_offset_ = 0;          // this frame's block of zeros, a material without parameters reads
	std::unique_ptr<VulkanDynamicUniformBuffer> draw_constants_ubo_;
	std::unique_ptr<VulkanDescriptorCache> descriptors_;

	std::unique_ptr<VulkanUploader> uploader_;

	/// Frees the retired sets no frame in flight can hold, and retires the sets of materials that have died.  Between frames.
	void reclaim_material_descriptor_sets();

	/// The frames in flight (VulkanFrameRing), as many as the renderer was made with.
	uint32_t frames_in_flight_ = 1;
	int joint_matrices_ = DEFAULT_JOINT_MATRICES;      // as asked for: the environment can override it
	std::unique_ptr<VulkanFrameRing> frames_;
	bool current_ready_ = false;       // the current share is free and its regions are rewound
	uint64_t frame_serial_ = 0;        // frames begun
	/// Makes the current share free (waiting for its frame) and rewinds it: before the host writes anything a frame reads.
	void prepare_current();

	VkCommandBuffer active_command_buffer_;
	VkRenderPass active_pass_render_pass_;
	uint32_t active_pass_color_attachments_ = 0;      // the colours of the pass's target, which its pipelines blend
	RenderTargetPtr default_render_target_;


	std::unique_ptr<VulkanGpuTimer> gpu_timer_;
	std::unique_ptr<VulkanRenderPassCache> render_pass_cache_;
	uint64_t single_time_submit_count_ = 0;

	void init();
	void init_rendering_resources();
	void create_command_pool();
	void create_descriptor_resources();
	void create_constants_buffers();
	void cleanup_rendering_resources();
	void destroy_descriptor_resources();
	void destroy_pipelines();
	void ensure_default_texture();
};

}
