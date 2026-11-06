// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#version 450
#extension GL_GOOGLE_include_directive : require

// test_render_targets.cc: diffuse texture 0, a cube render target, sampled in the direction of this pixel of a latitude-longitude map of
// the sphere over the target (extra_parameters[0].xy pixels): column x is the longitude (x + 0.5) / w * 2 pi - pi from +z towards +x, row
// y from the top the latitude pi / 2 - (y + 0.5) / h * pi towards +y.

#include "edge.glsl"
#include "edge_cube.glsl"

layout(location = 0) in vec2 frag_texcoord0;
layout(location = 0) out vec4 out_color;

layout(set = 0, binding = 0) uniform samplerCube diffuse_texture_0;

void main()
{
	vec2 uv = gl_FragCoord.xy / pc.extra_parameters[0].xy;
	float longitude = (uv.x * 2.0 - 1.0) * 3.14159265, latitude = (0.5 - uv.y) * 3.14159265;
	vec3 d = vec3(cos(latitude) * sin(longitude), sin(latitude), cos(latitude) * cos(longitude));
	out_color = vec4(textureLod(diffuse_texture_0, edge_cube_direction(d), 0.0).xyz, 1.0);
}
