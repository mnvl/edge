// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "log.h"

#include "material.h"
#include "pixel.h"
#include "shader_program.h"
#include "vertex_traits.h"
#include "vulkan_descriptor_cache.h"
#include "vulkan_device.h"
#include "vulkan_error.h"
#include "vulkan_frame_ring.h"
#include "vulkan_gpu_timer.h"
#include "vulkan_image_layout.h"
#include "vulkan_instance.h"
#include "vulkan_memory.h"
#include "vulkan_presenter.h"
#include "vulkan_render_pass_cache.h"
#include "vulkan_uploader.h"
#include "vulkan_index_buffer.h"
#include "vulkan_render_target.h"
#include "vulkan_commands.h"
#include "vulkan_renderer.h"
#include "draw_call.h"
#include "shader_interface.h"
#include "vulkan_index_buffer.h"
#include "vulkan_vertex_buffer.h"
#include "vulkan_device_info.h"
#include "vulkan_texture.h"
#include "vulkan_vertex_buffer.h"
#include "mesh.h"

namespace edge
{

namespace
{

constexpr VkDeviceSize VULKAN_DEFAULT_DRAW_CONSTANTS_BLOCKS_PER_FRAME = 262144;
constexpr VkDeviceSize VULKAN_DEFAULT_INSTANCE_BLOCKS_PER_FRAME = 1024;        // batches of instances, 16 KB each
constexpr VkDeviceSize VULKAN_DEFAULT_MATERIAL_PARAMETER_BLOCKS_PER_FRAME = 16384;      // draws of materials with parameters, 1 KB each

VkDeviceSize capacity_from_env(char const *name, VkDeviceSize default_value, VkDeviceSize min_value)
{
	char const *value = std::getenv(name);
	if (value == nullptr || *value == '\0')
	{
		return default_value;
	}

	char *end = nullptr;
	unsigned long long const parsed = std::strtoull(value, &end, 10);
	if (end == value || *end != '\0' || parsed < min_value)
	{
		throw std::invalid_argument(std::string(name) + "=" + value + ": not a number of at least " + std::to_string(min_value));
	}
	return static_cast<VkDeviceSize>(parsed);
}

struct VulkanDrawConstants
{
	rove::matrix<4,4> world_view_projection;
	rove::matrix<4,4> world_matrix;
	rove::matrix<4,4> previous_world_view_projection;
	rove::matrix<4,4> previous_world_matrix;
};

/// Whether two runs of matrices are the same, bit for bit: the frame before's that are this frame's take no room of their own.
bool same_matrices(rove::matrix<4,4> const *a, rove::matrix<4,4> const *b, size_t count)
{
	return std::memcmp(a, b, count * sizeof(rove::matrix<4,4>)) == 0;
}

}

VulkanRenderer::VulkanRenderer(int width, int height, std::string const &device_uuid,
	std::vector<std::string> const &instance_extensions, int frames_in_flight, int joint_matrices):
	Renderer(),
	screen_width_(width),
	screen_height_(height),
	instance_(VK_NULL_HANDLE),
	physical_device_(VK_NULL_HANDLE),
	device_uuid_(device_uuid),
	device_(VK_NULL_HANDLE),
	graphics_queue_(VK_NULL_HANDLE),
	graphics_queue_family_(0),
	command_pool_(VK_NULL_HANDLE),
	active_command_buffer_(VK_NULL_HANDLE),
	active_pass_render_pass_(VK_NULL_HANDLE)
{
	if (width <= 0 || height <= 0)
	{
		throw std::invalid_argument("a renderer of " + std::to_string(width) + "x" + std::to_string(height) + " pixels");
	}
	if (frames_in_flight < 1 || frames_in_flight > MAX_FRAMES_IN_FLIGHT)
	{
		throw std::invalid_argument("a renderer with " + std::to_string(frames_in_flight) + " frames in flight (1 to "
			+ std::to_string(MAX_FRAMES_IN_FLIGHT) + ")");
	}
	if (joint_matrices < frames_in_flight)
	{
		throw std::invalid_argument("joint_matrices " + std::to_string(joint_matrices) + " for "
			+ std::to_string(frames_in_flight) + " frames in flight (at least one for each)");
	}
	// its name when it counted blocks of 256: read as matrices, its number would be 256 times too few
	if (std::getenv("EDGE_VK_JOINT_MATRIX_BLOCKS") != nullptr)
	{
		throw std::invalid_argument("EDGE_VK_JOINT_MATRIX_BLOCKS is now EDGE_VK_JOINT_MATRICES, a number of joint matrices (a block was 256)");
	}
	frames_in_flight_ = static_cast<uint32_t>(frames_in_flight);
	joint_matrices_ = joint_matrices;
	instance_extensions_ = instance_extensions;

	// Created before init() because init() already builds textures (the default texture) that take a reference.
	device_context_ = std::make_shared<VulkanDeviceContext>(this);

	try
	{
		init();
	}
	catch (...)
	{
		frames_.reset();
		cleanup_rendering_resources();
		render_pass_cache_.reset();
		uploader_.reset();

		if (command_pool_ != VK_NULL_HANDLE)
		{
			vkDestroyCommandPool(device_, command_pool_, nullptr);
			command_pool_ = VK_NULL_HANDLE;
		}

		presenter_.reset();

		// Device and instance belong to the context: dropping this reference
		// destroys them now unless a resource built during the failed init()
		// is still alive, in which case they go with it.
		device_context_->detach_renderer();
		device_context_.reset();
		device_ = VK_NULL_HANDLE;
		instance_ = VK_NULL_HANDLE;

		throw;
	}
}

VulkanRenderer::~VulkanRenderer()
{
	// From here on the renderer is unusable, so resources that outlive it fail
	// their operations loudly instead of calling into a half-destroyed object.
	device_context_->detach_renderer();

	if (device_ != VK_NULL_HANDLE)
	{
		vkDeviceWaitIdle(device_);
	}

	frames_.reset();
	uploader_.reset();

	gpu_timer_.reset();

	cleanup_rendering_resources();
	render_pass_cache_.reset();
	presenter_.reset();

	if (command_pool_ != VK_NULL_HANDLE)
	{
		vkDestroyCommandPool(device_, command_pool_, nullptr);
		command_pool_ = VK_NULL_HANDLE;
	}

	// Textures and render targets handed out to callers may still be alive;
	// they own the context too, and the last of them destroys device+instance.
	device_context_.reset();
	device_ = VK_NULL_HANDLE;
	instance_ = VK_NULL_HANDLE;
}

void VulkanRenderer::init()
{
	VulkanInstancePtr const instance = VulkanInstance::acquire(instance_extensions_);
	device_context_->set_instance(instance);
	instance_ = instance->get();
	surface_instance_extension_ = instance->surface_extension();
	physical_device_ = pick_vulkan_physical_device(instance_, std::string(device_uuid_), device_uuid_);
	VulkanLogicalDevice const device = create_vulkan_device(physical_device_, surface_instance_extension_);
	device_ = device.device;
	device_context_->set_device(physical_device_, device_);
	graphics_queue_ = device.graphics_queue;
	graphics_queue_family_ = device.graphics_queue_family;
	swapchain_extension_enabled_ = device.swapchain;
	anisotropy_enabled_ = device.anisotropy;
	uint32_t const slots = frames_in_flight_;
	gpu_timer_ = std::make_unique<VulkanGpuTimer>(device_, physical_device_, graphics_queue_family_, slots);
	uploader_ = std::make_unique<VulkanUploader>(device_, physical_device_, slots);
	render_pass_cache_ = std::make_unique<VulkanRenderPassCache>(device_, physical_device_);
	create_command_pool();
	presenter_ = std::make_unique<VulkanPresenter>(instance_, physical_device_, device_, graphics_queue_, graphics_queue_family_,
		command_pool_, swapchain_extension_enabled_);
	// a frame known drawn gives its share back: the timer's queries, the staging arena, and what waited for the frame
	frames_ = std::make_unique<VulkanFrameRing>(device_, command_pool_, slots, [this](uint32_t slot, uint64_t serial) {
		gpu_timer_->read_back(slot, serial);
		uploader_->rewind_arena(slot);
		uploader_->collect(frames_->completed());
		descriptors_->free_drawn(frames_->completed());
	});
	init_rendering_resources();

	EDGE_LOG_INFO("Vulkan offscreen renderer created: {}x{}, {} frame{} in flight", screen_width_, screen_height_, slots,
		slots == 1 ? "" : "s");
}

void VulkanRenderer::create_command_pool()
{
	VkCommandPoolCreateInfo pool_info;
	std::memset(&pool_info, 0, sizeof(pool_info));
	pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	pool_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	pool_info.queueFamilyIndex = graphics_queue_family_;

	check_vk_result(vkCreateCommandPool(device_, &pool_info, nullptr, &command_pool_), "vkCreateCommandPool");
}

void VulkanRenderer::init_rendering_resources()
{
	create_descriptor_resources();
	ensure_default_texture();
}

void VulkanRenderer::cleanup_rendering_resources()
{
	if (device_ == VK_NULL_HANDLE)
	{
		return;
	}

	default_render_target_.reset();

	destroy_pipelines();
	destroy_descriptor_resources();

	active_command_buffer_ = VK_NULL_HANDLE;
	active_pass_render_pass_ = VK_NULL_HANDLE;
}

void VulkanRenderer::create_descriptor_resources()
{
	create_constants_buffers();
	VulkanDescriptorCache::Constants constants;
	constants.joint_matrices = &joint_matrices_ubo_;
	constants.instances = &instances_ubo_;
	constants.draw_constants = &draw_constants_ubo_;
	constants.frame_constants = &frame_constants_ubo_;
	constants.material_parameters = &material_parameters_ubo_;
	// $EDGE_VK_MAX_MATERIAL_SETS, when set, shrinks the descriptor pool below VulkanDescriptorCache::MAX_SETS: a test exhausting
	// it to prove reclaim works can afford to stream only as many materials as the pool actually holds.
	uint32_t const max_material_sets = static_cast<uint32_t>(capacity_from_env(
		"EDGE_VK_MAX_MATERIAL_SETS",
		static_cast<VkDeviceSize>(VulkanDescriptorCache::MAX_SETS),
		1));
	descriptors_ = std::make_unique<VulkanDescriptorCache>(device_, physical_device_, anisotropy_enabled_, constants, depth_format(),
		max_material_sets);
}

void VulkanRenderer::create_constants_buffers()
{
	// a region of each for every frame in flight; the draw constants and joint matrices share their capacity out among them
	uint32_t const slots = frames_in_flight_;
	frame_constants_ubo_.create(device_, physical_device_, static_cast<VkDeviceSize>(FRAME_CONSTANTS_SIZE), slots, slots);

	VkDeviceSize const joint_block_size =
		static_cast<VkDeviceSize>(MAX_JOINT_MATRICES * sizeof(rove::matrix<4,4>));
	VkDeviceSize const joint_matrices = capacity_from_env(
		"EDGE_VK_JOINT_MATRICES",
		static_cast<VkDeviceSize>(joint_matrices_),
		1);
	VkDeviceSize const draw_constants_blocks_per_frame = capacity_from_env(
		"EDGE_VK_DRAW_CONSTANTS_BLOCKS",
		VULKAN_DEFAULT_DRAW_CONSTANTS_BLOCKS_PER_FRAME,
		1);

	VkDeviceSize const instance_blocks_per_frame = capacity_from_env(
		"EDGE_VK_INSTANCE_BLOCKS",
		VULKAN_DEFAULT_INSTANCE_BLOCKS_PER_FRAME,
		1);

	joint_matrices_ubo_.create_packed(device_, physical_device_, joint_block_size, joint_matrices * sizeof(rove::matrix<4,4>), slots);
	instances_ubo_.create(device_, physical_device_, static_cast<VkDeviceSize>(MAX_INSTANCES * sizeof(rove::matrix<4,4>)),
		instance_blocks_per_frame, slots);
	VkDeviceSize const material_parameter_blocks_per_frame = capacity_from_env(
		"EDGE_VK_MATERIAL_PARAMETER_BLOCKS",
		VULKAN_DEFAULT_MATERIAL_PARAMETER_BLOCKS_PER_FRAME,
		1);
	material_parameters_ubo_.create(device_, physical_device_, static_cast<VkDeviceSize>(Material::PARAMETERS_SIZE),
		material_parameter_blocks_per_frame, slots);
	draw_constants_ubo_.create(device_, physical_device_,
		static_cast<VkDeviceSize>(sizeof(VulkanDrawConstants)), draw_constants_blocks_per_frame, slots);

	EDGE_LOG_INFO("Vulkan dynamic UBO capacity: joint matrices/frame = {}, draw blocks/frame = {} ({} frame{} in flight share {} and {})",
		joint_matrices / slots, draw_constants_blocks_per_frame / slots, slots, slots == 1 ? "" : "s",
		joint_matrices, draw_constants_blocks_per_frame);
}

void VulkanRenderer::destroy_descriptor_resources()
{
	default_texture_.reset();
	joint_matrices_ubo_.destroy(device_);
	instances_ubo_.destroy(device_);
	material_parameters_ubo_.destroy(device_);
	draw_constants_ubo_.destroy(device_);
	frame_constants_ubo_.destroy(device_);
	for (TexturePtr &t : global_textures_)
	{
		t.reset();
	}
	descriptors_.reset();
}

VkDescriptorSet VulkanRenderer::material_descriptor_set(MaterialPtr const &material)
{
	ensure_default_texture();
	return descriptors_->get(material, global_textures_, global_compare_, default_texture_, frame_serial_);
}

void VulkanRenderer::bind_descriptor_set(VkCommandBuffer command_buffer, VkDescriptorSet set, std::optional<Blocks> joint_matrices,
	std::optional<Blocks> instances, uint32_t draw_constants_offset, uint32_t material_parameters_offset)
{
	// in binding order: 6 joint matrices, 7 instances, 8 draw constants, 9 frame constants, 20 material parameters, 21 the previous
	// instances, 22 the previous joint matrices (the last two in the instances' and the joint matrices' buffers)
	uint32_t const joints_offset = joint_matrices ? joint_matrices->offset : joint_matrices_ubo_.region_offset();
	uint32_t const instances_offset = instances ? instances->offset : instances_ubo_.region_offset();
	uint32_t const offsets[VulkanDescriptorCache::UNIFORM_BUFFERS] = {
		joints_offset, instances_offset, draw_constants_offset, frame_constants_ubo_.region_offset(), material_parameters_offset,
		instances ? instances->previous_offset : instances_offset, joint_matrices ? joint_matrices->previous_offset : joints_offset};
	vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, descriptors_->pipeline_layout(), 0, 1, &set,
		VulkanDescriptorCache::UNIFORM_BUFFERS, offsets);
}

void VulkanRenderer::reclaim_material_descriptor_sets()
{
	if (in_frame())
	{
		throw std::logic_error("descriptor sets can only be reclaimed between frames");
	}
	// the frames up to the last one begun may hold a dead material's set
	descriptors_->reclaim(frame_serial_, frames_->completed());
}

size_t VulkanRenderer::material_descriptor_set_count() const
{
	return descriptors_->count();
}

VkRenderPass VulkanRenderer::offscreen_render_pass(VkFormat color_format, bool keep_depth)
{
	return render_pass_cache_->get(color_format, keep_depth);
}

VkRenderPass VulkanRenderer::offscreen_render_pass(std::vector<VkFormat> const &color_formats, bool keep_depth)
{
	return render_pass_cache_->get(color_formats, keep_depth);
}

VkFormat VulkanRenderer::depth_format() const
{
	return render_pass_cache_->depth_format();
}

void VulkanRenderer::upload_to_buffer(VkBuffer destination, void const *data, size_t size)
{
	if (size == 0)
	{
		return;
	}
	if (data == nullptr || destination == VK_NULL_HANDLE)
	{
		throw std::logic_error("an upload of " + std::to_string(size) + " bytes with no source or no buffer to take them");
	}
	// transfers are illegal inside a render pass, and a copy sent on its own would land before the uploads the frame already holds
	if (in_pass())
	{
		throw std::logic_error("a buffer was written inside a render pass: write it before the pass or between passes");
	}

	prepare_current();      // (the staging arena is the frame's: free once its last frame is drawn)
	VulkanUploader::Staged const staged = uploader_->stage(data, size);
	if ((in_frame() && !in_pass()))
	{
		VulkanUploader::record_upload(active_command_buffer_, staged, destination);
	}
	else
	{
		uploader_->defer(destination, staged);
	}
}

void VulkanRenderer::cancel_buffer_uploads(VkBuffer destination)
{
	uploader_->cancel(destination);
}

void VulkanRenderer::destroy_buffer(VkBuffer buffer, VkDeviceMemory memory)
{
	if (buffer == VK_NULL_HANDLE && memory == VK_NULL_HANDLE)
	{
		return;
	}

	// the frame open may name it, or a frame the GPU has not drawn yet: it goes once that frame is drawn
	if (in_frame())
	{
		uploader_->retire(buffer, memory, frame_serial_);
		return;
	}
	if (frames_->submitted() > frames_->completed())
	{
		uploader_->retire(buffer, memory, frames_->submitted());
		return;
	}

	// Every frame submitted is drawn, so nothing in flight can still be reading this buffer.
	if (buffer != VK_NULL_HANDLE)
	{
		vkDestroyBuffer(device_, buffer, nullptr);
	}
	if (memory != VK_NULL_HANDLE)
	{
		vkFreeMemory(device_, memory, nullptr);
	}
}

VkCommandBuffer VulkanRenderer::begin_single_time_commands() const
{
	return begin_one_time_commands(device_, command_pool_);
}

void VulkanRenderer::end_single_time_commands(VkCommandBuffer command_buffer)
{
	check_vk_result(vkEndCommandBuffer(command_buffer), "vkEndCommandBuffer");

	VkSubmitInfo submit_info;
	std::memset(&submit_info, 0, sizeof(submit_info));
	submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit_info.commandBufferCount = 1;
	submit_info.pCommandBuffers = &command_buffer;

	check_vk_result(vkQueueSubmit(graphics_queue_, 1, &submit_info, VK_NULL_HANDLE), "vkQueueSubmit");
	++single_time_submit_count_;
	check_vk_result(vkQueueWaitIdle(graphics_queue_), "vkQueueWaitIdle");

	vkFreeCommandBuffers(device_, command_pool_, 1, &command_buffer);

	// the queue is idle: every frame submitted is drawn
	frames_->retire_all();
}

void VulkanRenderer::transition_image_layout(VkImage image, VkImageLayout old_layout,
	VkImageLayout new_layout)
{
	VkCommandBuffer command_buffer = begin_single_time_commands();
	record_image_layout_transition(command_buffer, image, old_layout, new_layout);

	end_single_time_commands(command_buffer);
}

void VulkanRenderer::do_batch_download_textures(std::vector<TexturePtr> const &textures)
{
	std::vector<VulkanTexture *> batch;
	for (TexturePtr const &texture : textures)
	{
		VulkanTexture *const vulkan_texture = dynamic_cast<VulkanTexture *>(texture.get());
		if (!vulkan_texture || vulkan_texture->device() != device_)
		{
			throw std::invalid_argument("batch_download_textures: not a texture of this renderer");
		}
		vulkan_texture->check_downloadable();
		if (vulkan_texture->written())
			batch.push_back(vulkan_texture);
	}
	if (batch.empty())
	{
		return;
	}
	// one submit and one wait for them all
	VkCommandBuffer const command_buffer = begin_single_time_commands();
	for (VulkanTexture const *texture : batch)
	{
		texture->record_download(command_buffer);
	}
	end_single_time_commands(command_buffer);
	for (VulkanTexture *texture : batch)
	{
		texture->finish_download();
	}
}

void VulkanRenderer::do_batch_upload_textures(std::vector<TexturePtr> const &textures)
{
	std::vector<VulkanTexture *> batch;
	for (TexturePtr const &texture : textures)
	{
		VulkanTexture *const vulkan_texture = dynamic_cast<VulkanTexture *>(texture.get());
		if (!vulkan_texture || vulkan_texture->device() != device_)
		{
			throw std::invalid_argument("batch_upload_textures: not a texture of this renderer");
		}
		if (std::ranges::find(batch, vulkan_texture) != batch.end())
		{
			throw std::invalid_argument("batch_upload_textures: a texture twice");
		}
		vulkan_texture->check_uploadable();
		batch.push_back(vulkan_texture);
	}
	if (batch.empty())
	{
		return;
	}
	// one submit and one wait for them all
	VkCommandBuffer const command_buffer = begin_single_time_commands();
	for (VulkanTexture *texture : batch)
	{
		texture->record_upload(command_buffer);
	}
	end_single_time_commands(command_buffer);
	for (VulkanTexture *texture : batch)
	{
		texture->finish_upload();
	}
}

void VulkanRenderer::do_stream_textures(std::vector<TexturePtr> const &textures)
{
	std::vector<VulkanTexture *> batch;
	for (TexturePtr const &texture : textures)
	{
		VulkanTexture *const vulkan_texture = dynamic_cast<VulkanTexture *>(texture.get());
		if (!vulkan_texture || vulkan_texture->device() != device_)
		{
			throw std::invalid_argument("stream_textures: not a texture of this renderer");
		}
		if (std::ranges::find(batch, vulkan_texture) != batch.end())
		{
			throw std::invalid_argument("stream_textures: a texture twice");
		}
		vulkan_texture->check_streamable();
		batch.push_back(vulkan_texture);
	}
	if (batch.empty())
	{
		return;
	}
	if (!in_frame())
	{
		do_batch_upload_textures(textures);               // no frame to ride in
		return;
	}
	prepare_current();      // (the staging arena is the frame's: free once its last frame is drawn)
	for (VulkanTexture *texture : batch)
	{
		VulkanUploader::Staged const staged = uploader_->stage(texture->copy_data(), texture->copy_size());
		texture->record_stream(active_command_buffer_, staged.buffer, staged.offset);
	}
}

uint32_t VulkanRenderer::update_joint_matrices(std::vector<rove::matrix<4,4>> const &joint_matrices)
{
	if (joint_matrices.empty())
	{
		throw std::invalid_argument("a skinned draw without joint matrices");
	}
	if (joint_matrices.size() > MAX_JOINT_MATRICES)
	{
		throw std::length_error(std::to_string(joint_matrices.size()) + " joint matrices, a draw has room for " + std::to_string(MAX_JOINT_MATRICES));
	}
	return joint_matrices_ubo_.write_packed(joint_matrices.data(), joint_matrices.size() * sizeof(rove::matrix<4,4>),
		"Vulkan joint matrices buffer overflow. Increase the renderer's joint_matrices or EDGE_VK_JOINT_MATRICES");
}

uint32_t VulkanRenderer::update_draw_constants(rove::matrix<4,4> const &world_view_projection,
	rove::matrix<4,4> const &world_matrix, rove::matrix<4,4> const &previous_world_view_projection,
	rove::matrix<4,4> const &previous_world_matrix)
{
	VulkanDrawConstants constants;
	constants.world_view_projection = world_view_projection;
	constants.world_matrix = world_matrix;
	constants.previous_world_view_projection = previous_world_view_projection;
	constants.previous_world_matrix = previous_world_matrix;

	return draw_constants_ubo_.write(&constants, sizeof(constants),
		"Vulkan draw constants buffer overflow. Increase EDGE_VK_DRAW_CONSTANTS_BLOCKS");
}

int VulkanRenderer::screen_width() const
{
	return screen_width_;
}

int VulkanRenderer::screen_height() const
{
	return screen_height_;
}

void VulkanRenderer::resize(int width, int height)
{
	if (width <= 0 || height <= 0)
	{
		throw std::invalid_argument("a renderer resized to " + std::to_string(width) + "x" + std::to_string(height) + " pixels");
	}
	screen_width_ = width;
	screen_height_ = height;
	EDGE_LOG_INFO("renderer resized, new dimensions are {}x{}", screen_width_, screen_height_);
	default_render_target_.reset();         // made again at the new size when asked for
}

void VulkanRenderer::attach_surface(VkSurfaceKHR surface, bool vsync)
{
	if (in_frame())
	{
		throw std::logic_error("attach_surface inside a frame");
	}
	presenter_->attach(surface, vsync);
}

void VulkanRenderer::detach_surface()
{
	if (in_frame())
	{
		throw std::logic_error("detach_surface inside a frame");
	}
	presenter_->detach();
}

void VulkanRenderer::set_vsync(bool vsync)
{
	if (in_frame())
	{
		throw std::logic_error("set_vsync inside a frame");
	}
	presenter_->set_vsync(vsync);
}

bool VulkanRenderer::vsync() const
{
	return presenter_->vsync();
}

bool VulkanRenderer::surface_lost() const
{
	return presenter_->surface_lost();
}

bool VulkanRenderer::present(TexturePtr const &frame, int width, int height)
{
	if (in_frame())
	{
		throw std::logic_error("present inside a frame");
	}
	return presenter_->present(frame, width, height);
}

void VulkanRenderer::do_begin_frame()
{
	prepare_current();
	reclaim_material_descriptor_sets();

	++frame_serial_;
	active_command_buffer_ = begin_single_time_commands();
	frames_->begin(active_command_buffer_);

	// Uploads issued while the renderer was idle (model loading, animation
	// updates between frames) ride along in this frame's command buffer, ahead
	// of every pass that could read them.
	uploader_->flush(active_command_buffer_);

	gpu_timer_->begin_frame(active_command_buffer_, frames_->current());
}

void VulkanRenderer::prepare_current()
{
	if (current_ready_)
	{
		return;
	}
	uint32_t const current = frames_->current();
	frames_->wait(current);
	joint_matrices_ubo_.use_region(current);
	instances_ubo_.use_region(current);
	material_parameters_ubo_.use_region(current);
	{
		unsigned char const zeros[Material::PARAMETERS_SIZE] = {};
		material_zero_offset_ = material_parameters_ubo_.write(zeros, sizeof(zeros),
			"Vulkan material parameters buffer overflow. Increase EDGE_VK_MATERIAL_PARAMETER_BLOCKS");
	}
	draw_constants_ubo_.use_region(current);
	frame_constants_ubo_.use_region(current);
	uploader_->use_arena(current);
	current_ready_ = true;
}

void VulkanRenderer::do_begin_pass(RenderTargetPtr const &target, RenderPassClearState const &clear_state, uint32_t pass_type)
{
	VulkanRenderTarget *vulkan_target = dynamic_cast<VulkanRenderTarget *>(target.get());
	if (!vulkan_target || vulkan_target->device_context() != device_context_)
	{
		throw std::invalid_argument("a Vulkan render pass into a target of another renderer");
	}

	VkRenderPass const selected_render_pass = vulkan_target->render_pass();
	VkFramebuffer const selected_framebuffer = vulkan_target->framebuffer();
	VkExtent2D const selected_extent = vulkan_target->extent();
	if (selected_render_pass == VK_NULL_HANDLE || selected_framebuffer == VK_NULL_HANDLE
		|| selected_extent.width == 0 || selected_extent.height == 0)
	{
		throw std::logic_error("vulkan render pass target is not fully initialized");
	}
	// every colour, every layer of it: the passes that sampled it finish reading before this one draws it
	for (TexturePtr const &texture : vulkan_target->color_textures())
	{
		VulkanTexture *const color = dynamic_cast<VulkanTexture *>(texture.get());
		if (!color)
		{
			throw std::logic_error("offscreen Vulkan render target requires VulkanTexture color attachment");
		}
		if (color->current_layout() != VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
		{
			record_image_layout_transition(active_command_buffer_, color->image(), color->current_layout(),
				VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
			color->set_current_layout(VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
		}
	}
	// a readable depth: the passes that sampled it finish reading before this one clears it
	if (VulkanTexture *const depth = dynamic_cast<VulkanTexture *>(vulkan_target->depth_texture().get()))
	{
		record_depth_layout_transition(active_command_buffer_, depth->image(), depth->vk_format(), depth->current_layout(),
			VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
		depth->set_current_layout(VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
	}

	// every colour cleared to the clear colour, then the depth
	size_t const colors = vulkan_target->color_textures_count();
	std::vector<VkClearValue> clear_values(colors + 1);
	for (size_t i = 0; i < colors; ++i)
	{
		std::memset(&clear_values[i], 0, sizeof(clear_values[i]));
		clear_values[i].color.float32[0] = clear_state.clear_color_r;
		clear_values[i].color.float32[1] = clear_state.clear_color_g;
		clear_values[i].color.float32[2] = clear_state.clear_color_b;
		clear_values[i].color.float32[3] = clear_state.clear_color_a;
	}
	std::memset(&clear_values[colors], 0, sizeof(clear_values[colors]));
	clear_values[colors].depthStencil.depth = clear_state.clear_depth_value;
	clear_values[colors].depthStencil.stencil = clear_state.clear_stencil;

	VkRenderPassBeginInfo render_pass_info;
	std::memset(&render_pass_info, 0, sizeof(render_pass_info));
	render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	render_pass_info.renderPass = selected_render_pass;
	render_pass_info.framebuffer = selected_framebuffer;
	render_pass_info.renderArea.offset.x = 0;
	render_pass_info.renderArea.offset.y = 0;
	render_pass_info.renderArea.extent = selected_extent;
	render_pass_info.clearValueCount = static_cast<uint32_t>(clear_values.size());
	render_pass_info.pClearValues = clear_values.data();

	gpu_timer_->begin_pass(active_command_buffer_);

	vkCmdBeginRenderPass(active_command_buffer_, &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);

	VkViewport viewport;
	std::memset(&viewport, 0, sizeof(viewport));
	// Y-flip: map OpenGL Y-up NDC convention to Vulkan screen space.
	// VK NDC Y=-1 = top, Y=+1 = bottom; by setting y=height and height=-height,
	// we invert the mapping so Y=+1 in clip space goes to the top of the image.
	// Core since Vulkan 1.1 (VK_KHR_maintenance1).
	viewport.x = 0.0f;
	viewport.y = static_cast<float>(selected_extent.height);
	viewport.width = static_cast<float>(selected_extent.width);
	viewport.height = -static_cast<float>(selected_extent.height);
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;
	vkCmdSetViewport(active_command_buffer_, 0, 1, &viewport);

	VkRect2D scissor;
	std::memset(&scissor, 0, sizeof(scissor));
	scissor.offset.x = 0;
	scissor.offset.y = 0;
	scissor.extent = selected_extent;
	vkCmdSetScissor(active_command_buffer_, 0, 1, &scissor);

	active_pass_render_pass_ = selected_render_pass;
	active_pass_color_attachments_ = static_cast<uint32_t>(colors);
}

void VulkanRenderer::do_end_pass()
{
	RenderTargetPtr const &finished_target = pass_target();
	vkCmdEndRenderPass(active_command_buffer_);

	gpu_timer_->end_pass(active_command_buffer_);

	VulkanRenderTarget *vulkan_target = dynamic_cast<VulkanRenderTarget *>(finished_target.get());
	if (vulkan_target)
	{
		// every colour (every layer of it), for the passes after it to sample
		for (TexturePtr const &texture : vulkan_target->color_textures())
		{
			VulkanTexture *const offscreen_texture = dynamic_cast<VulkanTexture *>(texture.get());
			if (!offscreen_texture)
			{
				throw std::logic_error("offscreen Vulkan render target requires VulkanTexture color attachment");
			}
			if (offscreen_texture->current_layout() != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
			{
				record_image_layout_transition(active_command_buffer_,
					offscreen_texture->image(),
					offscreen_texture->current_layout(),
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
				offscreen_texture->set_current_layout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
			}
		}
		// the depth the pass kept, for the passes after it to sample
		if (VulkanTexture *const depth = dynamic_cast<VulkanTexture *>(vulkan_target->depth_texture().get()))
		{
			record_depth_layout_transition(active_command_buffer_, depth->image(), depth->vk_format(),
				VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
			depth->set_current_layout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		}
	}

	active_pass_render_pass_ = VK_NULL_HANDLE;
}

void VulkanRenderer::do_finish_frame()
{
	do_submit_frame();
	// every frame drawn: the timestamps have resolved, the staging space can be reused, and buffers orphaned mid-frame
	// are released
	wait_submitted();
}

void VulkanRenderer::do_submit_frame()
{
	// Nothing inside a frame is deferred (an upload goes into the frame's command buffer), so nothing may be left pending.
	if (uploader_->has_pending())
	{
		throw std::logic_error("vulkan buffer uploads pending at frame end");
	}

	frames_->submit(graphics_queue_, frame_serial_);
	++single_time_submit_count_;

	active_command_buffer_ = VK_NULL_HANDLE;
	active_pass_render_pass_ = VK_NULL_HANDLE;

	// the next frame is written into the next share, waited for when the host first writes into it
	current_ready_ = false;
}

void VulkanRenderer::wait_frames()
{
	if (in_frame())
	{
		throw std::logic_error("wait_frames inside a frame: the frame open is not submitted");
	}
	wait_submitted();
}

void VulkanRenderer::wait_submitted()
{
	frames_->wait_all();
	presenter_->wait();
}

void VulkanRenderer::ensure_default_texture()
{
	if (default_texture_)
	{
		return;
	}

	default_texture_ = create_texture(1, 1, PixelFormat::A8R8G8B8);
	auto texels = default_texture_->write(Contents::DISCARD);
	texels.as<unsigned>().data()[0] = 0xFFFFFFFFu;
	texels.unmap();
}

VkPipeline VulkanRenderer::pipeline(VertexTraits const *traits, MaterialPtr const &material)
{
	if (!in_pass() || active_pass_render_pass_ == VK_NULL_HANDLE)
	{
		throw std::logic_error("vulkan pipeline lookup requires an active render pass");
	}

	return pipeline_builder_.pipeline(device_, descriptors_->pipeline_layout(),
		active_pass_render_pass_, active_pass_color_attachments_, pass_type(), traits, material);
}

void VulkanRenderer::destroy_pipelines()
{
	pipeline_builder_.destroy_all(device_);
}

VertexBufferPtr VulkanRenderer::create_vertex_buffer(VertexTraits const *vertex_traits, size_t size, BufferUsage)
{
	return VertexBufferPtr(new VulkanVertexBuffer(shared_from_this(), size, vertex_traits));
}

IndexBufferPtr VulkanRenderer::create_index_buffer(IndexFormat index_format, size_t size, BufferUsage)
{
	return IndexBufferPtr(new VulkanIndexBuffer(shared_from_this(), index_format, size));
}

TexturePtr VulkanRenderer::create_texture(int width, int height, PixelFormat pixel_format,
	TextureUsage const &usage)
{
	return std::make_shared<VulkanTexture>(this, width, height, pixel_format, usage);
}

void VulkanRenderer::do_set_viewport(int x, int y, int width, int height)
{
	// the same Y flip as begin_pass, over the rectangle
	VkViewport viewport;
	std::memset(&viewport, 0, sizeof(viewport));
	viewport.x = static_cast<float>(x);
	viewport.y = static_cast<float>(y + height);
	viewport.width = static_cast<float>(width);
	viewport.height = -static_cast<float>(height);
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;
	vkCmdSetViewport(active_command_buffer_, 0, 1, &viewport);
	VkRect2D scissor;
	scissor.offset.x = x;
	scissor.offset.y = y;
	scissor.extent.width = static_cast<uint32_t>(width);
	scissor.extent.height = static_cast<uint32_t>(height);
	vkCmdSetScissor(active_command_buffer_, 0, 1, &scissor);
}

void VulkanRenderer::do_set_frame_constants(void const *data, size_t size)
{
	std::vector<unsigned char> block(FRAME_CONSTANTS_SIZE, 0);
	if (size)
	{
		std::memcpy(block.data(), data, size);
	}
	prepare_current();
	frame_constants_ubo_.write_static(block.data(), block.size());
}

void VulkanRenderer::do_set_global_texture(unsigned slot, TexturePtr const &texture, bool compare)
{
	VulkanTexture const *const vulkan_texture = dynamic_cast<VulkanTexture const *>(texture.get());
	if (texture && (!vulkan_texture || vulkan_texture->device() != device_))
	{
		throw std::invalid_argument("a global texture must be a texture of this renderer");
	}
	global_textures_[slot] = texture;
	global_compare_[slot] = compare;
}

RenderTargetPtr VulkanRenderer::default_render_target()
{
	// made when first asked for: an application that draws into targets of its own does not pay for a screen-sized one
	if (!default_render_target_)
	{
		default_render_target_ = create_render_target(screen_width_, screen_height_, PixelFormat::A8R8G8B8);
	}
	return default_render_target_;
}

unsigned VulkanRenderer::max_color_attachments() const
{
	VkPhysicalDeviceProperties properties;
	vkGetPhysicalDeviceProperties(physical_device_, &properties);
	return std::min(properties.limits.maxColorAttachments, properties.limits.maxFragmentOutputAttachments);
}

RenderTargetPtr VulkanRenderer::do_create_render_target(int width, int height, RenderTargetFormat const &format)
{
	VkPhysicalDeviceProperties properties;
	vkGetPhysicalDeviceProperties(physical_device_, &properties);
	if (format.layers > 0 && static_cast<uint32_t>(format.layers) > properties.limits.maxImageArrayLayers)
	{
		throw std::invalid_argument("a render target of " + std::to_string(format.layers) + " layers: this device has at most "
			+ std::to_string(properties.limits.maxImageArrayLayers) + " layers (maxImageArrayLayers)");
	}
	if (format.cube && static_cast<uint32_t>(width) > properties.limits.maxImageDimensionCube)
	{
		throw std::invalid_argument("a cube render target of " + std::to_string(width) + " pixel faces: this device's are at most "
			+ std::to_string(properties.limits.maxImageDimensionCube) + " (maxImageDimensionCube)");
	}
	// every colour one the device draws into, before any image is made
	for (PixelFormat pixel_format : format.color_formats)
	{
		VkFormat const vk_format = VulkanTexture::vk_format(pixel_format);
		VkFormatProperties format_properties;
		vkGetPhysicalDeviceFormatProperties(physical_device_, vk_format, &format_properties);
		if (!(format_properties.optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT))
		{
			throw std::invalid_argument("a render target's colour of pixel format " + to_string(pixel_format) + " (VkFormat "
				+ std::to_string(vk_format) + "): this device cannot draw into it (no VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT)");
		}
	}
	VulkanTexture::Layers layers;
	layers.count = format.cube ? 6 : format.layers;
	layers.cube = format.cube;
	std::vector<TexturePtr> color_textures;
	for (PixelFormat pixel_format : format.color_formats)
	{
		color_textures.push_back(layers.count ? std::make_shared<VulkanTexture>(this, width, height, pixel_format, layers)
			: std::make_shared<VulkanTexture>(this, width, height, pixel_format));
	}
	TexturePtr depth_texture = format.readable_depth ? std::make_shared<VulkanTexture>(this, width, height, VulkanTexture::Depth(), layers)
		: TexturePtr();
	return RenderTargetPtr(new VulkanRenderTarget(this, width, height, color_textures, depth_texture));
}

MaterialPtr VulkanRenderer::create_material()
{
	return std::make_shared<Material>(shared_from_this());
}

void VulkanRenderer::do_draw(DrawCall const &call)
{
	VulkanVertexBuffer *const vertex_buffer = dynamic_cast<VulkanVertexBuffer *>(call.vertex_buffer().get());
	if (!vertex_buffer)
	{
		throw std::invalid_argument("a Vulkan draw of a vertex buffer of another renderer");
	}
	VkCommandBuffer const command_buffer = active_command_buffer();
	VertexTraits const *const traits = vertex_buffer->vertex_traits();

	vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline(traits, call.material()));
	VkDescriptorSet const descriptor_set = material_descriptor_set(call.material());
	VkBuffer const vertex_buffers[] = {vertex_buffer->get()};
	VkDeviceSize const offsets[] = {0};
	vkCmdBindVertexBuffers(command_buffer, 0, 1, vertex_buffers, offsets);

	std::vector<rove::matrix<4,4>> const *const joint_matrices = call.joint_matrices();
	std::optional<Blocks> joint_blocks;      // none: not skinned
	if (joint_matrices)
	{
		std::vector<rove::matrix<4,4>> const &previous = *call.previous_joint_matrices();
		uint32_t const offset = update_joint_matrices(*joint_matrices);
		joint_blocks = Blocks{offset, same_matrices(previous.data(), joint_matrices->data(), joint_matrices->size()) ? offset
			: update_joint_matrices(previous)};
	}
	uint32_t const draw_constants_offset = update_draw_constants(call.shader_world_view_projection(), call.shader_world(),
		call.shader_previous_world_view_projection(), call.shader_previous_world());
	std::vector<unsigned char> const &parameters = call.material()->parameters();
	uint32_t const material_parameters_offset = parameters.empty() ? material_zero_offset_
		: material_parameters_ubo_.write(parameters.data(), parameters.size(),
			"Vulkan material parameters buffer overflow. Increase EDGE_VK_MATERIAL_PARAMETER_BLOCKS");
	shader::PushConstants const push_constants = shader::push_constants(*call.material(), *traits, call.is_uber(),
		joint_matrices != nullptr, call.camera()->eye_point(), call.is_instanced(), !call.has_previous());
	vkCmdPushConstants(command_buffer, pipeline_layout(), VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
		sizeof(push_constants), &push_constants);

	VulkanIndexBuffer *index_buffer = nullptr;
	if (call.index_buffer())
	{
		index_buffer = dynamic_cast<VulkanIndexBuffer *>(call.index_buffer().get());
		if (!index_buffer)
		{
			throw std::invalid_argument("a Vulkan draw of an index buffer of another renderer");
		}
		vkCmdBindIndexBuffer(command_buffer, index_buffer->get(), 0, index_buffer->index_type());
	}

	// one draw, or the instances in batches of MAX_INSTANCES, each its own block of matrices
	size_t const instances = call.instance_count();
	for (size_t first = 0; first < instances; first += MAX_INSTANCES)
	{
		size_t const batch = std::min(MAX_INSTANCES, instances - first);
		std::optional<Blocks> instance_blocks;
		if (call.is_instanced())
		{
			rove::matrix<4,4> const *const current = call.visible_instances().data() + first;
			rove::matrix<4,4> const *const previous = call.previous_visible_instances().data() + first;
			char const *const overflow = "Vulkan instances buffer overflow. Increase EDGE_VK_INSTANCE_BLOCKS";
			uint32_t const offset = instances_ubo_.write(current, batch * sizeof(rove::matrix<4,4>), overflow);
			instance_blocks = Blocks{offset, same_matrices(previous, current, batch) ? offset
				: instances_ubo_.write(previous, batch * sizeof(rove::matrix<4,4>), overflow)};
		}
		bind_descriptor_set(command_buffer, descriptor_set, joint_blocks, instance_blocks, draw_constants_offset,
			material_parameters_offset);
		if (index_buffer)
		{
			vkCmdDrawIndexed(command_buffer, static_cast<uint32_t>(call.triangles_count() * 3), static_cast<uint32_t>(batch),
				static_cast<uint32_t>(call.first_index_offset()), 0, 0);
		}
		else
		{
			vkCmdDraw(command_buffer, static_cast<uint32_t>(call.triangles_count() * 3), static_cast<uint32_t>(batch),
				static_cast<uint32_t>(call.minimal_vertex_index()), 0);
		}
	}
}

}
