// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#version 450
#extension GL_GOOGLE_include_directive : require

#include "edge.glsl"
#include "edge_instance.glsl"

layout(location = EDGE_LOCATION_POSITION) in vec3 in_position;
layout(location = EDGE_LOCATION_TANGENT) in vec3 in_tangent;
layout(location = EDGE_LOCATION_BINORMAL) in vec3 in_binormal;
layout(location = EDGE_LOCATION_NORMAL) in vec3 in_normal;
layout(location = EDGE_LOCATION_TEXCOORD_0) in vec2 in_texcoord0;
layout(location = EDGE_LOCATION_TEXCOORD_1) in vec4 in_blend_weights;

layout(location = EDGE_VARYING_TEXCOORD) out vec2 frag_texcoord0;
layout(location = EDGE_VARYING_BLEND_WEIGHTS) out vec4 frag_blend_weights;
layout(location = EDGE_VARYING_POSITION) out vec3 frag_position;
layout(location = EDGE_VARYING_NORMAL) out vec3 frag_normal;
layout(location = EDGE_VARYING_TANGENT) out vec3 frag_tangent;
layout(location = EDGE_VARYING_BINORMAL) out vec3 frag_binormal;

vec3 sanitize_normal(vec3 n)
{
	return (length(n) > 1e-5) ? normalize(n) : vec3(0.0, 0.0, 1.0);
}

vec3 sanitize_tangent(vec3 t)
{
	return (length(t) > 1e-5) ? normalize(t) : vec3(1.0, 0.0, 0.0);
}

vec3 sanitize_binormal(vec3 b)
{
	return (length(b) > 1e-5) ? normalize(b) : vec3(0.0, 1.0, 0.0);
}

void main()
{
	mat4 instance = edge_instance_matrix();
	mat4 world = dc.world_matrix * instance;
	gl_Position = dc.world_view_projection * (instance * vec4(in_position, 1.0));
	frag_texcoord0 = in_texcoord0;
	frag_blend_weights = in_blend_weights;
	frag_position = (world * vec4(in_position, 1.0)).xyz;
	frag_normal = sanitize_normal(mat3(world) * in_normal);
	frag_tangent = sanitize_tangent(mat3(world) * in_tangent);
	frag_binormal = sanitize_binormal(mat3(world) * in_binormal);
}
