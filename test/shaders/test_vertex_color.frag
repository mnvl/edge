// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#version 450
#extension GL_GOOGLE_include_directive : require

// test_vertex_color_shader.cc: returns the vertex color the vertex shader handed on.

#include "edge.glsl"

layout(location = 0) in vec4 frag_color;
layout(location = 0) out vec4 out_color;

void main()
{
	out_color = frag_color;
}
