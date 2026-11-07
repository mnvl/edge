// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "pixel.h"

#include <rove/aabb.h>
#include <rove/matrix.h>
#include <rove/vec.h>

#include "generic_vertex.h"
#include "mesh.h"
#include "material.h"

namespace edge {

constexpr std::uint32_t MODEL_BINARY_MAGIC = 0x4544424dU;  // "EDBM"
constexpr std::uint32_t MODEL_BINARY_VERSION = 11U;
/// The oldest version read: 10, whose materials have no occlusion (11 has SerializedMaterial::occlusion and its strength).
constexpr std::uint32_t MODEL_BINARY_OLDEST_VERSION = 10U;

/// The vertex format a mesh's serialization type names (`static_mesh:<format>`, Mesh::serialization_type()).  Throws
/// std::runtime_error for a type that names none.
GenericVertex::format_type serialized_vertex_format(std::string const &serialization_type);
/// The components of @p vertices that @p format names, in the order of their bits, packed, vertex after vertex.
std::vector<std::uint8_t> pack_vertices(std::vector<GenericVertex> const &vertices, GenericVertex::format_type format);
/// pack_vertices() undone; the components @p format does not name are zero.  Throws std::runtime_error for bytes that are not
/// a whole number of vertices.
std::vector<GenericVertex> unpack_vertices(std::vector<std::uint8_t> const &bytes, GenericVertex::format_type format);

/// A texture as the GPU has it, with the CPU's copy of what it holds: `pixels` is the texels of level 0, or of all
/// `provided_mip_levels` levels one after the other when the file brought its own (a DDS).  The mip chain of a texture with
/// `mipmaps` is made again from level 0 when it is uploaded.
struct SerializedTexture
{
	std::string filename;                 // where the loader took it from, for messages
	int width = 0;
	int height = 0;
	PixelFormat pixel_format{};
	bool srgb = false;
	bool mipmaps = false;
	int provided_mip_levels = 0;
	std::vector<std::uint8_t> pixels;
};

/// A material: every setting a material has except its shader programs, which are the renderer's.  Textures are indices into
/// the model's table of textures, -1 for none.
struct SerializedMaterial
{
	std::string name;
	int cull_mode = static_cast<int>(Material::CullMode::NONE);
	bool zwrite_enabled = true;
	bool alpha_blend_enabled = false;
	bool preserve_destination_alpha = false;
	int src_blend_mode = static_cast<int>(Material::BlendMode::ONE);
	int dest_blend_mode = static_cast<int>(Material::BlendMode::ZERO);
	std::array<rove::vec<4>, Material::EXTRA_PARAMETERS_COUNT> extra_parameters{};
	rove::vec<4> base_color_factor = rove::vec<4>(1.0f, 1.0f, 1.0f, 1.0f);      // as a new Material has them
	float metallic_factor = 1.0f;
	float roughness_factor = 1.0f;
	rove::vec<3> emissive_factor{};
	float alpha_cutoff = 0.0f;
	float normal_scale = 1.0f;
	std::vector<std::int64_t> diffuse_textures;
	std::int64_t texture_blending_weights = -1;
	std::int64_t normal_map = -1;
	std::int64_t metallic_roughness = -1;
	std::int64_t emissive = -1;
	bool occlusion = false;               // the metallic-roughness texture's red is occlusion (Material::occlusion; from version 11)
	float occlusion_strength = 1.0f;
};

// (the file's numbers are of fixed widths, never size_t's: a browser's WebAssembly reads with a size_t of 4 bytes what a desktop wrote with 8)
struct SerializedTriangles
{
	std::int64_t material_index = -1;
	std::uint64_t minimal_vertex_index = 0;
	std::uint64_t vertices_count = 0;
	std::uint64_t first_index_offset = 0;
	std::uint64_t triangles_count = 0;
};

/// A level of detail of a mesh (add_lods()): groups over the mesh's own vertices and indices, in the order of the mesh's `triangles`.
struct SerializedLod
{
	float error = 0;                               // how far the level's surface may be from level 0's, in the mesh's units
	std::vector<SerializedTriangles> triangles;
};

struct SerializedCustomAttribute
{
	std::string name;
	unsigned components = 1;
	std::vector<float> values;
};

/// A mesh.  Its vertices are kept as the components its vertex format names (the one its serialization type names) and no
/// more, packed (pack_vertices()); so are its morph targets'.
struct SerializedMesh
{
	std::string serialization_type;       // Mesh::serialization_type(): "<kind>:<vertex format>"
	std::string id;
	std::string name;
	rove::matrix<4,4> bind_shape_matrix{};
	std::vector<rove::matrix<4,4>> inverted_bind_matrices;
	std::vector<std::uint64_t> joint_index_transform;      // (Mesh::joint_index_transform, as wide on every machine)
	std::vector<SerializedTriangles> triangles;
	std::vector<SerializedCustomAttribute> custom_attributes;
	std::optional<rove::aabb<3>> pose_bounds;    // a skinned mesh's (BasicAnimatedMesh::pose_bounds()), none for the others
	bool always_visible = false;
	std::vector<GenericVertex> vertices;
	std::vector<unsigned> indices;
	std::vector<std::vector<GenericVertex>> morph_target_deltas;
	std::vector<float> morph_weights;
	std::vector<SerializedLod> lods;      // levels 1, 2, ... (level 0 is `triangles`); their indices follow the others' in `indices`
};

struct SerializedJointTransform
{
	std::string sid;
	rove::matrix<4,4> matrix{};
};

struct SerializedJoint
{
	std::uint64_t index = 0;
	std::string id;
	std::string name;
	std::int64_t parent_index = -1;
	std::vector<std::int64_t> child_indices;      // in the order Joint::childs has them
	std::int64_t mesh_index = -1;
	rove::matrix<4,4> matrix{};
	std::vector<SerializedJointTransform> transforms;
};

struct SerializedKeyframe
{
	float time = 0;
	std::vector<float> values;            // a joint sampler's translation xyz, rotation xyzw, scale xyz; a weights sampler's weights
};

/// What a clip writes to a joint's transform with this sid.
struct SerializedSampler
{
	std::int64_t joint_index = -1;
	std::string sid;
	std::vector<SerializedKeyframe> keyframes;
};

/// What a clip writes to a mesh's morph weights.
struct SerializedWeightsSampler
{
	std::int64_t mesh_index = -1;
	int interpolation = 0;
	std::vector<SerializedKeyframe> keyframes;
};

struct SerializedAnimation
{
	std::string name;
	std::vector<SerializedSampler> samplers;
	std::vector<SerializedWeightsSampler> weight_samplers;
};

struct SerializedModel
{
	std::uint32_t magic = MODEL_BINARY_MAGIC;
	std::uint32_t version = MODEL_BINARY_VERSION;
	/// What made the file: the pipeline_hash() of the edge that wrote it, and how it was made, the converter's options as
	/// convert_gltf_to_edge() spells them (none for a model saved by Model::save_to_file()).
	std::string pipeline;
	std::string options;
	std::vector<SerializedTexture> textures;
	std::vector<SerializedMaterial> materials;
	std::vector<SerializedMesh> meshes;
	std::vector<SerializedJoint> joints;
	std::vector<SerializedAnimation> animations;
};

/// The textures of a model, each once however many materials use it.
class TextureTable
{
public:
	/// The index of @p texture in the table, which it is added to on first sight; -1 for no texture.  Throws
	/// std::runtime_error for a texture that has no CPU copy to save.
	std::int64_t add(TexturePtr const &texture);
	std::vector<SerializedTexture> const &textures() const { return textures_; }

private:
	std::vector<SerializedTexture> textures_;
	std::unordered_map<Texture const *, std::int64_t> index_;
};

SerializedTexture export_texture(TexturePtr const &texture);
/// A texture on @p renderer_ptr as @p serialized describes it; its CPU copy is kept when @p keep_cpu_copy.  Throws
/// std::runtime_error when the texels are not as many as the size, format and levels need.
TexturePtr import_texture(SerializedTexture const &serialized, RendererPtr const &renderer_ptr, bool keep_cpu_copy);
SerializedMaterial export_material(MaterialPtr const &material_ptr, TextureTable &textures);
MaterialPtr import_material(SerializedMaterial const &serialized, std::vector<TexturePtr> const &textures, RendererPtr const &renderer_ptr);

}  // namespace edge
