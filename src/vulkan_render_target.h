// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

#include <memory>
#include <vector>
#include <vulkan/vulkan.h>
#include "render_target.h"
#include "vulkan_device_context.h"

namespace edge
{

class VulkanRenderer;

class VulkanRenderTarget: public RenderTarget
{
public:
	/// A target drawing into @p color_textures (VulkanTextures of the target's size; none for a target of depth alone) and, with
	/// @p depth_texture (a depth VulkanTexture of its size), drawing its depth into it and keeping it; without, into a depth image of its own
	/// that each pass throws away.  Textures of layers make a target of layers, which has no framebuffer and makes the target of each
	/// layer; @p layer >= 0 is that target, drawing into layer @p layer through views of that layer alone.
	VulkanRenderTarget(VulkanRenderer *renderer, int width, int height, std::vector<TexturePtr> const &color_textures,
		TexturePtr const &depth_texture = TexturePtr(), int layer = -1);
	~VulkanRenderTarget() override;

	/// The pass and framebuffer the target is drawn with: none for a target of layers (its layers' targets have them).
	VkRenderPass render_pass() const { return render_pass_; }
	VkFramebuffer framebuffer() const { return framebuffer_; }
	VkExtent2D extent() const { return extent_; }
	/// That of the renderer that made it.
	VulkanDeviceContextPtr const &device_context() const { return context_; }
	/// The image its passes draw their depth into: the readable depth's, or one it throws away, which the layers of a target of layers share.
	VkImage depth_image() const;

private:
	/// A depth image no one reads, with its memory: a plain target's own, or the one the layers of a target of layers share (a pass
	/// draws one layer and clears it).
	struct DepthImage;
	/// The target of layer @p layer, drawing its depth into @p depth_image when it keeps none (null: an image of its own).
	VulkanRenderTarget(VulkanRenderer *renderer, int width, int height, std::vector<TexturePtr> const &color_textures,
		TexturePtr const &depth_texture, int layer, std::shared_ptr<DepthImage> const &depth_image);

	// Keeps the device alive for as long as this target's framebuffer and
	// depth attachment exist, whichever is released last.
	VulkanDeviceContextPtr context_;
	VkRenderPass render_pass_;
	VkExtent2D extent_;
	VkFramebuffer framebuffer_;
	std::shared_ptr<DepthImage> depth_image_;      // the depth no one reads (none with a readable depth: the depth texture has it)
	VkImageView depth_image_view_;     // the attachment's view: of depth_image_ or of the depth texture's image (its layer)
	std::vector<VkImageView> layer_views_;     // a layer's target's: the colour attachments' views of that layer

	VkDevice device() const { return context_->device(); }
	/// Throws once the renderer is gone — everything but destruction needs it.
	VulkanRenderer &renderer() const { return context_->require_renderer(); }

	void create_offscreen_resources();
	void destroy_offscreen_resources();
};

}
