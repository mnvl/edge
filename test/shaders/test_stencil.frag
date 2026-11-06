// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#version 450

// test_stencil_texture.cc: the stencil value under this pixel (diffuse texture 0: a render target's stencil texture), in red.

layout(location = 0) in vec2 frag_texcoord0;
layout(location = 0) out vec4 out_color;

layout(set = 0, binding = 0) uniform usampler2D diffuse_texture_0;

void main()
{
	uint stencil = texelFetch(diffuse_texture_0, ivec2(gl_FragCoord.xy), 0).r;
	out_color = vec4(float(stencil), 0.0, 0.0, 1.0);
}
