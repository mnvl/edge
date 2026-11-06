// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#version 450

// test_depth_texture.cc: global texture 0 (a render target's depth, set by set_global_shadow_texture) compared with the reference
// depth extra_parameters[0].x at the texel under this pixel (the target extra_parameters[0].yz pixels): 1 where the reference is at most
// the depth there, 0 where it is past it.

#extension GL_GOOGLE_include_directive : require
#include "edge.glsl"

layout(location = 0) in vec2 frag_texcoord0;
layout(location = 0) out vec4 out_color;

layout(set = 0, binding = EDGE_BINDING_GLOBAL_TEXTURE(0)) uniform sampler2DShadow shadow_map;

void main()
{
	vec2 uv = (floor(gl_FragCoord.xy) + 0.5) / pc.extra_parameters[0].yz;
	float lit = texture(shadow_map, vec3(uv, pc.extra_parameters[0].x));
	out_color = vec4(lit, 0.0, 0.0, 1.0);
}
