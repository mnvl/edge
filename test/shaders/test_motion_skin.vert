// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#version 450
#extension GL_GOOGLE_include_directive : require

// test_motion.cc: a skinned vertex where it is this frame (this frame's joints) and where it was the frame before (the frame before's
// joints and placement, edge_motion.glsl).

#include "edge.glsl"
#include "edge_skin.glsl"
#include "edge_motion.glsl"

layout(location = EDGE_LOCATION_POSITION) in vec3 in_position;
layout(location = EDGE_LOCATION_JOINTS_0) in uvec4 in_joints0;
layout(location = EDGE_LOCATION_JOINTS_1) in uvec4 in_joints1;
layout(location = EDGE_LOCATION_WEIGHTS_0) in vec4 in_weights0;
layout(location = EDGE_LOCATION_WEIGHTS_1) in vec4 in_weights1;

layout(location = 0) out vec4 clip;
layout(location = 1) out vec4 previous_clip;

void main()
{
	vec4 position = vec4(in_position, 1.0);
	clip = dc.world_view_projection * (edge_instance_matrix() * (edge_skin_matrix(in_joints0, in_joints1, in_weights0, in_weights1) * position));
	previous_clip = edge_previous_clip(edge_previous_skin_matrix(in_joints0, in_joints1, in_weights0, in_weights1) * position);
	gl_Position = clip;
}
