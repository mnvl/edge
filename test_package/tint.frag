#version 450
#extension GL_GOOGLE_include_directive : require
#include "edge.glsl"
layout(location = 0) out vec4 color;
void main() { color = vec4(1.0, 0.5, 0.25, 1.0); }
