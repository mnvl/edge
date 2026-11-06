// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#version 450
#extension GL_GOOGLE_include_directive : require

// ImGuiRenderer's (imgui_renderer.h): the vertex's colour times the texture (the font atlas, white where there is no glyph).

#include "edge.glsl"

layout(location = EDGE_VARYING_TEXCOORD) in vec2 frag_texcoord0;
layout(location = EDGE_VARYING_COLOR) in vec4 frag_color;
layout(location = 0) out vec4 out_color;

layout(set = 0, binding = EDGE_BINDING_DIFFUSE_TEXTURE_0) uniform sampler2D diffuse_texture_0;

void main()
{
	out_color = frag_color * texture(diffuse_texture_0, frag_texcoord0);
}
