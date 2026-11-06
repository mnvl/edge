// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include <cstring>
#include <stdexcept>
#include <string>

#include "index.h"
#include "vulkan_error.h"
#include "vulkan_index_buffer.h"
#include "vulkan_memory.h"
#include "vulkan_renderer.h"

namespace edge
{

namespace
{

VkIndexType resolve_vk_index_type(IndexFormat index_format)
{
	switch (index_format)
	{
	case IndexFormat::U16:
		return VK_INDEX_TYPE_UINT16;
	case IndexFormat::U32:
		return VK_INDEX_TYPE_UINT32;
	default:
		throw std::runtime_error("unsupported Vulkan index format");
	}
}

}

VulkanIndexBuffer::VulkanIndexBuffer(VulkanRendererPtr const &renderer, IndexFormat index_format, size_t size):
	context_(renderer->device_context()),
	index_format_(index_format),
	index_type_(resolve_vk_index_type(index_format)),
	buffer_(VK_NULL_HANDLE),
	memory_(VK_NULL_HANDLE),
	memory_properties_(0),
	clone_(size)
{
	VkDeviceSize const allocation_size = size > 0 ? static_cast<VkDeviceSize>(size) : 1;

	// Indices are fetched per vertex just like attributes, so they belong in
	// device-local memory for the same reason; see VulkanVertexBuffer.
	memory_properties_ = create_buffer(context_->device(), context_->physical_device(), allocation_size,
		VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0,
		buffer_, memory_);
}

VulkanIndexBuffer::~VulkanIndexBuffer()
{
	if (context_->has_renderer())
	{
		// a deferred upload still holds both handles, and a frame the GPU has not drawn may read them: the renderer lets them go
		VulkanRenderer &renderer = context_->require_renderer();
		renderer.cancel_buffer_uploads(buffer_);
		renderer.destroy_buffer(buffer_, memory_);
	}
	else
	{
		// the renderer is gone, and with it every frame and upload (it waited for the device to be idle): only the device is left
		vkDestroyBuffer(context_->device(), buffer_, nullptr);
		vkFreeMemory(context_->device(), memory_, nullptr);
	}
	buffer_ = VK_NULL_HANDLE;
	memory_ = VK_NULL_HANDLE;
}

const void *VulkanIndexBuffer::do_lock() const
{
	return clone_.empty() ? nullptr : &clone_[0];
}

void VulkanIndexBuffer::do_unlock() const
{
}

void *VulkanIndexBuffer::do_lock(bool discard)
{
	(void)discard;
	return clone_.empty() ? nullptr : &clone_[0];
}

void VulkanIndexBuffer::do_unlock()
{
	if (clone_.empty())
	{
		return;
	}

	context_->require_renderer().upload_to_buffer(buffer_, &clone_[0], clone_.size());
}

size_t VulkanIndexBuffer::size() const
{
	return clone_.size();
}



}
