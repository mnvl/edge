// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include "vulkan_descriptor_cache.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

#include "vulkan_error.h"
#include "shader_interface.h"
#include "vulkan_texture.h"

namespace edge
{

float VulkanDescriptorCache::sampler_anisotropy(char const *device_name, float limit)
{
	// RADV names its devices "AMD ... (RADV <chip>)"; VANGOGH is the Steam Deck's APU
	if (std::strstr(device_name, "RADV VANGOGH"))
		return 1.0f;
	// lavapipe (Mesa's CPU Vulkan, "llvmpipe (LLVM ...)") samples NaN anisotropically where the UVs do not change (Mesa 25.0)
	if (std::strncmp(device_name, "llvmpipe", 8) == 0)
		return 1.0f;
	return std::min(8.0f, limit);
}

VulkanDescriptorCache::VulkanDescriptorCache(VkDevice device, VkPhysicalDevice physical_device, bool anisotropy_enabled,
	Constants const &constants, VkFormat depth_format, uint32_t max_sets):
	device_(device),
	constants_(constants)
{
	// Whatever was made before a throw goes with it: a destructor does not run for a constructor that threw.
	try
	{
		VkPhysicalDeviceProperties physical_device_properties;
		std::memset(&physical_device_properties, 0, sizeof(physical_device_properties));
		vkGetPhysicalDeviceProperties(physical_device, &physical_device_properties);

		// 0-5 material textures (a material's own vertex shader may sample them too: test_vertex_texture.cc), 6 joint matrices,
		// 7 instances, 8 draw constants, 9 frame constants, 10-11 material PBR textures, 12-19 the global textures, 20 the material's
		// parameters, 21 and 22 the previous frame's instances and joint matrices
		std::vector<VkDescriptorSetLayoutBinding> bindings;
		auto bind = [&](uint32_t binding, VkDescriptorType type, VkShaderStageFlags stages)
		{
			VkDescriptorSetLayoutBinding b;
			std::memset(&b, 0, sizeof(b));
			b.binding = binding;
			b.descriptorCount = 1;
			b.descriptorType = type;
			b.stageFlags = stages;
			bindings.push_back(b);
		};
		VkShaderStageFlags const both = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
		for (uint32_t i = 0; i < 6; ++i)
			bind(i, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, both);
		bind(6, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, VK_SHADER_STAGE_VERTEX_BIT);
		bind(7, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, VK_SHADER_STAGE_VERTEX_BIT);
		bind(8, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, VK_SHADER_STAGE_VERTEX_BIT);
		bind(9, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, both);
		for (uint32_t i = 10; i < 12 + GLOBAL_TEXTURES_COUNT; ++i)
			bind(i, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, both);
		bind(20, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, both);
		bind(21, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, VK_SHADER_STAGE_VERTEX_BIT);
		bind(22, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, VK_SHADER_STAGE_VERTEX_BIT);
		if (physical_device_properties.limits.maxDescriptorSetUniformBuffersDynamic < UNIFORM_BUFFERS)
		{
			throw std::runtime_error("the Vulkan device binds " + std::to_string(physical_device_properties.limits.maxDescriptorSetUniformBuffersDynamic)
				+ " dynamic uniform buffers a set, edge's set has " + std::to_string(UNIFORM_BUFFERS));
		}

		VkDescriptorSetLayoutCreateInfo layout_info;
		std::memset(&layout_info, 0, sizeof(layout_info));
		layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		layout_info.bindingCount = static_cast<uint32_t>(bindings.size());
		layout_info.pBindings = bindings.data();

		check_vk_result(vkCreateDescriptorSetLayout(device_, &layout_info, nullptr, &layout_),
			"vkCreateDescriptorSetLayout");

		VkPushConstantRange push_constant_range;
		std::memset(&push_constant_range, 0, sizeof(push_constant_range));
		push_constant_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
		push_constant_range.offset = 0;
		push_constant_range.size = sizeof(shader::PushConstants);
		if (push_constant_range.size > physical_device_properties.limits.maxPushConstantsSize)
		{
			throw std::runtime_error("Vulkan device push constant size is too small for draw constants");
		}

		VkPipelineLayoutCreateInfo pipeline_layout_info;
		std::memset(&pipeline_layout_info, 0, sizeof(pipeline_layout_info));
		pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		pipeline_layout_info.setLayoutCount = 1;
		pipeline_layout_info.pSetLayouts = &layout_;
		pipeline_layout_info.pushConstantRangeCount = 1;
		pipeline_layout_info.pPushConstantRanges = &push_constant_range;

		check_vk_result(vkCreatePipelineLayout(device_, &pipeline_layout_info, nullptr, &pipeline_layout_),
			"vkCreatePipelineLayout");

		// the uniform buffers are bound with dynamic offsets: each frame in flight reads its own region of them
		VkDescriptorPoolSize pool_sizes[2];
		std::memset(&pool_sizes, 0, sizeof(pool_sizes));
		pool_sizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		pool_sizes[0].descriptorCount = max_sets * DESCRIPTOR_TEXTURES;
		pool_sizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
		pool_sizes[1].descriptorCount = max_sets * UNIFORM_BUFFERS;

		VkDescriptorPoolCreateInfo pool_info;
		std::memset(&pool_info, 0, sizeof(pool_info));
		pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		// Sets are returned to the pool one at a time as their materials die.
		pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
		pool_info.maxSets = max_sets;
		pool_info.poolSizeCount = 2;
		pool_info.pPoolSizes = pool_sizes;

		check_vk_result(vkCreateDescriptorPool(device_, &pool_info, nullptr, &pool_),
			"vkCreateDescriptorPool");

		VkSamplerCreateInfo sampler_info;
		std::memset(&sampler_info, 0, sizeof(sampler_info));
		sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		sampler_info.magFilter = VK_FILTER_LINEAR;
		sampler_info.minFilter = VK_FILTER_LINEAR;
		sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		// Trilinear over whatever mips a texture has, anisotropic where the device can.
		sampler_info.anisotropyEnable = anisotropy_enabled ? VK_TRUE : VK_FALSE;
		sampler_info.maxAnisotropy = anisotropy_enabled
			? sampler_anisotropy(physical_device_properties.deviceName, physical_device_properties.limits.maxSamplerAnisotropy) : 1.0f;
		sampler_info.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
		sampler_info.unnormalizedCoordinates = VK_FALSE;
		sampler_info.compareEnable = VK_FALSE;
		sampler_info.compareOp = VK_COMPARE_OP_ALWAYS;
		sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		sampler_info.mipLodBias = 0.0f;
		sampler_info.minLod = 0.0f;
		sampler_info.maxLod = VK_LOD_CLAMP_NONE;

		check_vk_result(vkCreateSampler(device_, &sampler_info, nullptr, &texture_sampler_), "vkCreateSampler");

		// The global textures (shadow maps, earlier passes): clamped, no anisotropy.
		sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		sampler_info.anisotropyEnable = VK_FALSE;
		sampler_info.maxAnisotropy = 1.0f;
		check_vk_result(vkCreateSampler(device_, &sampler_info, nullptr, &clamp_sampler_), "vkCreateSampler(clamp)");

		// A shadow texture (Renderer::global_shadow_texture): the depth compared with the reference, less or equal, then filtered
		// linearly where the device filters its depth format (a 2 x 2 PCF in the sampler), else the nearest texel's answer.
		VkFormatProperties depth_properties;
		std::memset(&depth_properties, 0, sizeof(depth_properties));
		vkGetPhysicalDeviceFormatProperties(physical_device, depth_format, &depth_properties);
		VkFilter const depth_filter = (depth_properties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) != 0
			? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
		sampler_info.magFilter = depth_filter;
		sampler_info.minFilter = depth_filter;
		sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
		sampler_info.compareEnable = VK_TRUE;
		sampler_info.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
		check_vk_result(vkCreateSampler(device_, &sampler_info, nullptr, &compare_sampler_), "vkCreateSampler(compare)");
	}
	catch (...)
	{
		destroy();
		throw;
	}
}

VulkanDescriptorCache::~VulkanDescriptorCache()
{
	destroy();
}

void VulkanDescriptorCache::destroy()
{
	// The pool takes the sets with it; no need to free them individually.
	sets_.clear();
	retired_.clear();
	if (compare_sampler_ != VK_NULL_HANDLE)
	{
		vkDestroySampler(device_, compare_sampler_, nullptr);
		compare_sampler_ = VK_NULL_HANDLE;
	}
	if (clamp_sampler_ != VK_NULL_HANDLE)
	{
		vkDestroySampler(device_, clamp_sampler_, nullptr);
		clamp_sampler_ = VK_NULL_HANDLE;
	}
	if (texture_sampler_ != VK_NULL_HANDLE)
	{
		vkDestroySampler(device_, texture_sampler_, nullptr);
		texture_sampler_ = VK_NULL_HANDLE;
	}
	if (pool_ != VK_NULL_HANDLE)
	{
		vkDestroyDescriptorPool(device_, pool_, nullptr);
		pool_ = VK_NULL_HANDLE;
	}
	if (pipeline_layout_ != VK_NULL_HANDLE)
	{
		vkDestroyPipelineLayout(device_, pipeline_layout_, nullptr);
		pipeline_layout_ = VK_NULL_HANDLE;
	}
	if (layout_ != VK_NULL_HANDLE)
	{
		vkDestroyDescriptorSetLayout(device_, layout_, nullptr);
		layout_ = VK_NULL_HANDLE;
	}
}

VkDescriptorSet VulkanDescriptorCache::get(MaterialPtr const &material, TexturePtr const (&global_textures)[GLOBAL_TEXTURES_COUNT],
	bool const (&global_compare)[GLOBAL_TEXTURES_COUNT], TexturePtr const &default_texture, uint64_t frame)
{
	// Material textures 0-7 (bindings 0-5, 10, 11), then the global ones (12-19).
	TexturePtr textures[DESCRIPTOR_TEXTURES];
	Texture const *texture_keys[DESCRIPTOR_TEXTURES];
	for (size_t i = 0; i < 4; ++i)
	{
		if (material && i < material->diffuse_textures_count())
		{
			textures[i] = material->diffuse_texture(i);
		}
	}
	if (material)
	{
		textures[4] = material->texture_blending_weights_texture();
		textures[5] = material->normal_map_texture();
		textures[6] = material->metallic_roughness_texture();
		textures[7] = material->emissive_texture();
	}
	for (size_t i = 0; i < GLOBAL_TEXTURES_COUNT; ++i)
	{
		textures[MATERIAL_TEXTURES + i] = global_textures[i];
	}
	for (size_t i = 0; i < DESCRIPTOR_TEXTURES; ++i)
	{
		if (!textures[i])
		{
			textures[i] = default_texture;
		}
		texture_keys[i] = textures[i].get();
	}

	Entry &entry = sets_[material.get()];
	if (entry.descriptor_set != VK_NULL_HANDLE)
	{
		if (entry.matches(material.get(), texture_keys, global_compare))
		{
			return entry.descriptor_set;
		}
		// Dead material at a recycled address, or a live one whose textures
		// changed: either way the set on file describes objects that are
		// gone or wrong.  It may be recorded in the current frame's command
		// buffer already, so it is freed once that frame is drawn rather
		// than rewritten under the GPU's feet.
		retired_.push_back({entry.descriptor_set, frame});
		entry.descriptor_set = VK_NULL_HANDLE;
	}

	VkDescriptorSetAllocateInfo alloc_info;
	std::memset(&alloc_info, 0, sizeof(alloc_info));
	alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	alloc_info.descriptorPool = pool_;
	alloc_info.descriptorSetCount = 1;
	alloc_info.pSetLayouts = &layout_;
	check_vk_result(vkAllocateDescriptorSets(device_, &alloc_info, &entry.descriptor_set),
		"vkAllocateDescriptorSets");
	entry.material = material;

	VulkanTexture *fallback_texture = dynamic_cast<VulkanTexture *>(default_texture.get());
	if (!fallback_texture)
	{
		throw std::logic_error("default Vulkan texture is not available");
	}

	uint32_t texture_bindings[DESCRIPTOR_TEXTURES] = {0, 1, 2, 3, 4, 5, 10, 11};
	for (uint32_t i = 0; i < GLOBAL_TEXTURES_COUNT; ++i)
	{
		texture_bindings[MATERIAL_TEXTURES + i] = 12 + i;
	}
	VkDescriptorImageInfo image_infos[DESCRIPTOR_TEXTURES];
	VkDescriptorBufferInfo buffer_infos[UNIFORM_BUFFERS];
	VkWriteDescriptorSet descriptor_writes[DESCRIPTOR_TEXTURES + UNIFORM_BUFFERS];
	std::memset(image_infos, 0, sizeof(image_infos));
	std::memset(buffer_infos, 0, sizeof(buffer_infos));
	std::memset(descriptor_writes, 0, sizeof(descriptor_writes));

	for (uint32_t i = 0; i < DESCRIPTOR_TEXTURES; ++i)
	{
		// no texture: white; a texture of another renderer: a mistake
		VulkanTexture *vulkan_texture = textures[i] ? dynamic_cast<VulkanTexture *>(textures[i].get()) : fallback_texture;
		if (!vulkan_texture)
		{
			throw std::invalid_argument("texture '" + textures[i]->filename() + "' is not a texture of this Vulkan renderer");
		}

		image_infos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		image_infos[i].imageView = vulkan_texture->image_view();
		image_infos[i].sampler = i < MATERIAL_TEXTURES ? texture_sampler_
			: global_compare[i - MATERIAL_TEXTURES] ? compare_sampler_ : clamp_sampler_;

		descriptor_writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		descriptor_writes[i].dstSet = entry.descriptor_set;
		descriptor_writes[i].dstBinding = texture_bindings[i];
		descriptor_writes[i].dstArrayElement = 0;
		descriptor_writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		descriptor_writes[i].descriptorCount = 1;
		descriptor_writes[i].pImageInfo = &image_infos[i];
	}

	struct
	{
		uint32_t binding;
		VkDescriptorType type;
		VkBuffer buffer;
		VkDeviceSize range;
	} const buffers[UNIFORM_BUFFERS] = {
		{6, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, constants_.joint_matrices->buffer(), constants_.joint_matrices->block_size()},
		{7, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, constants_.instances->buffer(), constants_.instances->block_size()},
		{8, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, constants_.draw_constants->buffer(), constants_.draw_constants->block_size()},
		{9, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, constants_.frame_constants->buffer(), constants_.frame_constants->block_size()},
		{20, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, constants_.material_parameters->buffer(),
			constants_.material_parameters->block_size()},
		{21, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, constants_.instances->buffer(), constants_.instances->block_size()},
		{22, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, constants_.joint_matrices->buffer(), constants_.joint_matrices->block_size()},
	};
	for (uint32_t k = 0; k < UNIFORM_BUFFERS; ++k)
	{
		buffer_infos[k].buffer = buffers[k].buffer;
		buffer_infos[k].offset = 0;
		buffer_infos[k].range = buffers[k].range;
		VkWriteDescriptorSet &w = descriptor_writes[DESCRIPTOR_TEXTURES + k];
		w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		w.dstSet = entry.descriptor_set;
		w.dstBinding = buffers[k].binding;
		w.dstArrayElement = 0;
		w.descriptorType = buffers[k].type;
		w.descriptorCount = 1;
		w.pBufferInfo = &buffer_infos[k];
	}

	vkUpdateDescriptorSets(device_, DESCRIPTOR_TEXTURES + UNIFORM_BUFFERS, descriptor_writes, 0, nullptr);

	for (size_t i = 0; i < DESCRIPTOR_TEXTURES; ++i)
	{
		entry.bound_textures[i] = textures[i];
	}
	for (size_t i = 0; i < GLOBAL_TEXTURES_COUNT; ++i)
	{
		entry.bound_compare[i] = global_compare[i];
	}
	return entry.descriptor_set;
}

bool VulkanDescriptorCache::Entry::matches(Material const *material_key,
	Texture const *const texture_keys[DESCRIPTOR_TEXTURES], bool const (&compare)[GLOBAL_TEXTURES_COUNT]) const
{
	for (size_t i = 0; i < GLOBAL_TEXTURES_COUNT; ++i)
	{
		if (bound_compare[i] != compare[i])
		{
			return false;
		}
	}
	// lock() rather than expired(): a weak_ptr to a dead object compares
	// equal to nothing, and a live object at the same address is a different
	// object precisely when the old one has expired.
	std::shared_ptr<Material const> live_material = material.lock();
	if (live_material.get() != material_key)
	{
		return false;
	}
	for (size_t i = 0; i < DESCRIPTOR_TEXTURES; ++i)
	{
		std::shared_ptr<Texture const> live_texture = bound_textures[i].lock();
		if (live_texture.get() != texture_keys[i])
		{
			return false;
		}
	}
	return true;
}

void VulkanDescriptorCache::reclaim(uint64_t recorded, uint64_t completed)
{
	for (auto it = sets_.begin();
		it != sets_.end();)
	{
		// the set of draws without a material (key null) has no material to die
		if (it->first != nullptr && it->second.material.expired())
		{
			// A null set is an entry whose allocation threw: nothing to free.
			if (it->second.descriptor_set != VK_NULL_HANDLE)
			{
				retired_.push_back({it->second.descriptor_set, recorded});
			}
			it = sets_.erase(it);
		}
		else
		{
			++it;
		}
	}

	free_drawn(completed);
}

void VulkanDescriptorCache::free_drawn(uint64_t completed)
{
	std::vector<VkDescriptorSet> drawn;
	std::vector<Retired> held;
	for (Retired const &r : retired_)
	{
		(r.frame <= completed ? drawn.push_back(r.set) : held.push_back(r));
	}
	if (!drawn.empty())
	{
		check_vk_result(vkFreeDescriptorSets(device_, pool_,
			static_cast<uint32_t>(drawn.size()),
			drawn.data()), "vkFreeDescriptorSets");
	}
	retired_.swap(held);
}

}
