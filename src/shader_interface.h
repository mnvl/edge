// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

/// What edge's C++ and its shaders agree on, the C++ side of edge.glsl: the descriptor bindings, the flags, the vertex attribute
/// locations, the push constants, and which built-in shaders draw a vertex.  Every renderer fills and binds by this; a change here is a
/// change to edge.glsl (test_frame_resources.cc probes each of them through a shader, on every backend).

#include <cstddef>
#include <cstdint>
#include <string>

#include "forward.h"
#include "material.h"
#include "vertex_traits.h"

namespace edge
{

class VertexTraits;

namespace shader
{

/// Descriptor bindings (set 0), edge.glsl's EDGE_BINDING_*.
enum Binding : uint32_t
{
	BINDING_DIFFUSE_TEXTURE_0 = 0,           // the base colour; 1-3: texture blending
	BINDING_TEXTURE_BLENDING_WEIGHTS = 4,
	BINDING_NORMAL_MAP = 5,
	BINDING_JOINT_MATRICES = 6,              // vertex stage
	BINDING_INSTANCES = 7,                   // vertex stage: an instanced draw's matrices (edge_instance.glsl)
	BINDING_DRAW_CONSTANTS = 8,              // vertex stage
	BINDING_FRAME_CONSTANTS = 9,             // Renderer::frame_constants
	BINDING_METALLIC_ROUGHNESS = 10,
	BINDING_EMISSIVE = 11,
	BINDING_GLOBAL_TEXTURE_0 = 12,           // to 12 + Renderer::GLOBAL_TEXTURES_COUNT - 1
	BINDING_MATERIAL_PARAMETERS = 20,        // both stages: Material::parameters
	BINDING_PREVIOUS_INSTANCES = 21,         // vertex stage: an instanced draw's matrices the frame before (edge_motion.glsl)
	BINDING_PREVIOUS_JOINT_MATRICES = 22,    // vertex stage: a skinned draw's joint matrices the frame before (edge_motion.glsl)
};

/// PushConstants::flags, edge.glsl's EDGE_FLAG_*: what a material has (every draw), and what the uber draw call draws (texture
/// blending, a skin), and whether it is instanced; the bits from OCCLUSION_STRENGTH_SHIFT up are no flags.
enum Flag : uint32_t
{
	FLAG_TEXTURE_BLENDING_VERTEX = 1u,
	FLAG_TEXTURE_BLENDING_TEXTURE = 2u,
	FLAG_NORMAL_MAP = 4u,
	FLAG_SKELETAL = 8u,
	FLAG_INSTANCED = 16u,                    // the draw's instances are at BINDING_INSTANCES, gl_InstanceIndex each
	FLAG_METALLIC_ROUGHNESS_MAP = 32u,
	FLAG_EMISSIVE_MAP = 64u,
	FLAG_OCCLUSION_MAP = 128u,               // the metallic-roughness texture's red is occlusion (Material::occlusion)
	FLAG_PREVIOUS_UNKNOWN = 256u,            // the draw's previous frame is not known (!DrawCall::has_previous()): no history to reuse
};

/// PushConstants::flags' bits from this one up (16 to 31): the material's occlusion strength in 65535ths of 1 where it has occlusion
/// (Material::occlusion_strength), 0 otherwise; edge.glsl's EDGE_OCCLUSION_STRENGTH(flags).
uint32_t const OCCLUSION_STRENGTH_SHIFT = 16;

/// The attribute locations a vertex element is at, edge.glsl's EDGE_LOCATION_*: 0 position, 1 tangent, 2 binormal, 3 normal, 4-5 joint
/// indices, 6-7 joint weights, 8 texture coordinates, 9 the second set (blend weights), 10 colour.  -1 for an element no shader reads.
int attribute_location(VertexElement::Usage usage, std::uint8_t usage_index);
/// The location of @p element; std::logic_error for an element no shader reads.
int attribute_location(VertexElement const &element);
int const ATTRIBUTE_LOCATIONS = 11;

/// Whether the vertex has all of what skinning reads: indices and weights, two words of each.
bool skinned(VertexTraits const &traits);

/// The push constants every draw gets, edge.glsl's PushConstants: 128 bytes, the most Vulkan promises.
struct PushConstants
{
	float extra_parameters[Material::EXTRA_PARAMETERS_COUNT][4];   // Material::extra_parameter
	float camera_eye[4];                                           // xyz, 1
	float base_color_factor[4];
	float material_pbr[4];                                         // metallic factor, roughness factor, alpha cutoff, normal map scale
	float emissive_factor[3];                                      // rgb (emissive strength applied)
	uint32_t flags;                                                // Flag; in the fourth word of the vec3's 16 bytes
};
static_assert(sizeof(PushConstants) == 128, "the push constants are 128 bytes, the most every Vulkan device has");
static_assert(offsetof(PushConstants, flags) == 124, "flags are the fourth word of emissive_factor's vec3 slot");

/// The push constants of a draw of @p material seen from @p eye: the material's values and the flags of the textures it has, and, for
/// the uber draw call (@p uber), the flags of what it draws: texture blending (a weights texture, or the vertex's second set of
/// coordinates) and a skin (@p skin, its joint matrices given).
PushConstants push_constants(Material const &material, VertexTraits const &traits, bool uber, bool skin, rove::vec<3> const &eye,
	bool instanced = false, bool previous_unknown = false);

/// The shaders that draw a vertex of @p traits with @p program: the program's own where it names one, or the built-in ones (a .spv
/// name, which each renderer finds its translation of): vertex_skin for a skinned vertex, vertex_blend for one with a second set of
/// texture coordinates, vertex otherwise; fragment.
struct Shaders
{
	std::string vertex, fragment;
};
Shaders shaders(ShaderProgram const *program, VertexTraits const &traits);

}

}
