// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#version 450
#extension GL_GOOGLE_include_directive : require

// test_render_targets.cc: two colour attachments, the first extra_parameters[0], the second (R32F) extra_parameters[1].x.

#include "edge.glsl"

layout(location = 0) in vec2 frag_texcoord0;
layout(location = 0) out vec4 out_first;
layout(location = 1) out float out_second;

void main()
{
	out_first = pc.extra_parameters[0];
	out_second = pc.extra_parameters[1].x;
}
