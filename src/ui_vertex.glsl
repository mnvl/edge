// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#version 450
#extension GL_GOOGLE_include_directive : require

// ImGuiRenderer's (imgui_renderer.h): Dear ImGui's triangles.  Position: pixels of the target; the draw's world matrix takes them to
// clip space for the rectangle its viewport covers.  The vertex's colour, straight alpha.

#include "edge.glsl"

layout(location = EDGE_LOCATION_POSITION) in vec3 in_position;
layout(location = EDGE_LOCATION_TEXCOORD_0) in vec2 in_texcoord0;
layout(location = EDGE_LOCATION_COLOR) in vec4 in_color;

layout(location = EDGE_VARYING_TEXCOORD) out vec2 frag_texcoord0;
layout(location = EDGE_VARYING_COLOR) out vec4 frag_color;

void main()
{
	gl_Position = dc.world_matrix * vec4(in_position.xy, 0.0, 1.0);
	frag_texcoord0 = in_texcoord0;
	frag_color = in_color;
}
