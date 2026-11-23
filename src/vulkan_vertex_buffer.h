// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

#include <vulkan/vulkan.h>
#include <memory>

#include "cpu_copy.h"
#include "vertex_buffer.h"
#include "vulkan_device_context.h"
#include "vertex_traits.h"

namespace edge
{

class VulkanVertexBuffer: public VertexBuffer
{
	friend class VulkanRenderer;

public:
	~VulkanVertexBuffer() override;


	size_t size() const override;


	VkBuffer get() const { return buffer_; }
	VkDevice device() const { return context_->device(); }
	VertexTraits const *vertex_traits() const override { return traits_; }

	/// Property flags of the memory backing the buffer the GPU reads from.
	/// Device-local is what keeps geometry off the PCIe bus.
	VkMemoryPropertyFlags memory_properties() const { return memory_properties_; }

protected:
	const void *do_lock() const override;
	void do_unlock() const override;
	void *do_lock(bool discard) override;
	void do_unlock() override;
	void do_share(std::shared_ptr<std::byte const> bytes) override;

private:
	std::shared_ptr<VulkanDeviceContext> context_;     // the device, not the renderer: a buffer kept after it can still be let go
	VkBuffer buffer_;
	VkDeviceMemory memory_;
	VkMemoryPropertyFlags memory_properties_;
	CpuCopy copy_;
	VertexTraits const *traits_;

	VulkanVertexBuffer(std::shared_ptr<VulkanRenderer> const &renderer, size_t size, VertexTraits const *traits);
};

}
