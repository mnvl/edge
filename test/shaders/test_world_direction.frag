// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#version 450
#extension GL_GOOGLE_include_directive : require

// test_render_targets.cc: the direction of this fragment from the point extra_parameters[0].xyz (a cube's centre), as a colour.

#include "edge.glsl"

layout(location = 0) in vec3 frag_world;
layout(location = 0) out vec4 out_color;

void main()
{
	out_color = vec4(normalize(frag_world - pc.extra_parameters[0].xyz), 1.0);
}
