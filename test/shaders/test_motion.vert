// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#version 450
#extension GL_GOOGLE_include_directive : require

// test_motion.cc: a vertex where it is this frame and where it was the frame before (edge_motion.glsl), instanced or not, both clip
// positions passed on for the fragment shader to make the motion of.

#include "edge.glsl"
#include "edge_motion.glsl"

layout(location = EDGE_LOCATION_POSITION) in vec3 in_position;

layout(location = 0) out vec4 clip;
layout(location = 1) out vec4 previous_clip;

void main()
{
	vec4 position = vec4(in_position, 1.0);
	clip = dc.world_view_projection * (edge_instance_matrix() * position);
	previous_clip = edge_previous_clip(position);
	gl_Position = clip;
}
