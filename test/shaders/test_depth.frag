// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#version 450

// test_depth_texture.cc: the texel of diffuse texture 0 (a render target's depth texture) under this pixel, in red.

layout(location = 0) in vec2 frag_texcoord0;
layout(location = 0) out vec4 out_color;

layout(set = 0, binding = 0) uniform sampler2D diffuse_texture_0;

void main()
{
	float depth = texelFetch(diffuse_texture_0, ivec2(gl_FragCoord.xy), 0).r;
	out_color = vec4(depth, 0.0, 0.0, 1.0);
}
