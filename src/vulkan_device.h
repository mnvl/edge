// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

// A VulkanRenderer's instance and device, made once when it is (vulkan_renderer.cc's init()): what extensions and features edge asks
// for, which device it takes, and why.

#include <cstdint>
#include <string>
#include <vector>

#include <vulkan/vulkan.h>

namespace edge
{

/// An instance with what edge wants of the loader where it is there (portability enumeration, the device properties that give a
/// device's UUID) and @p application_extensions (a window system's: std::runtime_error naming one that is not there).
/// @p surface_extension: VK_KHR_surface is on it, so a swapchain can be had; @p enabled: the extensions it was made with.  A renderer
/// takes the process's instance (VulkanInstance::acquire), which this makes.
VkInstance create_vulkan_instance(std::vector<std::string> const &application_extensions, bool &surface_extension,
	std::vector<std::string> &enabled);

/// The first queue family of @p device that does graphics, -1 for none.
int vulkan_graphics_queue_family(VkPhysicalDevice device);

/// The canonical UUID of @p device (vulkan_device_info.h), empty where the driver gives none.
std::string vulkan_device_uuid(VkInstance instance, VkPhysicalDevice device);

/// The device with the UUID @p wanted_uuid (std::runtime_error listing the devices there if none has it: never another), or without
/// one the first with a graphics queue; @p uuid its UUID.  std::runtime_error for no device that does graphics.
VkPhysicalDevice pick_vulkan_physical_device(VkInstance instance, std::string const &wanted_uuid, std::string &uuid);

/// A device made of a physical one, its graphics queue, and what it was given.
struct VulkanLogicalDevice
{
	VkDevice device = VK_NULL_HANDLE;
	VkQueue graphics_queue = VK_NULL_HANDLE;
	uint32_t graphics_queue_family = 0;
	bool swapchain = false;        // VK_KHR_swapchain: present() can show frames
	bool anisotropy = false;       // samplerAnisotropy
};

/// The device of @p physical_device: its graphics queue; VK_KHR_maintenance1 (edge's upright viewport; std::runtime_error without),
/// VK_KHR_swapchain where @p surface_extension and the device has it, portability subset where there; BC and ASTC sampling and
/// anisotropy where the device has them.  Logs what it has and what it lacks.
VulkanLogicalDevice create_vulkan_device(VkPhysicalDevice physical_device, bool surface_extension);

}
