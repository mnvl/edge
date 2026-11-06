// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#version 450
#extension GL_GOOGLE_include_directive : require

// FullscreenCopy's (fullscreen_copy.h): a triangle over the whole target, its vertices already in clip space.

#include "edge.glsl"

layout(location = EDGE_LOCATION_POSITION) in vec3 in_position;
layout(location = EDGE_LOCATION_TEXCOORD_0) in vec2 in_texcoord0;

layout(location = EDGE_VARYING_TEXCOORD) out vec2 frag_texcoord0;

void main()
{
	gl_Position = vec4(in_position.xy, 0.0, 1.0);
	frag_texcoord0 = in_texcoord0;
}
