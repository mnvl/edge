// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

#include <vulkan/vulkan.h>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace edge
{

/// Moves bytes from the host into device-local buffers, and holds back whatever the GPU may still be reading until
/// the frame that reads it is drawn.
///
/// The bytes are staged through a frame-scoped arena, never a per-buffer scratch area: the host would otherwise
/// overwrite bytes belonging to a copy that is recorded but not yet executed, and earlier draws in the frame would
/// silently render the newer geometry.  There is an arena for each frame the renderer keeps in flight: the host stages
/// into the one of the frame it builds (use_arena) while the GPU copies from the others.  An arena is rewound by
/// rewind_arena(), once its frame is drawn; its blocks are never freed early, since a copy recorded in that frame still
/// points into one.  Rewinding gives back the blocks nothing used since the arena was last rewound: what a load staged
/// goes a frame after it was copied, what every frame uses stays.
///
/// Where a copy is recorded depends on the renderer's state, which the renderer knows and this class does not: in the
/// frame's command buffer with the two barriers (record_upload), or deferred (defer) while the renderer is idle and
/// flushed into the next frame's command buffer.  Inside a render pass, where copies are illegal, the renderer refuses one.
class VulkanUploader {
public:
	/// arenas: one for each frame in flight (at least one).
	VulkanUploader(VkDevice device, VkPhysicalDevice physical_device, uint32_t arenas = 1);
	/// The GPU must be idle.  Pending uploads are dropped.
	~VulkanUploader();
	VulkanUploader(VulkanUploader const &) = delete;
	VulkanUploader &operator=(VulkanUploader const &) = delete;

	/// Bytes copied into the arena, where a copy can read them from.
	struct Staged
	{
		VkBuffer buffer;
		VkDeviceSize offset;
		VkDeviceSize size;
	};
	/// Into the current arena; size must be non-zero.
	Staged stage(void const *data, size_t size);
	/// The arena staged into from now on: the one of the frame the host builds.
	void use_arena(uint32_t arena);

	/// Both barriers around the copy: the first stops it from overtaking draws already recorded in this command buffer
	/// that still read the old contents, the second makes the new contents visible to the draws that follow.
	static void record_upload(VkCommandBuffer command_buffer, Staged const &staged, VkBuffer destination);

	/// For the next frame's command buffer.  At most one per destination: a repeated one replaces the earlier, since
	/// only the newest bytes matter to a frame that has not started yet.
	void defer(VkBuffer destination, Staged const &staged);
	/// Drops what is deferred for destination; before the buffer is destroyed, so no copy names a dead handle.
	void cancel(VkBuffer destination);
	/// Records every deferred copy into command_buffer (outside a render pass), then one barrier making them visible
	/// to vertex and index fetch.
	void flush(VkCommandBuffer command_buffer);

	/// A buffer that frames up to `frame` may still name; destroyed by collect() once that frame is drawn.
	void retire(VkBuffer buffer, VkDeviceMemory memory, uint64_t frame);
	/// Destroys what was retired for frames up to `completed`, the last the GPU is known to have finished.
	void collect(uint64_t completed);

	/// Once the frame that staged into `arena` is drawn: its space can be used again.
	void rewind_arena(uint32_t arena);
	/// Uploads deferred and not yet flushed into a frame.
	bool has_pending() const { return !pending_.empty(); }

	/// The bytes of staging space held, used or not.
	VkDeviceSize size() const;

private:
	/// The bare copy into destination; the caller orders it.
	static void record_copy(VkCommandBuffer command_buffer, Staged const &staged, VkBuffer destination);
	struct Block
	{
		VkBuffer buffer;
		VkDeviceMemory memory;
		char *mapped;
		VkDeviceSize capacity;
		VkDeviceSize used;
	};
	struct Pending
	{
		Staged staged;
		VkBuffer destination;
	};
	struct Retired
	{
		VkBuffer buffer;
		VkDeviceMemory memory;
		uint64_t frame;
	};

	/// In the smallest block with room, or a new one: host-visible and coherent, as the host only ever writes it.
	char *allocate(VkDeviceSize size, VkBuffer &out_buffer, VkDeviceSize &out_offset);
	void destroy_block(Block const &block);
	void destroy(Retired const &retired);
	static void record_pre_upload_barrier(VkCommandBuffer command_buffer);
	static void record_post_upload_barrier(VkCommandBuffer command_buffer);

	VkDevice device_;
	VkPhysicalDevice physical_device_;
	std::vector<std::vector<Block>> arenas_;
	uint32_t current_ = 0;
	std::vector<Pending> pending_;
	std::vector<Retired> retired_;
};

}
