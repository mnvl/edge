// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include <cstring>

#include "vulkan_draw_list.h"

#include "vulkan_error.h"
#include "vulkan_renderer.h"

namespace edge
{

void record_viewport(VkCommandBuffer command_buffer, int x, int y, int width, int height)
{
	// Y-flip: map OpenGL Y-up NDC convention to Vulkan screen space.
	// VK NDC Y=-1 = top, Y=+1 = bottom; by setting y to the rectangle's bottom and the height negative,
	// we invert the mapping so Y=+1 in clip space goes to the top of the rectangle.
	// Core since Vulkan 1.1 (VK_KHR_maintenance1).
	VkViewport viewport;
	std::memset(&viewport, 0, sizeof(viewport));
	viewport.x = static_cast<float>(x);
	viewport.y = static_cast<float>(y + height);
	viewport.width = static_cast<float>(width);
	viewport.height = -static_cast<float>(height);
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;
	vkCmdSetViewport(command_buffer, 0, 1, &viewport);
	VkRect2D scissor;
	scissor.offset.x = x;
	scissor.offset.y = y;
	scissor.extent.width = static_cast<uint32_t>(width);
	scissor.extent.height = static_cast<uint32_t>(height);
	vkCmdSetScissor(command_buffer, 0, 1, &scissor);
}

VulkanDrawList::VulkanDrawList(VulkanRenderer &renderer):
	DrawList(renderer),
	renderer_(renderer)
{
}

void VulkanDrawList::begin(VkCommandBuffer commands)
{
	commands_ = commands;
	bound.clear();
}

void VulkanDrawList::do_set_viewport(int x, int y, int width, int height)
{
	record_viewport(commands_, x, y, width, height);
}

void VulkanDrawList::do_draw(DrawCall const &call)
{
	renderer_.record_draw(*this, call);
}

VulkanDrawLists::VulkanDrawLists(VulkanRenderer &renderer, VkDevice device, uint32_t queue_family):
	renderer_(renderer),
	device_(device),
	queue_family_(queue_family)
{
}

VulkanDrawLists::~VulkanDrawLists()
{
	for (std::vector<Pool> &share : pools_)
		for (Pool &pool : share)
			vkDestroyCommandPool(device_, pool.pool, nullptr);
}

void VulkanDrawLists::reset(uint32_t share)
{
	if (share >= pools_.size())
		return;
	for (Pool &pool : pools_[share])
	{
		check_vk_result(vkResetCommandPool(device_, pool.pool, 0), "vkResetCommandPool(list)");
		pool.used = 0;
	}
}

VulkanDrawList &VulkanDrawLists::list(size_t index)
{
	while (lists_.size() <= index)
		lists_.push_back(std::make_unique<VulkanDrawList>(renderer_));
	return *lists_[index];
}

std::vector<DrawList *> VulkanDrawLists::begin_inline(VkCommandBuffer primary)
{
	secondaries_.clear();       // (none of a pass of several whose render pass then failed to begin)
	VulkanDrawList &only = list(0);
	only.begin(primary);
	return {&only};
}

void VulkanDrawLists::allocate(uint32_t share, size_t lists)
{
	if (pools_.size() <= share)
		pools_.resize(share + 1);
	std::vector<Pool> &pools = pools_[share];
	while (pools.size() < lists)
	{
		VkCommandPoolCreateInfo pool_info;
		std::memset(&pool_info, 0, sizeof(pool_info));
		pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		pool_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
		pool_info.queueFamilyIndex = queue_family_;
		Pool pool;
		check_vk_result(vkCreateCommandPool(device_, &pool_info, nullptr, &pool.pool), "vkCreateCommandPool(list)");
		pools.push_back(pool);
	}
	std::vector<VkCommandBuffer> buffers;
	for (size_t i = 0; i < lists; ++i)
	{
		Pool &pool = pools[i];
		if (pool.used == pool.buffers.size())
		{
			VkCommandBufferAllocateInfo allocate;
			std::memset(&allocate, 0, sizeof(allocate));
			allocate.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
			allocate.commandPool = pool.pool;
			allocate.level = VK_COMMAND_BUFFER_LEVEL_SECONDARY;
			allocate.commandBufferCount = 1;
			VkCommandBuffer buffer = VK_NULL_HANDLE;
			check_vk_result(vkAllocateCommandBuffers(device_, &allocate, &buffer), "vkAllocateCommandBuffers(list)");
			pool.buffers.push_back(buffer);
		}
		buffers.push_back(pool.buffers[pool.used++]);
	}
	secondaries_ = std::move(buffers);
}

std::vector<DrawList *> VulkanDrawLists::begin_secondary(VkRenderPass render_pass, VkFramebuffer framebuffer, VkExtent2D extent)
{
	std::vector<DrawList *> result;
	for (size_t i = 0; i < secondaries_.size(); ++i)
	{
		VkCommandBufferInheritanceInfo inheritance;
		std::memset(&inheritance, 0, sizeof(inheritance));
		inheritance.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO;
		inheritance.renderPass = render_pass;
		inheritance.subpass = 0;
		inheritance.framebuffer = framebuffer;
		VkCommandBufferBeginInfo begin;
		std::memset(&begin, 0, sizeof(begin));
		begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT | VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT;
		begin.pInheritanceInfo = &inheritance;
		check_vk_result(vkBeginCommandBuffer(secondaries_[i], &begin), "vkBeginCommandBuffer(list)");
		// a list starts with the whole target, as a pass does (the viewport is not inherited)
		record_viewport(secondaries_[i], 0, 0, static_cast<int>(extent.width), static_cast<int>(extent.height));
		VulkanDrawList &each = list(i);
		each.begin(secondaries_[i]);
		result.push_back(&each);
	}
	return result;
}

void VulkanDrawLists::end(VkCommandBuffer primary)
{
	if (secondaries_.empty())
		return;
	// the lists, in their order: what each recorded is drawn after the ones before it
	for (VkCommandBuffer buffer : secondaries_)
		check_vk_result(vkEndCommandBuffer(buffer), "vkEndCommandBuffer(list)");
	vkCmdExecuteCommands(primary, static_cast<uint32_t>(secondaries_.size()), secondaries_.data());
	secondaries_.clear();
}

}
