// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

// GltfLoader::read()'s reader, edge's own (not installed for a client): a glTF file as the structures a .edge file holds (SerializedModel),
// which Model::load() makes a model of.  Its parts: gltf_reader.cc (the file, its nodes as joints, skins, buffers), gltf_materials.cc
// (images, textures, materials), gltf_meshes.cc (primitives as vertices), gltf_animation.cc (clips as keyframes).

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#if __has_include(<fastgltf/core.hpp>)
#include <fastgltf/core.hpp>
#else
#include <fastgltf/parser.hpp>
#endif
#include <fastgltf/tools.hpp>
#include <fastgltf/types.hpp>

#include "generic_vertex.h"
#include "gltf_loader.h"
#include "serialized_model.h"

namespace edge
{

/// The glTF extensions edge reads.
fastgltf::Extensions gltf_extensions();
/// The glTF file's JSON, parsed: no buffers, no images.
fastgltf::Asset read_gltf_json(std::string const &filename);
/// The glTF file and the files its buffers and images name (GltfContents::files); load_error for one that is not there.
std::vector<std::filesystem::path> gltf_named_files(fastgltf::Asset const &asset, std::string const &filename);

template<class Attribute>
auto attribute_semantic(Attribute const &attribute, int) -> decltype(attribute.first, std::string_view())
{
	return std::string_view(attribute.first);
}

template<class Attribute>
auto attribute_semantic(Attribute const &attribute, long) -> decltype(attribute.name, std::string_view())
{
	return std::string_view(attribute.name);
}

template<class Attribute>
auto attribute_accessor_index(Attribute const &attribute, int) -> decltype(attribute.second, std::size_t())
{
	return static_cast<std::size_t>(attribute.second);
}

template<class Attribute>
auto attribute_accessor_index(Attribute const &attribute, long) -> decltype(attribute.accessorIndex, std::size_t())
{
	return static_cast<std::size_t>(attribute.accessorIndex);
}

/// The accessor of a primitive's attribute @p semantic, none if it has no such attribute.
template<class Primitive>
std::optional<std::size_t> find_attribute_accessor(Primitive const &primitive, std::string_view semantic)
{
	for (auto const &attribute : primitive.attributes)
	{
		if (attribute_semantic(attribute, 0) == semantic)
		{
			return attribute_accessor_index(attribute, 0);
		}
	}
	return std::nullopt;
}

inline rove::matrix<4,4> convert_matrix(fastgltf::math::fmat4x4 const &m)
{
	rove::matrix<4,4> result;
	for (std::size_t col = 0; col < 4; ++col)
	{
		for (std::size_t row = 0; row < 4; ++row)
		{
			result.ij[col][row] = m[col][row];
		}
	}
	return result;
}

class GltfReader
{
public:
	/// For the glTF file @p filename read as @p options say, its meshes of @p mesh_type (Mesh::serialization_type()).
	/// std::invalid_argument for options that do not go together.
	GltfReader(std::string const &filename, GltfOptions const &options, std::string const &mesh_type);

	/// The model.  Throws GltfLoader::load_error for a file that cannot be read or is not one edge loads.
	SerializedModel read();

private:
	using load_error = GltfLoader::load_error;

	// --- gltf_reader.cc: the file, its nodes, skins and buffers
	void parse();
	/// load_error for an index of a @p what that is not among the @p count there are.
	void check_index(std::size_t index, std::size_t count, char const *what) const;
	/// load_error for an accessor whose elements are not all in its buffer view, or a view not all in its buffer: fastgltf reads them unchecked.
	void check_accessors();
	/// An accessor read as element type E, where it is one: fastgltf asserts that in a Debug build and reads regardless in a Release one.
	template <class E, class F>
	void read_accessor(fastgltf::Accessor const &accessor, F &&f) const
	{
		check_element<E>(accessor);
		fastgltf::iterateAccessor<E>(asset_, accessor, std::forward<F>(f));
	}
	template <class E, class F>
	void read_accessor_with_index(fastgltf::Accessor const &accessor, F &&f) const
	{
		check_element<E>(accessor);
		fastgltf::iterateAccessorWithIndex<E>(asset_, accessor, std::forward<F>(f));
	}
	template <class E>
	void check_element(fastgltf::Accessor const &accessor) const
	{
		if (accessor.type != fastgltf::ElementTraits<E>::type)
			throw load_error("glTF '" + filename_ + "': an accessor of type " + std::string(fastgltf::getAccessorTypeName(accessor.type)) + " where " +
				std::string(fastgltf::getAccessorTypeName(fastgltf::ElementTraits<E>::type)) + " is read");
	}
	void mark_node_recursive(std::size_t node_index);
	/// The nodes the model is made of: the default scene's (all scenes' without one, all nodes without scenes), with the joints of their
	/// skins and those joints' ancestors.
	void select_imported_nodes();
	/// A joint of the model for each imported node, in the nodes' order, with its local transform.
	void build_joints();
	void link_joint_hierarchy();

	struct PreparedSkin
	{
		std::vector<std::size_t> joint_index_transform;          // a skin joint's joint of the model
		std::vector<rove::matrix<4,4>> inverted_bind_matrices;  // by the model's joints (identities but the skin's)
	};
	PreparedSkin const *prepare_skin(std::size_t skin_index);

	fastgltf::span<const std::byte> buffer_data(std::size_t buffer_index);
	fastgltf::span<const std::byte> buffer_view_data(std::size_t buffer_view_index);

	// --- gltf_materials.cc: images, textures, materials
	std::optional<std::size_t> resolve_texture_image_index(std::size_t texture_index) const;
	/// The encoded bytes of an image the file holds (not one it names by URI).
	fastgltf::span<const std::byte> image_encoded_data(std::size_t image_index);
	/// The index in the model of the texture of an image, a file it names or bytes it holds, decoded once for each of colour and data;
	/// load_error for one that cannot be had.
	std::int64_t load_image_texture(std::size_t image_index, bool srgb);
	std::int64_t load_texture(std::size_t texture_index, bool srgb);
	/// The bytes of an image, a file it names or bytes it holds; @p source says which (the file's path, or the image's number).
	/// load_error for a file that cannot be read.
	std::vector<std::byte> image_bytes(std::size_t image_index, std::string &source);
	/// The index in the model of the metallic-roughness texture of material @p material_index with occlusion texture @p occlusion
	/// packed into its red (pack_occlusion()): @p metallic_roughness's (texture) as it is where both are of one image, made once for
	/// each two images otherwise.  load_error naming the file for what cannot be packed.
	std::int64_t load_occlusion(std::size_t material_index, std::optional<std::size_t> metallic_roughness, std::size_t occlusion);
	void build_materials();
	std::int64_t default_material();
	std::int64_t material_for_primitive(fastgltf::Primitive const &primitive);

	/// The texture coordinates a material's textures read: TEXCOORD_<index>, moved by KHR_texture_transform (translation * rotation *
	/// scale of (u, v, 1)).  A mesh has one set of them, so every texture of the material must read the same.
	struct UvSet
	{
		std::size_t index = 0;
		float offset[2] = {0, 0};
		float scale[2] = {1, 1};
		float rotation = 0;
		bool textured = false;

		bool operator==(UvSet const &other) const;
		bool moves() const;
		void apply(float &u, float &v) const;
	};
	UvSet material_uv_set(std::optional<std::size_t> const &material_index) const;

	// --- gltf_meshes.cc: primitives as vertices, meshes
	/// The accessor of a vertex attribute: there, and a value for each of the @p vertices_count vertices.
	fastgltf::Accessor const &attribute_accessor(std::size_t accessor_index, std::string_view semantic, std::size_t vertices_count) const;
	void build_primitive(fastgltf::Primitive const &primitive, std::vector<GenericVertex> &vertices, std::vector<unsigned> &indices,
		GenericVertex::format_type &vertex_format, PreparedSkin const *prepared_skin_ptr, std::vector<std::vector<GenericVertex>> &morph_target_deltas,
		std::map<std::string, SerializedCustomAttribute> &custom_attributes);
	/// The index in the model of the mesh of glTF mesh @p mesh_index as node @p node_index has it (skinned by @p skin_index).
	std::int64_t build_mesh(std::size_t mesh_index, std::optional<std::size_t> skin_index, std::size_t node_index,
		std::vector<float> const &initial_morph_weights, std::size_t &imported_primitives);
	std::size_t build_meshes_for_nodes();
	std::size_t mesh_morph_targets_count(std::size_t mesh_index) const;
	std::vector<float> node_initial_morph_weights(std::size_t node_index) const;

	// --- gltf_animation.cc: clips
	void build_animations();

	std::string filename_;
	GltfOptions options_;
	std::string mesh_type_;
	std::filesystem::path basepath_;
	fastgltf::Asset asset_;
	bool read_ = false;

	SerializedModel model_;
	std::vector<bool> imported_nodes_;
	std::vector<std::int64_t> node_to_joint_;                                    // the joint of each node, -1 for none
	std::map<std::tuple<std::size_t, std::size_t, std::size_t>, std::int64_t> mesh_cache_;
	std::vector<std::int64_t> gltf_materials_;
	std::vector<PreparedSkin> skin_cache_;
	std::vector<bool> skin_prepared_;
	// Keyed by (index, sRGB): one image can serve as colour and as data.
	std::map<std::pair<std::size_t, bool>, std::int64_t> image_texture_cache_;
	/// load_occlusion()'s textures by the images packed: metallic-roughness (-1 for none), occlusion
	std::map<std::pair<std::int64_t, std::size_t>, std::int64_t> occlusion_texture_cache_;
	std::int64_t default_material_ = -1;
};

}
