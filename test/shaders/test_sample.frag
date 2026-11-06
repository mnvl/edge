// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#version 450

// test_frame_resources.cc: diffuse texture 0 at the UV, as sampled.

layout(location = 0) in vec2 frag_texcoord0;
layout(location = 0) out vec4 out_color;

layout(set = 0, binding = 0) uniform sampler2D diffuse_texture_0;

void main()
{
	out_color = texture(diffuse_texture_0, frag_texcoord0);
}
