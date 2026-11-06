// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

// glTF's metallic-roughness BRDF (GGX, Smith's visibility, Schlick's Fresnel), and sRGB's
// transfer.  Functions only: it declares nothing bound, so a shader includes it beside its own textures and constants.
//
//   #include "edge.glsl"
//   #include "edge_brdf.glsl"

#ifndef EDGE_BRDF_GLSL
#define EDGE_BRDF_GLSL

const float EDGE_PI = 3.14159265358979;

// GGX's distribution of the microfacets' normals, a = roughness squared.
float edge_d_ggx(float ndh, float a)
{
	float a2 = a * a;
	float d = ndh * ndh * (a2 - 1.0) + 1.0;
	return a2 / (EDGE_PI * d * d);
}

// Smith's visibility (Schlick-GGX), with the 1 / (4 n.v n.l) of the specular term in it.
float edge_v_smith(float ndv, float ndl, float a)
{
	float k = a * 0.5;
	return 0.25 / ((ndv * (1.0 - k) + k) * (ndl * (1.0 - k) + k));
}

vec3 edge_f_schlick(vec3 f0, float vdh)
{
	return f0 + (1.0 - f0) * pow(1.0 - vdh, 5.0);
}

// What a surface of this albedo, metalness and roughness at unit normal n sends toward v (unit, toward the eye) of a unit irradiance
// arriving along l (unit, toward the light): diffuse and specular, times n.l.  Zero for a light behind the surface.
vec3 edge_brdf(vec3 albedo, float metal, float rough, vec3 n, vec3 v, vec3 l)
{
	float ndl = dot(n, l);
	if (ndl <= 0.0)
		return vec3(0.0);
	vec3 h = normalize(l + v);
	float ndv = max(dot(n, v), 1e-3);
	float a = max(rough * rough, 0.002);
	vec3 f0 = mix(vec3(0.04), albedo, metal);
	vec3 f = edge_f_schlick(f0, max(dot(v, h), 0.0));
	vec3 spec = edge_d_ggx(max(dot(n, h), 0.0), a) * edge_v_smith(ndv, ndl, a) * f;
	vec3 diffuse = (1.0 - f) * (1.0 - metal) * albedo / EDGE_PI;
	return (diffuse + spec) * ndl;
}

// sRGB's transfer: what a screen shows of a linear colour, and back (edge::srgb_encode, srgb_decode).
vec3 edge_srgb_encode(vec3 c)
{
	c = clamp(c, 0.0, 1.0);
	return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(vec3(0.0031308), c));
}

vec3 edge_srgb_decode(vec3 c)
{
	return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), c));
}

#endif
