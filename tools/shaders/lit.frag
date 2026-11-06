// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#version 450
#extension GL_GOOGLE_include_directive : require

// The light of edge_shot (and of a Python model viewer that loads lib/view_shaders): the material as edge_material.glsl makes it, lit by a key light over the eye's
// shoulder and a dimmer fill from below it, with an ambient floor, through edge_brdf.glsl.  The eye is pc.camera_eye.

#include "edge.glsl"
#include "edge_material.glsl"
#include "edge_brdf.glsl"

layout(location = EDGE_VARYING_TEXCOORD) in vec2 frag_texcoord0;
layout(location = EDGE_VARYING_POSITION) in vec3 frag_position;
layout(location = EDGE_VARYING_NORMAL) in vec3 frag_normal;
layout(location = EDGE_VARYING_TANGENT) in vec3 frag_tangent;
layout(location = EDGE_VARYING_BINORMAL) in vec3 frag_binormal;

layout(location = 0) out vec4 out_color;

void main()
{
	EdgeSurface s = edge_surface(frag_texcoord0, frag_normal, frag_tangent, frag_binormal);
	if (s.alpha < max(s.alpha_cutoff, 1.0 / 255.0))
		discard;
	vec3 v = normalize(pc.camera_eye.xyz - frag_position);
	vec3 n = gl_FrontFacing ? s.normal : -s.normal;
	vec3 up = abs(v.y) < 0.99 ? vec3(0, 1, 0) : vec3(1, 0, 0);
	vec3 side = normalize(cross(up, v));
	vec3 key = normalize(v + 0.8 * up + 0.5 * side);
	vec3 fill = normalize(v - 0.6 * up - 0.4 * side);
	float rough = clamp(s.roughness, 0.04, 1.0);
	vec3 light = edge_brdf(s.base_color, s.metallic, rough, n, v, key) * 2.6 + edge_brdf(s.base_color, s.metallic, rough, n, v, fill) * 0.9;
	out_color = vec4(light + s.base_color * 0.12 + s.emissive, s.alpha);
}
