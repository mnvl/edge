// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include "vulkan_uploader.h"

#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

#include "log.h"
#include "vulkan_error.h"
#include "vulkan_memory.h"

namespace edge
{

VulkanUploader::VulkanUploader(VkDevice device, VkPhysicalDevice physical_device, uint32_t arenas):
	device_(device),
	physical_device_(physical_device),
	arenas_(arenas)
{
	if (arenas == 0)
	{
		throw std::invalid_argument("an uploader with no arena");
	}
}

VulkanUploader::~VulkanUploader()
{
	for (std::vector<Block> &arena : arenas_)
	{
		for (Block &block : arena)
		{
			destroy_block(block);
		}
	}
	for (Retired const &retired : retired_)
	{
		destroy(retired);
	}
}

void VulkanUploader::destroy_block(Block const &block)
{
	vkUnmapMemory(device_, block.memory);
	vkDestroyBuffer(device_, block.buffer, nullptr);
	vkFreeMemory(device_, block.memory, nullptr);
}

VkDeviceSize VulkanUploader::size() const
{
	VkDeviceSize total = 0;
	for (std::vector<Block> const &arena : arenas_)
	{
		for (Block const &block : arena)
		{
			total += block.capacity;
		}
	}
	return total;
}

VulkanUploader::Staged VulkanUploader::stage(void const *data, size_t size)
{
	Staged staged;
	staged.size = static_cast<VkDeviceSize>(size);
	char *mapped = allocate(staged.size, staged.buffer, staged.offset);
	std::memcpy(mapped, data, size);
	return staged;
}

void VulkanUploader::use_arena(uint32_t arena)
{
	if (arena >= arenas_.size())
	{
		throw std::logic_error("staging arena " + std::to_string(arena) + " of " + std::to_string(arenas_.size()));
	}
	current_ = arena;
}

void VulkanUploader::record_copy(VkCommandBuffer command_buffer, Staged const &staged, VkBuffer destination)
{
	VkBufferCopy region;
	std::memset(&region, 0, sizeof(region));
	region.srcOffset = staged.offset;
	region.size = staged.size;
	vkCmdCopyBuffer(command_buffer, staged.buffer, destination, 1, &region);
}

void VulkanUploader::record_upload(VkCommandBuffer command_buffer, Staged const &staged, VkBuffer destination)
{
	record_pre_upload_barrier(command_buffer);
	record_copy(command_buffer, staged, destination);
	record_post_upload_barrier(command_buffer);
}

void VulkanUploader::defer(VkBuffer destination, Staged const &staged)
{
	for (Pending &pending : pending_)
	{
		if (pending.destination == destination)
		{
			pending.staged = staged;
			return;
		}
	}
	pending_.push_back({staged, destination});
}

void VulkanUploader::cancel(VkBuffer destination)
{
	for (size_t i = 0; i < pending_.size(); )
	{
		if (pending_[i].destination == destination)
		{
			pending_.erase(pending_.begin() + i);
		}
		else
		{
			++i;
		}
	}
}

void VulkanUploader::flush(VkCommandBuffer command_buffer)
{
	if (pending_.empty())
	{
		return;
	}
	record_pre_upload_barrier(command_buffer);
	for (Pending const &pending : pending_)
	{
		record_copy(command_buffer, pending.staged, pending.destination);
	}
	pending_.clear();
	record_post_upload_barrier(command_buffer);
}

void VulkanUploader::retire(VkBuffer buffer, VkDeviceMemory memory, uint64_t frame)
{
	retired_.push_back({buffer, memory, frame});
}

void VulkanUploader::collect(uint64_t completed)
{
	std::vector<Retired> held;
	for (Retired const &retired : retired_)
	{
		if (retired.frame <= completed)
		{
			destroy(retired);
		}
		else
		{
			held.push_back(retired);
		}
	}
	retired_.swap(held);
}

void VulkanUploader::rewind_arena(uint32_t arena)
{
	if (arena >= arenas_.size())
	{
		throw std::logic_error("staging arena " + std::to_string(arena) + " of " + std::to_string(arenas_.size()));
	}
	// a block no upload since the last rewind used goes back: the space a load staged once it has been copied, while the
	// space every frame uses stays (allocate() fills the smallest block with room, so a big one is not kept busy by small uploads)
	std::vector<Block> kept;
	for (Block &block : arenas_[arena])
	{
		if (block.used > 0)
		{
			block.used = 0;
			kept.push_back(block);
		}
		else
		{
			destroy_block(block);
		}
	}
	arenas_[arena].swap(kept);
}

void VulkanUploader::destroy(Retired const &retired)
{
	if (retired.buffer != VK_NULL_HANDLE)
	{
		vkDestroyBuffer(device_, retired.buffer, nullptr);
	}
	if (retired.memory != VK_NULL_HANDLE)
	{
		vkFreeMemory(device_, retired.memory, nullptr);
	}
}

char *VulkanUploader::allocate(VkDeviceSize size, VkBuffer &out_buffer, VkDeviceSize &out_offset)
{
	VkDeviceSize const ALIGNMENT = 16;
	VkDeviceSize const aligned_size = (size + ALIGNMENT - 1) & ~(ALIGNMENT - 1);

	std::vector<Block> &blocks = arenas_[current_];
	Block *fit = nullptr;
	for (Block &block : blocks)
	{
		if (block.capacity - block.used >= aligned_size && (!fit || block.capacity < fit->capacity))
		{
			fit = &block;
		}
	}
	if (fit)
	{
		out_buffer = fit->buffer;
		out_offset = fit->used;
		char *mapped = fit->mapped + fit->used;
		fit->used += aligned_size;
		return mapped;
	}

	VkDeviceSize const MINIMUM_BLOCK_SIZE = 1u << 20;
	Block block;
	block.capacity = aligned_size > MINIMUM_BLOCK_SIZE ? aligned_size : MINIMUM_BLOCK_SIZE;
	block.used = 0;

	create_buffer(device_, physical_device_, block.capacity,
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 0,
		block.buffer, block.memory);

	void *mapped = nullptr;
	check_vk_result(vkMapMemory(device_, block.memory, 0, block.capacity, 0, &mapped),
		"vkMapMemory(staging_arena)");
	block.mapped = static_cast<char *>(mapped);

	block.used = aligned_size;
	blocks.push_back(block);
	EDGE_LOG_DEBUG("Vulkan staging block {} of arena {}: {} KB ({} KB in all)", blocks.size(), current_, block.capacity >> 10, this->size() >> 10);

	out_buffer = block.buffer;
	out_offset = 0;
	return block.mapped;
}

void VulkanUploader::record_pre_upload_barrier(VkCommandBuffer command_buffer)
{
	// Write-after-read: a copy must not overtake vertex/index fetch of draws
	// recorded earlier in the same command buffer.  Without this the GPU is
	// free to run the transfer first and those draws see the new geometry.
	VkMemoryBarrier barrier;
	std::memset(&barrier, 0, sizeof(barrier));
	barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
	barrier.srcAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDEX_READ_BIT;
	barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

	vkCmdPipelineBarrier(command_buffer,
		VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
		VK_PIPELINE_STAGE_TRANSFER_BIT,
		0,
		1, &barrier,
		0, nullptr,
		0, nullptr);
}

void VulkanUploader::record_post_upload_barrier(VkCommandBuffer command_buffer)
{
	// Read-after-write: make the copied bytes visible to later vertex/index
	// fetch.
	VkMemoryBarrier barrier;
	std::memset(&barrier, 0, sizeof(barrier));
	barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
	barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDEX_READ_BIT;

	vkCmdPipelineBarrier(command_buffer,
		VK_PIPELINE_STAGE_TRANSFER_BIT,
		VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
		0,
		1, &barrier,
		0, nullptr,
		0, nullptr);
}

}
