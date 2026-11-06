// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include "vulkan_memory.h"

#include <cstring>
#include <stdexcept>

#include "log.h"
#include "vulkan_error.h"

namespace edge
{

uint32_t find_memory_type(VkPhysicalDevice physical_device, uint32_t type_filter, VkMemoryPropertyFlags required,
	VkMemoryPropertyFlags preferred)
{
	VkPhysicalDeviceMemoryProperties memory_properties;
	vkGetPhysicalDeviceMemoryProperties(physical_device, &memory_properties);

	// A type carrying the preferred bits wins; failing that, any type with the
	// required bits will do.
	VkMemoryPropertyFlags const wanted_by_pass[2] = { required | preferred, required };

	for (int pass = 0; pass < 2; ++pass)
	{
		VkMemoryPropertyFlags const wanted = wanted_by_pass[pass];

		for (uint32_t i = 0; i < memory_properties.memoryTypeCount; ++i)
		{
			bool const type_matches = (type_filter & (1u << i)) != 0;
			bool const property_matches =
				(memory_properties.memoryTypes[i].propertyFlags & wanted) == wanted;
			if (type_matches && property_matches)
			{
				if (pass == 1)
					EDGE_LOG_DEBUG("Vulkan memory type {} taken without the preferred properties 0x{:x} (required 0x{:x}, it has 0x{:x})", i, preferred,
						required, memory_properties.memoryTypes[i].propertyFlags);
				return i;
			}
		}
	}

	throw std::runtime_error("failed to find suitable Vulkan memory type");
}

VkMemoryPropertyFlags create_buffer(VkDevice device, VkPhysicalDevice physical_device, VkDeviceSize size,
	VkBufferUsageFlags usage, VkMemoryPropertyFlags required, VkMemoryPropertyFlags preferred,
	VkBuffer &buffer, VkDeviceMemory &memory)
{
	VkBufferCreateInfo buffer_info;
	std::memset(&buffer_info, 0, sizeof(buffer_info));
	buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	buffer_info.size = size;
	buffer_info.usage = usage;
	buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	check_vk_result(vkCreateBuffer(device, &buffer_info, nullptr, &buffer), "vkCreateBuffer");

	VkMemoryRequirements memory_requirements;
	vkGetBufferMemoryRequirements(device, buffer, &memory_requirements);

	uint32_t const memory_type = find_memory_type(physical_device, memory_requirements.memoryTypeBits,
		required, preferred);

	VkMemoryAllocateInfo alloc_info;
	std::memset(&alloc_info, 0, sizeof(alloc_info));
	alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	alloc_info.allocationSize = memory_requirements.size;
	alloc_info.memoryTypeIndex = memory_type;

	check_vk_result(vkAllocateMemory(device, &alloc_info, nullptr, &memory), "vkAllocateMemory(buffer)");
	check_vk_result(vkBindBufferMemory(device, buffer, memory, 0), "vkBindBufferMemory");

	VkPhysicalDeviceMemoryProperties memory_properties;
	vkGetPhysicalDeviceMemoryProperties(physical_device, &memory_properties);
	return memory_properties.memoryTypes[memory_type].propertyFlags;
}

}
