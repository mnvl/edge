// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

#include <vulkan/vulkan.h>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
#include "vulkan_error.h"
#include "vulkan_memory.h"

namespace edge
{

/// A uniform buffer of blocks bound with a dynamic offset, cut into regions: one for each frame the renderer keeps in flight, so
/// that the host writes the blocks of one frame while the GPU still reads those of another.  Within the current region, write()
/// hands out a block after another (a draw's constants), rewound when the region is taken again (use_region); write_static() puts
/// the bytes every draw of the frame reads in the region's first block, and they stay: a region taken later gets them too.
///
/// Made with create_packed(), it is written with write_packed() instead: what a draw writes is laid where the last ended (rounded up
/// to the device's offset alignment), so a region holds as many bytes as it was made with, however they are cut (a skinned draw's
/// joints, not the 256 it could have).  The range a shader is bound to stays a whole block from there: each region keeps a block more
/// at its end for the last one's.
class VulkanDynamicUniformBuffer {
public:
	VulkanDynamicUniformBuffer() = default;
	~VulkanDynamicUniformBuffer() = default;

	VulkanDynamicUniformBuffer(VulkanDynamicUniformBuffer const &) = delete;
	VulkanDynamicUniformBuffer &operator=(VulkanDynamicUniformBuffer const &) = delete;

	/// block_count blocks shared out among `regions` regions (std::invalid_argument when that leaves a region none).
	void create(VkDevice device, VkPhysicalDevice physical_device,
		VkDeviceSize block_size, VkDeviceSize block_count, uint32_t regions = 1)
	{
		if (regions == 0 || block_count / regions == 0)
		{
			throw std::invalid_argument("a uniform buffer of " + std::to_string(static_cast<unsigned long long>(block_count))
				+ " blocks cannot be shared out among " + std::to_string(regions) + " frames");
		}
		VkDeviceSize const alignment = offset_alignment(physical_device);
		block_size_ = block_size;
		stride_ = round_up(block_size, alignment);
		region_size_ = stride_ * (block_count / regions);
		capacity_ = region_size_;
		packed_ = false;
		allocate(device, physical_device, regions);
	}

	/// `capacity` bytes shared out among `regions` regions for write_packed(), each write bound as a block of `block_size` bytes
	/// (std::invalid_argument when that leaves a region none).
	void create_packed(VkDevice device, VkPhysicalDevice physical_device,
		VkDeviceSize block_size, VkDeviceSize capacity, uint32_t regions = 1)
	{
		if (regions == 0 || capacity / regions == 0)
		{
			throw std::invalid_argument("a uniform buffer of " + std::to_string(static_cast<unsigned long long>(capacity))
				+ " bytes cannot be shared out among " + std::to_string(regions) + " frames");
		}
		VkDeviceSize const alignment = offset_alignment(physical_device);
		block_size_ = block_size;
		stride_ = alignment;
		capacity_ = round_up(capacity / regions, alignment);
		region_size_ = capacity_ + round_up(block_size, alignment);
		packed_ = true;
		allocate(device, physical_device, regions);
	}

	void destroy(VkDevice device)
	{
		if (mapped_ != nullptr)
		{
			vkUnmapMemory(device, memory_);
			mapped_ = nullptr;
		}
		if (buffer_ != VK_NULL_HANDLE)
		{
			vkDestroyBuffer(device, buffer_, nullptr);
			buffer_ = VK_NULL_HANDLE;
		}
		if (memory_ != VK_NULL_HANDLE)
		{
			vkFreeMemory(device, memory_, nullptr);
			memory_ = VK_NULL_HANDLE;
		}
		stride_ = 0;
		buffer_size_ = 0;
		region_size_ = 0;
		capacity_ = 0;
		packed_ = false;
		regions_ = 0;
		region_ = 0;
		next_offset_ = 0;
		block_size_ = 0;
		static_bytes_.clear();
	}

	/// Makes `region` the one written, from its first block on, with the static bytes written so far in it.  Only once the GPU
	/// has done with what the region held.
	void use_region(uint32_t region)
	{
		if (region >= regions_)
		{
			throw std::logic_error("uniform buffer region " + std::to_string(region) + " of " + std::to_string(regions_));
		}
		region_ = region;
		next_offset_ = region_offset();
		if (!static_bytes_.empty())
		{
			std::memcpy(static_cast<char *>(mapped_) + region_offset(), static_bytes_.data(), static_bytes_.size());
		}
	}

	/// Rewinds the current region.
	void reset()
	{
		next_offset_ = region_offset();
	}

	uint32_t write(void const *data, size_t data_size, std::string const &overflow_message)
	{
		if (mapped_ == nullptr || stride_ == 0 || region_size_ < stride_)
		{
			throw std::logic_error("a write to a uniform buffer that is not there");
		}
		if (packed_)
		{
			throw std::logic_error("write() to a packed uniform buffer (write_packed)");
		}
		if (data_size > stride_)
		{
			throw std::length_error(std::to_string(data_size) + " bytes for a uniform block of " + std::to_string(stride_));
		}

		if ((next_offset_ + stride_) > region_offset() + region_size_)
		{
			VkDeviceSize const used_blocks = (next_offset_ - region_offset()) / stride_;
			VkDeviceSize const total_blocks = region_size_ / stride_;
			throw std::runtime_error(
				overflow_message + " (" +
				std::to_string(static_cast<unsigned long long>(used_blocks)) + "/" +
				std::to_string(static_cast<unsigned long long>(total_blocks)) +
				" blocks used this frame)");
		}

		VkDeviceSize const offset = next_offset_;
		next_offset_ += stride_;

		char *dst = static_cast<char *>(mapped_) + offset;
		std::memset(dst, 0, static_cast<size_t>(stride_));
		std::memcpy(dst, data, data_size);
		return static_cast<uint32_t>(offset);
	}

	/// data_size bytes (at least one, at most a block) where the last write ended; std::runtime_error naming overflow_message when the
	/// region's bytes cannot hold them.
	uint32_t write_packed(void const *data, size_t data_size, std::string const &overflow_message)
	{
		if (mapped_ == nullptr || !packed_)
		{
			throw std::logic_error("write_packed() to a uniform buffer that is not there or not packed");
		}
		if (data_size == 0 || data_size > block_size_)
		{
			throw std::length_error(std::to_string(data_size) + " bytes for a uniform block of " + std::to_string(block_size_));
		}
		VkDeviceSize const used = next_offset_ - region_offset();
		if (used + data_size > capacity_)
		{
			throw std::runtime_error(overflow_message + " (" + std::to_string(static_cast<unsigned long long>(used)) + " of "
				+ std::to_string(static_cast<unsigned long long>(capacity_)) + " bytes used this frame, "
				+ std::to_string(data_size) + " more asked for)");
		}
		VkDeviceSize const offset = next_offset_;
		next_offset_ += round_up(data_size, stride_);
		std::memcpy(static_cast<char *>(mapped_) + offset, data, data_size);
		return static_cast<uint32_t>(offset);
	}

	void write_static(void const *data, size_t data_size)
	{
		if (mapped_ == nullptr || stride_ == 0)
		{
			throw std::logic_error("a write to a uniform buffer that is not there");
		}
		if (data_size > stride_)
		{
			throw std::length_error(std::to_string(data_size) + " bytes for a uniform block of " + std::to_string(stride_));
		}
		static_bytes_.assign(static_cast<char const *>(data), static_cast<char const *>(data) + data_size);
		std::memcpy(static_cast<char *>(mapped_) + region_offset(), data, data_size);
	}

	VkBuffer buffer() const { return buffer_; }
	VkDeviceSize stride() const { return stride_; }
	VkDeviceSize buffer_size() const { return buffer_size_; }
	VkDeviceSize block_size() const { return block_size_; }
	VkDeviceSize next_offset() const { return next_offset_; }
	/// Where the current region begins: the dynamic offset of its first block.
	uint32_t region_offset() const { return static_cast<uint32_t>(region_size_ * region_); }
	bool is_valid() const { return mapped_ != nullptr; }

private:
	static VkDeviceSize offset_alignment(VkPhysicalDevice physical_device)
	{
		VkPhysicalDeviceProperties props;
		std::memset(&props, 0, sizeof(props));
		vkGetPhysicalDeviceProperties(physical_device, &props);
		return props.limits.minUniformBufferOffsetAlignment > 0 ? props.limits.minUniformBufferOffsetAlignment : 1;
	}

	static VkDeviceSize round_up(VkDeviceSize size, VkDeviceSize alignment)
	{
		return (size + alignment - 1) / alignment * alignment;
	}

	void allocate(VkDevice device, VkPhysicalDevice physical_device, uint32_t regions)
	{
		regions_ = regions;
		buffer_size_ = region_size_ * regions;
		region_ = 0;
		next_offset_ = 0;
		static_bytes_.clear();

		edge::create_buffer(device, physical_device, buffer_size_, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 0, buffer_, memory_);

		VkResult result = vkMapMemory(device, memory_, 0, buffer_size_, 0, &mapped_);
		if (result != VK_SUCCESS)
		{
			throw std::runtime_error("vkMapMemory failed for VulkanDynamicUniformBuffer");
		}
		std::memset(mapped_, 0, static_cast<size_t>(buffer_size_));
	}

	VkBuffer buffer_ = VK_NULL_HANDLE;
	VkDeviceMemory memory_ = VK_NULL_HANDLE;
	void *mapped_ = nullptr;
	VkDeviceSize stride_ = 0;
	VkDeviceSize buffer_size_ = 0;
	VkDeviceSize region_size_ = 0;
	VkDeviceSize capacity_ = 0;      // the bytes of a region writes may take (all of it, but for a packed buffer's last block)
	bool packed_ = false;            // written by write_packed(): stride_ is the offset alignment
	uint32_t regions_ = 0;
	uint32_t region_ = 0;
	VkDeviceSize next_offset_ = 0;
	VkDeviceSize block_size_ = 0;
	std::vector<char> static_bytes_;     // what write_static() last wrote: a region taken later gets it too
};

}
