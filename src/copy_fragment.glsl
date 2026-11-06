// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#version 450
#extension GL_GOOGLE_include_directive : require

// FullscreenCopy's (fullscreen_copy.h): the base colour texture over the whole target, stretched if the sizes differ, opaque.  A
// material's textures repeat, so the sampling stays half a texel inside: the texture's far edge does not blend into its near one.

#include "edge.glsl"

layout(location = EDGE_VARYING_TEXCOORD) in vec2 frag_texcoord0;
layout(location = 0) out vec4 out_color;

layout(set = 0, binding = EDGE_BINDING_DIFFUSE_TEXTURE_0) uniform sampler2D diffuse_texture_0;

void main()
{
	vec2 half_texel = 0.5 / vec2(textureSize(diffuse_texture_0, 0));
	out_color = vec4(texture(diffuse_texture_0, clamp(frag_texcoord0, half_texel, 1.0 - half_texel)).rgb, 1.0);
}
