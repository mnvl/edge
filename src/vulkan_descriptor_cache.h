// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

#include <vulkan/vulkan.h>
#include <cstddef>
#include <map>
#include <memory>
#include <utility>
#include <vector>

#include "material.h"
#include "renderer.h"
#include "texture.h"
#include "vulkan_dynamic_uniform_buffer.h"

namespace edge
{

/// The descriptor set layout every pipeline is made against, the pipeline layout, the samplers, and one descriptor
/// set per live material.
///
/// Bindings: 0-5 material textures, 6 joint matrices, 7 instances, 8 draw constants, 9 frame constants, 10-11 the material's PBR
/// textures, 12-19 the global textures, 20 the material's parameters, 21 the previous instances, 22 the previous joint matrices.  The
/// uniform buffers are bound with dynamic offsets, in binding order: a draw's own blocks, and the region of the frame it is recorded in
/// for the frame constants.
class VulkanDescriptorCache {
public:
	/// Textures in a descriptor set: the material's 8 (diffuse 0-3, blend weights, normal map at bindings 0-5;
	/// metallic-roughness and emissive at 10-11) and the global ones (12-19).
	static constexpr size_t MATERIAL_TEXTURES = 8;
	static constexpr unsigned GLOBAL_TEXTURES_COUNT = Renderer::GLOBAL_TEXTURES_COUNT;
	static constexpr size_t DESCRIPTOR_TEXTURES = MATERIAL_TEXTURES + GLOBAL_TEXTURES_COUNT;
	/// Sets a pool holds; when every pool is full another is made (a dead material's set is reclaimed at the next frame boundary,
	/// into the pool it came from, so a renderer that streams materials keeps to one).
	static constexpr uint32_t MAX_SETS = 8192;
	/// The uniform buffers of a set, bound with dynamic offsets in binding order: 6 joint matrices, 7 instances, 8 draw constants, 9 frame
	/// constants, 20 material parameters, 21 the previous instances (in the instances' buffer), 22 the previous joint matrices (in the
	/// joint matrices' buffer).  Vulkan promises 8 dynamic ones a set.
	static constexpr uint32_t UNIFORM_BUFFERS = 7;

	/// The uniform buffers the sets point at.  They must be created before the first get() and outlive the cache.
	struct Constants
	{
		VulkanDynamicUniformBuffer const *joint_matrices;
		VulkanDynamicUniformBuffer const *instances;
		VulkanDynamicUniformBuffer const *draw_constants;
		VulkanDynamicUniformBuffer const *frame_constants;
		VulkanDynamicUniformBuffer const *material_parameters;
	};

	/// The anisotropy the material sampler asks for on a device named `device_name` whose limit is `limit`: 8 at most, and none (1) on the Steam
	/// Deck's APU under RADV (VANGOGH), whose anisotropic filtering (seen with Mesa 26.2.0-devel) returns scattered garbage texels.  Other RADV
	/// devices keep it.  Temporary: lift it when the driver is fixed.
	static float sampler_anisotropy(char const *device_name, float limit);

	/// anisotropy_enabled: the device was made with samplerAnisotropy.  depth_format: the renderer's, which a shadow texture's
	/// comparison sampler filters linearly where the device can.  max_sets: each pool's capacity (MAX_SETS by default; a test
	/// shrinking it to make a stream that exhausts a pool affordable passes a smaller one instead).
	VulkanDescriptorCache(VkDevice device, VkPhysicalDevice physical_device, bool anisotropy_enabled,
		Constants const &constants, VkFormat depth_format, uint32_t max_sets = MAX_SETS);
	/// The GPU must be idle.  The pools take the sets with them.
	~VulkanDescriptorCache();
	VulkanDescriptorCache(VulkanDescriptorCache const &) = delete;
	VulkanDescriptorCache &operator=(VulkanDescriptorCache const &) = delete;

	VkPipelineLayout pipeline_layout() const { return pipeline_layout_; }

	/// The set describing material (null for none) with the global textures, each sampled with a comparison where global_compare
	/// says (a shadow texture); a texture that is missing is replaced by default_texture.  Reused while the material and every
	/// texture it was written for are the same live objects, sampled alike; otherwise the old set is retired (it may be recorded in
	/// `frame`, the frame being recorded) and a new one made.
	VkDescriptorSet get(MaterialPtr const &material, TexturePtr const (&global_textures)[GLOBAL_TEXTURES_COUNT],
		bool const (&global_compare)[GLOBAL_TEXTURES_COUNT], TexturePtr const &default_texture, uint64_t frame);

	/// Retires the sets of materials that have died (frames up to `recorded`, the last one recorded, may hold them), then frees every
	/// retired set no frame after `completed`, the last one the GPU is known to have finished, can still hold.
	void reclaim(uint64_t recorded, uint64_t completed);
	/// Frees the retired sets no frame after `completed` can hold (as reclaim(), without looking for dead materials).
	void free_drawn(uint64_t completed);

	/// Sets allocated: one per live material that has been drawn, those of materials that died since the last reclaim(), and the
	/// retired ones a frame in flight may still hold.
	size_t count() const { return sets_.size() + retired_.size(); }
	/// Pools made: one, until more materials lived at once than it holds.
	size_t pool_count() const { return pools_.size(); }

private:
	/// One entry per live material.  The map is keyed by the material's address for lookup, but the address alone
	/// cannot tell a live material from a dead one whose block the allocator handed out again, hence the weak
	/// references: a set is only reused while the material and every texture it was written for are still the same
	/// live objects.  Anything else is stale and is retired, never rewritten in place (the old set may already be
	/// recorded in this frame).
	struct Entry
	{
		Entry():
			descriptor_set(VK_NULL_HANDLE)
		{
		}

		bool matches(Material const *material_key, Texture const *const texture_keys[DESCRIPTOR_TEXTURES],
			bool const (&compare)[GLOBAL_TEXTURES_COUNT]) const;

		VkDescriptorSet descriptor_set;
		VkDescriptorPool pool = VK_NULL_HANDLE;   // descriptor_set's
		std::weak_ptr<Material const> material;
		std::weak_ptr<Texture const> bound_textures[DESCRIPTOR_TEXTURES];
		bool bound_compare[GLOBAL_TEXTURES_COUNT] = {};
	};

	void destroy();
	VkDescriptorPool create_pool();
	/// A set of the layout from the newest pool with room, or from a new one when none has.
	std::pair<VkDescriptorSet, VkDescriptorPool> allocate();

	VkDevice device_;
	Constants constants_;
	VkDescriptorSetLayout layout_ = VK_NULL_HANDLE;
	VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
	uint32_t max_sets_;
	std::vector<VkDescriptorPool> pools_;
	VkSampler texture_sampler_ = VK_NULL_HANDLE;
	VkSampler clamp_sampler_ = VK_NULL_HANDLE;      // the global textures
	VkSampler compare_sampler_ = VK_NULL_HANDLE;    // a shadow texture's
	std::map<Material const *, Entry> sets_;
	/// Sets whose material died or changed textures, each with the last frame that may hold it, awaiting reclaim().
	struct Retired
	{
		VkDescriptorSet set;
		VkDescriptorPool pool;
		uint64_t frame;
	};
	std::vector<Retired> retired_;
};

}
