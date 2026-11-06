// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#version 450
#extension GL_GOOGLE_include_directive : require

// test_frame_resources.cc: a quad through edge.glsl's draw constants, with
// the world matrix's origin passed on for the fragment shader to return, and
// the previous frame's world origin and where it was in clip space.

#include "edge.glsl"

layout(location = 0) in vec3 in_position;
layout(location = 8) in vec2 in_texcoord0;

layout(location = 0) out vec2 frag_texcoord0;
layout(location = 1) flat out vec4 frag_world_origin;
layout(location = 2) flat out vec4 frag_previous_world_origin;
layout(location = 3) flat out vec4 frag_previous_clip_origin;

void main()
{
	gl_Position = dc.world_view_projection * vec4(in_position, 1.0);
	frag_texcoord0 = in_texcoord0;
	frag_world_origin = dc.world_matrix * vec4(0.0, 0.0, 0.0, 1.0);
	frag_previous_world_origin = dc.previous_world_matrix * vec4(0.0, 0.0, 0.0, 1.0);
	frag_previous_clip_origin = dc.previous_world_view_projection * vec4(0.0, 0.0, 0.0, 1.0);
}
