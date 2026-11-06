// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#version 450

// test_motion.cc: the motion of the fragment since the frame before in the target's texture coordinates (u right, v down), as
// edge_motion.glsl says to make it, in red and green; blue 0, alpha 1 where something is drawn.

layout(location = 0) in vec4 clip;
layout(location = 1) in vec4 previous_clip;

layout(location = 0) out vec4 out_color;

void main()
{
	vec2 motion = (clip.xy / clip.w - previous_clip.xy / previous_clip.w) * vec2(0.5, -0.5);
	out_color = vec4(motion, 0.0, 1.0);
}
