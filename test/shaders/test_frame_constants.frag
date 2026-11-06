// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#version 450

// test_frame_resources.cc: the frame constants' first vec4, plus the second's x on red.

layout(location = 0) in vec2 frag_texcoord0;
layout(location = 0) out vec4 out_color;

layout(set = 0, binding = 9) uniform FrameConstants
{
	vec4 a;
	vec4 b;
} fc;

void main()
{
	out_color = fc.a + vec4(fc.b.x, 0.0, 0.0, 0.0);
}
