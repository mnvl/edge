// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#version 450
#extension GL_GOOGLE_include_directive : require

// test_render_targets.cc: three colour attachments written at once, each a function of the pixel (gl_FragCoord: its column, and its row
// from the top) and of extra_parameters[0] (p), so that what lands in one cannot pass for another's:
//   location 0 (RGBA32F): x, y, p.x, 1
//   location 1 (8-bit):   x / p.y, y / p.z, p.w, 1
//   location 2 (R32F):    1000 x + y

#include "edge.glsl"

layout(location = 0) in vec2 frag_texcoord0;
layout(location = 0) out vec4 out_first;
layout(location = 1) out vec4 out_second;
layout(location = 2) out float out_third;

void main()
{
	vec4 p = pc.extra_parameters[0];
	vec2 c = gl_FragCoord.xy;
	out_first = vec4(c, p.x, 1.0);
	out_second = vec4(c.x / p.y, c.y / p.z, p.w, 1.0);
	out_third = 1000.0 * c.x + c.y;
}
