// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

// GltfReader's primitives and meshes (gltf_reader.h).

#include "gltf_reader.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <numeric>
#include <stdexcept>

#include "log.h"
#include "tangent_space.h"

namespace edge
{

/// The accessor of a vertex attribute: there, and a value for each of the @p vertices_count vertices.
fastgltf::Accessor const &GltfReader::attribute_accessor(std::size_t accessor_index, std::string_view semantic, std::size_t vertices_count) const
{
	if (accessor_index >= asset_.accessors.size())
	{
		throw load_error("glTF '" + filename_ + "': " + std::string(semantic) + " names accessor " + std::to_string(accessor_index) +
			", which is not there");
	}
	fastgltf::Accessor const &accessor = asset_.accessors[accessor_index];
	if (accessor.count != vertices_count)
	{
		throw load_error("glTF '" + filename_ + "': " + std::string(semantic) + " has " + std::to_string(accessor.count) +
			" values for " + std::to_string(vertices_count) + " vertices");
	}
	return accessor;
}

void GltfReader::build_primitive(fastgltf::Primitive const &primitive, std::vector<GenericVertex> &vertices, std::vector<unsigned> &indices,
	GenericVertex::format_type &vertex_format, PreparedSkin const *prepared_skin_ptr, std::vector<std::vector<GenericVertex>> &morph_target_deltas,
	std::map<std::string, SerializedCustomAttribute> &custom_attributes)
{
	if (primitive.type != fastgltf::PrimitiveType::Triangles)
	{
		throw load_error("glTF '" + filename_ + "': a primitive of mode " + std::to_string(static_cast<int>(primitive.type)) +
			": edge draws triangles (mode 4)");
	}

	std::optional<std::size_t> const position_accessor_index =
		find_attribute_accessor(primitive, "POSITION");
	if (!position_accessor_index)
	{
		throw load_error("glTF '" + filename_ + "': a primitive without POSITION");
	}
	check_index(*position_accessor_index, asset_.accessors.size(), "accessor");
	fastgltf::Accessor const &position_accessor = asset_.accessors[*position_accessor_index];
	if (position_accessor.count == 0)
	{
		throw load_error("glTF '" + filename_ + "': a primitive of no vertices");
	}

	vertices.assign(position_accessor.count, GenericVertex());
	std::memset(vertices.data(), 0, sizeof(GenericVertex) * vertices.size());
	vertex_format = GenericVertex::FORMAT_POSITION;
	morph_target_deltas.clear();

	read_accessor_with_index<fastgltf::math::fvec3>(position_accessor,
		[&](fastgltf::math::fvec3 const &position, std::size_t index)
		{
			if (index >= vertices.size())
			{
				return;
			}
			vertices[index].position.set(position[0], position[1], position[2]);
		});

	if (std::optional<std::size_t> normal_accessor_index = find_attribute_accessor(primitive, "NORMAL"))
	{
		read_accessor_with_index<fastgltf::math::fvec3>(attribute_accessor(*normal_accessor_index, "NORMAL", vertices.size()),
			[&](fastgltf::math::fvec3 const &normal, std::size_t index)
			{
				vertices[index].normal.set(normal[0], normal[1], normal[2]);
			});
		vertex_format |= GenericVertex::FORMAT_NORMAL;
	}

	UvSet const uv_set = material_uv_set(primitive.materialIndex);
	std::string const texcoord_semantic = "TEXCOORD_" + std::to_string(uv_set.index);
	if (std::optional<std::size_t> texcoord_accessor_index = find_attribute_accessor(primitive, texcoord_semantic))
	{
		read_accessor_with_index<fastgltf::math::fvec2>(attribute_accessor(*texcoord_accessor_index, texcoord_semantic, vertices.size()),
			[&](fastgltf::math::fvec2 const &texcoord, std::size_t index)
			{
				float u = texcoord[0], v = texcoord[1];
				if (uv_set.moves())
					uv_set.apply(u, v);
				vertices[index].texcoords.set(u, v);
			});
		vertex_format |= GenericVertex::FORMAT_TEXCOORDS;
	}
	else if (uv_set.textured)
	{
		throw load_error("glTF '" + filename_ + "': a primitive of a textured material has no " + texcoord_semantic);
	}

	if (std::optional<std::size_t> color_accessor_index = find_attribute_accessor(primitive, "COLOR_0"))
	{
		fastgltf::Accessor const &color_accessor = attribute_accessor(*color_accessor_index, "COLOR_0", vertices.size());
		// glTF gives a VEC3 color an alpha of 1; normalised integers come out as floats as glTF normalises them
		if (color_accessor.type == fastgltf::AccessorType::Vec3)
		{
			read_accessor_with_index<fastgltf::math::fvec3>(color_accessor,
				[&](fastgltf::math::fvec3 const &color, std::size_t index)
				{
					vertices[index].color.set(color[0], color[1], color[2], 1.0f);
				});
		}
		else if (color_accessor.type == fastgltf::AccessorType::Vec4)
		{
			read_accessor_with_index<fastgltf::math::fvec4>(color_accessor,
				[&](fastgltf::math::fvec4 const &color, std::size_t index)
				{
					vertices[index].color.set(color[0], color[1], color[2], color[3]);
				});
		}
		else
		{
			throw std::runtime_error("glTF '" + filename_ + "': COLOR_0 is neither a VEC3 nor a VEC4");
		}
		vertex_format |= GenericVertex::FORMAT_COLOR;
	}

	custom_attributes.clear();
	for (auto const &attribute : primitive.attributes)
	{
		std::string_view const name = attribute_semantic(attribute, 0);
		if (name.empty() || name[0] != '_')
		{
			continue;
		}
		fastgltf::Accessor const &accessor = attribute_accessor(attribute_accessor_index(attribute, 0), name, vertices.size());
		SerializedCustomAttribute custom;
		custom.name = std::string(name);
		switch (accessor.type)
		{
		case fastgltf::AccessorType::Scalar:
			custom.components = 1;
			read_accessor<float>(accessor, [&](float value) { custom.values.push_back(value); });
			break;
		case fastgltf::AccessorType::Vec2:
			custom.components = 2;
			read_accessor<fastgltf::math::fvec2>(accessor, [&](fastgltf::math::fvec2 const &v) { custom.values.insert(custom.values.end(), {v[0], v[1]}); });
			break;
		case fastgltf::AccessorType::Vec3:
			custom.components = 3;
			read_accessor<fastgltf::math::fvec3>(accessor, [&](fastgltf::math::fvec3 const &v) { custom.values.insert(custom.values.end(), {v[0], v[1], v[2]}); });
			break;
		case fastgltf::AccessorType::Vec4:
			custom.components = 4;
			read_accessor<fastgltf::math::fvec4>(accessor, [&](fastgltf::math::fvec4 const &v) { custom.values.insert(custom.values.end(), {v[0], v[1], v[2], v[3]}); });
			break;
		default:
			throw std::runtime_error("glTF '" + filename_ + "': " + std::string(name) + " is a matrix: a custom attribute is a scalar or a vector");
		}
		custom_attributes.emplace(std::string(name), std::move(custom));
	}

	bool imported_tangent_space = false;
	if (std::optional<std::size_t> tangent_accessor_index = find_attribute_accessor(primitive, "TANGENT"))
	{
		if (!(vertex_format & GenericVertex::FORMAT_NORMAL))
		{
			throw load_error("glTF '" + filename_ + "': a primitive has TANGENT and no NORMAL");
		}
		read_accessor_with_index<fastgltf::math::fvec4>(attribute_accessor(*tangent_accessor_index, "TANGENT", vertices.size()),
			[&](fastgltf::math::fvec4 const &tangent, std::size_t index)
			{
				vertices[index].tangent.set(tangent[0], tangent[1], tangent[2]);

				rove::vec<3> bitangent = rove::cross(vertices[index].normal, vertices[index].tangent);
				if (tangent[3] < 0.0f)
				{
					bitangent = bitangent * -1.0f;
				}

				vertices[index].binormal = bitangent;
			});
		vertex_format |= GenericVertex::FORMAT_TANGENT;
		vertex_format |= GenericVertex::FORMAT_BINORMAL;
		imported_tangent_space = true;
	}

	if (prepared_skin_ptr)
	{
		std::size_t const joints_count = prepared_skin_ptr->joint_index_transform.size();
		std::string const context = "glTF '" + filename_ + "'";

		auto load_joint_attribute = [&](std::string_view semantic, std::size_t base_offset)
		{
			std::optional<std::size_t> const accessor_index = find_attribute_accessor(primitive, semantic);
			if (!accessor_index)
			{
				return false;
			}
			fastgltf::Accessor const &accessor = attribute_accessor(*accessor_index, semantic, vertices.size());
			read_accessor_with_index<fastgltf::math::u32vec4>(accessor,
				[&](fastgltf::math::u32vec4 const &joint_indices, std::size_t vertex_index)
				{
					for (std::size_t i = 0; i < 4; ++i)
					{
						unsigned const joint_index = static_cast<unsigned>(joint_indices[i]);
						if (joint_index >= joints_count)
						{
							throw load_error(context + ": vertex " + std::to_string(vertex_index) + " names joint " +
								std::to_string(joint_index) + " in " + std::string(semantic) + ", its skin has " +
								std::to_string(joints_count) + " joints");
						}
						vertices[vertex_index].joints.i[base_offset + i] = joint_index;
					}
				});
			return true;
		};

		auto load_weight_attribute = [&](std::string_view semantic, std::size_t base_offset)
		{
			std::optional<std::size_t> const accessor_index = find_attribute_accessor(primitive, semantic);
			if (!accessor_index)
			{
				return false;
			}
			fastgltf::Accessor const &accessor = attribute_accessor(*accessor_index, semantic, vertices.size());
			read_accessor_with_index<fastgltf::math::fvec4>(accessor,
				[&](fastgltf::math::fvec4 const &joint_weights, std::size_t vertex_index)
				{
					for (std::size_t i = 0; i < 4; ++i)
					{
						vertices[vertex_index].weights.i[base_offset + i] = static_cast<float>(joint_weights[i]);
					}
				});
			return true;
		};

		bool const joints_0 = load_joint_attribute("JOINTS_0", 0), joints_1 = load_joint_attribute("JOINTS_1", 4);
		bool const weights_0 = load_weight_attribute("WEIGHTS_0", 0), weights_1 = load_weight_attribute("WEIGHTS_1", 4);
		for (auto const &[joints, weights, n] : {std::tuple{joints_0, weights_0, 0}, std::tuple{joints_1, weights_1, 1}})
		{
			if (joints != weights)
			{
				std::string const j = "JOINTS_" + std::to_string(n), w = "WEIGHTS_" + std::to_string(n);
				throw load_error(context + ": a skinned primitive has " + (joints ? j + " without " + w : w + " without " + j));
			}
		}
		bool const has_joint_indices = joints_0 || joints_1;
		bool const has_joint_weights = weights_0 || weights_1;

		if (has_joint_indices && has_joint_weights)
		{
			vertex_format |= GenericVertex::FORMAT_JOINT_INDICES;
			vertex_format |= GenericVertex::FORMAT_JOINT_WEIGHTS;

			for (auto &vertex : vertices)
			{
				float total_weight = 0.0f;
				for (std::size_t i = 0; i < 8; ++i)
				{
					total_weight += vertex.weights.i[i];
				}

				if (total_weight > 0.0f)
				{
					float const inv_total_weight = 1.0f / total_weight;
					for (std::size_t i = 0; i < 8; ++i)
					{
						vertex.weights.i[i] *= inv_total_weight;
					}
				}
			}
		}
	}

	if (!primitive.targets.empty())
	{
		GenericVertex zero_delta;
		std::memset(&zero_delta, 0, sizeof(zero_delta));

		morph_target_deltas.assign(primitive.targets.size(),
			std::vector<GenericVertex>(vertices.size(), zero_delta));

		for (std::size_t target_index = 0; target_index < primitive.targets.size(); ++target_index)
		{
			// (a delta's other components are zero, as they are where a target moves nothing)
			auto const load_target_attribute = [&](std::string_view semantic, auto setter)
			{
				auto const attribute_it = primitive.findTargetAttribute(target_index, semantic);
				if (attribute_it == primitive.targets[target_index].end())
				{
					return;
				}

				read_accessor_with_index<fastgltf::math::fvec3>(attribute_accessor(static_cast<std::size_t>(attribute_it->accessorIndex),
						"target " + std::to_string(target_index) + " " + std::string(semantic), vertices.size()),
					[&](fastgltf::math::fvec3 const &delta, std::size_t vertex_index)
					{
						setter(morph_target_deltas[target_index][vertex_index], delta);
					});
			};

			load_target_attribute("POSITION",
				[](GenericVertex &vertex, fastgltf::math::fvec3 const &delta)
				{
					vertex.position.set(delta[0], delta[1], delta[2]);
				});

			load_target_attribute("NORMAL",
				[](GenericVertex &vertex, fastgltf::math::fvec3 const &delta)
				{
					vertex.normal.set(delta[0], delta[1], delta[2]);
				});

			load_target_attribute("TANGENT",
				[](GenericVertex &vertex, fastgltf::math::fvec3 const &delta)
				{
					vertex.tangent.set(delta[0], delta[1], delta[2]);
				});
		}
	}

	indices.clear();
	indices.reserve(position_accessor.count);

	if (primitive.indicesAccessor)
	{
		std::size_t const index_accessor_index = static_cast<std::size_t>(*primitive.indicesAccessor);
		check_index(index_accessor_index, asset_.accessors.size(), "accessor");
		fastgltf::Accessor const &index_accessor = asset_.accessors[index_accessor_index];
		indices.reserve(index_accessor.count);
		read_accessor<std::uint32_t>(index_accessor,
			[&](std::uint32_t index)
			{
				indices.push_back(static_cast<unsigned>(index));
			});
	}
	else
	{
		indices.resize(vertices.size());
		std::iota(indices.begin(), indices.end(), 0u);
	}

	if (indices.size() < 3 || (indices.size() % 3) != 0)
	{
		throw load_error("glTF '" + filename_ + "': a primitive of " + std::to_string(indices.size()) + " indices, not whole triangles");
	}
	for (unsigned index : indices)
	{
		if (index >= vertices.size())
		{
			throw load_error("glTF '" + filename_ + "': a primitive's index " + std::to_string(index) + " of " +
				std::to_string(vertices.size()) + " vertices");
		}
	}

	if (!imported_tangent_space)
	{
		generate_tangent_space(vertices, indices, vertex_format);
	}
}

std::int64_t GltfReader::build_mesh(std::size_t mesh_index, std::optional<std::size_t> skin_index, std::size_t node_index,
	std::vector<float> const &initial_morph_weights, std::size_t &imported_primitives)
{
	check_index(mesh_index, asset_.meshes.size(), "mesh");
	std::size_t const skin_cache_key = skin_index ? *skin_index : std::numeric_limits<std::size_t>::max();
	std::tuple<std::size_t, std::size_t, std::size_t> const mesh_cache_key(mesh_index, skin_cache_key, node_index);
	auto const cached = mesh_cache_.find(mesh_cache_key);
	if (cached != mesh_cache_.end())
	{
		return cached->second;
	}

	fastgltf::Mesh const &gltf_mesh = asset_.meshes[mesh_index];
	PreparedSkin const *prepared_skin_ptr = skin_index ? prepare_skin(*skin_index) : nullptr;

	SerializedMesh mesh;
	GenericVertex::format_type mesh_vertex_format = 0;
	std::map<std::string, SerializedCustomAttribute> mesh_custom;

	for (std::size_t primitive_index = 0; primitive_index < gltf_mesh.primitives.size(); ++primitive_index)
	{
		std::vector<GenericVertex> primitive_vertices;
		std::vector<unsigned> primitive_indices;
		GenericVertex::format_type primitive_vertex_format = 0;
		std::vector<std::vector<GenericVertex>> primitive_morph_target_deltas;
		std::map<std::string, SerializedCustomAttribute> primitive_custom;

		build_primitive(gltf_mesh.primitives[primitive_index], primitive_vertices, primitive_indices, primitive_vertex_format, prepared_skin_ptr,
			primitive_morph_target_deltas, primitive_custom);

		if (!mesh.triangles.empty() && ((primitive_vertex_format ^ mesh_vertex_format) & GenericVertex::FORMAT_COLOR) != 0)
		{
			throw std::runtime_error("glTF '" + filename_ + "': mesh '" + std::string(gltf_mesh.name.c_str()) +
				"' has COLOR_0 on some of its primitives and not on others");
		}

		if (mesh.triangles.empty())
		{
			mesh_custom = std::move(primitive_custom);
		}
		else
		{
			for (auto const &entry : primitive_custom)
			{
				auto const found = mesh_custom.find(entry.first);
				if (found == mesh_custom.end() || found->second.components != entry.second.components)
				{
					throw std::runtime_error("glTF '" + filename_ + "': mesh '" + std::string(gltf_mesh.name.c_str()) + "' has " + entry.first +
						(found == mesh_custom.end() ? " on some of its primitives and not on others" : " with a different width on its primitives"));
				}
			}
			for (auto &entry : mesh_custom)
			{
				auto const found = primitive_custom.find(entry.first);
				if (found == primitive_custom.end())
				{
					throw std::runtime_error("glTF '" + filename_ + "': mesh '" + std::string(gltf_mesh.name.c_str()) + "' has " + entry.first +
						" on some of its primitives and not on others");
				}
				entry.second.values.insert(entry.second.values.end(), found->second.values.begin(), found->second.values.end());
			}
		}

		std::size_t const vertex_base = mesh.vertices.size();
		std::size_t const first_index_offset = mesh.indices.size();

		mesh.vertices.insert(mesh.vertices.end(), primitive_vertices.begin(), primitive_vertices.end());

		// the mesh's targets over all its vertices: a primitive that a target does not move has zero deltas in it
		GenericVertex zero_delta;
		std::memset(&zero_delta, 0, sizeof(zero_delta));
		if (mesh.morph_target_deltas.size() < primitive_morph_target_deltas.size())
		{
			std::size_t const old_targets_count = mesh.morph_target_deltas.size();
			mesh.morph_target_deltas.resize(primitive_morph_target_deltas.size());
			for (std::size_t target_index = old_targets_count; target_index < mesh.morph_target_deltas.size(); ++target_index)
			{
				mesh.morph_target_deltas[target_index].assign(vertex_base, zero_delta);
			}
		}
		for (std::size_t target_index = 0; target_index < mesh.morph_target_deltas.size(); ++target_index)
		{
			std::vector<GenericVertex> &mesh_target = mesh.morph_target_deltas[target_index];
			mesh_target.resize(vertex_base + primitive_vertices.size(), zero_delta);
			if (target_index < primitive_morph_target_deltas.size())
			{
				std::vector<GenericVertex> const &primitive_target = primitive_morph_target_deltas[target_index];
				std::copy(primitive_target.begin(), primitive_target.end(), mesh_target.begin() + std::ptrdiff_t(vertex_base));
			}
		}

		unsigned primitive_min_index = std::numeric_limits<unsigned>::max();
		unsigned primitive_max_index = 0;
		mesh.indices.reserve(mesh.indices.size() + primitive_indices.size());
		for (unsigned index : primitive_indices)
		{
			primitive_min_index = std::min(primitive_min_index, index);
			primitive_max_index = std::max(primitive_max_index, index);
			mesh.indices.push_back(static_cast<unsigned>(vertex_base) + index);
		}

		SerializedTriangles triangles;
		triangles.material_index = material_for_primitive(gltf_mesh.primitives[primitive_index]);
		triangles.minimal_vertex_index = vertex_base + primitive_min_index;
		triangles.vertices_count = static_cast<std::size_t>(primitive_max_index - primitive_min_index + 1);
		triangles.first_index_offset = first_index_offset;
		triangles.triangles_count = primitive_indices.size() / 3;
		mesh.triangles.push_back(triangles);

		mesh_vertex_format |= primitive_vertex_format;
		imported_primitives++;
	}

	if (mesh.triangles.empty())
	{
		throw load_error("glTF '" + filename_ + "': mesh " + std::to_string(mesh_index) + " has no primitives");
	}

	mesh.serialization_type = mesh_type_;
	mesh.id = "gltf_mesh_" + std::to_string(mesh_index);
	if (skin_index)
	{
		mesh.id += "_skin_" + std::to_string(*skin_index);
	}
	mesh.name = gltf_mesh.name.empty() ? mesh.id : std::string(gltf_mesh.name.c_str());
	mesh.bind_shape_matrix.identity();
	for (auto &entry : mesh_custom)
	{
		mesh.custom_attributes.push_back(std::move(entry.second));
	}
	if (prepared_skin_ptr)
	{
		mesh.joint_index_transform.assign(prepared_skin_ptr->joint_index_transform.begin(), prepared_skin_ptr->joint_index_transform.end());
		mesh.inverted_bind_matrices = prepared_skin_ptr->inverted_bind_matrices;
	}
	if (!mesh.morph_target_deltas.empty())
	{
		mesh.morph_weights = initial_morph_weights;
	}

	// what the model's vertex type has and the glTF does not: a color is not made up (white, say), the rest left as nothing
	GenericVertex::format_type const wanted = serialized_vertex_format(mesh_type_);
	if ((wanted & GenericVertex::FORMAT_COLOR) != 0 && (mesh_vertex_format & GenericVertex::FORMAT_COLOR) == 0)
	{
		throw std::runtime_error("mesh '" + mesh.name + "' has no vertex colors (COLOR_0), which a model of this vertex type needs");
	}
	if ((wanted & ~mesh_vertex_format) != 0)
	{
		EDGE_LOG_WARN("mesh '{}': vertex components not initialized: {} (expected {}, got {})", mesh.name,
			format_components_string(wanted & ~mesh_vertex_format), format_components_string(wanted),
			format_components_string(mesh_vertex_format & wanted));
	}

	std::int64_t const index = std::int64_t(model_.meshes.size());
	model_.meshes.push_back(std::move(mesh));
	mesh_cache_[mesh_cache_key] = index;
	return index;
}

std::size_t GltfReader::build_meshes_for_nodes()
{
	mesh_cache_.clear();

	std::size_t imported_primitives = 0;
	for (std::size_t node_index = 0; node_index < asset_.nodes.size(); ++node_index)
	{
		if (!imported_nodes_[node_index] || node_to_joint_[node_index] < 0)
		{
			continue;
		}

		fastgltf::Node const &node = asset_.nodes[node_index];
		if (!node.meshIndex)
		{
			continue;
		}

		std::optional<std::size_t> skin_index;
		if (node.skinIndex)
		{
			skin_index = static_cast<std::size_t>(*node.skinIndex);
			check_index(*skin_index, asset_.skins.size(), "skin");
		}

		std::vector<float> const initial_morph_weights = node_initial_morph_weights(node_index);
		model_.joints[std::size_t(node_to_joint_[node_index])].mesh_index =
			build_mesh(static_cast<std::size_t>(*node.meshIndex), skin_index, node_index, initial_morph_weights, imported_primitives);
	}

	return imported_primitives;
}

std::size_t GltfReader::mesh_morph_targets_count(std::size_t mesh_index) const
{
	if (mesh_index >= asset_.meshes.size())
	{
		return 0;
	}

	std::size_t max_targets_count = 0;
	fastgltf::Mesh const &mesh = asset_.meshes[mesh_index];
	for (auto const &primitive : mesh.primitives)
	{
		max_targets_count = std::max(max_targets_count, primitive.targets.size());
	}

	return max_targets_count;
}

std::vector<float> GltfReader::node_initial_morph_weights(std::size_t node_index) const
{
	if (node_index >= asset_.nodes.size())
	{
		return std::vector<float>();
	}

	fastgltf::Node const &node = asset_.nodes[node_index];
	if (!node.meshIndex)
	{
		return std::vector<float>();
	}

	std::size_t const mesh_index = static_cast<std::size_t>(*node.meshIndex);
	if (mesh_index >= asset_.meshes.size())
	{
		return std::vector<float>();
	}

	std::size_t const morph_targets_count = mesh_morph_targets_count(mesh_index);
	if (morph_targets_count == 0)
	{
		return std::vector<float>();
	}

	std::vector<float> result(morph_targets_count, 0.0f);
	if (!node.weights.empty())
	{
		for (std::size_t i = 0; i < result.size() && i < node.weights.size(); ++i)
		{
			result[i] = static_cast<float>(node.weights[i]);
		}
	}
	else if (!asset_.meshes[mesh_index].weights.empty())
	{
		fastgltf::Mesh const &mesh = asset_.meshes[mesh_index];
		for (std::size_t i = 0; i < result.size() && i < mesh.weights.size(); ++i)
		{
			result[i] = static_cast<float>(mesh.weights[i]);
		}
	}

	return result;
}

}
