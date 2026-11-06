// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#version 450

// test_material_parameters.cc: the material's own uniform block, its vec4 number extra_parameters[0].x.

#extension GL_GOOGLE_include_directive : require
#include "edge.glsl"

layout(location = 0) in vec2 frag_texcoord0;
layout(location = 0) out vec4 out_color;

layout(set = 0, binding = EDGE_BINDING_MATERIAL_PARAMETERS) uniform MaterialParameters
{
	vec4 v[EDGE_MATERIAL_PARAMETERS_SIZE / 16];
} mp;

void main()
{
	out_color = mp.v[int(pc.extra_parameters[0].x)];
}
