// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

// How a .edge file is laid out: cereal's binary archive over the structures of serialized_model.h.  edge's own, and its tests':
// no public header includes this, so cereal is no business of a program built against edge (Model::read_file() and write_file()
// read and write the file).  The order of the fields is the file's; a change is a new MODEL_BINARY_VERSION.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

#include <cereal/cereal.hpp>
#include <cereal/archives/binary.hpp>
#include <cereal/types/common.hpp>
#include <cereal/types/array.hpp>
#include <cereal/types/optional.hpp>
#include <cereal/types/string.hpp>
#include <cereal/types/vector.hpp>

#include "serialized_model.h"

namespace edge {

/// cereal's binary input over the bytes of a whole file, which knows how many are left: a length that has no room in them is
/// damage, refused before anything that long is allocated (every element of every sequence in the file takes a byte at least).
class BoundedInputArchive: public cereal::InputArchive<BoundedInputArchive, cereal::AllowEmptyClassElision>
{
public:
	BoundedInputArchive(char const *data, std::size_t size): InputArchive(this), data_(data), left_(size) {}

	void loadBinary(void *destination, std::size_t size)
	{
		if (size > left_)
		{
			throw cereal::Exception("damaged: " + std::to_string(size) + " bytes wanted where " + std::to_string(left_) + " are left");
		}
		if (size != 0)      // (an empty vector's data() may be null, which memcpy may not be given)
			std::memcpy(destination, data_, size);
		data_ += size;
		left_ -= size;
	}

	std::size_t left() const { return left_; }

private:
	char const *data_;
	std::size_t left_;
};

template <class T>
std::enable_if_t<std::is_arithmetic_v<T> && !std::is_same_v<T, bool>> CEREAL_LOAD_FUNCTION_NAME(BoundedInputArchive &archive, T &value)
{
	archive.loadBinary(&value, sizeof(T));
}

// (a bool whose byte is neither 0 nor 1 is undefined behaviour to read as one, and a file may hold any byte)
inline void CEREAL_LOAD_FUNCTION_NAME(BoundedInputArchive &archive, bool &value)
{
	std::uint8_t byte = 0;
	archive.loadBinary(&byte, 1);
	if (byte > 1)
		throw cereal::Exception("damaged: a true or false of " + std::to_string(byte));
	value = byte != 0;
}

template <class T>
void CEREAL_LOAD_FUNCTION_NAME(BoundedInputArchive &archive, cereal::NameValuePair<T> &pair)
{
	archive(pair.value);
}

template <class T>
void CEREAL_LOAD_FUNCTION_NAME(BoundedInputArchive &archive, cereal::SizeTag<T> &tag)
{
	archive(tag.size);
	if (tag.size > archive.left())
	{
		throw cereal::Exception("damaged: a length of " + std::to_string(tag.size) + " where " + std::to_string(archive.left()) + " bytes are left");
	}
}

template <class T>
void CEREAL_LOAD_FUNCTION_NAME(BoundedInputArchive &archive, cereal::BinaryData<T> &data)
{
	archive.loadBinary(data.data, static_cast<std::size_t>(data.size));
}

// The structures, field by field (found by cereal through ADL)

template <class Archive>
void serialize(Archive &archive, SerializedTexture &t)
{
	archive(t.filename, t.width, t.height, t.pixel_format, t.srgb, t.mipmaps, t.provided_mip_levels, t.pixels);
}

/// A material as a file of @p version has it: no occlusion before 11.
template <class Archive>
void load_material(Archive &archive, SerializedMaterial &m, std::uint32_t version)
{
	archive(m.name, m.cull_mode, m.zwrite_enabled, m.alpha_blend_enabled, m.preserve_destination_alpha, m.src_blend_mode,
		m.dest_blend_mode, m.extra_parameters, m.base_color_factor, m.metallic_factor, m.roughness_factor, m.emissive_factor,
		m.alpha_cutoff, m.normal_scale, m.diffuse_textures, m.texture_blending_weights, m.normal_map, m.metallic_roughness, m.emissive);
	if (version >= 11)
	{
		archive(m.occlusion, m.occlusion_strength);
	}
}

template <class Archive>
void save(Archive &archive, SerializedMaterial const &m)
{
	archive(m.name, m.cull_mode, m.zwrite_enabled, m.alpha_blend_enabled, m.preserve_destination_alpha, m.src_blend_mode,
		m.dest_blend_mode, m.extra_parameters, m.base_color_factor, m.metallic_factor, m.roughness_factor, m.emissive_factor,
		m.alpha_cutoff, m.normal_scale, m.diffuse_textures, m.texture_blending_weights, m.normal_map, m.metallic_roughness, m.emissive,
		m.occlusion, m.occlusion_strength);
}

template <class Archive>
void load(Archive &archive, SerializedMaterial &m)
{
	load_material(archive, m, MODEL_BINARY_VERSION);
}

template <class Archive>
void serialize(Archive &archive, SerializedTriangles &t)
{
	archive(t.material_index, t.minimal_vertex_index, t.vertices_count, t.first_index_offset, t.triangles_count);
}

template <class Archive>
void serialize(Archive &archive, SerializedLod &lod)
{
	archive(lod.error, lod.triangles);
}

template <class Archive>
void serialize(Archive &archive, SerializedCustomAttribute &a)
{
	archive(a.name, a.components, a.values);
}

// (a mesh's vertices and morph targets as the components its vertex format names, packed: pack_vertices())
template <class Archive>
void save(Archive &archive, SerializedMesh const &m)
{
	GenericVertex::format_type const format = serialized_vertex_format(m.serialization_type);
	std::vector<std::vector<std::uint8_t>> deltas;
	for (std::vector<GenericVertex> const &target : m.morph_target_deltas)
	{
		deltas.push_back(pack_vertices(target, format));
	}
	archive(m.serialization_type, m.id, m.name, m.bind_shape_matrix, m.inverted_bind_matrices, m.joint_index_transform, m.triangles,
		m.custom_attributes, m.pose_bounds, m.always_visible, pack_vertices(m.vertices, format), m.indices, deltas, m.morph_weights, m.lods);
}

template <class Archive>
void load(Archive &archive, SerializedMesh &m)
{
	std::vector<std::uint8_t> packed;
	std::vector<std::vector<std::uint8_t>> deltas;
	archive(m.serialization_type, m.id, m.name, m.bind_shape_matrix, m.inverted_bind_matrices, m.joint_index_transform, m.triangles,
		m.custom_attributes, m.pose_bounds, m.always_visible, packed, m.indices, deltas, m.morph_weights, m.lods);
	GenericVertex::format_type const format = serialized_vertex_format(m.serialization_type);
	m.vertices = unpack_vertices(packed, format);
	m.morph_target_deltas.clear();
	for (std::vector<std::uint8_t> const &target : deltas)
	{
		m.morph_target_deltas.push_back(unpack_vertices(target, format));
	}
}

template <class Archive>
void serialize(Archive &archive, SerializedJointTransform &t)
{
	archive(t.sid, t.matrix);
}

template <class Archive>
void serialize(Archive &archive, SerializedJoint &j)
{
	archive(j.index, j.id, j.name, j.parent_index, j.child_indices, j.mesh_index, j.matrix, j.transforms);
}

template <class Archive>
void serialize(Archive &archive, SerializedKeyframe &k)
{
	archive(k.time, k.values);
}

template <class Archive>
void serialize(Archive &archive, SerializedSampler &s)
{
	archive(s.joint_index, s.sid, s.keyframes);
}

template <class Archive>
void serialize(Archive &archive, SerializedWeightsSampler &s)
{
	archive(s.mesh_index, s.interpolation, s.keyframes);
}

template <class Archive>
void serialize(Archive &archive, SerializedAnimation &a)
{
	archive(a.name, a.samplers, a.weight_samplers);
}

template <class Archive>
void save(Archive &archive, SerializedModel const &m)
{
	archive(m.magic, m.version, m.pipeline, m.options, m.textures, m.materials, m.meshes, m.joints, m.animations);
}

template <class Archive>
void load(Archive &archive, SerializedModel &m)
{
	archive(m.magic, m.version);
	if (m.magic != MODEL_BINARY_MAGIC)
	{
		throw std::runtime_error("invalid model file: bad magic");
	}
	if (m.version < MODEL_BINARY_OLDEST_VERSION || m.version > MODEL_BINARY_VERSION)
	{
		throw std::runtime_error("unsupported model file version " + std::to_string(m.version) + ": this reader takes versions " +
			std::to_string(MODEL_BINARY_OLDEST_VERSION) + " to " + std::to_string(MODEL_BINARY_VERSION) +
			" (convert the model again from its glTF)");
	}
	archive(m.pipeline, m.options, m.textures);
	// the materials as a vector is saved (a length, then each), each as the file's version has it
	cereal::size_type count = 0;
	archive(cereal::make_size_tag(count));
	m.materials.assign(static_cast<std::size_t>(count), SerializedMaterial());
	for (SerializedMaterial &material : m.materials)
	{
		load_material(archive, material, m.version);
	}
	archive(m.meshes, m.joints, m.animations);
}

}  // namespace edge

CEREAL_REGISTER_ARCHIVE(edge::BoundedInputArchive)
// it reads what BinaryOutputArchive writes, an enum as the number under it (cereal/types/common.hpp) among the rest
namespace cereal::traits::detail {
template <> struct get_output_from_input<edge::BoundedInputArchive> { using type = cereal::BinaryOutputArchive; };
}
