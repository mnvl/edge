// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

#include <vulkan/vulkan.h>
#include <algorithm>
#include <vector>

#include "texture.h"
#include "vulkan_device_context.h"

namespace edge
{

/// VulkanTexture::Layers.
struct VulkanTextureLayers
{
	int count = 0;
	bool cube = false;
};

class VulkanTexture: public Texture
{

public:
	VulkanTexture(VulkanRenderer *renderer_ptr, int width, int height, PixelFormat pixel_format,
		TextureUsage const &usage = TextureUsage());
	/// The layers of a render target's texture (RenderTargetFormat): none, an array of @p count, or a cube (@p count 6).
	using Layers = VulkanTextureLayers;
	/// A colour texture of a render target of layers: an image of @p layers layers, sampled as an array or a cube.  The GPU writes it
	/// and shaders read it; there is no CPU copy, so read(), write() and download() throw std::logic_error.
	VulkanTexture(VulkanRenderer *renderer_ptr, int width, int height, PixelFormat pixel_format, Layers layers);
	/// Tag of the depth texture's constructor.
	struct Depth {};
	/// A render target's readable depth (Renderer::create_render_target): an image of the renderer's depth format that is the target's
	/// depth attachment, sampled through a view of its depth alone (a float in red: its pixel format reads R32F), of @p layers layers.
	/// The GPU writes it and shaders read it; there is no CPU copy, so write() and download() throw std::logic_error.
	VulkanTexture(VulkanRenderer *renderer_ptr, int width, int height, Depth, Layers layers = Layers());
	/// Tag of the stencil texture's constructor.
	struct Stencil {};
	/// A readable depth's stencil (RenderTarget::stencil_texture): a view of the stencil of @p depth's image (its format must have one),
	/// R8UI, an unsigned integer that a shader reads with texelFetch.  It keeps @p depth, whose image it is, alive; the renderer moves that
	/// image's layout for both.  No CPU copy, as the depth's.
	VulkanTexture(std::shared_ptr<VulkanTexture> const &depth, Stencil);
	~VulkanTexture() override;

	/// The VkFormat a texture of @p pixel_format is made of (sRGB where asked); std::invalid_argument for sRGB of a format that has
	/// none, std::runtime_error for a pixel format Vulkan has not.
	static VkFormat vk_format(PixelFormat pixel_format, bool srgb = false);

	/// Whether this is a depth texture (the Depth constructor's).
	bool is_depth() const override { return depth_; }

	void download() override;
	/// What download() refuses, std::logic_error: an array (no one image to read), a texture without its CPU copy.
	void check_downloadable() const;
	/// What an upload refuses, std::logic_error: a texture without its CPU copy.
	void check_uploadable() const;
	/// What streaming refuses, std::logic_error: what check_uploadable() does, and levels or layers.
	void check_streamable() const;
	/// The CPU copy, as written through write().
	void const *copy_data() const { return clone_.empty() ? nullptr : &clone_[0]; }
	size_t copy_size() const { return clone_.size(); }
	/// Records the commands that copy `size` bytes from source at offset into the image, in a frame's command buffer: the image
	/// is waited for where shaders read it, and read again from after the copy.
	void record_stream(VkCommandBuffer command_buffer, VkBuffer source, VkDeviceSize offset);
	/// The copy of level 0 into the staging buffer, recorded into @p command_buffer, the image left in the layout it had;
	/// finish_download() takes the texels once the command buffer has run.  Renderer::batch_download_textures() records
	/// several into one.
	/// Whether the image has been written (uploaded or drawn into): one never written has nothing on the GPU, and a download leaves
	/// the CPU copy as it is.
	bool written() const { return current_layout_ != VK_IMAGE_LAYOUT_UNDEFINED; }
	void record_download(VkCommandBuffer command_buffer) const;
	void finish_download();

	VkImage image() const { return image_; }
	VkImageView image_view() const { return image_view_; }
	VkFormat vk_format() const { return vk_format_; }
	/// The device the image is on: its renderer's.
	VkDevice device() const { return context_->device(); }
	/// (a stencil view's is its depth texture's)
	VkImageLayout current_layout() const { return stencil_of_ ? stencil_of_->current_layout() : current_layout_; }
	void transition_layout(VkImageLayout new_layout);
	void set_current_layout(VkImageLayout layout) { current_layout_ = layout; }

	/// Property flags of the host-visible buffer download() reads back through.
	/// Cached memory is what keeps readback off a ~80 MB/s uncached path.
	VkMemoryPropertyFlags staging_memory_properties() const { return staging_memory_properties_; }

protected:
	const void *do_lock() const override;
	void do_unlock() const override;
	void *do_lock(bool discard) override;
	void do_unlock() override;

private:
	// Keeps the device alive for as long as this texture holds images and
	// allocations made from it, whichever is released last.
	VulkanDeviceContextPtr context_;
	VkImage image_;
	VkDeviceMemory image_memory_;
	VkImageView image_view_;
	VkBuffer staging_buffer_;
	VkDeviceMemory staging_memory_;
	VkMemoryPropertyFlags staging_memory_properties_;
	void *staging_mapped_;
	std::vector<char> clone_;
	VkFormat vk_format_;
	VkImageLayout current_layout_;
	bool depth_;
	std::shared_ptr<VulkanTexture> stencil_of_;      // the depth texture whose image this stencil view reads, which owns it

	/// Throws once the renderer is gone — everything but destruction needs it.
	VulkanRenderer &renderer() const { return context_->require_renderer(); }

	/// Layers of the image: 1 for a plain texture.
	uint32_t layer_count() const { return static_cast<uint32_t>(std::max(1, usage_.array_layers)); }

	void create_image();
	void create_staging_buffer();
	void release_staging();
	void upload_to_gpu();
public:
	/// What upload_to_gpu() does in two halves, for Renderer::batch_upload_textures(): copy the CPU copy to the staging buffer and
	/// record the commands that send it (std::logic_error without a CPU copy), then, once they have run, settle the layout and let go of
	/// the staging buffer unless the CPU copy is kept.
	void record_upload(VkCommandBuffer command_buffer);
	void finish_upload();
private:
	void upload_mipmapped(VkCommandBuffer command_buffer);
	void upload_levels(VkCommandBuffer command_buffer);
};

}
