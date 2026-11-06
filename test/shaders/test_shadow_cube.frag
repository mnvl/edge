// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#version 450
#extension GL_GOOGLE_include_directive : require

// test_render_targets.cc: global texture 0, a cube target's depth set by set_global_shadow_texture (samplerCubeShadow), drawn from its
// centre by Camera::set_cube_face with near and far extra_parameters[1].xy.  Each pixel looks in the direction of test_sample_cube.frag's
// map (extra_parameters[0].xy pixels) at the point whose largest coordinate is extra_parameters[0].z, and compares that point's depth
// with the one stored: 1 where it is at most the depth there, 0 where it is past it.

#include "edge.glsl"
#include "edge_cube.glsl"

layout(location = 0) in vec2 frag_texcoord0;
layout(location = 0) out vec4 out_color;

layout(set = 0, binding = EDGE_BINDING_GLOBAL_TEXTURE(0)) uniform samplerCubeShadow shadow_cube;

void main()
{
	vec4 p = pc.extra_parameters[0];
	vec2 uv = gl_FragCoord.xy / p.xy;
	float longitude = (uv.x * 2.0 - 1.0) * 3.14159265, latitude = (0.5 - uv.y) * 3.14159265;
	vec3 d = vec3(cos(latitude) * sin(longitude), sin(latitude), cos(latitude) * cos(longitude));
	vec3 point = d * (p.z / max(max(abs(d.x), abs(d.y)), abs(d.z)));
	float depth = edge_cube_depth(point, pc.extra_parameters[1].x, pc.extra_parameters[1].y);
	out_color = vec4(texture(shadow_cube, vec4(edge_cube_direction(point), depth)), 0.0, 0.0, 1.0);
}
