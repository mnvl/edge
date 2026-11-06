// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "material.h"
#include "shader_dirs.h"
#include "shader_interface.h"
#include "shader_program.h"
#include "vertex_traits.h"
#include "vulkan_error.h"
#include "vulkan_pipeline_builder.h"

namespace edge
{

namespace
{

VkFormat resolve_vertex_attribute_format(VertexElement::Type type)
{
	switch (type)
	{
	case VertexElement::Type::FLOAT2:
		return VK_FORMAT_R32G32_SFLOAT;
	case VertexElement::Type::FLOAT3:
		return VK_FORMAT_R32G32B32_SFLOAT;
	case VertexElement::Type::FLOAT4:
		return VK_FORMAT_R32G32B32A32_SFLOAT;
	case VertexElement::Type::UBYTE4:
		return VK_FORMAT_R8G8B8A8_UINT;
	default:
		throw std::runtime_error("unsupported Vulkan vertex attribute type");
	}
}

VkFormat resolve_required_attribute_format(int location)
{
	switch (location)
	{
	case 0:
	case 1:
	case 2:
	case 3:
		return VK_FORMAT_R32G32B32_SFLOAT;
	case 4:
	case 5:
		return VK_FORMAT_R8G8B8A8_UINT;
	case 6:
	case 7:
	case 9:
		return VK_FORMAT_R32G32B32A32_SFLOAT;
	case 8:
		return VK_FORMAT_R32G32_SFLOAT;
	default:
		throw std::runtime_error("unsupported required Vulkan vertex attribute location");
	}
}

VkBlendFactor resolve_blend_factor(Material::BlendMode blend_mode)
{
	switch (blend_mode)
	{
	case Material::BlendMode::ZERO:
		return VK_BLEND_FACTOR_ZERO;
	case Material::BlendMode::ONE:
		return VK_BLEND_FACTOR_ONE;
	case Material::BlendMode::SRC_ALPHA:
		return VK_BLEND_FACTOR_SRC_ALPHA;
	case Material::BlendMode::INV_SRC_ALPHA:
		return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	case Material::BlendMode::DEST_ALPHA:
		return VK_BLEND_FACTOR_DST_ALPHA;
	case Material::BlendMode::INV_DEST_ALPHA:
		return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
	default:
		throw std::runtime_error("unsupported Vulkan blend mode");
	}
}

VkCullModeFlags resolve_cull_mode(Material::CullMode cull_mode)
{
	if (cull_mode == Material::CullMode::NONE)
	{
		return VK_CULL_MODE_NONE;
	}

	return VK_CULL_MODE_BACK_BIT;
}

/// The winding the material's front faces have on the screen (Material::CullMode): CullMode::CCW keeps what runs counter-clockwise, as glTF's
/// front faces do (test_vulkan_cull.cc renders it).
VkFrontFace resolve_front_face(Material::CullMode cull_mode)
{
	return cull_mode == Material::CullMode::CW
		? VK_FRONT_FACE_CLOCKWISE
		: VK_FRONT_FACE_COUNTER_CLOCKWISE;
}

std::string vulkan_shader_path(char const *filename)
{
	return vulkan_shader_dir() + "/" + filename;
}

std::string resolve_vulkan_shader_path(std::string const &filename)
{
	if (filename.empty())
	{
		throw std::invalid_argument("Vulkan shader filename is empty");
	}

	std::filesystem::path path(filename);
	if (path.is_absolute())
	{
		return filename;
	}

	return vulkan_shader_path(filename.c_str());
}

std::vector<unsigned char> read_binary_file(std::string const &filename)
{
	std::ifstream file(filename.c_str(), std::ios::binary | std::ios::ate);
	if (!file.is_open())
	{
		throw std::runtime_error("failed to open Vulkan shader file: " + filename);
	}

	std::ifstream::pos_type const end_pos = file.tellg();
	if (end_pos < 0)
	{
		throw std::runtime_error("failed to read Vulkan shader file size: " + filename);
	}

	size_t const file_size = static_cast<size_t>(end_pos);
	std::vector<unsigned char> bytes(file_size);
	file.seekg(0, std::ios::beg);
	if (file_size > 0)
	{
		file.read(reinterpret_cast<char *>(&bytes[0]), static_cast<std::streamsize>(file_size));
		if (!file)
		{
			throw std::runtime_error("failed to read Vulkan shader file: " + filename);
		}
	}

	return bytes;
}

VkShaderModule create_shader_module(VkDevice device, std::vector<unsigned char> const &code)
{
	size_t const size = code.size();
	if ((size % 4u) != 0u)
	{
		throw std::runtime_error("SPIR-V code size must be divisible by 4");
	}

	std::vector<uint32_t> code_words(size / 4u, 0u);
	if (size > 0)
	{
		std::memcpy(&code_words[0], &code[0], size);
	}

	VkShaderModuleCreateInfo create_info;
	std::memset(&create_info, 0, sizeof(create_info));
	create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	create_info.codeSize = size;
	create_info.pCode = code_words.empty() ? nullptr : &code_words[0];

	VkShaderModule shader_module = VK_NULL_HANDLE;
	check_vk_result(vkCreateShaderModule(device, &create_info, nullptr, &shader_module),
		"vkCreateShaderModule");
	return shader_module;
}

}

BlendFactors blend_factors(Material::BlendMode src_mode, Material::BlendMode dest_mode, bool preserve_destination_alpha)
{
	BlendFactors f;
	f.src_color = resolve_blend_factor(src_mode);
	f.dst_color = resolve_blend_factor(dest_mode);
	f.src_alpha = preserve_destination_alpha ? VK_BLEND_FACTOR_ZERO : f.src_color;
	f.dst_alpha = preserve_destination_alpha ? VK_BLEND_FACTOR_ONE : f.dst_color;
	return f;
}

VkPipeline VulkanPipelineBuilder::pipeline(VkDevice device, VkPipelineLayout pipeline_layout,
	VkRenderPass render_pass, uint32_t color_attachments, uint32_t pass_type,
	VertexTraits const *traits, MaterialPtr const &material)
{
	PipelineKey key;
	ShaderProgramPtr shader_program = material
		? material->shader_program(pass_type)
		: ShaderProgramPtr();
	key.render_pass = render_pass;
	key.traits = traits;
	if (shader_program)
	{
		key.vertex_shader = shader_program->vertex_shader_filename();
		key.fragment_shader = shader_program->fragment_shader_filename();
	}
	key.cull_mode = material ? material->cull_mode() : Material::CullMode::NONE;
	key.zwrite_enabled = material ? material->zwrite_enabled() : true;
	key.stencil_write = material && material->stencil_write() ? static_cast<int>(*material->stencil_write()) : -1;
	key.alpha_blend_enabled = material ? material->alpha_blend_enabled() : false;
	key.preserve_destination_alpha = material ? material->preserve_destination_alpha() : false;
	key.src_blend_mode = material ? material->src_blend_mode() : Material::BlendMode::ONE;
	key.dest_blend_mode = material ? material->dest_blend_mode() : Material::BlendMode::ZERO;

	std::map<PipelineKey, VkPipeline>::const_iterator found = pipelines_.find(key);
	if (found != pipelines_.end())
	{
		return found->second;
	}

	if (!traits)
	{
		throw std::logic_error("vertex traits are required for Vulkan pipeline");
	}

	bool const use_custom_vertex_shader =
		shader_program && !shader_program->vertex_shader_filename().empty();

	shader::Shaders const shaders = shader::shaders(shader_program.get(), *traits);
	bool const use_vertex_skin_shader = !use_custom_vertex_shader && shader::skinned(*traits);
	bool const use_vertex_blend_shader = !use_custom_vertex_shader && !use_vertex_skin_shader && traits->has(VertexElement::Usage::TEXCOORD, 1);
	std::string const &vertex_shader_filename = shaders.vertex;
	std::string const &fragment_shader_filename = shaders.fragment;

	std::vector<unsigned char> const vertex_shader_code = read_binary_file(
		resolve_vulkan_shader_path(vertex_shader_filename));
	std::vector<unsigned char> const fragment_shader_code = read_binary_file(
		resolve_vulkan_shader_path(fragment_shader_filename));

	VkShaderModule vertex_shader = create_shader_module(device, vertex_shader_code);
	VkShaderModule fragment_shader = create_shader_module(device, fragment_shader_code);

	VkPipelineShaderStageCreateInfo shader_stages[2];
	std::memset(&shader_stages, 0, sizeof(shader_stages));
	shader_stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	shader_stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	shader_stages[0].module = vertex_shader;
	shader_stages[0].pName = "main";

	shader_stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	shader_stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	shader_stages[1].module = fragment_shader;
	shader_stages[1].pName = "main";

	VkVertexInputBindingDescription binding_description;
	std::memset(&binding_description, 0, sizeof(binding_description));
	binding_description.binding = 0;
	binding_description.stride = static_cast<uint32_t>(traits->size());
	binding_description.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

	std::vector<VkVertexInputAttributeDescription> attribute_descriptions;
	bool attribute_location_present[shader::ATTRIBUTE_LOCATIONS] = {};
	for (VertexElement const &element : traits->elements())
	{
		int const location = shader::attribute_location(element);

		VkVertexInputAttributeDescription attr;
		std::memset(&attr, 0, sizeof(attr));
		attr.binding = 0;
		attr.location = static_cast<uint32_t>(location);
		attr.format = resolve_vertex_attribute_format(element.type);
		attr.offset = element.offset;
		attribute_descriptions.push_back(attr);
		if (location < static_cast<int>(std::size(attribute_location_present)))
		{
			attribute_location_present[location] = true;
		}
	}

	if (!use_custom_vertex_shader)
	{
		std::vector<int> required_locations;
		required_locations.push_back(0);
		required_locations.push_back(1);
		required_locations.push_back(2);
		required_locations.push_back(3);
		required_locations.push_back(8);

		if (use_vertex_blend_shader)
		{
			required_locations.push_back(9);
		}

		if (use_vertex_skin_shader)
		{
			required_locations.push_back(4);
			required_locations.push_back(5);
			required_locations.push_back(6);
			required_locations.push_back(7);
		}

		for (int location : required_locations)
		{
				if (location < 0
				|| location >= static_cast<int>(std::size(attribute_location_present)))
			{
				continue;
			}

			if (attribute_location_present[location])
			{
				continue;
			}

			VkVertexInputAttributeDescription attr;
			std::memset(&attr, 0, sizeof(attr));
			attr.binding = 0;
			attr.location = static_cast<uint32_t>(location);
			attr.format = resolve_required_attribute_format(location);
			attr.offset = 0;
			attribute_descriptions.push_back(attr);
			attribute_location_present[location] = true;
		}
	}

	std::sort(attribute_descriptions.begin(), attribute_descriptions.end(),
		[](VkVertexInputAttributeDescription const &lhs, VkVertexInputAttributeDescription const &rhs)
		{
			return lhs.location < rhs.location;
		});

	VkPipelineVertexInputStateCreateInfo vertex_input_info;
	std::memset(&vertex_input_info, 0, sizeof(vertex_input_info));
	vertex_input_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vertex_input_info.vertexBindingDescriptionCount = 1;
	vertex_input_info.pVertexBindingDescriptions = &binding_description;
	vertex_input_info.vertexAttributeDescriptionCount = static_cast<uint32_t>(attribute_descriptions.size());
	vertex_input_info.pVertexAttributeDescriptions =
		attribute_descriptions.empty() ? nullptr : &attribute_descriptions[0];

	VkPipelineInputAssemblyStateCreateInfo input_assembly;
	std::memset(&input_assembly, 0, sizeof(input_assembly));
	input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	input_assembly.primitiveRestartEnable = VK_FALSE;

	VkPipelineViewportStateCreateInfo viewport_state;
	std::memset(&viewport_state, 0, sizeof(viewport_state));
	viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewport_state.viewportCount = 1;
	viewport_state.scissorCount = 1;

	VkPipelineRasterizationStateCreateInfo rasterizer;
	std::memset(&rasterizer, 0, sizeof(rasterizer));
	rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rasterizer.depthClampEnable = VK_FALSE;
	rasterizer.rasterizerDiscardEnable = VK_FALSE;
	rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
	rasterizer.lineWidth = 1.0f;
	rasterizer.cullMode = resolve_cull_mode(key.cull_mode);
	rasterizer.frontFace = resolve_front_face(key.cull_mode);
	rasterizer.depthBiasEnable = VK_FALSE;

	VkPipelineMultisampleStateCreateInfo multisampling;
	std::memset(&multisampling, 0, sizeof(multisampling));
	multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisampling.sampleShadingEnable = VK_FALSE;
	multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	VkPipelineColorBlendAttachmentState color_blend_attachment;
	std::memset(&color_blend_attachment, 0, sizeof(color_blend_attachment));
	color_blend_attachment.colorWriteMask =
		VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
		| VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	color_blend_attachment.blendEnable = key.alpha_blend_enabled ? VK_TRUE : VK_FALSE;
	BlendFactors const factors = blend_factors(key.src_blend_mode, key.dest_blend_mode, key.preserve_destination_alpha);
	color_blend_attachment.srcColorBlendFactor = factors.src_color;
	color_blend_attachment.dstColorBlendFactor = factors.dst_color;
	color_blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
	color_blend_attachment.srcAlphaBlendFactor = factors.src_alpha;
	color_blend_attachment.dstAlphaBlendFactor = factors.dst_alpha;
	color_blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;

	VkPipelineDepthStencilStateCreateInfo depth_stencil;
	std::memset(&depth_stencil, 0, sizeof(depth_stencil));
	depth_stencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	depth_stencil.depthTestEnable = VK_TRUE;
	depth_stencil.depthBoundsTestEnable = VK_FALSE;
	depth_stencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

	depth_stencil.depthWriteEnable = key.zwrite_enabled ? VK_TRUE : VK_FALSE;
	// a material's stencil value replaces what is there wherever its fragment passes the depth test
	depth_stencil.stencilTestEnable = key.stencil_write >= 0 ? VK_TRUE : VK_FALSE;
	if (key.stencil_write >= 0)
	{
		VkStencilOpState write;
		std::memset(&write, 0, sizeof(write));
		write.failOp = VK_STENCIL_OP_KEEP;
		write.passOp = VK_STENCIL_OP_REPLACE;
		write.depthFailOp = VK_STENCIL_OP_KEEP;
		write.compareOp = VK_COMPARE_OP_ALWAYS;
		write.compareMask = 0xff;
		write.writeMask = 0xff;
		write.reference = static_cast<uint32_t>(key.stencil_write);
		depth_stencil.front = write;
		depth_stencil.back = write;
	}

	VkPipelineColorBlendStateCreateInfo color_blending;
	std::memset(&color_blending, 0, sizeof(color_blending));
	color_blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	color_blending.logicOpEnable = VK_FALSE;
	// (the render pass is in the key: its colours are as many each time)
	std::vector<VkPipelineColorBlendAttachmentState> const color_blend_attachments(color_attachments, color_blend_attachment);
	color_blending.attachmentCount = color_attachments;
	color_blending.pAttachments = color_blend_attachments.empty() ? nullptr : color_blend_attachments.data();

	VkDynamicState dynamic_states[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	VkPipelineDynamicStateCreateInfo dynamic_state;
	std::memset(&dynamic_state, 0, sizeof(dynamic_state));
	dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamic_state.dynamicStateCount = 2;
	dynamic_state.pDynamicStates = dynamic_states;

	VkGraphicsPipelineCreateInfo pipeline_info;
	std::memset(&pipeline_info, 0, sizeof(pipeline_info));
	pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pipeline_info.stageCount = 2;
	pipeline_info.pStages = shader_stages;
	pipeline_info.pVertexInputState = &vertex_input_info;
	pipeline_info.pInputAssemblyState = &input_assembly;
	pipeline_info.pViewportState = &viewport_state;
	pipeline_info.pRasterizationState = &rasterizer;
	pipeline_info.pMultisampleState = &multisampling;
	pipeline_info.pDepthStencilState = &depth_stencil;
	pipeline_info.pColorBlendState = &color_blending;
	pipeline_info.pDynamicState = &dynamic_state;
	pipeline_info.layout = pipeline_layout;
	pipeline_info.renderPass = key.render_pass;
	pipeline_info.subpass = 0;
	pipeline_info.basePipelineHandle = VK_NULL_HANDLE;

	VkPipeline pipeline = VK_NULL_HANDLE;
	check_vk_result(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline),
		"vkCreateGraphicsPipelines");

	vkDestroyShaderModule(device, fragment_shader, nullptr);
	vkDestroyShaderModule(device, vertex_shader, nullptr);

	pipelines_[key] = pipeline;
	return pipeline;
}

void VulkanPipelineBuilder::destroy_all(VkDevice device)
{
	for (auto &pipeline : pipelines_)
	{
		if (pipeline.second != VK_NULL_HANDLE)
		{
			vkDestroyPipeline(device, pipeline.second, nullptr);
		}
	}
	pipelines_.clear();
}

}
