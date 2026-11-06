// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

#include <vulkan/vulkan.h>
#include <cstdint>

namespace edge
{

/// A memory type allowed by type_filter that carries every bit of required.  When preferred is non-zero a type
/// carrying required | preferred wins, falling back to one carrying only required if the device exposes no such type.
/// The distinction matters: memory types are ordered by the driver, and the first required match is frequently the
/// slowest usable one (uncached write-combined host memory on NVIDIA, for instance).  std::runtime_error for none.
uint32_t find_memory_type(VkPhysicalDevice physical_device, uint32_t type_filter, VkMemoryPropertyFlags required,
	VkMemoryPropertyFlags preferred = 0);

/// A buffer with its own memory bound to it; returns the property flags of the memory type chosen.
VkMemoryPropertyFlags create_buffer(VkDevice device, VkPhysicalDevice physical_device, VkDeviceSize size,
	VkBufferUsageFlags usage, VkMemoryPropertyFlags required, VkMemoryPropertyFlags preferred,
	VkBuffer &buffer, VkDeviceMemory &memory);

}
