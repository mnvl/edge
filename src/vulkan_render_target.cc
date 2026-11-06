// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include <algorithm>
#include <cstring>
#include <stdexcept>

#include "vulkan_error.h"
#include "vulkan_memory.h"
#include "vulkan_renderer.h"
#include "vulkan_render_target.h"
#include "vulkan_texture.h"

namespace edge
{

namespace
{

bool has_stencil_component(VkFormat format)
{
	return format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT || format == VK_FORMAT_D16_UNORM_S8_UINT;
}

VulkanDeviceContextPtr require_device_context(VulkanRenderer *renderer)
{
	if (!renderer)
	{
		throw std::invalid_argument("VulkanRenderTarget requires non-null renderer");
	}

	return renderer->device_context();
}

/// A plain target's readable depth's stencil, where its format has one; none for a target of layers.
TexturePtr stencil_view_of(TexturePtr const &depth_texture)
{
	std::shared_ptr<VulkanTexture> const readable = std::dynamic_pointer_cast<VulkanTexture>(depth_texture);
	if (!readable || readable->array_layers() > 0 || !has_stencil_component(readable->vk_format()))
		return TexturePtr();
	return std::make_shared<VulkanTexture>(readable, VulkanTexture::Stencil());
}

}

struct VulkanRenderTarget::DepthImage
{
	VulkanDeviceContextPtr context;
	VkImage image = VK_NULL_HANDLE;
	VkDeviceMemory memory = VK_NULL_HANDLE;

	/// An image of @p format and @p extent in memory of its own.
	DepthImage(VulkanDeviceContextPtr const &device_context, VkExtent2D extent, VkFormat format):
		context(device_context)
	{
		VkDevice const device = context->device();
		VkImageCreateInfo image_info;
		std::memset(&image_info, 0, sizeof(image_info));
		image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		image_info.imageType = VK_IMAGE_TYPE_2D;
		image_info.extent.width = extent.width;
		image_info.extent.height = extent.height;
		image_info.extent.depth = 1;
		image_info.mipLevels = 1;
		image_info.arrayLayers = 1;
		image_info.format = format;
		image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
		image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		image_info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
		image_info.samples = VK_SAMPLE_COUNT_1_BIT;
		image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		try
		{
			check_vk_result(vkCreateImage(device, &image_info, nullptr, &image), "vkCreateImage(offscreen_depth)");

			VkMemoryRequirements memory_requirements;
			vkGetImageMemoryRequirements(device, image, &memory_requirements);

			VkMemoryAllocateInfo alloc_info;
			std::memset(&alloc_info, 0, sizeof(alloc_info));
			alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
			alloc_info.allocationSize = memory_requirements.size;
			alloc_info.memoryTypeIndex = find_memory_type(context->physical_device(), memory_requirements.memoryTypeBits,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
			check_vk_result(vkAllocateMemory(device, &alloc_info, nullptr, &memory), "vkAllocateMemory(offscreen_depth)");
			check_vk_result(vkBindImageMemory(device, image, memory, 0), "vkBindImageMemory(offscreen_depth)");
		}
		catch (...)
		{
			release();
			throw;
		}
	}

	/// The targets that draw into it have waited for the GPU before they let it go.
	~DepthImage()
	{
		release();
	}

	void release()
	{
		if (image != VK_NULL_HANDLE)
			vkDestroyImage(context->device(), image, nullptr);
		if (memory != VK_NULL_HANDLE)
			vkFreeMemory(context->device(), memory, nullptr);
		image = VK_NULL_HANDLE;
		memory = VK_NULL_HANDLE;
	}
};

VulkanRenderTarget::VulkanRenderTarget(VulkanRenderer *renderer, int width, int height, std::vector<TexturePtr> const &color_textures,
	TexturePtr const &depth_texture, int layer):
	VulkanRenderTarget(renderer, width, height, color_textures, depth_texture, layer, nullptr)
{
}

VulkanRenderTarget::VulkanRenderTarget(VulkanRenderer *renderer, int width, int height, std::vector<TexturePtr> const &color_textures,
	TexturePtr const &depth_texture, int layer, std::shared_ptr<DepthImage> const &depth_image):
	RenderTarget(width, height, color_textures, depth_texture, layer < 0 ? stencil_view_of(depth_texture) : TexturePtr(), layer),
	context_(require_device_context(renderer)),
	render_pass_(VK_NULL_HANDLE),
	framebuffer_(VK_NULL_HANDLE),
	depth_image_(depth_image),
	depth_image_view_(VK_NULL_HANDLE)
{
	if (width_ <= 0 || height_ <= 0)
	{
		throw std::invalid_argument("a render target of " + std::to_string(width_) + "x" + std::to_string(height_) + " pixels");
	}
	extent_.width = static_cast<uint32_t>(width_);
	extent_.height = static_cast<uint32_t>(height_);
	try
	{
		create_offscreen_resources();
		if (this->layer_index() < 0 && layers() > 0)
		{
			// one depth for every layer, where it is thrown away: a pass draws one layer, and clears it first
			std::shared_ptr<DepthImage> const shared = depth_texture ? nullptr
				: std::make_shared<DepthImage>(context_, extent_, renderer->depth_format());
			for (int i = 0; i < layers(); ++i)
				layer_targets_.push_back(RenderTargetPtr(new VulkanRenderTarget(renderer, width, height, color_textures, depth_texture, i,
					shared)));
		}
	}
	catch (...)
	{
		destroy_offscreen_resources();
		throw;
	}
}

VulkanRenderTarget::~VulkanRenderTarget()
{
	destroy_offscreen_resources();
}

VkImage VulkanRenderTarget::depth_image() const
{
	VulkanTexture const *const readable = dynamic_cast<VulkanTexture const *>(depth_texture().get());
	return readable ? readable->image() : depth_image_ ? depth_image_->image : VK_NULL_HANDLE;
}

void VulkanRenderTarget::create_offscreen_resources()
{
	if (extent_.width == 0 || extent_.height == 0)
	{
		throw std::invalid_argument("offscreen Vulkan render target must have non-zero size");
	}

	std::vector<VulkanTexture *> colors;
	std::vector<VkFormat> color_formats;
	for (TexturePtr const &texture : color_textures())
	{
		VulkanTexture *const color = dynamic_cast<VulkanTexture *>(texture.get());
		if (!color || color->is_depth() || color->width() != width_ || color->height() != height_)
		{
			throw std::logic_error("a Vulkan render target's colours must be VulkanTextures of its size");
		}
		colors.push_back(color);
		color_formats.push_back(color->vk_format());
	}
	VulkanTexture *const depth_texture = dynamic_cast<VulkanTexture *>(this->depth_texture().get());
	if (this->depth_texture() && (!depth_texture || !depth_texture->is_depth()
		|| depth_texture->width() != width_ || depth_texture->height() != height_))
	{
		throw std::logic_error("a Vulkan render target's readable depth must be a depth VulkanTexture of its size");
	}
	if (layer_index() < 0)
	{
		// as a texture of the target: in the layout the passes that sample it want, before the first pass draws it
		for (VulkanTexture *color : colors)
			color->transition_layout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		if (layers() > 0)
			return;         // a target of layers: its layers' targets draw them
	}
	render_pass_ = renderer().offscreen_render_pass(color_formats, depth_texture != nullptr);
	if (render_pass_ == VK_NULL_HANDLE)
	{
		throw std::logic_error("Vulkan offscreen render pass is not available");
	}

	VkFormat const depth_format = renderer().depth_format();
	if (depth_format == VK_FORMAT_UNDEFINED)
	{
		throw std::logic_error("Vulkan depth format is undefined");
	}

	// the depth attachment: the readable depth's image, or one no one reads (its own, or the one the target of layers gave it)
	if (!depth_texture && !depth_image_)
	{
		depth_image_ = std::make_shared<DepthImage>(context_, extent_, depth_format);
	}
	VkImage const depth_image = depth_texture ? depth_texture->image() : depth_image_->image;

	VkImageViewCreateInfo depth_view_info;
	std::memset(&depth_view_info, 0, sizeof(depth_view_info));
	depth_view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	depth_view_info.image = depth_image;
	depth_view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
	depth_view_info.format = depth_format;
	depth_view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
	if (has_stencil_component(depth_format))
	{
		depth_view_info.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
	}
	depth_view_info.subresourceRange.baseMipLevel = 0;
	depth_view_info.subresourceRange.levelCount = 1;
	depth_view_info.subresourceRange.baseArrayLayer = depth_texture ? static_cast<uint32_t>(std::max(0, layer_index())) : 0;
	depth_view_info.subresourceRange.layerCount = 1;
	check_vk_result(vkCreateImageView(device(), &depth_view_info, nullptr, &depth_image_view_),
		"vkCreateImageView(offscreen_depth)");

	// the colours as they are, or, for a layer's target, views of that layer
	std::vector<VkImageView> attachments;
	for (VulkanTexture *color : colors)
	{
		if (layer_index() < 0)
		{
			attachments.push_back(color->image_view());
			continue;
		}
		VkImageViewCreateInfo view_info;
		std::memset(&view_info, 0, sizeof(view_info));
		view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		view_info.image = color->image();
		view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
		view_info.format = color->vk_format();
		view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		view_info.subresourceRange.levelCount = 1;
		view_info.subresourceRange.baseArrayLayer = static_cast<uint32_t>(layer_index());
		view_info.subresourceRange.layerCount = 1;
		VkImageView view = VK_NULL_HANDLE;
		check_vk_result(vkCreateImageView(device(), &view_info, nullptr, &view), "vkCreateImageView(render target layer)");
		layer_views_.push_back(view);
		attachments.push_back(view);
	}
	attachments.push_back(depth_image_view_);
	VkFramebufferCreateInfo framebuffer_info;
	std::memset(&framebuffer_info, 0, sizeof(framebuffer_info));
	framebuffer_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	framebuffer_info.renderPass = render_pass_;
	framebuffer_info.attachmentCount = static_cast<uint32_t>(attachments.size());
	framebuffer_info.pAttachments = attachments.data();
	framebuffer_info.width = extent_.width;
	framebuffer_info.height = extent_.height;
	framebuffer_info.layers = 1;
	check_vk_result(vkCreateFramebuffer(device(), &framebuffer_info, nullptr, &framebuffer_),
		"vkCreateFramebuffer(offscreen)");
}

void VulkanRenderTarget::destroy_offscreen_resources()
{
	// Deliberately taken from the context, not the renderer: the renderer may
	// already be destroyed, while the device it created is kept alive by the
	// reference this target holds.
	VkDevice const vk_device = device();

	vkDeviceWaitIdle(vk_device);
	// the renderer's frames are drawn now: it gives their shares back
	if (context_->has_renderer())
	{
		context_->require_renderer().wait_submitted();
	}

	if (framebuffer_ != VK_NULL_HANDLE)
	{
		vkDestroyFramebuffer(vk_device, framebuffer_, nullptr);
		framebuffer_ = VK_NULL_HANDLE;
	}
	for (VkImageView view : layer_views_)
	{
		vkDestroyImageView(vk_device, view, nullptr);
	}
	layer_views_.clear();
	if (depth_image_view_ != VK_NULL_HANDLE)
	{
		vkDestroyImageView(vk_device, depth_image_view_, nullptr);
		depth_image_view_ = VK_NULL_HANDLE;
	}
	depth_image_.reset();         // (the last target of a target of layers to go destroys it)
}

}
