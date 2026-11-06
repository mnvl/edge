// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

#include <vulkan/vulkan.h>
#include <memory>
#include <stdexcept>

#include "forward.h"
#include "vulkan_instance.h"

namespace edge
{

/// Shared owner of the VkDevice backing one VulkanRenderer, and of its hold on the process's VkInstance (VulkanInstance), which other
/// renderers share: the device goes when the last of its holders does, and the instance after the last device made from it.
///
/// Device-owned resources (VulkanTexture, VulkanRenderTarget) hold a shared_ptr
/// to the context rather than a bare VulkanRenderer pointer, so the device is
/// guaranteed to outlive every image, view and allocation created from it no
/// matter which owner is released last.  Python hands out Textures and
/// RenderTargets that are independent of the Renderer object, so "renderer
/// freed first" is a normal ordering, not a caller error: destroying such a
/// resource against a dangling device made the Vulkan loader abort the process.
///
/// The renderer back-pointer is cleared by ~VulkanRenderer.  Resources need the
/// renderer itself for anything beyond a device handle (command buffers, memory
/// type lookup, frame state); once it is detached those operations throw
/// instead of dereferencing freed memory.
class VulkanDeviceContext
{
public:
	explicit VulkanDeviceContext(VulkanRenderer *renderer):
		renderer_(renderer),
		physical_device_(VK_NULL_HANDLE),
		device_(VK_NULL_HANDLE)
	{
	}

	VulkanDeviceContext(VulkanDeviceContext const &) = delete;
	VulkanDeviceContext &operator=(VulkanDeviceContext const &) = delete;

	~VulkanDeviceContext()
	{
		if (device_ != VK_NULL_HANDLE)
		{
			vkDestroyDevice(device_, nullptr);
			device_ = VK_NULL_HANDLE;
		}

		// after the device made from it: the last holder destroys it
		instance_.reset();
	}

	VkInstance instance() const { return instance_ ? instance_->get() : VK_NULL_HANDLE; }
	VkPhysicalDevice physical_device() const { return physical_device_; }
	VkDevice device() const { return device_; }

	void set_instance(VulkanInstancePtr const &instance) { instance_ = instance; }
	void set_device(VkPhysicalDevice physical_device, VkDevice device) { physical_device_ = physical_device; device_ = device; }

	/// Called by ~VulkanRenderer: the device stays alive until the last
	/// resource holding this context is gone, but the renderer does not.
	void detach_renderer() { renderer_ = nullptr; }

	bool has_renderer() const { return renderer_ != nullptr; }

	/// Renderer that owns this device, for operations that need more than a
	/// device handle.  Throws once the renderer has been destroyed.
	VulkanRenderer &require_renderer() const
	{
		if (!renderer_)
		{
			throw std::logic_error(
				"Vulkan resource used after its renderer was destroyed");
		}

		return *renderer_;
	}

private:
	VulkanRenderer *renderer_;
	VulkanInstancePtr instance_;
	VkPhysicalDevice physical_device_;
	VkDevice device_;
};

using VulkanDeviceContextPtr = std::shared_ptr<VulkanDeviceContext>;

}
