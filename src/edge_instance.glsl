// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

// An instanced draw's instances: a vertex shader includes this after edge.glsl and places a vertex by edge_instance_matrix(), as the
// built-in vertex shaders do:
//
//   gl_Position = dc.world_view_projection * (edge_instance_matrix() * vec4(in_position, 1.0));
//   vec3 world = (dc.world_matrix * edge_instance_matrix() * vec4(in_position, 1.0)).xyz;
//
// A draw that is not instanced has the identity for it, so the shader draws it as before.  In an instanced draw dc's matrices are the
// camera's alone (world_view_projection is the view-projection, world_matrix the identity) and each instance's matrix is that
// instance's world: a shader that does not include this draws every instance at the origin.

#ifndef EDGE_INSTANCE_GLSL
#define EDGE_INSTANCE_GLSL

#include "edge.glsl"

layout(set = 0, binding = EDGE_BINDING_INSTANCES) uniform Instances
{
	mat4 instance_matrix[EDGE_MAX_INSTANCES];
} edge_instances;

mat4 edge_instance_matrix()
{
	return (pc.flags & EDGE_FLAG_INSTANCED) != 0u ? edge_instances.instance_matrix[gl_InstanceIndex] : mat4(1.0);
}

#endif
