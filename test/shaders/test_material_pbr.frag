// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#version 450

// test_frame_resources.cc: (roughness, metallic, emissive red, alpha cutoff) as the material gives them.

layout(location = 0) in vec2 frag_texcoord0;
layout(location = 0) out vec4 out_color;

layout(set = 0, binding = 10) uniform sampler2D metallic_roughness_texture;
layout(set = 0, binding = 11) uniform sampler2D emissive_texture;

layout(push_constant) uniform PushConstants
{
	vec4 extra_parameters[4];
	vec4 camera_eye;
	vec4 base_color_factor;
	vec4 material_pbr;      // metallic factor, roughness factor, alpha cutoff, normal map scale
	vec3 emissive_factor;   // rgb (strength applied), 0
	uint flags;
} pc;

const uint FLAG_METALLIC_ROUGHNESS_MAP = 32u;
const uint FLAG_EMISSIVE_MAP = 64u;

void main()
{
	vec4 mr = (pc.flags & FLAG_METALLIC_ROUGHNESS_MAP) != 0u ? texture(metallic_roughness_texture, frag_texcoord0) : vec4(1.0);
	vec4 em = (pc.flags & FLAG_EMISSIVE_MAP) != 0u ? texture(emissive_texture, frag_texcoord0) : vec4(1.0);
	out_color = vec4(mr.g * pc.material_pbr.y, mr.b * pc.material_pbr.x, em.r * pc.emissive_factor.r, pc.material_pbr.z);
}
