// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include "vulkan_render_pass_cache.h"

#include <cstring>
#include <stdexcept>
#include <vector>

#include "log.h"
#include "vulkan_error.h"

namespace edge
{

namespace
{

VkRenderPass make_render_pass(VkDevice device, std::vector<VkFormat> const &color_formats, VkFormat depth_format, bool keep_depth)
{
	// the colours, attachments 0 to n - 1, then the depth
	std::vector<VkAttachmentDescription> attachments;
	std::vector<VkAttachmentReference> color_refs;
	for (VkFormat color_format : color_formats)
	{
		VkAttachmentDescription color_attachment;
		std::memset(&color_attachment, 0, sizeof(color_attachment));
		color_attachment.format = color_format;
		color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
		color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		color_attachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		color_attachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		VkAttachmentReference color_attachment_ref;
		std::memset(&color_attachment_ref, 0, sizeof(color_attachment_ref));
		color_attachment_ref.attachment = static_cast<uint32_t>(attachments.size());
		color_attachment_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		attachments.push_back(color_attachment);
		color_refs.push_back(color_attachment_ref);
	}

	VkAttachmentDescription depth_attachment;
	std::memset(&depth_attachment, 0, sizeof(depth_attachment));
	depth_attachment.format = depth_format;
	depth_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
	depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	depth_attachment.storeOp = keep_depth ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE;
	// the stencil, when the format has one, as the depth: cleared (to the clear state's value), and kept with a kept depth
	depth_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	depth_attachment.stencilStoreOp = keep_depth ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE;
	depth_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	depth_attachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	VkAttachmentReference depth_attachment_ref;
	std::memset(&depth_attachment_ref, 0, sizeof(depth_attachment_ref));
	depth_attachment_ref.attachment = static_cast<uint32_t>(attachments.size());
	depth_attachment_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	attachments.push_back(depth_attachment);

	VkSubpassDescription subpass;
	std::memset(&subpass, 0, sizeof(subpass));
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = static_cast<uint32_t>(color_refs.size());
	subpass.pColorAttachments = color_refs.empty() ? nullptr : color_refs.data();
	subpass.pDepthStencilAttachment = &depth_attachment_ref;

	VkSubpassDependency dependency;
	std::memset(&dependency, 0, sizeof(dependency));
	dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
	dependency.dstSubpass = 0;
	// after the earlier writes of the same attachments: an earlier pass's, or another frame's still drawn (the depth is
	// written in the late tests, and cleared again here)
	dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
		| VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
	dependency.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
		| VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
	dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
		| VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

	VkRenderPassCreateInfo render_pass_info;
	std::memset(&render_pass_info, 0, sizeof(render_pass_info));
	render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	render_pass_info.attachmentCount = static_cast<uint32_t>(attachments.size());
	render_pass_info.pAttachments = attachments.data();
	render_pass_info.subpassCount = 1;
	render_pass_info.pSubpasses = &subpass;
	render_pass_info.dependencyCount = 1;
	render_pass_info.pDependencies = &dependency;

	VkRenderPass render_pass = VK_NULL_HANDLE;
	check_vk_result(vkCreateRenderPass(device, &render_pass_info, nullptr, &render_pass),
		"vkCreateRenderPass(offscreen)");
	return render_pass;
}

}

VulkanRenderPassCache::VulkanRenderPassCache(VkDevice device, VkPhysicalDevice physical_device):
	device_(device),
	depth_format_(VK_FORMAT_UNDEFINED)
{
	VkFormat const candidates[] = {
		VK_FORMAT_D32_SFLOAT_S8_UINT,
		VK_FORMAT_D24_UNORM_S8_UINT,
		VK_FORMAT_D32_SFLOAT
	};
	for (VkFormat candidate : candidates)
	{
		VkFormatProperties props;
		vkGetPhysicalDeviceFormatProperties(physical_device, candidate, &props);
		if (!(props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT))
			EDGE_LOG_DEBUG("Vulkan depth format {} (VkFormat) cannot be a depth attachment", static_cast<int>(candidate));
		if (props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
		{
			depth_format_ = candidate;
			EDGE_LOG_INFO("Vulkan depth format: {} (VkFormat)", static_cast<int>(candidate));
			return;
		}
	}
	throw std::runtime_error("no supported Vulkan depth format found");
}

VulkanRenderPassCache::~VulkanRenderPassCache()
{
	for (auto const &entry : passes_)
	{
		vkDestroyRenderPass(device_, entry.second, nullptr);
	}
}

VkRenderPass VulkanRenderPassCache::get(VkFormat color_format, bool keep_depth)
{
	return get(std::vector<VkFormat>{color_format}, keep_depth);
}

VkRenderPass VulkanRenderPassCache::get(std::vector<VkFormat> const &color_formats, bool keep_depth)
{
	auto const key = std::make_pair(color_formats, keep_depth);
	auto it = passes_.find(key);
	if (it == passes_.end())
	{
		it = passes_.emplace(key, make_render_pass(device_, color_formats, depth_format_, keep_depth)).first;
	}
	return it->second;
}

}
