// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

#include <vulkan/vulkan.h>
#include <cstdint>

namespace edge
{

/// One side of an image barrier: the layout, and the access and the stage that write or read the image in it.
struct ImageUse
{
	VkImageLayout layout;
	VkAccessFlags access;
	VkPipelineStageFlags stage;
};

/// Records the barrier taking levels [base_level, base_level + levels) of the first @p layers layers of an image (a colour one unless
/// @p aspect says otherwise) from @p from to @p to.
void record_image_barrier(VkCommandBuffer command_buffer, VkImage image, ImageUse const &from, ImageUse const &to,
	uint32_t base_level = 0, uint32_t levels = 1, uint32_t layers = 1, VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT);

/// Records the barrier taking a single-mip colour image, every layer of it, from old_layout to new_layout.  Only the
/// transitions between UNDEFINED, TRANSFER_DST/SRC, SHADER_READ_ONLY and COLOR_ATTACHMENT that the renderer makes are
/// known; std::runtime_error for any other.
void record_image_layout_transition(VkCommandBuffer command_buffer, VkImage image,
	VkImageLayout old_layout, VkImageLayout new_layout);

/// The same for a depth image of @p format (its stencil too, when the format has one; every layer), between the two layouts a readable depth takes:
/// DEPTH_STENCIL_ATTACHMENT (the pass that draws it) and SHADER_READ_ONLY (the passes that sample it), or from UNDEFINED (a new image) to
/// SHADER_READ_ONLY; std::runtime_error for any other.
void record_depth_layout_transition(VkCommandBuffer command_buffer, VkImage image, VkFormat format,
	VkImageLayout old_layout, VkImageLayout new_layout);

}
