// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#version 450
#extension GL_GOOGLE_include_directive : require

// test_render_targets.cc: the texel under this pixel of layer extra_parameters[0].x of diffuse texture 0, an array (a layered render
// target's colour or depth: a depth reads in red).

#include "edge.glsl"

layout(location = 0) in vec2 frag_texcoord0;
layout(location = 0) out vec4 out_color;

layout(set = 0, binding = 0) uniform sampler2DArray diffuse_texture_0;

void main()
{
	out_color = texelFetch(diffuse_texture_0, ivec3(ivec2(gl_FragCoord.xy), int(pc.extra_parameters[0].x + 0.5)), 0);
}
