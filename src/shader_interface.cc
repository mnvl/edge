// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include "shader_interface.h"

#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string>

#include "shader_program.h"
#include "vertex_traits.h"

namespace edge
{

namespace shader
{

int attribute_location(VertexElement::Usage usage, std::uint8_t usage_index)
{
	switch (usage)
	{
	case VertexElement::Usage::POSITION:     return usage_index == 0 ? 0 : -1;
	case VertexElement::Usage::TANGENT:      return usage_index == 0 ? 1 : -1;
	case VertexElement::Usage::BINORMAL:     return usage_index == 0 ? 2 : -1;
	case VertexElement::Usage::NORMAL:       return usage_index == 0 ? 3 : -1;
	case VertexElement::Usage::BLEND_INDICES: return usage_index < 2 ? 4 + usage_index : -1;
	case VertexElement::Usage::BLEND_WEIGHT:  return usage_index < 2 ? 6 + usage_index : -1;
	case VertexElement::Usage::TEXCOORD:     return usage_index < 2 ? 8 + usage_index : -1;
	case VertexElement::Usage::COLOR:        return usage_index == 0 ? 10 : -1;
	}
	return -1;
}

int attribute_location(VertexElement const &element)
{
	int const location = attribute_location(element.usage, element.usage_index);
	if (location < 0)
		throw std::logic_error("a vertex element of usage " + std::to_string(static_cast<unsigned>(element.usage)) + " index "
			+ std::to_string(element.usage_index) + " has no shader location");
	return location;
}

bool skinned(VertexTraits const &traits)
{
	return traits.has(VertexElement::Usage::BLEND_INDICES, 0) && traits.has(VertexElement::Usage::BLEND_INDICES, 1) && traits.has(VertexElement::Usage::BLEND_WEIGHT, 0)
		&& traits.has(VertexElement::Usage::BLEND_WEIGHT, 1);
}

PushConstants push_constants(Material const &material, VertexTraits const &traits, bool uber, bool skin, rove::vec<3> const &eye,
	bool instanced, bool previous_unknown)
{
	PushConstants pc;
	std::memset(&pc, 0, sizeof(pc));
	for (size_t i = 0; i < Material::EXTRA_PARAMETERS_COUNT; ++i)
	{
		rove::vec<4> const &value = material.extra_parameter(i);
		pc.extra_parameters[i][0] = value.x;
		pc.extra_parameters[i][1] = value.y;
		pc.extra_parameters[i][2] = value.z;
		pc.extra_parameters[i][3] = value.w;
	}
	pc.camera_eye[0] = eye.x;
	pc.camera_eye[1] = eye.y;
	pc.camera_eye[2] = eye.z;
	pc.camera_eye[3] = 1.0f;
	rove::vec<4> const &bc = material.base_color_factor();
	pc.base_color_factor[0] = bc.x;
	pc.base_color_factor[1] = bc.y;
	pc.base_color_factor[2] = bc.z;
	pc.base_color_factor[3] = bc.w;
	pc.material_pbr[0] = material.metallic_factor();
	pc.material_pbr[1] = material.roughness_factor();
	pc.material_pbr[2] = material.alpha_cutoff();
	pc.material_pbr[3] = material.normal_scale();
	rove::vec<3> const &e = material.emissive_factor();
	pc.emissive_factor[0] = e.x;
	pc.emissive_factor[1] = e.y;
	pc.emissive_factor[2] = e.z;

	if (material.normal_map_texture())
		pc.flags |= FLAG_NORMAL_MAP;
	if (material.metallic_roughness_texture())
		pc.flags |= FLAG_METALLIC_ROUGHNESS_MAP;
	if (material.emissive_texture())
		pc.flags |= FLAG_EMISSIVE_MAP;
	if (material.occlusion())       // (a draw of it has the texture: Renderer::draw refuses it otherwise)
		pc.flags |= FLAG_OCCLUSION_MAP | static_cast<uint32_t>(std::lround(material.occlusion_strength() * 65535.0f)) << OCCLUSION_STRENGTH_SHIFT;
	if (instanced)
		pc.flags |= FLAG_INSTANCED;
	if (previous_unknown)
		pc.flags |= FLAG_PREVIOUS_UNKNOWN;
	if (uber)
	{
		if (skin)
			pc.flags |= FLAG_SKELETAL;
		if (material.texture_blending_weights_texture())
			pc.flags |= FLAG_TEXTURE_BLENDING_TEXTURE;
		else if (traits.has(VertexElement::Usage::TEXCOORD, 1))
			pc.flags |= FLAG_TEXTURE_BLENDING_VERTEX;
	}
	return pc;
}

Shaders shaders(ShaderProgram const *program, VertexTraits const &traits)
{
	Shaders result;
	if (program && !program->vertex_shader_filename().empty())
		result.vertex = program->vertex_shader_filename();
	else
		result.vertex = skinned(traits) ? "vertex_skin.spv" : traits.has(VertexElement::Usage::TEXCOORD, 1) ? "vertex_blend.spv" : "vertex.spv";
	result.fragment = program && !program->fragment_shader_filename().empty() ? program->fragment_shader_filename() : "fragment.spv";
	return result;
}

}

}
