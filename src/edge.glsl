// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

// The contract between edge's renderers and a shader drawn through them,
// edge's own or an application's.  The C++ side is shader_interface.h;
// test_frame_resources.cc (edge_glsl) renders through this file on every
// backend and checks each name reaches what the API puts there.
//
// After #version:
//
//   #extension GL_GOOGLE_include_directive : require
//   #include "edge.glsl"
//
// (glslangValidator finds it next to edge's shaders; an application passes
// -I with EDGE_SHADER_INCLUDE_DIR.)  It declares the push constants (pc) and
// the draw constants (dc).  Textures and the frame constants are declared by
// the shader that reads them, at these bindings, under names of its own:
//
//   layout(set = 0, binding = EDGE_BINDING_GLOBAL_TEXTURE(0)) uniform sampler2D shadow_map;

#ifndef EDGE_GLSL
#define EDGE_GLSL

// Descriptor set 0.  Material textures use the material's (repeating,
// anisotropic) sampler, global textures a clamped one.
#define EDGE_BINDING_DIFFUSE_TEXTURE_0 0          // the base colour
#define EDGE_BINDING_DIFFUSE_TEXTURE_1 1          // 1-3: texture blending
#define EDGE_BINDING_DIFFUSE_TEXTURE_2 2
#define EDGE_BINDING_DIFFUSE_TEXTURE_3 3
#define EDGE_BINDING_TEXTURE_BLENDING_WEIGHTS 4
#define EDGE_BINDING_NORMAL_MAP 5
#define EDGE_BINDING_JOINT_MATRICES 6             // vertex stage only
#define EDGE_BINDING_INSTANCES 7                  // vertex stage only: an instanced draw's instances' matrices (edge_instance.glsl)
#define EDGE_BINDING_DRAW_CONSTANTS 8             // vertex stage only
#define EDGE_BINDING_FRAME_CONSTANTS 9            // the application's block, Renderer::set_frame_constants
#define EDGE_BINDING_METALLIC_ROUGHNESS 10        // glTF: roughness in g, metallic in b
#define EDGE_BINDING_EMISSIVE 11
#define EDGE_BINDING_GLOBAL_TEXTURE(i) (12 + (i)) // i < EDGE_GLOBAL_TEXTURES, Renderer::set_global_texture
#define EDGE_BINDING_MATERIAL_PARAMETERS 20       // the material's own block, Material::set_parameters (zeros past what it sets)
#define EDGE_BINDING_PREVIOUS_INSTANCES 21        // vertex stage only: an instanced draw's instances the frame before (edge_motion.glsl)
#define EDGE_BINDING_PREVIOUS_JOINT_MATRICES 22   // vertex stage only: a skinned draw's joint matrices the frame before (edge_motion.glsl)

// Vertex attribute locations (shader::attribute_location): a vertex shader's
// inputs, which a vertex type has or not.
#define EDGE_LOCATION_POSITION 0
#define EDGE_LOCATION_TANGENT 1
#define EDGE_LOCATION_BINORMAL 2
#define EDGE_LOCATION_NORMAL 3
#define EDGE_LOCATION_JOINTS_0 4                  // uvec4: joint indices 0-3
#define EDGE_LOCATION_JOINTS_1 5                  // uvec4: 4-7
#define EDGE_LOCATION_WEIGHTS_0 6                 // vec4: joint weights 0-3
#define EDGE_LOCATION_WEIGHTS_1 7                 // vec4: 4-7
#define EDGE_LOCATION_TEXCOORD_0 8
#define EDGE_LOCATION_TEXCOORD_1 9                // the texture blending weights of a vertex that has them
#define EDGE_LOCATION_COLOR 10

// What the built-in vertex shaders (vertex*.glsl) hand a fragment shader: a
// fragment shader of a material's own declares the ones it reads.  Position,
// normal, tangent and binormal are in world space.
#define EDGE_VARYING_TEXCOORD 0                   // vec2
#define EDGE_VARYING_BLEND_WEIGHTS 1              // vec4: texture blending's (1, 0, 0, 0 without)
#define EDGE_VARYING_POSITION 2                   // vec3
#define EDGE_VARYING_NORMAL 3                     // vec3, unit
#define EDGE_VARYING_TANGENT 4                    // vec3, unit
#define EDGE_VARYING_BINORMAL 5                   // vec3, unit
#define EDGE_VARYING_COLOR 6                      // vec4: the vertex's colour (ui_vertex's; the built-in vertex shaders pass none)

#define EDGE_GLOBAL_TEXTURES 8                    // Renderer::GLOBAL_TEXTURES_COUNT
#define EDGE_EXTRA_PARAMETERS 4                   // Material::EXTRA_PARAMETERS_COUNT
#define EDGE_MATERIAL_PARAMETERS_SIZE 1024        // bytes, Material::PARAMETERS_SIZE
#define EDGE_MAX_INSTANCES 256                    // Renderer::MAX_INSTANCES: the instances of one batch (a draw of more is drawn in batches)
#define EDGE_FRAME_CONSTANTS_SIZE 16384           // bytes, Renderer::FRAME_CONSTANTS_SIZE: what every backend promises a uniform block

// pc.flags: the material's textures (any draw call) ...
const uint EDGE_FLAG_NORMAL_MAP = 4u;
const uint EDGE_FLAG_METALLIC_ROUGHNESS_MAP = 32u;
const uint EDGE_FLAG_EMISSIVE_MAP = 64u;
const uint EDGE_FLAG_OCCLUSION_MAP = 128u;        // the metallic-roughness texture's red is occlusion (Material::has_occlusion)
// ... and in bits 16 to 31 the material's occlusion strength, 0 to 1 (Material::get_occlusion_strength; 0 without occlusion) ...
#define EDGE_OCCLUSION_STRENGTH(flags) (float((flags) >> 16u) / 65535.0)
// ... the draw's instances (edge_instance.glsl reads it) ...
const uint EDGE_FLAG_INSTANCED = 16u;
// ... whether the draw's previous frame (dc.previous_*, edge_motion.glsl) is not known, so that its previous matrices are this frame's
// for want of them: seen the first time, at two places in the frame before, otherwise placed then, through a camera seen the first time
// or cut (DrawCall::has_previous()); a TAA pass throws its history away for it ...
const uint EDGE_FLAG_PREVIOUS_UNKNOWN = 256u;

// ... and what only the uber draw call sets
const uint EDGE_FLAG_TEXTURE_BLENDING_VERTEX = 1u;
const uint EDGE_FLAG_TEXTURE_BLENDING_TEXTURE = 2u;
const uint EDGE_FLAG_SKELETAL = 8u;

// The descriptor set layout shows binding 8 to the vertex stage only: a
// fragment shader may declare dc (through this file) but must not read it.
layout(set = 0, binding = EDGE_BINDING_DRAW_CONSTANTS) uniform DrawConstants
{
	mat4 world_view_projection;   // projection * view * world
	mat4 world_matrix;
	// the same the frame before (DrawCall::get_shader_previous_world_view_projection(), motion vectors: edge_motion.glsl): the
	// camera's then times the draw's world then; this frame's for a draw seen the first time through a camera seen the first time
	mat4 previous_world_view_projection;
	mat4 previous_world_matrix;
} dc;

// Both stages.
layout(push_constant) uniform PushConstants
{
	vec4 extra_parameters[EDGE_EXTRA_PARAMETERS];   // Material::set_extra_parameter
	vec4 camera_eye;              // xyz, 1 (0 without a camera)
	vec4 base_color_factor;
	vec4 material_pbr;            // metallic factor, roughness factor, alpha cutoff, normal map scale
	vec3 emissive_factor;         // rgb (emissive strength applied)
	uint flags;                   // EDGE_FLAG_*; in the fourth word of the vec3's 16 bytes: all of it is 128 bytes, the most Vulkan promises every device
} pc;

#endif
