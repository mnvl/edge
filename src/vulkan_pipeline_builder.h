// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

#include <vulkan/vulkan.h>
#include <map>
#include <memory>
#include <string>
#include "forward.h"
#include "material.h"
#include "shader_dirs.h"

namespace edge
{

class VertexTraits;

// The four Vulkan blend factors of a material: the colour channels by its blend modes (Material::BlendMode); the alpha channel the same
// unless the material preserves the destination alpha, then source zero and destination one (the alpha in the target is left as it is).
// Throws std::runtime_error for a mode that is not one.
struct BlendFactors
{
	VkBlendFactor src_color, dst_color, src_alpha, dst_alpha;
};
BlendFactors blend_factors(Material::BlendMode src_mode, Material::BlendMode dest_mode, bool preserve_destination_alpha);

// (vulkan_shader_dir(), the directory edge's own SPIR-V is read from: shader_dirs.h)

class VulkanPipelineBuilder {
public:
	VulkanPipelineBuilder() = default;
	~VulkanPipelineBuilder() = default;

	VulkanPipelineBuilder(VulkanPipelineBuilder const &) = delete;
	VulkanPipelineBuilder &operator=(VulkanPipelineBuilder const &) = delete;

	/// @p color_attachments: the colours of @p render_pass's subpass, each blended as the material says.
	VkPipeline pipeline(VkDevice device, VkPipelineLayout pipeline_layout,
		VkRenderPass render_pass, uint32_t color_attachments, uint32_t pass_type,
		VertexTraits const *traits, MaterialPtr const &material);

	void destroy_all(VkDevice device);

private:
	struct PipelineKey
	{
		VkRenderPass render_pass;
		VertexTraits const *traits;
		std::string vertex_shader, fragment_shader;      // the program's files, not its address: a freed program's address goes to the next one made
		Material::CullMode cull_mode;
		bool zwrite_enabled;
		int stencil_write;      // the material's stencil value, -1 for none
		bool alpha_blend_enabled;
		bool preserve_destination_alpha;
		Material::BlendMode src_blend_mode;
		Material::BlendMode dest_blend_mode;

		bool operator <(PipelineKey const &rhs) const
		{
			if (render_pass != rhs.render_pass) return render_pass < rhs.render_pass;
			if (traits != rhs.traits) return traits < rhs.traits;
			if (vertex_shader != rhs.vertex_shader) return vertex_shader < rhs.vertex_shader;
			if (fragment_shader != rhs.fragment_shader) return fragment_shader < rhs.fragment_shader;
			if (cull_mode != rhs.cull_mode) return cull_mode < rhs.cull_mode;
			if (zwrite_enabled != rhs.zwrite_enabled) return zwrite_enabled < rhs.zwrite_enabled;
			if (stencil_write != rhs.stencil_write) return stencil_write < rhs.stencil_write;
			if (alpha_blend_enabled != rhs.alpha_blend_enabled) return alpha_blend_enabled < rhs.alpha_blend_enabled;
			if (preserve_destination_alpha != rhs.preserve_destination_alpha) return preserve_destination_alpha < rhs.preserve_destination_alpha;
			if (src_blend_mode != rhs.src_blend_mode) return src_blend_mode < rhs.src_blend_mode;
			return dest_blend_mode < rhs.dest_blend_mode;
		}
	};

	std::map<PipelineKey, VkPipeline> pipelines_;
};

}
