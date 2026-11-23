// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>

#include "tolerance.h"
#include "renderer.h"
#include "model_builder.h"
#include "mesh_builder.h"
#include "serialized_model.h"

namespace edge {

MeshBuilder::MeshBuilder(ModelBuilder *m):
	Mesh(m),
	initialized_vertex_components_(0)
{
}

MeshBuilder::~MeshBuilder() = default;

rove::aabb<3> const &MeshBuilder::local_bounds() const
{
	if (!bounds_valid_)
	{
		if (vertices_.empty())
		{
			throw std::runtime_error("failed to compute bounds: no vertices in mesh builder");
		}
		bounds_ = rove::aabb<3>(vertices_[0].position);
		for (std::size_t i = 1; i < vertices_.size(); ++i)
		{
			bounds_.extend(vertices_[i].position);
		}
		bounds_valid_ = true;
	}
	return bounds_;
}

rove::aabb<3> MeshBuilder::exact_bounds(std::vector<std::shared_ptr<Joint>> const &, Joint const &owner,
	rove::matrix<4,4> const &world_matrix) const
{
	if (vertices_.empty())
	{
		throw std::runtime_error("failed to compute bounds: no vertices in mesh builder");
	}

	rove::matrix<4,4> const transform = world_matrix * owner.matrix;
	rove::aabb<3> bounds(rove::transform_point(transform, vertices_[0].position));

	for (std::size_t i = 1; i < vertices_.size(); ++i)
	{
		bounds.extend(rove::transform_point(transform, vertices_[i].position));
	}

	return bounds;
}

rove::obb<3> MeshBuilder::conservative_bounds(std::vector<std::shared_ptr<Joint>> const &, Joint const &owner,
	rove::matrix<4,4> const &world_matrix) const
{
	rove::obb<3> bounds(local_bounds());
	bounds.transform(world_matrix * owner.matrix);
	return bounds;
}

void MeshBuilder::insert_triangles(octree_type &octree, int id, std::vector<std::shared_ptr<Joint>> const &,
	Joint const &owner, rove::matrix<4,4> const &world_matrix, unsigned lod) const
{
	TriangleIterator<vertex_type, index_type, Mesh::Triangles>
		tris(vertices_, indices_, triangles_of_lod(lod), world_matrix * owner.matrix);
	for (auto const &tri : tris)
	{
		octree.insert(tri, id);
	}
}

std::shared_ptr<Mesh> MeshBuilder::shallow_copy() const
{
	return std::make_shared<MeshBuilder>(*this);
}

void MeshBuilder::clear_geometry()
{
	vertices_.clear();
	indices_.clear();
	morph_target_deltas_.clear();
	initialized_vertex_components_ = 0;
	bounds_valid_ = false;
	clear_triangles();
}

void MeshBuilder::set_geometry(std::vector<vertex_type> const &vertices, std::vector<index_type> const &indices)
{
	vertices_ = vertices;
	indices_ = indices;
	bounds_valid_ = false;

	morph_target_deltas_.clear();
	initialized_vertex_components_ = vertex_type::FORMAT;
	clear_triangles();
}

void MeshBuilder::append_geometry(std::vector<vertex_type> const &vertices, std::vector<index_type> const &indices,
	bool add_default_triangles)
{
	if (vertices.empty() && indices.empty())
	{
		return;
	}

	size_t const vertex_offset = vertices_.size();
	size_t const first_index_offset = indices_.size();

	vertices_.insert(vertices_.end(), vertices.begin(), vertices.end());

	for (unsigned int indice : indices)
	{
		unsigned const shifted = static_cast<unsigned>(indice) + static_cast<unsigned>(vertex_offset);
		indices_.push_back(index_type(shifted));
	}

	bounds_valid_ = false;
	morph_target_deltas_.clear();
	initialized_vertex_components_ |= vertex_type::FORMAT;

	if (add_default_triangles && !indices.empty())
	{
		if (indices.size() % 3 != 0)
		{
			throw std::runtime_error("mesh builder indices count must be divisible by 3 for triangle lists");
		}
		add_triangles(std::shared_ptr<Material>(), first_index_offset, indices.size() / 3);
	}
}

void MeshBuilder::clear_triangles()
{
	triangles_.clear();
}

void MeshBuilder::add_triangles(std::shared_ptr<Material> const &material_ptr, size_t first_index_offset, size_t triangles_count)
{
	if (triangles_count == 0)
	{
		return;
	}

	if (first_index_offset > indices_.size())
	{
		throw std::runtime_error("triangle range start index is out of bounds");
	}

	size_t const remaining_indices = indices_.size() - first_index_offset;
	if (triangles_count > remaining_indices / 3)
	{
		throw std::runtime_error("triangle range exceeds index buffer bounds");
	}

	size_t const index_count = triangles_count * 3;
	unsigned min_index = std::numeric_limits<unsigned>::max();
	unsigned max_index = 0;

	for (size_t i = 0; i < index_count; ++i)
	{
		unsigned const value = static_cast<unsigned>(indices_[first_index_offset + i]);
		if (value < min_index) min_index = value;
		if (value > max_index) max_index = value;
	}

	Triangles triangles;
	triangles.material_ptr = material_ptr;
	triangles.minimal_vertex_index = min_index;
	triangles.vertices_count = max_index - min_index + 1;
	triangles.first_index_offset = first_index_offset;
	triangles.triangles_count = triangles_count;
	triangles_.push_back(triangles);
}

void MeshBuilder::set_vertices(GenericVertex *vertices, GenericVertex::format_type format, size_t count)
{
	if (count != vertices_.size())
	{
		morph_target_deltas_.clear();      // they are offsets of as many vertices as there were
	}
	vertices_.resize(count);
	if ((format & (GenericVertex::FORMAT_JOINT_INDICES | GenericVertex::FORMAT_JOINT_WEIGHTS)) != 0)
	{
		throw std::invalid_argument("mesh '" + name() + "': a MeshBuilder holds no skin (it is saved as a static PTBNT mesh): no joints or weights");
	}

	{
		GenericVertex *vertex = vertices;

		if (initialized_vertex_components_ == 0 || format == vertex_type::FORMAT)
		{
			for (auto &v : vertices_)
			{
				v = *vertex++;
			}
		}
		else
		{
			for (auto &v : vertices_)
			{
				v.assign(*vertex++, format);
			}
		}
	}

	initialized_vertex_components_ |= format;
	bounds_valid_ = false;

}

void MeshBuilder::set_indices(unsigned *indices, size_t count)
{
	indices_.resize(count);

	for (size_t i = 0; i < count; ++i)
	{
		indices_[i] = index_type(*indices++);
	}
}

void MeshBuilder::set_always_visible(bool /*flag*/)
{
	// Frustum culling is not supported for builder mesh.
}

std::size_t MeshBuilder::morph_targets_count() const
{
	return morph_target_deltas_.size();
}

std::string MeshBuilder::serialization_type() const
{
	return class_serialization_type();
}

std::string MeshBuilder::class_serialization_type()
{
	// Emit the same type string as BasicStaticMesh<VERTEX_PTBNT> so that
	// .edge files saved by ModelBuilder can be loaded by StaticModelPTBNT.
	return std::string("static_mesh:") + std::to_string(static_cast<unsigned>(vertex_type::FORMAT));
}

void MeshBuilder::save_geometry(SerializedMesh &serialized) const
{
	// as a BasicStaticMesh<VERTEX_PTBNT> keeps its own, so that StaticModelPTBNT loads what a ModelBuilder saves
	serialized.always_visible = false;
	serialized.vertices.clear();
	for (vertex_type const &vertex : vertices_)
	{
		serialized.vertices.push_back(to_generic_vertex(vertex));
	}
	serialized.indices.clear();
	for (index_type const &index : indices_)
	{
		serialized.indices.push_back(static_cast<unsigned>(index));
	}
	serialized.morph_target_deltas.clear();
	for (std::vector<vertex_type> const &target : morph_target_deltas_)
	{
		serialized.morph_target_deltas.emplace_back();
		for (vertex_type const &delta : target)
		{
			serialized.morph_target_deltas.back().push_back(to_generic_vertex(delta));
		}
	}
	serialized.pose_bounds.reset();
}

void MeshBuilder::load_geometry(SerializedMesh const &serialized, bool)
{
	if (serialized.pose_bounds)
	{
		throw std::runtime_error("the file gives a pose box to mesh '" + name() + "', which is not skinned");
	}

	initialized_vertex_components_ = vertex_type::FORMAT;
	vertices_.resize(serialized.vertices.size());
	for (std::size_t i = 0; i < vertices_.size(); ++i)
	{
		vertices_[i] = serialized.vertices[i];
	}
	indices_.resize(serialized.indices.size());
	for (std::size_t i = 0; i < indices_.size(); ++i)
	{
		indices_[i] = index_type(serialized.indices[i]);
	}


	bounds_valid_ = false;

	morph_target_deltas_.resize(serialized.morph_target_deltas.size());
	for (std::size_t t = 0; t < morph_target_deltas_.size(); ++t)
	{
		morph_target_deltas_[t].resize(serialized.morph_target_deltas[t].size());
		for (std::size_t i = 0; i < morph_target_deltas_[t].size(); ++i)
		{
			morph_target_deltas_[t][i] = serialized.morph_target_deltas[t][i];
		}
	}

}

}
