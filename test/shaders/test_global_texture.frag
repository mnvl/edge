// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#version 450

// test_frame_resources.cc: global texture number extra_parameters[0].x, at the UV.

layout(location = 0) in vec2 frag_texcoord0;
layout(location = 0) out vec4 out_color;

layout(set = 0, binding = 12) uniform sampler2D global_texture_0;
layout(set = 0, binding = 13) uniform sampler2D global_texture_1;
layout(set = 0, binding = 14) uniform sampler2D global_texture_2;
layout(set = 0, binding = 15) uniform sampler2D global_texture_3;
layout(set = 0, binding = 16) uniform sampler2D global_texture_4;
layout(set = 0, binding = 17) uniform sampler2D global_texture_5;
layout(set = 0, binding = 18) uniform sampler2D global_texture_6;
layout(set = 0, binding = 19) uniform sampler2D global_texture_7;

layout(push_constant) uniform PushConstants
{
	vec4 extra_parameters[4];
	vec4 camera_eye;
	vec4 base_color_factor;
	vec4 material_pbr;
	vec3 emissive_factor;
	uint flags;
} pc;

void main()
{
	int slot = int(pc.extra_parameters[0].x + 0.5);
	vec2 uv = frag_texcoord0;
	out_color = slot == 0 ? texture(global_texture_0, uv) :
		slot == 1 ? texture(global_texture_1, uv) :
		slot == 2 ? texture(global_texture_2, uv) :
		slot == 3 ? texture(global_texture_3, uv) :
		slot == 4 ? texture(global_texture_4, uv) :
		slot == 5 ? texture(global_texture_5, uv) :
		slot == 6 ? texture(global_texture_6, uv) : texture(global_texture_7, uv);
}
