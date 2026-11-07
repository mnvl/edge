// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

#include <vulkan/vulkan.h>
#include <vector>

#include "index_buffer.h"
#include "vulkan_device_context.h"

namespace edge
{

class VulkanIndexBuffer: public IndexBuffer
{
	friend class VulkanRenderer;

public:
	~VulkanIndexBuffer() override;

	size_t size() const override;


	VkBuffer get() const { return buffer_; }
	VkDevice device() const { return context_->device(); }
	VkIndexType index_type() const { return index_type_; }
	IndexFormat index_format() const { return index_format_; }
	size_t index_size() const override { return index_format_ == IndexFormat::U16 ? 2 : 4; }

	/// Property flags of the memory backing the buffer the GPU reads from.
	VkMemoryPropertyFlags memory_properties() const { return memory_properties_; }

protected:
	const void *do_lock() const override;
	void do_unlock() const override;
	void *do_lock(bool discard) override;
	void do_unlock() override;

private:
	VulkanDeviceContextPtr context_;     // the device, not the renderer: a buffer kept after it can still be let go
	IndexFormat index_format_;
	VkIndexType index_type_;
	VkBuffer buffer_;
	VkDeviceMemory memory_;
	VkMemoryPropertyFlags memory_properties_;
	std::vector<char> clone_;

	VulkanIndexBuffer(VulkanRendererPtr const &renderer, IndexFormat index_format, size_t size);
};

}
