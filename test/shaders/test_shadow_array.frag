// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#version 450
#extension GL_GOOGLE_include_directive : require

// test_render_targets.cc: global texture 0, a layered target's depth set by set_global_shadow_texture (sampler2DArrayShadow), compared
// at the texel under this pixel of layer extra_parameters[0].y with the reference extra_parameters[0].x (the target extra_parameters[0].zw
// pixels): 1 where the reference is at most the depth there, 0 where it is past it.

#include "edge.glsl"

layout(location = 0) in vec2 frag_texcoord0;
layout(location = 0) out vec4 out_color;

layout(set = 0, binding = EDGE_BINDING_GLOBAL_TEXTURE(0)) uniform sampler2DArrayShadow shadow_maps;

void main()
{
	vec4 p = pc.extra_parameters[0];
	vec2 uv = (floor(gl_FragCoord.xy) + 0.5) / p.zw;
	out_color = vec4(texture(shadow_maps, vec4(uv, p.y, p.x)), 0.0, 0.0, 1.0);
}
