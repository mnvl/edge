// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include <cstring>
#include <stdexcept>
#include <string>

#include "vulkan_error.h"
#include "vulkan_memory.h"
#include "vulkan_renderer.h"
#include "vulkan_vertex_buffer.h"

namespace edge
{

namespace
{

}

VulkanVertexBuffer::VulkanVertexBuffer(VulkanRendererPtr const &renderer, size_t size, VertexTraits const *traits):
	context_(renderer->device_context()),
	buffer_(VK_NULL_HANDLE),
	memory_(VK_NULL_HANDLE),
	memory_properties_(0),
	clone_(size),
	traits_(traits)
{
	VkDeviceSize const allocation_size = size > 0 ? static_cast<VkDeviceSize>(size) : 1;

	// The GPU reads vertex attributes for every vertex of every draw of every
	// pass.  In host memory that traffic crosses PCIe each time — on a discrete
	// GPU it dominates the frame and is entirely resolution-independent — so
	// the buffer the GPU reads lives in device-local memory, written through
	// the renderer's staging arena by do_unlock() (the end of a write()).
	memory_properties_ = create_buffer(context_->device(), context_->physical_device(), allocation_size,
		VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0,
		buffer_, memory_);
}

VulkanVertexBuffer::~VulkanVertexBuffer()
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

const void *VulkanVertexBuffer::do_lock() const
{
	return clone_.empty() ? nullptr : &clone_[0];
}

void VulkanVertexBuffer::do_unlock() const
{
}

void *VulkanVertexBuffer::do_lock(bool discard)
{
	(void)discard;
	return clone_.empty() ? nullptr : &clone_[0];
}

void VulkanVertexBuffer::do_unlock()
{
	if (clone_.empty())
	{
		return;
	}

	context_->require_renderer().upload_to_buffer(buffer_, &clone_[0], clone_.size());
}

size_t VulkanVertexBuffer::size() const
{
	return clone_.size();
}





}
