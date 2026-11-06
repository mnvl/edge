// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

// Cube render targets (RenderTargetFormat::cube): face i of one is drawn by a pass into RenderTarget::layer(i) with the camera
// Camera::set_cube_face(eye, i, z_near, z_far) makes, and a shader samples the cube in a world direction d from the eye with
// edge_cube_direction(d):
//
//   layout(set = 0, binding = EDGE_BINDING_GLOBAL_TEXTURE(0)) uniform samplerCube reflection;
//   vec3 seen = texture(reflection, edge_cube_direction(d)).rgb;
//
// Edge draws an image top row first, as Vulkan does, while the cube's faces are laid out for images drawn bottom row first (OpenGL's):
// the faces hold the world mirrored in y, and this mirrors the direction back.  A point light's shadow, a cube's readable depth set by
// set_global_shadow_texture (a samplerCubeShadow), compares a point p (from the light) at the depth its face's camera gave it:
//
//   float lit = texture(shadow, vec4(edge_cube_direction(p), edge_cube_depth(p, z_near, z_far)));

#ifndef EDGE_CUBE_GLSL
#define EDGE_CUBE_GLSL

vec3 edge_cube_direction(vec3 d)
{
	return vec3(d.x, -d.y, d.z);
}

/// The depth Camera::set_cube_face(eye, i, z_near, z_far)'s projection gives a point p from the eye, on the face it is seen through: its
/// largest coordinate is the distance along that face's axis, 0 at z_near and 1 at z_far.
float edge_cube_depth(vec3 p, float z_near, float z_far)
{
	float z = max(max(abs(p.x), abs(p.y)), abs(p.z));
	return z_far / (z_far - z_near) * (1.0 - z_near / z);
}

#endif
