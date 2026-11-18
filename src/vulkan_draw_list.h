// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

#include <vulkan/vulkan.h>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>
#include "draw_list.h"

namespace edge
{

class Material;
class VertexTraits;
class VulkanRenderer;

/// The viewport and scissor of the rectangle x, y (from the target's top left corner), w x h pixels, recorded into @p command_buffer,
/// flipped so that clip space's +Y is the rectangle's top.
void record_viewport(VkCommandBuffer command_buffer, int x, int y, int width, int height);

/// A list of a Vulkan pass: the frame's command buffer in a pass of one list, a secondary command buffer of the pass in a pass of
/// several, recorded by one thread.  Its draws are VulkanRenderer::record_draw().
class VulkanDrawList final: public DrawList {
public:
	explicit VulkanDrawList(VulkanRenderer &renderer);

	VkCommandBuffer commands() const { return commands_; }
	/// A new pass: the list records into @p commands, and forgets what materials drew with in the last (another render pass, pass
	/// type).
	void begin(VkCommandBuffer commands);

	/// What a material draws with in this pass, for vertices of one layout, as of its revision: looked up again when the material
	/// has changed (VulkanRenderer::record_draw).
	struct Bound
	{
		uint64_t revision = 0;
		VkPipeline pipeline = VK_NULL_HANDLE;       // none: not looked up yet
		VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
	};
	struct BoundKey
	{
		Material const *material;
		VertexTraits const *traits;
		bool operator==(BoundKey const &o) const { return material == o.material && traits == o.traits; }
	};
	struct BoundKeyHash
	{
		size_t operator()(BoundKey const &k) const
		{
			return std::hash<void const *>()(k.material) * 31 + std::hash<void const *>()(k.traits);
		}
	};
	/// (a material drawn is held by the list until the pass ends: no other takes its address meanwhile)
	std::unordered_map<BoundKey, Bound, BoundKeyHash> bound;

protected:
	void do_set_viewport(int x, int y, int width, int height) override;
	void do_draw(DrawCall const &call) override;

private:
	VulkanRenderer &renderer_;
	VkCommandBuffer commands_ = VK_NULL_HANDLE;
};

/// The lists of a VulkanRenderer's passes, kept from pass to pass, and the command pools the lists of a pass of several record
/// from: for each share of the host's memory (a frame in flight), pool i is the i-th list's, with the secondary command buffers it
/// has handed out in the share's frame (kept, and begun again by the share's next frame).  The renderer's thread alone calls these.
class VulkanDrawLists {
public:
	VulkanDrawLists(VulkanRenderer &renderer, VkDevice device, uint32_t queue_family);
	/// The pools, and their buffers with them.
	~VulkanDrawLists();
	VulkanDrawLists(VulkanDrawLists const &) = delete;
	VulkanDrawLists &operator=(VulkanDrawLists const &) = delete;

	/// The frame of @p share is drawn: what its lists recorded is free to be recorded over.
	void reset(uint32_t share);
	/// A pass of one list, recorded into the frame's command buffer @p primary (begun, with its viewport set).
	std::vector<DrawList *> begin_inline(VkCommandBuffer primary);
	/// The secondary command buffers of a pass of @p lists lists, of @p share's pools: before the render pass begins, so that
	/// what throws here leaves none open.
	void allocate(uint32_t share, size_t lists);
	/// The lists of the pass allocate() was for, once its render pass @p render_pass into @p framebuffer has begun: each buffer
	/// begun to continue it, its viewport the whole @p extent.
	std::vector<DrawList *> begin_secondary(VkRenderPass render_pass, VkFramebuffer framebuffer, VkExtent2D extent);
	/// The pass ends: its secondary buffers, if any, ended and executed into @p primary in their order.
	void end(VkCommandBuffer primary);

private:
	struct Pool
	{
		VkCommandPool pool = VK_NULL_HANDLE;
		std::vector<VkCommandBuffer> buffers;
		size_t used = 0;
	};
	VulkanRenderer &renderer_;
	VkDevice const device_;
	uint32_t const queue_family_;
	std::vector<std::vector<Pool>> pools_;
	/// The active pass's secondary command buffers, in the order they are drawn: none for a pass of one list.
	std::vector<VkCommandBuffer> secondaries_;
	/// As many as the most a pass has had.
	std::vector<std::unique_ptr<VulkanDrawList>> lists_;
	VulkanDrawList &list(size_t index);
};

}
