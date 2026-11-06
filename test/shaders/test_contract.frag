// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#version 450
#extension GL_GOOGLE_include_directive : require

// test_frame_resources.cc: one thing edge.glsl names, read through edge.glsl;
// which one by pc.extra_parameters[0].x (the PROBE_ numbers in the test).

#include "edge.glsl"

layout(location = 0) in vec2 frag_texcoord0;
layout(location = 1) flat in vec4 frag_world_origin;
layout(location = 2) flat in vec4 frag_previous_world_origin;
layout(location = 3) flat in vec4 frag_previous_clip_origin;
layout(location = 0) out vec4 out_color;

layout(set = 0, binding = EDGE_BINDING_DIFFUSE_TEXTURE_0) uniform sampler2D diffuse_0;
layout(set = 0, binding = EDGE_BINDING_DIFFUSE_TEXTURE_1) uniform sampler2D diffuse_1;
layout(set = 0, binding = EDGE_BINDING_DIFFUSE_TEXTURE_2) uniform sampler2D diffuse_2;
layout(set = 0, binding = EDGE_BINDING_DIFFUSE_TEXTURE_3) uniform sampler2D diffuse_3;
layout(set = 0, binding = EDGE_BINDING_TEXTURE_BLENDING_WEIGHTS) uniform sampler2D blending_weights;
layout(set = 0, binding = EDGE_BINDING_NORMAL_MAP) uniform sampler2D normal_map;
layout(set = 0, binding = EDGE_BINDING_METALLIC_ROUGHNESS) uniform sampler2D metallic_roughness;
layout(set = 0, binding = EDGE_BINDING_EMISSIVE) uniform sampler2D emissive;
layout(set = 0, binding = EDGE_BINDING_GLOBAL_TEXTURE(0)) uniform sampler2D global_0;
layout(set = 0, binding = EDGE_BINDING_GLOBAL_TEXTURE(1)) uniform sampler2D global_1;
layout(set = 0, binding = EDGE_BINDING_GLOBAL_TEXTURE(2)) uniform sampler2D global_2;
layout(set = 0, binding = EDGE_BINDING_GLOBAL_TEXTURE(3)) uniform sampler2D global_3;
layout(set = 0, binding = EDGE_BINDING_GLOBAL_TEXTURE(4)) uniform sampler2D global_4;
layout(set = 0, binding = EDGE_BINDING_GLOBAL_TEXTURE(5)) uniform sampler2D global_5;
layout(set = 0, binding = EDGE_BINDING_GLOBAL_TEXTURE(6)) uniform sampler2D global_6;
layout(set = 0, binding = EDGE_BINDING_GLOBAL_TEXTURE(7)) uniform sampler2D global_7;

layout(set = 0, binding = EDGE_BINDING_FRAME_CONSTANTS) uniform Frame
{
	vec4 first;
	vec4 second;
} frame;

bool on(uint flag)
{
	return (pc.flags & flag) != 0u;
}

void main()
{
	vec2 uv = frag_texcoord0;
	switch (int(pc.extra_parameters[0].x + 0.5))
	{
	case 0: out_color = texture(diffuse_0, uv); break;
	case 1: out_color = texture(diffuse_1, uv); break;
	case 2: out_color = texture(diffuse_2, uv); break;
	case 3: out_color = texture(diffuse_3, uv); break;
	case 4: out_color = texture(blending_weights, uv); break;
	case 5: out_color = texture(normal_map, uv); break;
	case 6: out_color = texture(metallic_roughness, uv); break;
	case 7: out_color = texture(emissive, uv); break;
	case 8: out_color = texture(global_0, uv); break;
	case 9: out_color = texture(global_1, uv); break;
	case 10: out_color = texture(global_2, uv); break;
	case 11: out_color = texture(global_3, uv); break;
	case 12: out_color = texture(global_4, uv); break;
	case 13: out_color = texture(global_5, uv); break;
	case 14: out_color = texture(global_6, uv); break;
	case 15: out_color = texture(global_7, uv); break;
	case 16: out_color = pc.base_color_factor; break;
	case 17: out_color = pc.material_pbr; break;
	case 18: out_color = vec4(pc.emissive_factor, 0.0); break;
	case 19: out_color = pc.camera_eye; break;
	case 20: out_color = vec4(pc.extra_parameters[0].yzw, 0.0); break;
	case 21: out_color = pc.extra_parameters[1]; break;
	case 22: out_color = pc.extra_parameters[2]; break;
	case 23: out_color = pc.extra_parameters[EDGE_EXTRA_PARAMETERS - 1]; break;
	case 24: out_color = vec4(on(EDGE_FLAG_NORMAL_MAP), on(EDGE_FLAG_METALLIC_ROUGHNESS_MAP),
		on(EDGE_FLAG_EMISSIVE_MAP), float(pc.flags)); break;
	case 25: out_color = vec4(EDGE_FLAG_TEXTURE_BLENDING_VERTEX, EDGE_FLAG_TEXTURE_BLENDING_TEXTURE,
		EDGE_FLAG_SKELETAL, 0.0); break;
	case 26: out_color = vec4(EDGE_FLAG_NORMAL_MAP, EDGE_FLAG_METALLIC_ROUGHNESS_MAP, EDGE_FLAG_EMISSIVE_MAP, 0.0); break;
	case 27: out_color = vec4(EDGE_GLOBAL_TEXTURES, EDGE_EXTRA_PARAMETERS, EDGE_FRAME_CONSTANTS_SIZE, 0.0); break;
	case 28: out_color = frame.first; break;
	case 30: out_color = frame.second; break;
	case 29: out_color = frag_world_origin; break;
	case 31: out_color = vec4(EDGE_MAX_INSTANCES, EDGE_MATERIAL_PARAMETERS_SIZE, EDGE_FLAG_INSTANCED, 0.0); break;
	case 32: out_color = vec4(EDGE_BINDING_INSTANCES, EDGE_BINDING_MATERIAL_PARAMETERS, 0.0, 0.0); break;
	case 33: out_color = frag_previous_world_origin; break;
	case 34: out_color = frag_previous_clip_origin; break;
	case 35: out_color = vec4(EDGE_BINDING_PREVIOUS_INSTANCES, EDGE_BINDING_PREVIOUS_JOINT_MATRICES, 0.0, 0.0); break;
	case 37: out_color = vec4(on(EDGE_FLAG_PREVIOUS_UNKNOWN), EDGE_FLAG_PREVIOUS_UNKNOWN, 0.0, 0.0); break;
	case 36: out_color = vec4(on(EDGE_FLAG_OCCLUSION_MAP), EDGE_FLAG_OCCLUSION_MAP, EDGE_OCCLUSION_STRENGTH(pc.flags), float(pc.flags & 0xFFFFu)); break;
	default: out_color = vec4(-2.0); break;
	}
}
