// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#version 300 es
precision highp float;

// test_gles_renderer.cc: a fragment shader that does not compile (an undeclared name), whose error the renderer reports.

layout(location = 0) out vec4 out_color;

void main()
{
	out_color = no_such_colour;
}
