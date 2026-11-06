// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

#include <vulkan/vulkan.h>
#include <map>
#include <utility>
#include <vector>

namespace edge
{

/// The colour+depth render passes offscreen targets are drawn with, made on first request and kept until the cache
/// goes, one per list of colour formats (none for a target of depth alone) and whether the depth is kept.
class VulkanRenderPassCache {
public:
	/// std::runtime_error when the device can attach none of the depth formats.
	VulkanRenderPassCache(VkDevice device, VkPhysicalDevice physical_device);
	~VulkanRenderPassCache();
	VulkanRenderPassCache(VulkanRenderPassCache const &) = delete;
	VulkanRenderPassCache &operator=(VulkanRenderPassCache const &) = delete;

	/// Depth (and stencil) format of every pass and of the depth images of render targets.
	VkFormat depth_format() const { return depth_format_; }
	/// With @p keep_depth the depth and the stencil are stored when the pass ends (a readable depth: RenderTarget::depth_texture and
	/// stencil_texture), else they are thrown away.  Either way they are cleared when the pass begins and left as a depth attachment
	/// when it ends.
	VkRenderPass get(VkFormat color_format, bool keep_depth = false);
	/// The pass of these colour attachments, in their order (attachment i, the fragment shader's output i), and the depth after them.
	VkRenderPass get(std::vector<VkFormat> const &color_formats, bool keep_depth);

private:
	VkDevice device_;
	VkFormat depth_format_;
	std::map<std::pair<std::vector<VkFormat>, bool>, VkRenderPass> passes_;
};

}
