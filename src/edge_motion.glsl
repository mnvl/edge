// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

// Where a vertex was the frame before, for motion vectors (TAA, motion blur).  A vertex shader includes this after edge.glsl and hands
// the fragment shader both clip positions, this frame's and the frame before's:
//
//   clip = dc.world_view_projection * (edge_instance_matrix() * position);
//   previous_clip = edge_previous_clip(position);
//   gl_Position = clip;
//
// (a skinned vertex: skin * position and edge_previous_skin_matrix(...) * position), and the fragment shader makes its pixel's motion,
// in the target's texture coordinates (u right, v down), where the pixel is less where it was:
//
//   vec2 motion = (clip.xy / clip.w - previous_clip.xy / previous_clip.w) * vec2(0.5, -0.5);
//
// (dividing in the fragment shader: a pixel's own clip position, not one made of its triangle's corners').  Where the draw's previous
// frame is not known (pc.flags has EDGE_FLAG_PREVIOUS_UNKNOWN), the previous clip position is this frame's: no motion, and no history
// to trust.  DrawCall says what the
// frame before is: a draw seen the first time was where it is, and a camera seen the first time, or cut (Camera::cut()), looked where it
// looks.  Vertex stage only (the instances' and joints' blocks are the vertex stage's).

#ifndef EDGE_MOTION_GLSL
#define EDGE_MOTION_GLSL

#include "edge.glsl"
#include "edge_instance.glsl"

#ifndef EDGE_MAX_JOINT_MATRICES
#define EDGE_MAX_JOINT_MATRICES 256               // MAX_JOINT_MATRICES (edge_skin.glsl's)
#endif

layout(set = 0, binding = EDGE_BINDING_PREVIOUS_INSTANCES) uniform EdgePreviousInstances
{
	mat4 instance_matrix[EDGE_MAX_INSTANCES];
} edge_previous_instances;

layout(set = 0, binding = EDGE_BINDING_PREVIOUS_JOINT_MATRICES) uniform EdgePreviousJointMatrices
{
	mat4 joint_matrix_array[EDGE_MAX_JOINT_MATRICES];
} edge_previous_jm;

// edge_instance_matrix() the frame before: the instance's world then (a new instance's is its world now); the identity for a draw that
// is not instanced.
mat4 edge_previous_instance_matrix()
{
	return (pc.flags & EDGE_FLAG_INSTANCED) != 0u ? edge_previous_instances.instance_matrix[gl_InstanceIndex] : mat4(1.0);
}

// edge_skin_matrix() the frame before: the joints' matrices then.
mat4 edge_previous_skin_matrix(uvec4 joints0, uvec4 joints1, vec4 weights0, vec4 weights1)
{
	return edge_previous_jm.joint_matrix_array[joints0.x] * weights0.x
		+ edge_previous_jm.joint_matrix_array[joints0.y] * weights0.y
		+ edge_previous_jm.joint_matrix_array[joints0.z] * weights0.z
		+ edge_previous_jm.joint_matrix_array[joints0.w] * weights0.w
		+ edge_previous_jm.joint_matrix_array[joints1.x] * weights1.x
		+ edge_previous_jm.joint_matrix_array[joints1.y] * weights1.y
		+ edge_previous_jm.joint_matrix_array[joints1.z] * weights1.z
		+ edge_previous_jm.joint_matrix_array[joints1.w] * weights1.w;
}

// The clip position the frame before of a vertex at @p position in the draw's own space (skinned already, where it is skinned): what
// gl_Position = dc.world_view_projection * (edge_instance_matrix() * position) was then.
vec4 edge_previous_clip(vec4 position)
{
	return dc.previous_world_view_projection * (edge_previous_instance_matrix() * position);
}

#endif
