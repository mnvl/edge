// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#version 450
#extension GL_GOOGLE_include_directive : require

// test_vertex_color_shader.cc: a quad through edge.glsl's draw constants, handing on the vertex color (location 10, a vec4 of usage
// COLOR) for the fragment shader to return.

#include "edge.glsl"

layout(location = 0) in vec3 in_position;
layout(location = 10) in vec4 in_color;

layout(location = 0) out vec4 frag_color;

void main()
{
	gl_Position = dc.world_view_projection * vec4(in_position, 1.0);
	frag_color = in_color;
}
