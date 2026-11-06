// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#version 450

// test_frame_resources.cc: a quad with its UVs.

layout(location = 0) in vec3 in_position;
layout(location = 8) in vec2 in_texcoord0;

layout(location = 0) out vec2 frag_texcoord0;

layout(set = 0, binding = 8) uniform DrawConstants
{
	mat4 world_view_projection;
	mat4 world_matrix;
} dc;

void main()
{
	gl_Position = dc.world_view_projection * vec4(in_position, 1.0);
	frag_texcoord0 = in_texcoord0;
}
