// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

#include "pixel.h"
#include "vulkan_error.h"
#include "vulkan_image_layout.h"
#include "vulkan_memory.h"
#include "vulkan_renderer.h"
#include "vulkan_texture.h"

namespace edge
{

namespace
{

VulkanDeviceContextPtr require_device_context(VulkanRenderer *renderer)
{
	if (!renderer)
	{
		throw std::invalid_argument("VulkanTexture requires non-null renderer");
	}

	return renderer->device_context();
}

/// A depth texture's, or a layered target's colour's: one level, no CPU copy, @p layers layers.
TextureUsage depth_usage(int layers = 0)
{
	TextureUsage usage;
	usage.keep_cpu_copy = false;
	usage.array_layers = layers;
	return usage;
}

VkFormat resolve_vk_format(PixelFormat pixel_format, bool srgb)
{
	if (srgb)
	{
		switch (pixel_format)
		{
		case PixelFormat::X8R8G8B8:
		case PixelFormat::A8R8G8B8:
			return VK_FORMAT_B8G8R8A8_SRGB;
		case PixelFormat::BC1:
			return VK_FORMAT_BC1_RGBA_SRGB_BLOCK;
		case PixelFormat::BC3:
			return VK_FORMAT_BC3_SRGB_BLOCK;
		case PixelFormat::BC7:
			return VK_FORMAT_BC7_SRGB_BLOCK;
		case PixelFormat::ASTC_4x4:
			return VK_FORMAT_ASTC_4x4_SRGB_BLOCK;
		case PixelFormat::ASTC_6x6:
			return VK_FORMAT_ASTC_6x6_SRGB_BLOCK;
		case PixelFormat::ASTC_8x8:
			return VK_FORMAT_ASTC_8x8_SRGB_BLOCK;
		default:
			throw std::invalid_argument("sRGB is for 8-bit and block-compressed colour formats only");
		}
	}
	switch (pixel_format)
	{
	case PixelFormat::X8R8G8B8:
	case PixelFormat::A8R8G8B8:
		return VK_FORMAT_B8G8R8A8_UNORM;
	case PixelFormat::R16G16B16A16F:
		return VK_FORMAT_R16G16B16A16_SFLOAT;
	case PixelFormat::R32G32B32A32F:
		return VK_FORMAT_R32G32B32A32_SFLOAT;
	case PixelFormat::R32F:
		return VK_FORMAT_R32_SFLOAT;
	case PixelFormat::R8UI:
		return VK_FORMAT_R8_UINT;
	case PixelFormat::BC1:
		return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
	case PixelFormat::BC3:
		return VK_FORMAT_BC3_UNORM_BLOCK;
	case PixelFormat::BC5:
		return VK_FORMAT_BC5_UNORM_BLOCK;
	case PixelFormat::BC7:
		return VK_FORMAT_BC7_UNORM_BLOCK;
	case PixelFormat::ASTC_4x4:
		return VK_FORMAT_ASTC_4x4_UNORM_BLOCK;
	case PixelFormat::ASTC_6x6:
		return VK_FORMAT_ASTC_6x6_UNORM_BLOCK;
	case PixelFormat::ASTC_8x8:
		return VK_FORMAT_ASTC_8x8_UNORM_BLOCK;
	default:
		throw std::runtime_error("unsupported Vulkan texture pixel format");
	}
}

}

VkFormat VulkanTexture::vk_format(PixelFormat pixel_format, bool srgb)
{
	return resolve_vk_format(pixel_format, srgb);
}

VulkanTexture::VulkanTexture(VulkanRenderer *renderer_ptr, int width, int height,
	PixelFormat pixel_format, TextureUsage const &usage):
	Texture(width, height, pixel_format, usage),
	context_(require_device_context(renderer_ptr)),
	image_(VK_NULL_HANDLE),
	image_memory_(VK_NULL_HANDLE),
	image_view_(VK_NULL_HANDLE),
	staging_buffer_(VK_NULL_HANDLE),
	staging_memory_(VK_NULL_HANDLE),
	staging_memory_properties_(0),
	staging_mapped_(nullptr),
	vk_format_(resolve_vk_format(pixel_format, usage.srgb)),
	current_layout_(VK_IMAGE_LAYOUT_UNDEFINED),
	depth_(false)
{
	// (before the CPU's copy, which a size past them would ask gigabytes for)
	VkPhysicalDeviceProperties properties;
	vkGetPhysicalDeviceProperties(context_->physical_device(), &properties);
	if (static_cast<uint32_t>(width) > properties.limits.maxImageDimension2D || static_cast<uint32_t>(height) > properties.limits.maxImageDimension2D)
	{
		throw std::invalid_argument("a texture of " + std::to_string(width) + " x " + std::to_string(height) + ": this device has at most " +
			std::to_string(properties.limits.maxImageDimension2D) + " on a side (maxImageDimension2D)");
	}
	if (usage.array_layers > 0 && static_cast<uint32_t>(usage.array_layers) > properties.limits.maxImageArrayLayers)
	{
		throw std::invalid_argument(std::to_string(usage.array_layers) + " array layers: this device has at most " +
			std::to_string(properties.limits.maxImageArrayLayers) + " (maxImageArrayLayers)");
	}

	clone_.resize(cpu_size());

	// a block format the GPU lacks (BC on a phone, ASTC on a desktop) is an error here, not a black texture
	if (is_block_compressed(pixel_format))
	{
		VkFormatProperties properties;
		vkGetPhysicalDeviceFormatProperties(context_->physical_device(), vk_format_, &properties);
		if (!(properties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT))
		{
			throw std::runtime_error("this GPU cannot sample " + std::string(is_astc(pixel_format) ? "ASTC" : "BC")
				+ " textures (pixel format " + to_string(pixel_format) + "): the asset set was compressed for another kind of GPU");
		}
	}

	// (a constructor that throws runs no destructor: what it made so far is let go here)
	try
	{
		create_image();
		create_staging_buffer();
	}
	catch (...)
	{
		release_all();
		throw;
	}
}

VulkanTexture::VulkanTexture(VulkanRenderer *renderer_ptr, int width, int height, PixelFormat pixel_format, Layers layers):
	Texture(width, height, pixel_format, depth_usage(layers.count)),
	context_(require_device_context(renderer_ptr)),
	image_(VK_NULL_HANDLE),
	image_memory_(VK_NULL_HANDLE),
	image_view_(VK_NULL_HANDLE),
	staging_buffer_(VK_NULL_HANDLE),
	staging_memory_(VK_NULL_HANDLE),
	staging_memory_properties_(0),
	staging_mapped_(nullptr),
	vk_format_(resolve_vk_format(pixel_format, false)),
	current_layout_(VK_IMAGE_LAYOUT_UNDEFINED),
	depth_(false)
{
	if (layers.count <= 0 || (layers.cube && layers.count != 6))
		throw std::logic_error("a layered target's colour of " + std::to_string(layers.count) + " layers");
	cube_ = layers.cube;
	create_image();
}

VulkanTexture::VulkanTexture(VulkanRenderer *renderer_ptr, int width, int height, Depth, Layers layers):
	Texture(width, height, PixelFormat::R32F, depth_usage(layers.count)),
	context_(require_device_context(renderer_ptr)),
	image_(VK_NULL_HANDLE),
	image_memory_(VK_NULL_HANDLE),
	image_view_(VK_NULL_HANDLE),
	staging_buffer_(VK_NULL_HANDLE),
	staging_memory_(VK_NULL_HANDLE),
	staging_memory_properties_(0),
	staging_mapped_(nullptr),
	vk_format_(renderer().depth_format()),
	current_layout_(VK_IMAGE_LAYOUT_UNDEFINED),
	depth_(true)
{
	VkFormatProperties properties;
	vkGetPhysicalDeviceFormatProperties(context_->physical_device(), vk_format_, &properties);
	if (!(properties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT))
	{
		throw std::runtime_error("this GPU cannot sample its depth format (VkFormat " + std::to_string(vk_format_) + "): no readable depth");
	}
	if (layers.count < 0 || (layers.cube && layers.count != 6))
		throw std::logic_error("a readable depth of " + std::to_string(layers.count) + " layers");
	cube_ = layers.cube;
	create_image();
	// as a render target's colour texture: in the layout the passes that sample it want, before the first pass draws it
	VkCommandBuffer const command_buffer = renderer().begin_single_time_commands();
	record_depth_layout_transition(command_buffer, image_, vk_format_, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	renderer().end_single_time_commands(command_buffer);
	current_layout_ = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
}

VulkanTexture::VulkanTexture(std::shared_ptr<VulkanTexture> const &depth, Stencil):
	Texture(depth ? depth->width() : 0, depth ? depth->height() : 0, PixelFormat::R8UI, depth_usage()),
	context_(depth ? depth->context_ : VulkanDeviceContextPtr()),
	image_(depth ? depth->image_ : VK_NULL_HANDLE),
	image_memory_(VK_NULL_HANDLE),
	image_view_(VK_NULL_HANDLE),
	staging_buffer_(VK_NULL_HANDLE),
	staging_memory_(VK_NULL_HANDLE),
	staging_memory_properties_(0),
	staging_mapped_(nullptr),
	vk_format_(depth ? depth->vk_format_ : VK_FORMAT_UNDEFINED),
	current_layout_(VK_IMAGE_LAYOUT_UNDEFINED),
	depth_(false),
	stencil_of_(depth)
{
	if (!depth || !depth->is_depth())
		throw std::logic_error("a stencil texture is a view of a depth texture's image");
	if (vk_format_ != VK_FORMAT_D32_SFLOAT_S8_UINT && vk_format_ != VK_FORMAT_D24_UNORM_S8_UINT && vk_format_ != VK_FORMAT_D16_UNORM_S8_UINT)
		throw std::logic_error("the depth format (VkFormat " + std::to_string(vk_format_) + ") has no stencil to read");
	VkImageViewCreateInfo view_info;
	std::memset(&view_info, 0, sizeof(view_info));
	view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	view_info.image = image_;
	view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
	view_info.format = vk_format_;
	view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT;
	view_info.subresourceRange.levelCount = 1;
	view_info.subresourceRange.layerCount = 1;
	check_vk_result(vkCreateImageView(device(), &view_info, nullptr, &image_view_), "vkCreateImageView(stencil)");
}

VulkanTexture::~VulkanTexture()
{
	// a frame the GPU has not drawn yet, or the last copy to a window, may still read the image
	if (context_->has_renderer())
	{
		context_->require_renderer().wait_submitted();
	}
	release_all();
}

void VulkanTexture::release_all()
{
	release_staging();

	if (image_view_ != VK_NULL_HANDLE)
	{
		vkDestroyImageView(device(), image_view_, nullptr);
		image_view_ = VK_NULL_HANDLE;
	}

	if (image_ != VK_NULL_HANDLE && !stencil_of_)      // (a stencil view's image is its depth texture's)
	{
		vkDestroyImage(device(), image_, nullptr);
		image_ = VK_NULL_HANDLE;
	}

	if (image_memory_ != VK_NULL_HANDLE)
	{
		vkFreeMemory(device(), image_memory_, nullptr);
		image_memory_ = VK_NULL_HANDLE;
	}
}

void VulkanTexture::release_staging()
{
	if (staging_mapped_ != nullptr)
	{
		vkUnmapMemory(device(), staging_memory_);
		staging_mapped_ = nullptr;
	}

	if (staging_buffer_ != VK_NULL_HANDLE)
	{
		vkDestroyBuffer(device(), staging_buffer_, nullptr);
		staging_buffer_ = VK_NULL_HANDLE;
	}

	if (staging_memory_ != VK_NULL_HANDLE)
	{
		vkFreeMemory(device(), staging_memory_, nullptr);
		staging_memory_ = VK_NULL_HANDLE;
	}
}

void VulkanTexture::create_image()
{
	VkImageCreateInfo image_info;
	std::memset(&image_info, 0, sizeof(image_info));
	image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	image_info.imageType = VK_IMAGE_TYPE_2D;
	image_info.extent.width = static_cast<uint32_t>(width_);
	image_info.extent.height = static_cast<uint32_t>(height_);
	image_info.extent.depth = 1;
	image_info.mipLevels = static_cast<uint32_t>(mip_levels_);
	image_info.arrayLayers = layer_count();
	image_info.format = vk_format_;
	image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
	image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	// read back by download(), shown by VulkanRenderer::present(), and each mip level is blitted from the one above
	image_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	if (depth_)
	{
		image_info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	}
	else if (!is_block_compressed(pixel_format_))
	{
		image_info.usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	}
	image_info.samples = VK_SAMPLE_COUNT_1_BIT;
	image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	if (cube_)
		image_info.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;

	check_vk_result(vkCreateImage(device(), &image_info, nullptr, &image_), "vkCreateImage");

	VkMemoryRequirements memory_requirements;
	vkGetImageMemoryRequirements(device(), image_, &memory_requirements);

	VkMemoryAllocateInfo alloc_info;
	std::memset(&alloc_info, 0, sizeof(alloc_info));
	alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	alloc_info.allocationSize = memory_requirements.size;
	alloc_info.memoryTypeIndex = find_memory_type(context_->physical_device(), memory_requirements.memoryTypeBits,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	check_vk_result(vkAllocateMemory(device(), &alloc_info, nullptr, &image_memory_),
		"vkAllocateMemory(image)");
	check_vk_result(vkBindImageMemory(device(), image_, image_memory_, 0),
		"vkBindImageMemory");

	VkImageViewCreateInfo view_info;
	std::memset(&view_info, 0, sizeof(view_info));
	view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	view_info.image = image_;
	view_info.viewType = cube_ ? VK_IMAGE_VIEW_TYPE_CUBE : usage_.array_layers > 0 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
	view_info.format = vk_format_;
	view_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
	view_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
	view_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
	view_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
	// a shader reads one aspect: of a depth texture, its depth (a stencil, if the format has one, is the attachment's alone)
	view_info.subresourceRange.aspectMask = depth_ ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
	view_info.subresourceRange.baseMipLevel = 0;
	view_info.subresourceRange.levelCount = static_cast<uint32_t>(mip_levels_);
	view_info.subresourceRange.baseArrayLayer = 0;
	view_info.subresourceRange.layerCount = layer_count();

	check_vk_result(vkCreateImageView(device(), &view_info, nullptr, &image_view_),
		"vkCreateImageView(texture)");
}

void VulkanTexture::create_staging_buffer()
{
	VkDeviceSize const staging_size = clone_.empty() ? 1 : static_cast<VkDeviceSize>(clone_.size());

	// download() and batch_download_textures() memcpy *out* of this buffer.
	// Reading uncached write-combined memory runs at a fraction of RAM speed,
	// so ask for a cached type; drivers list the uncached one first and
	// find_memory_type would otherwise take it.
	staging_memory_properties_ = create_buffer(device(), context_->physical_device(), staging_size,
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
		staging_buffer_, staging_memory_);

	if (!clone_.empty())
	{
		check_vk_result(vkMapMemory(device(), staging_memory_, 0, clone_.size(), 0, &staging_mapped_),
			"vkMapMemory(texture_staging)");
	}
}

void VulkanTexture::check_uploadable() const
{
	if (staging_buffer_ == VK_NULL_HANDLE)
	{
		throw std::logic_error("a texture without its CPU copy (TextureUsage::keep_cpu_copy) cannot be uploaded again");
	}
}

void VulkanTexture::check_streamable() const
{
	check_uploadable();
	if (usage_.mipmaps || usage_.array_layers > 0 || mip_levels_ != 1)
	{
		throw std::logic_error("a texture with levels or layers cannot be streamed");
	}
}

void VulkanTexture::record_stream(VkCommandBuffer command_buffer, VkBuffer source, VkDeviceSize offset)
{
	ImageUse const read = {VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_SHADER_READ_BIT,
		VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT};
	ImageUse const written = {VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT};
	// the whole image is written: what it held is not kept
	record_image_barrier(command_buffer, image_, {current_layout_ == VK_IMAGE_LAYOUT_UNDEFINED ? VK_IMAGE_LAYOUT_UNDEFINED
		: VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, read.access, read.stage}, written, 0, 1, 1);
	VkBufferImageCopy region;
	std::memset(&region, 0, sizeof(region));
	region.bufferOffset = offset;
	region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	region.imageSubresource.layerCount = 1;
	region.imageExtent.width = static_cast<uint32_t>(width_);
	region.imageExtent.height = static_cast<uint32_t>(height_);
	region.imageExtent.depth = 1;
	vkCmdCopyBufferToImage(command_buffer, source, image_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
	record_image_barrier(command_buffer, image_, written, read, 0, 1, 1);
	current_layout_ = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
}

void VulkanTexture::record_upload(VkCommandBuffer command_buffer)
{
	if (staging_buffer_ == VK_NULL_HANDLE)
	{
		throw std::logic_error("a texture without its CPU copy (TextureUsage::keep_cpu_copy) cannot be uploaded again");
	}
	if (!clone_.empty() && staging_mapped_ != nullptr)
	{
		std::memcpy(staging_mapped_, &clone_[0], clone_.size());
	}
	if (usage_.mipmaps)
		upload_mipmapped(command_buffer);
	else
		upload_levels(command_buffer);
}

void VulkanTexture::finish_upload()
{
	current_layout_ = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	if (!usage_.keep_cpu_copy)
	{
		release_staging();
		std::vector<char>().swap(clone_);
	}
}

void VulkanTexture::upload_to_gpu()
{
	if (staging_buffer_ == VK_NULL_HANDLE)
	{
		throw std::logic_error("a texture without its CPU copy (TextureUsage::keep_cpu_copy) cannot be uploaded again");
	}
	VkCommandBuffer command_buffer = renderer().begin_single_time_commands();
	record_upload(command_buffer);
	renderer().end_single_time_commands(command_buffer);
	finish_upload();
}

void VulkanTexture::upload_mipmapped(VkCommandBuffer command_buffer)
{
	// Every level to TRANSFER_DST, level 0 copied in; then each level i is
	// blitted (linear) from level i - 1, which turns TRANSFER_SRC first and
	// SHADER_READ_ONLY after; the last level goes straight to SHADER_READ_ONLY.
	ImageUse const undefined = {VK_IMAGE_LAYOUT_UNDEFINED, 0, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT};
	ImageUse const written = {VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT};
	ImageUse const read = {VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT};
	ImageUse const sampled = {VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT};
	auto barrier = [&](uint32_t level, uint32_t count, ImageUse const &from, ImageUse const &to) {
		record_image_barrier(command_buffer, image_, from, to, level, count, layer_count());
	};
	uint32_t const levels = static_cast<uint32_t>(mip_levels_);
	barrier(0, levels, undefined, written);

	// level 0 of every layer, the layers one after the other in the staging buffer
	VkBufferImageCopy region;
	std::memset(&region, 0, sizeof(region));
	region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	region.imageSubresource.layerCount = layer_count();
	region.imageExtent.width = static_cast<uint32_t>(width_);
	region.imageExtent.height = static_cast<uint32_t>(height_);
	region.imageExtent.depth = 1;
	vkCmdCopyBufferToImage(command_buffer, staging_buffer_, image_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

	int32_t w = width_, h = height_;
	for (uint32_t i = 1; i < levels; ++i)
	{
		barrier(i - 1, 1, written, read);
		int32_t const nw = std::max(1, w / 2), nh = std::max(1, h / 2);
		VkImageBlit blit;
		std::memset(&blit, 0, sizeof(blit));
		blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		blit.srcSubresource.mipLevel = i - 1;
		blit.srcSubresource.layerCount = layer_count();
		blit.srcOffsets[1] = {w, h, 1};
		blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		blit.dstSubresource.mipLevel = i;
		blit.dstSubresource.layerCount = layer_count();
		blit.dstOffsets[1] = {nw, nh, 1};
		vkCmdBlitImage(command_buffer, image_, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			image_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
		barrier(i - 1, 1, read, sampled);
		w = nw;
		h = nh;
	}
	barrier(levels - 1, 1, written, sampled);
}

void VulkanTexture::upload_levels(VkCommandBuffer command_buffer)
{
	// every level of every layer from the staging buffer, one after the other: a layer's levels, largest first, then the next layer
	uint32_t const levels = static_cast<uint32_t>(mip_levels_), layers = layer_count();
	ImageUse const written = {VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT};
	record_image_barrier(command_buffer, image_, {VK_IMAGE_LAYOUT_UNDEFINED, 0, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT}, written, 0, levels, layers);
	std::vector<VkBufferImageCopy> regions(size_t(levels) * layers);
	VkDeviceSize offset = 0;
	for (uint32_t layer = 0; layer < layers; ++layer)
		for (uint32_t i = 0; i < levels; ++i)
		{
			VkBufferImageCopy &r = regions[layer * levels + i];
			std::memset(&r, 0, sizeof(r));
			r.bufferOffset = offset;
			r.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			r.imageSubresource.mipLevel = i;
			r.imageSubresource.baseArrayLayer = layer;
			r.imageSubresource.layerCount = 1;
			r.imageExtent.width = static_cast<uint32_t>(std::max(1, width_ >> i));
			r.imageExtent.height = static_cast<uint32_t>(std::max(1, height_ >> i));
			r.imageExtent.depth = 1;
			offset += Texture::level_size(width_, height_, pixel_format_, int(i));
		}
	vkCmdCopyBufferToImage(command_buffer, staging_buffer_, image_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		uint32_t(regions.size()), regions.data());
	record_image_barrier(command_buffer, image_, written,
		{VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT}, 0, levels, layers);
}

void VulkanTexture::check_downloadable() const
{
	if (usage_.array_layers > 0)
	{
		throw std::logic_error("an array texture cannot be downloaded");
	}
	if (!usage_.keep_cpu_copy || clone_.empty() || staging_mapped_ == nullptr)
	{
		throw std::logic_error("a texture without its CPU copy (TextureUsage::keep_cpu_copy) cannot be downloaded");
	}
}

void VulkanTexture::download()
{
	check_downloadable();
	if (renderer().in_frame())
	{
		throw std::runtime_error("Vulkan texture download requires an idle renderer; call after finish_frame()");
	}
	if (!written())
	{
		return;
	}
	VkCommandBuffer const command_buffer = renderer().begin_single_time_commands();
	record_download(command_buffer);
	renderer().end_single_time_commands(command_buffer);
	finish_download();
}

void VulkanTexture::record_download(VkCommandBuffer command_buffer) const
{
	if (current_layout_ != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
	{
		record_image_layout_transition(command_buffer, image_, current_layout_, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
	}
	VkBufferImageCopy region;
	std::memset(&region, 0, sizeof(region));
	region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	region.imageSubresource.layerCount = 1;
	region.imageExtent.width = static_cast<uint32_t>(width_);
	region.imageExtent.height = static_cast<uint32_t>(height_);
	region.imageExtent.depth = 1;
	vkCmdCopyImageToBuffer(command_buffer, image_, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging_buffer_, 1, &region);
	if (current_layout_ != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
	{
		record_image_layout_transition(command_buffer, image_, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, current_layout_);
	}
}

void VulkanTexture::finish_download()
{
	// the staging memory is host-coherent: once the command buffer has run, the host sees what the copy wrote
	std::memcpy(&clone_[0], staging_mapped_, clone_.size());
}

void VulkanTexture::transition_layout(VkImageLayout new_layout)
{
	if (depth_ || stencil_of_)
	{
		throw std::logic_error("a depth or stencil texture's layout is the renderer's to change (begin_pass, end_pass)");
	}
	if (current_layout_ == new_layout)
	{
		return;
	}

	renderer().transition_image_layout(image_, current_layout_, new_layout);
	current_layout_ = new_layout;
}

const void *VulkanTexture::do_lock() const
{
	if (clone_.empty())
	{
		throw std::logic_error("a texture without its CPU copy (TextureUsage::keep_cpu_copy) cannot be read");
	}
	return clone_.data();
}

void VulkanTexture::do_unlock() const
{
}

void *VulkanTexture::do_lock(bool discard)
{
	(void)discard;
	if (staging_buffer_ == VK_NULL_HANDLE)
	{
		throw std::logic_error("a texture without its CPU copy (TextureUsage::keep_cpu_copy) cannot be written");
	}
	return clone_.data();
}

void VulkanTexture::do_unlock()
{
	upload_to_gpu();
}

}
