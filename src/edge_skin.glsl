// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

// A skinned vertex shader's joints, as vertex_skin.glsl skins (and BasicAnimatedMesh on the CPU): up to eight joints a vertex, from the
// joint matrices of the uber draw call.  After edge.glsl, in a vertex shader:
//
//   layout(location = EDGE_LOCATION_JOINTS_0) in uvec4 joints0;  ... JOINTS_1, WEIGHTS_0, WEIGHTS_1
//   mat4 skin = edge_skin_matrix(joints0, joints1, weights0, weights1);
//   vec3 position = (skin * vec4(in_position, 1.0)).xyz;

#ifndef EDGE_SKIN_GLSL
#define EDGE_SKIN_GLSL

#define EDGE_MAX_JOINT_MATRICES 256               // MAX_JOINT_MATRICES

layout(set = 0, binding = EDGE_BINDING_JOINT_MATRICES) uniform EdgeJointMatrices
{
	mat4 joint_matrix_array[EDGE_MAX_JOINT_MATRICES];
} jm;

// The weighted sum of the joints' matrices (the weights add up to 1).
mat4 edge_skin_matrix(uvec4 joints0, uvec4 joints1, vec4 weights0, vec4 weights1)
{
	return jm.joint_matrix_array[joints0.x] * weights0.x
		+ jm.joint_matrix_array[joints0.y] * weights0.y
		+ jm.joint_matrix_array[joints0.z] * weights0.z
		+ jm.joint_matrix_array[joints0.w] * weights0.w
		+ jm.joint_matrix_array[joints1.x] * weights1.x
		+ jm.joint_matrix_array[joints1.y] * weights1.y
		+ jm.joint_matrix_array[joints1.z] * weights1.z
		+ jm.joint_matrix_array[joints1.w] * weights1.w;
}

#endif
