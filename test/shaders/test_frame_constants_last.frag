// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#version 450

// test_frame_resources.cc: the last vec4 of a frame constants block as large as edge's promise, EDGE_FRAME_CONSTANTS_SIZE bytes.

#extension GL_GOOGLE_include_directive : require
#include "edge.glsl"

layout(location = 0) in vec2 frag_texcoord0;
layout(location = 0) out vec4 out_color;

layout(set = 0, binding = EDGE_BINDING_FRAME_CONSTANTS) uniform FrameConstants
{
	vec4 v[EDGE_FRAME_CONSTANTS_SIZE / 16];
} fc;

void main()
{
	out_color = fc.v[EDGE_FRAME_CONSTANTS_SIZE / 16 - 1];
}
