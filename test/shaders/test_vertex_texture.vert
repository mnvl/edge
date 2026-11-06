// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#version 450

// test_vertex_texture.cc: moves the quad right by the red of diffuse texture 0.

layout(location = 0) in vec3 in_position;

layout(set = 0, binding = 0) uniform sampler2D diffuse_texture_0;

layout(set = 0, binding = 8) uniform DrawConstants
{
	mat4 world_view_projection;
	mat4 world_matrix;
} dc;

void main()
{
	float shift = textureLod(diffuse_texture_0, vec2(0.5), 0.0).r;
	gl_Position = dc.world_view_projection * vec4(in_position + vec3(shift, 0.0, 0.0), 1.0);
}
