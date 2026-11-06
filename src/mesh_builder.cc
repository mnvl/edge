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
		if (base_vertices_.empty())
		{
			throw std::runtime_error("failed to compute bounds: no vertices in mesh builder");
		}
		bounds_ = rove::aabb<3>(base_vertices_[0].position);
		for (std::size_t i = 1; i < base_vertices_.size(); ++i)
		{
			bounds_.extend(base_vertices_[i].position);
		}
		bounds_valid_ = true;
	}
	return bounds_;
}

rove::aabb<3> MeshBuilder::exact_bounds(std::vector<JointPtr> const &, Joint const &owner,
	rove::matrix<4,4> const &world_matrix) const
{
	if (base_vertices_.empty())
	{
		throw std::runtime_error("failed to compute bounds: no vertices in mesh builder");
	}

	rove::matrix<4,4> const transform = world_matrix * owner.matrix;
	rove::aabb<3> bounds(rove::transform_point(transform, base_vertices_[0].position));

	for (std::size_t i = 1; i < base_vertices_.size(); ++i)
	{
		bounds.extend(rove::transform_point(transform, base_vertices_[i].position));
	}

	return bounds;
}

rove::obb<3> MeshBuilder::conservative_bounds(std::vector<JointPtr> const &, Joint const &owner,
	rove::matrix<4,4> const &world_matrix) const
{
	rove::obb<3> bounds(local_bounds());
	bounds.transform(world_matrix * owner.matrix);
	return bounds;
}

void MeshBuilder::insert_triangles(octree_type &octree, int id, std::vector<JointPtr> const &,
	Joint const &owner, rove::matrix<4,4> const &world_matrix) const
{
	TriangleIterator<vertex_type, index_type, Mesh::Triangles>
		tris(vertices_, indices_, triangles_, world_matrix * owner.matrix);
	for (auto const &tri : tris)
	{
		octree.insert(tri, id);
	}
}

MeshPtr MeshBuilder::shallow_copy() const
{
	return std::make_shared<MeshBuilder>(*this);
}

void MeshBuilder::clear_geometry()
{
	vertices_.clear();
	indices_.clear();
	base_vertices_.clear();
	morph_target_deltas_.clear();
	morph_weights_.clear();
	initialized_vertex_components_ = 0;
	bounds_valid_ = false;
	clear_triangles();
}

void MeshBuilder::set_geometry(std::vector<vertex_type> const &vertices, std::vector<index_type> const &indices)
{
	vertices_ = vertices;
	base_vertices_ = vertices_;
	indices_ = indices;
	bounds_valid_ = false;

	morph_target_deltas_.clear();
	morph_weights_.clear();
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

	base_vertices_ = vertices_;
	bounds_valid_ = false;
	morph_target_deltas_.clear();
	morph_weights_.clear();
	initialized_vertex_components_ |= vertex_type::FORMAT;

	if (add_default_triangles && !indices.empty())
	{
		if (indices.size() % 3 != 0)
		{
			throw std::runtime_error("mesh builder indices count must be divisible by 3 for triangle lists");
		}
		add_triangles(MaterialPtr(), first_index_offset, indices.size() / 3);
	}
}

void MeshBuilder::clear_triangles()
{
	triangles_.clear();
}

void MeshBuilder::add_triangles(MaterialPtr const &material_ptr, size_t first_index_offset, size_t triangles_count)
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
	base_vertices_ = vertices_;
	bounds_valid_ = false;

	if (!morph_target_deltas_.empty())
	{
		apply_morph_weights();
	}
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

void MeshBuilder::set_morph_targets(std::vector<std::vector<GenericVertex>> const &targets,
	GenericVertex::format_type format)
{
	morph_target_deltas_.clear();
	morph_weights_.clear();

	if (base_vertices_.empty() || targets.empty())
	{
		return;
	}

	morph_target_deltas_.resize(targets.size());
	for (std::size_t target_index = 0; target_index < targets.size(); ++target_index)
	{
		std::vector<GenericVertex> const &target = targets[target_index];
		if (target.size() != base_vertices_.size())
		{
			morph_target_deltas_.clear();
			return;
		}

		std::vector<vertex_type> &converted_target = morph_target_deltas_[target_index];
		converted_target.resize(base_vertices_.size());
		for (std::size_t vertex_index = 0; vertex_index < target.size(); ++vertex_index)
		{
			vertex_type delta;
			std::memset(&delta, 0, sizeof(delta));
			delta.assign(target[vertex_index], format);
			converted_target[vertex_index] = delta;
		}
	}

	morph_weights_.assign(morph_target_deltas_.size(), 0.0f);
	apply_morph_weights();
}

void MeshBuilder::set_morph_weights(std::vector<float> const &weights)
{
	if (morph_target_deltas_.empty())
	{
		return;
	}

	std::vector<float> new_weights(morph_target_deltas_.size(), 0.0f);
	for (std::size_t i = 0; i < new_weights.size() && i < weights.size(); ++i)
	{
		new_weights[i] = weights[i];
	}

	if (new_weights == morph_weights_)
	{
		return;
	}

	morph_weights_.swap(new_weights);
	apply_morph_weights();
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
	// as a BasicStaticMesh<VERTEX_PTBNT> keeps its own, so that StaticModelPTBNT loads what a ModelBuilder saves: the base
	// vertices, which the morph targets are offsets from, and the weights apart
	serialized.always_visible = false;
	serialized.vertices.clear();
	for (vertex_type const &vertex : base_vertices_)
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
	serialized.morph_weights = morph_weights_;
	serialized.pose_bounds.reset();
}

void MeshBuilder::load_geometry(SerializedMesh const &serialized, bool)
{
	if (serialized.pose_bounds)
	{
		throw std::runtime_error("the file gives a pose box to mesh '" + name() + "', which is not skinned");
	}
	if (serialized.morph_weights.size() != serialized.morph_target_deltas.size())
	{
		throw std::runtime_error("mesh '" + name() + "' has " + std::to_string(serialized.morph_weights.size()) + " morph weights for " +
			std::to_string(serialized.morph_target_deltas.size()) + " morph targets");
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


	base_vertices_ = vertices_;
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
	morph_weights_ = serialized.morph_weights;

	if (!morph_target_deltas_.empty())
	{
		apply_morph_weights();
	}
}

void MeshBuilder::apply_morph_weights()
{
	if (base_vertices_.empty() || morph_target_deltas_.empty())
	{
		return;
	}

	vertices_ = base_vertices_;
	for (std::size_t vertex_index = 0; vertex_index < vertices_.size(); ++vertex_index)
	{
		vertex_type &vertex = vertices_[vertex_index];
		for (std::size_t target_index = 0; target_index < morph_target_deltas_.size(); ++target_index)
		{
			float const weight = target_index < morph_weights_.size() ? morph_weights_[target_index] : 0.0f;
			if (std::abs(weight) <= ZERO_WEIGHT)
			{
				continue;
			}

			vertex_type const &delta = morph_target_deltas_[target_index][vertex_index];
			vertex.position += delta.position * weight;
			vertex.normal += delta.normal * weight;
			vertex.tangent += delta.tangent * weight;
			vertex.binormal += delta.binormal * weight;
		}

		if (vertex.normal.length_sq() > NO_DIRECTION_LENGTH_SQ)
		{
			vertex.normal.normalize();
		}
		if (vertex.tangent.length_sq() > NO_DIRECTION_LENGTH_SQ)
		{
			vertex.tangent.normalize();
		}
		if (vertex.binormal.length_sq() > NO_DIRECTION_LENGTH_SQ)
		{
			vertex.binormal.normalize();
		}
	}
}


}
