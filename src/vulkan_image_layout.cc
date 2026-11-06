// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include "vulkan_image_layout.h"

#include <cstring>
#include <stdexcept>

namespace edge
{

void record_image_barrier(VkCommandBuffer command_buffer, VkImage image, ImageUse const &from, ImageUse const &to,
	uint32_t base_level, uint32_t levels, uint32_t layers, VkImageAspectFlags aspect)
{
	VkImageMemoryBarrier barrier;
	std::memset(&barrier, 0, sizeof(barrier));
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier.oldLayout = from.layout;
	barrier.newLayout = to.layout;
	barrier.srcAccessMask = from.access;
	barrier.dstAccessMask = to.access;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = image;
	barrier.subresourceRange.aspectMask = aspect;
	barrier.subresourceRange.baseMipLevel = base_level;
	barrier.subresourceRange.levelCount = levels;
	barrier.subresourceRange.layerCount = layers;
	vkCmdPipelineBarrier(command_buffer, from.stage, to.stage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

void record_image_layout_transition(VkCommandBuffer command_buffer, VkImage image,
	VkImageLayout old_layout, VkImageLayout new_layout)
{
	// what the image is used for in each layout the renderer puts one in
	ImageUse const undefined = {VK_IMAGE_LAYOUT_UNDEFINED, 0, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT};
	ImageUse const transfer_dst = {VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT};
	ImageUse const transfer_src = {VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT};
	ImageUse const shader_read = {VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_SHADER_READ_BIT,
		VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT};
	ImageUse const attachment = {VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
		VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
	struct { ImageUse from, to; } const transitions[] = {
		{undefined, transfer_dst}, {undefined, shader_read}, {transfer_dst, shader_read}, {undefined, attachment},
		{shader_read, attachment}, {attachment, shader_read}, {shader_read, transfer_dst}, {shader_read, transfer_src},
		{attachment, transfer_src}, {transfer_src, shader_read}, {transfer_src, attachment},
	};
	for (auto const &transition : transitions)
	{
		if (transition.from.layout == old_layout && transition.to.layout == new_layout)
		{
			record_image_barrier(command_buffer, image, transition.from, transition.to, 0, 1, VK_REMAINING_ARRAY_LAYERS);
			return;
		}
	}
	throw std::runtime_error("unsupported Vulkan image layout transition");
}

void record_depth_layout_transition(VkCommandBuffer command_buffer, VkImage image, VkFormat format,
	VkImageLayout old_layout, VkImageLayout new_layout)
{
	ImageUse const attachment = {VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
		VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
		VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT};
	ImageUse const shader_read = {VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT};
	bool const stencil = format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT || format == VK_FORMAT_D16_UNORM_S8_UINT;
	VkImageAspectFlags const aspect = VK_IMAGE_ASPECT_DEPTH_BIT | (stencil ? VK_IMAGE_ASPECT_STENCIL_BIT : 0);
	ImageUse const undefined = {VK_IMAGE_LAYOUT_UNDEFINED, 0, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT};
	if (old_layout == undefined.layout && new_layout == shader_read.layout)
		record_image_barrier(command_buffer, image, undefined, shader_read, 0, 1, VK_REMAINING_ARRAY_LAYERS, aspect);
	else if (old_layout == attachment.layout && new_layout == shader_read.layout)
		record_image_barrier(command_buffer, image, attachment, shader_read, 0, 1, VK_REMAINING_ARRAY_LAYERS, aspect);
	else if (old_layout == shader_read.layout && new_layout == attachment.layout)
		record_image_barrier(command_buffer, image, shader_read, attachment, 0, 1, VK_REMAINING_ARRAY_LAYERS, aspect);
	else
		throw std::runtime_error("unsupported Vulkan depth layout transition");
}

}
