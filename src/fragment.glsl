// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#version 450
#extension GL_GOOGLE_include_directive : require

#include "edge.glsl"

layout(location = EDGE_VARYING_TEXCOORD) in vec2 frag_texcoord0;
layout(location = EDGE_VARYING_BLEND_WEIGHTS) in vec4 frag_blend_weights;
layout(location = EDGE_VARYING_POSITION) in vec3 frag_position;
layout(location = EDGE_VARYING_NORMAL) in vec3 frag_normal;
layout(location = EDGE_VARYING_TANGENT) in vec3 frag_tangent;
layout(location = EDGE_VARYING_BINORMAL) in vec3 frag_binormal;

layout(location = 0) out vec4 out_color;

layout(set = 0, binding = EDGE_BINDING_DIFFUSE_TEXTURE_0) uniform sampler2D diffuse_texture_0;
layout(set = 0, binding = EDGE_BINDING_DIFFUSE_TEXTURE_1) uniform sampler2D diffuse_texture_1;
layout(set = 0, binding = EDGE_BINDING_DIFFUSE_TEXTURE_2) uniform sampler2D diffuse_texture_2;
layout(set = 0, binding = EDGE_BINDING_DIFFUSE_TEXTURE_3) uniform sampler2D diffuse_texture_3;
layout(set = 0, binding = EDGE_BINDING_TEXTURE_BLENDING_WEIGHTS) uniform sampler2D texture_blending_weights_texture;

vec4 sample_diffuse_color()
{
	if ((pc.flags & EDGE_FLAG_TEXTURE_BLENDING_VERTEX) != 0u)
	{
		vec4 blend_factor = frag_blend_weights;
		return
			texture(diffuse_texture_0, frag_texcoord0) * blend_factor.x +
			texture(diffuse_texture_1, frag_texcoord0) * blend_factor.y +
			texture(diffuse_texture_2, frag_texcoord0) * blend_factor.z +
			texture(diffuse_texture_3, frag_texcoord0) * blend_factor.w;
	}

	if ((pc.flags & EDGE_FLAG_TEXTURE_BLENDING_TEXTURE) != 0u)
	{
		vec4 blend_factor = texture(texture_blending_weights_texture, frag_texcoord0);
		return
			texture(diffuse_texture_0, frag_texcoord0) * blend_factor.x +
			texture(diffuse_texture_1, frag_texcoord0) * blend_factor.y +
			texture(diffuse_texture_2, frag_texcoord0) * blend_factor.z +
			texture(diffuse_texture_3, frag_texcoord0) * blend_factor.w;
	}

	return texture(diffuse_texture_0, frag_texcoord0);
}

void main()
{
	vec4 color = sample_diffuse_color() * pc.base_color_factor;
	if (color.a < (1.0 / 255.0))
	{
		discard;
	}

	out_color = color;
}
