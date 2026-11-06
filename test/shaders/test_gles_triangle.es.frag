// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#version 300 es
precision highp float;

// test_gles_renderer.cc: the material's base colour factor.

uniform vec4 u_base_color_factor;

layout(location = 0) out vec4 out_color;

void main()
{
	out_color = u_base_color_factor;
}
