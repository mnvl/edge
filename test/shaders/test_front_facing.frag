// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#version 450
#extension GL_GOOGLE_include_directive : require

// test_vulkan_cull.cc: red where the face drawn is a front face (gl_FrontFacing), green where it is a back face.

#include "edge.glsl"

layout(location = 0) in vec4 frag_color;
layout(location = 0) out vec4 out_color;

void main()
{
	out_color = gl_FrontFacing ? vec4(1.0, 0.0, 0.0, 1.0) : vec4(0.0, 1.0, 0.0, 1.0);
}
