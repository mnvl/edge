// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#version 300 es

// test_gles_renderer.cc: a triangle through the world-view-projection edge hands every draw (the draw constants of edge.glsl).
//
// The GLES backend's shader contract, as edge.glsl's is Vulkan's: the clip space a shader writes is Vulkan's as edge's camera makes it
// (y up, z from 0 to w), and the two lines below turn it into GL's: z to -w..w, and y upside down, so that the target's row 0 is its top
// as in Vulkan (whose flipped viewport does the same).  This is what SPIRV-Cross will do to every translated shader.

layout(location = 0) in vec3 in_position;

uniform mat4 u_world_view_projection;

void main()
{
	vec4 p = u_world_view_projection * vec4(in_position, 1.0);
	gl_Position = vec4(p.x, -p.y, 2.0 * p.z - p.w, p.w);
}
