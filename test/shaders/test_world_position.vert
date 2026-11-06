// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#version 450
#extension GL_GOOGLE_include_directive : require

// test_render_targets.cc: the vertex's world position, passed on.

#include "edge.glsl"

layout(location = 0) in vec3 in_position;
layout(location = 0) out vec3 frag_world;

void main()
{
	gl_Position = dc.world_view_projection * vec4(in_position, 1.0);
	frag_world = (dc.world_matrix * vec4(in_position, 1.0)).xyz;
}
