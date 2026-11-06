// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

#include <vulkan/vulkan.h>
#include <cstring>

#include "vulkan_error.h"

namespace edge
{

/// A primary command buffer from @p command_pool, begun for one submit; the caller submits, waits and frees it.
inline VkCommandBuffer begin_one_time_commands(VkDevice device, VkCommandPool command_pool)
{
	VkCommandBufferAllocateInfo alloc_info;
	std::memset(&alloc_info, 0, sizeof(alloc_info));
	alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	alloc_info.commandPool = command_pool;
	alloc_info.commandBufferCount = 1;
	VkCommandBuffer command_buffer = VK_NULL_HANDLE;
	check_vk_result(vkAllocateCommandBuffers(device, &alloc_info, &command_buffer), "vkAllocateCommandBuffers");

	VkCommandBufferBeginInfo begin_info;
	std::memset(&begin_info, 0, sizeof(begin_info));
	begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	check_vk_result(vkBeginCommandBuffer(command_buffer, &begin_info), "vkBeginCommandBuffer");
	return command_buffer;
}

}
