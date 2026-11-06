// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

// A material as edge's draws give it, glTF's metallic-roughness (occlusion in its texture's red, glTF's ORM): its textures at their
// bindings and its factors in the push constants, made into the surface a fragment shader lights.  After edge.glsl; it declares the
// material's textures (edge_* names), so a shader that includes it does not declare them again.
//
//   EdgeSurface s = edge_surface(uv, normal, tangent, binormal);   // the built-in vertex shaders' EDGE_VARYING_*
//   if (s.alpha < s.alpha_cutoff) discard;

#ifndef EDGE_MATERIAL_GLSL
#define EDGE_MATERIAL_GLSL

layout(set = 0, binding = EDGE_BINDING_DIFFUSE_TEXTURE_0) uniform sampler2D edge_base_color_texture;
layout(set = 0, binding = EDGE_BINDING_NORMAL_MAP) uniform sampler2D edge_normal_texture;
layout(set = 0, binding = EDGE_BINDING_METALLIC_ROUGHNESS) uniform sampler2D edge_metallic_roughness_texture;
layout(set = 0, binding = EDGE_BINDING_EMISSIVE) uniform sampler2D edge_emissive_texture;

struct EdgeSurface
{
	vec3 base_color;      // linear: base colour factor times the texture (sRGB textures are sampled linear)
	float alpha;
	float alpha_cutoff;   // 0: none (blend or opaque)
	float metallic;       // factors times the texture's b and g
	float roughness;
	float occlusion;      // ambient occlusion: 1 + strength * (the texture's r - 1) where the material has it (glTF's ORM), 1 otherwise
	vec3 emissive;        // factor (strength applied) times the texture
	vec3 normal;          // unit: the geometry's, or the normal map's (two channels, z rebuilt, x and y times its scale)
};

// The shading normal: the map's (BC5 maps hold x and y; z is rebuilt) in the frame of tangent, binormal and normal, the map's x and y
// times glTF's normalTexture.scale (material_pbr.w); the geometry's where the material has no map.
vec3 edge_surface_normal(vec2 uv, vec3 normal, vec3 tangent, vec3 binormal)
{
	vec3 n = normalize(normal);
	if ((pc.flags & EDGE_FLAG_NORMAL_MAP) == 0u)
		return n;
	vec3 t = normalize(tangent - n * dot(n, tangent));
	vec3 b = dot(cross(n, t), binormal) < 0.0 ? -cross(n, t) : cross(n, t);
	vec2 xy = (texture(edge_normal_texture, uv).xy * 2.0 - 1.0) * pc.material_pbr.w;
	vec3 m = vec3(xy, sqrt(max(1.0 - dot(xy, xy), 0.0)));
	return normalize(mat3(t, b, n) * m);
}

EdgeSurface edge_surface(vec2 uv, vec3 normal, vec3 tangent, vec3 binormal)
{
	EdgeSurface s;
	vec4 base = texture(edge_base_color_texture, uv) * pc.base_color_factor;
	s.base_color = base.rgb;
	s.alpha = base.a;
	s.alpha_cutoff = pc.material_pbr.z;
	s.metallic = pc.material_pbr.x;
	s.roughness = pc.material_pbr.y;
	s.occlusion = 1.0;
	if ((pc.flags & EDGE_FLAG_METALLIC_ROUGHNESS_MAP) != 0u)
	{
		vec4 mr = texture(edge_metallic_roughness_texture, uv);
		s.roughness *= mr.g;
		s.metallic *= mr.b;
		if ((pc.flags & EDGE_FLAG_OCCLUSION_MAP) != 0u)
			s.occlusion = 1.0 + EDGE_OCCLUSION_STRENGTH(pc.flags) * (mr.r - 1.0);
	}
	s.emissive = pc.emissive_factor;
	if ((pc.flags & EDGE_FLAG_EMISSIVE_MAP) != 0u)
		s.emissive *= texture(edge_emissive_texture, uv).rgb;
	s.normal = edge_surface_normal(uv, normal, tangent, binormal);
	return s;
}

#endif
