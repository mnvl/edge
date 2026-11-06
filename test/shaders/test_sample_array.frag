// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#version 450
#extension GL_GOOGLE_include_directive : require

// test_texture_array.cc: diffuse texture 0, an array, at the UV, from the layer and at the mip level of the material's extra parameter 0
// (x the layer, y the level); with z above 0 the layer is chosen per pixel instead: floor(u * z), z layers across the quad.

#include "edge.glsl"

layout(location = 0) in vec2 frag_texcoord0;
layout(location = 0) out vec4 out_color;

layout(set = 0, binding = 0) uniform sampler2DArray diffuse_texture_0;


void main()
{
	vec4 p = pc.extra_parameters[0];
	float layer = p.z > 0.0 ? floor(frag_texcoord0.x * p.z) : p.x;
	out_color = textureLod(diffuse_texture_0, vec3(frag_texcoord0, layer), p.y);
}
