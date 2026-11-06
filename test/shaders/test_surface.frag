// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#version 450
#extension GL_GOOGLE_include_directive : require

// test_shader_includes.cc: edge_material.glsl's surface and edge_brdf.glsl's BRDF, one at a time by pc.extra_parameters[0].x.

#include "edge.glsl"
#include "edge_material.glsl"
#include "edge_brdf.glsl"

layout(location = EDGE_VARYING_TEXCOORD) in vec2 frag_texcoord0;
layout(location = EDGE_VARYING_NORMAL) in vec3 frag_normal;
layout(location = EDGE_VARYING_TANGENT) in vec3 frag_tangent;
layout(location = EDGE_VARYING_BINORMAL) in vec3 frag_binormal;

layout(location = 0) out vec4 out_color;

void main()
{
	EdgeSurface s = edge_surface(frag_texcoord0, frag_normal, frag_tangent, frag_binormal);
	switch (int(pc.extra_parameters[0].x))
	{
	case 0: out_color = vec4(s.base_color, s.alpha); break;
	case 1: out_color = vec4(s.metallic, s.roughness, s.alpha_cutoff, 0.0); break;
	case 2: out_color = vec4(s.emissive, 0.0); break;
	case 3: out_color = vec4(s.normal, 0.0); break;
	case 4: out_color = vec4(edge_brdf(vec3(1.0), 0.0, 1.0, vec3(0, 0, 1), vec3(0, 0, 1), vec3(0, 0, 1)), 0.0); break;
	case 5: out_color = vec4(edge_srgb_encode(vec3(0.0, 0.214041, 1.0)), 0.0); break;
	case 6: out_color = vec4(s.occlusion, s.roughness, s.metallic, EDGE_OCCLUSION_STRENGTH(pc.flags)); break;
	default: out_color = vec4(-2.0);
	}
}
