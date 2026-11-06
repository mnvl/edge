// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#version 450
#extension GL_GOOGLE_include_directive : require

// test_vulkan_pipeline_cache.cc: the vertex colour, inverted (1 - rgb), so that a draw with this program tells from one with test_vertex_color_frag.

#include "edge.glsl"

layout(location = 0) in vec4 frag_color;
layout(location = 0) out vec4 out_color;

void main()
{
	out_color = vec4(1.0 - frag_color.rgb, frag_color.a);
}
