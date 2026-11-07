// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include "tolerance.h"
#include "log.h"
#include <algorithm>
#include <cstring>
#include <span>
#include <cmath>
#include <limits>
#include <stdexcept>

#include "serialized_model.h"
#include "uber_draw_call.h"
#include "static_model.h"
#include "renderer.h"
#include "scheduler.h"
#include "static_mesh.h"

namespace edge
{

template <vertex_layout T>
BasicStaticMesh<T>::BasicStaticMesh(Model *model_ptr):
    Mesh(model_ptr),
	initialized_vertex_components_(0),
	always_visible_(false)
{
}

template <vertex_layout T>
BasicStaticMesh<T>::~BasicStaticMesh() = default;

template <vertex_layout T>
rove::aabb<3> BasicStaticMesh<T>::transformed_bounds(rove::matrix<4,4> const &transform) const
{
	if (base_vertices_.empty())
	{
		throw std::runtime_error("failed to compute bounds: no vertices in mesh '" + name() + "'");
	}

	rove::aabb<3> bounds(rove::transform_point(transform, base_vertices_[0].position));

	for (std::size_t i = 1; i < base_vertices_.size(); ++i)
	{
		bounds.extend(rove::transform_point(transform, base_vertices_[i].position));
	}

	return bounds;
}

template <vertex_layout T>
rove::aabb<3> BasicStaticMesh<T>::exact_bounds(std::vector<JointPtr> const &, Joint const &owner,
	rove::matrix<4,4> const &world_matrix) const
{
	build_model_joints();
	return transformed_bounds(world_matrix * owner.matrix);
}

template <vertex_layout T>
rove::obb<3> BasicStaticMesh<T>::conservative_bounds(std::vector<JointPtr> const &, Joint const &owner,
	rove::matrix<4,4> const &world_matrix) const
{
	build_model_joints();
	rove::obb<3> bounds(bounds_);
	bounds.transform(world_matrix * owner.matrix);
	return bounds;
}

template <vertex_layout T>
void BasicStaticMesh<T>::insert_triangles(octree_type &octree, int id, std::vector<JointPtr> const &,
	Joint const &owner, rove::matrix<4,4> const &world_matrix) const
{
	build_model_joints();
	TriangleIterator<T, index_type, Mesh::Triangles>
		tris(base_vertices_, base_indices_, triangles_, world_matrix * owner.matrix);
	for (auto const &tri : tris)
	{
		octree.insert(tri, id);
	}
}

template <vertex_layout T>
MeshPtr BasicStaticMesh<T>::shallow_copy() const
{
	std::shared_ptr<BasicStaticMesh<T>> copy(new BasicStaticMesh<T>(*this));
	copy->unshare_morphed_vertex_array();
	return copy;
}

template <vertex_layout T>
void BasicStaticMesh<T>::unshare_morphed_vertex_array()
{
	if (morph_target_deltas_.empty())
	{
		return;
	}

	vertex_array_ = model_->renderer()->
		template create_vertex_array<vertex_type>(base_vertices_.size());
	apply_morph_weights_to_vertex_array();
}

template <vertex_layout T>
void BasicStaticMesh<T>::build_model_joints() const
{
	// a static mesh stands where its owner joint is: a model in a cached pose has not built the joints (Model::ensure_joints())
	if (Model const *model = Mesh::model())
		model->ensure_joints();
}

template <vertex_layout T>
BasicStaticModel<T> *BasicStaticMesh<T>::model()
{
	// (null where its model is of another kind: a converter's, say, holds these meshes too)
	return dynamic_cast<BasicStaticModel<T> *>(model_);
}

template <vertex_layout T>
void BasicStaticMesh<T>::check()
{
	Mesh::check();

	// a color that is not there is not made up (white, say): the type that has one needs the file's
	if constexpr ((vertex_type::FORMAT & GenericVertex::FORMAT_COLOR) != 0)
	{
		if ((initialized_vertex_components_ & GenericVertex::FORMAT_COLOR) == 0)
		{
			throw std::runtime_error("mesh '" + name() + "' has no vertex colors (COLOR_0), which a model of this vertex type needs");
		}
	}

	if (initialized_vertex_components_ != vertex_type::FORMAT)
	{
		unsigned const missing = vertex_type::FORMAT & ~initialized_vertex_components_;
		EDGE_LOG_WARN("mesh '{}': vertex components not initialized: {} (expected {}, got {})", this->name(),
			format_components_string(missing), format_components_string(vertex_type::FORMAT),
			format_components_string(initialized_vertex_components_));
	}
}

template <vertex_layout T>
void BasicStaticMesh<T>::draw(Joint *joint_ptr, CameraPtr const &camera, rove::matrix<4,4> const &world_matrix, unsigned lod)
{
	submit(joint_ptr, camera, world_matrix, lod, nullptr);
}

template <vertex_layout T>
void BasicStaticMesh<T>::draw_instances(Joint *joint_ptr, CameraPtr const &camera, std::vector<rove::matrix<4,4>> const &world_matrices,
	unsigned lod)
{
	if (world_matrices.empty())
		return;
	// the draw calls at the model's own space, each instance placing them (and their bounds) by its world
	submit(joint_ptr, camera, rove::matrix<4,4>(), lod, &world_matrices);
}

template <vertex_layout T>
void BasicStaticMesh<T>::submit(Joint *joint_ptr, CameraPtr const &camera, rove::matrix<4,4> const &world_matrix, unsigned lod,
	std::vector<rove::matrix<4,4>> const *instances)
{
	build_model_joints();
	rove::matrix<4,4> const transform = world_matrix * joint_ptr->matrix;
	rove::obb<3> const bounds = conservative_bounds(Mesh::model()->joints(), *joint_ptr, world_matrix);

	// one list of instances for the calls and what the mesh keeps of the frame (shared, not copied)
	Placement::Matrices const shared_instances = instances ? std::make_shared<std::vector<rove::matrix<4,4>> const>(*instances) : nullptr;
	Placement const *const previous = previous_placement(joint_ptr, camera, Placement{transform, shared_instances, nullptr});

	triangles_list_type const &level = triangles_of_lod(lod);
	for (auto const &triangles : level)
	{
		UberDrawCallPtr draw_call = Mesh::model()->renderer()->create_uber_draw_call();
		draw_call->set_obb(bounds);
		give_previous(*draw_call, previous);

		draw_call->set_vertex_buffer(vertex_array_->vertex_buffer());
		draw_call->set_index_buffer(index_array_->index_buffer());
		draw_call->set_material(triangles.material_ptr);
		draw_call->set_minimal_vertex_index(triangles.minimal_vertex_index);
		draw_call->set_first_index_offset(triangles.first_index_offset);
		draw_call->set_triangles_count(triangles.triangles_count);
		draw_call->set_world_matrix(transform);
		if (instances)
			draw_call->set_instances(shared_instances);
		draw_call->set_camera(camera);
		draw_call->set_always_visible(always_visible_);
		draw_call->commit();

		Mesh::model()->scheduler()->add(draw_call);
	}
}

template <vertex_layout V>
void BasicStaticMesh<V>::set_vertices(GenericVertex *vertices, GenericVertex::format_type format, size_t count)
{
	EDGE_LOG_DEBUG("vertices: count = {}, format = {:x}", count, format);

	bool const created = !vertex_array_;
	if (created)
	{
		vertex_array_ = Mesh::model()->renderer()->
			template create_vertex_array<vertex_type>(count);
	}
	auto mapped = vertex_array_->write(created ? Contents::DISCARD : Contents::KEEP);
	std::span<vertex_type> const buffer = mapped.template as<vertex_type>();
	if (created)
	{
		std::memset(buffer.data(), 0, buffer.size_bytes());
	}

	if (initialized_vertex_components_ == 0 || vertex_type::FORMAT == format)
	{
		vertex_type *p = buffer.data();
		GenericVertex *vertex = vertices;

		for (size_t i = 0; i < count; i++, p++, vertex++)
		{
			*p = *vertex;
		}
	}
	else
	{
		vertex_type *p = buffer.data();
		GenericVertex *vertex = vertices;

		for (size_t i = 0; i < count; i++, p++, vertex++)
		{
			p->assign(*vertex, format);
		}
	}

	if (format & GenericVertex::FORMAT_POSITION)
	{
		GenericVertex *vertex = vertices;

		bounds_.lo = bounds_.hi = vertex->position;

		for (size_t i = 0; i < count; i++, vertex++)
		{
			bounds_.extend(vertex->position);
		}

		EDGE_LOG_DEBUG("bounds = ({}, {}, {}) x ({}, {}, {})", bounds_.lo.x, bounds_.lo.y, bounds_.lo.z,
			bounds_.hi.x, bounds_.hi.y, bounds_.hi.z);
	}

	initialized_vertex_components_ |= format;

	base_vertices_.assign(buffer.begin(), buffer.begin() + count);

	mapped.unmap();

	if (!morph_target_deltas_.empty())
	{
		apply_morph_weights_to_vertex_array();
	}
}

template <vertex_layout T>
void BasicStaticMesh<T>::set_indices(unsigned *buffer, size_t count)
{
	EDGE_LOG_DEBUG("indices: count = {}", count);

	index_array_ = Mesh::model()->renderer()->
		template create_index_array<index_type>(count);
	base_indices_.resize(count);

	auto mapped = index_array_->write(Contents::DISCARD);
	std::span<index_type> const indices = mapped.template as<index_type>();
	for (size_t i = 0; i < count; i++)
	{
		index_type const value = index_type(buffer[i]);
		indices[i] = value;
		base_indices_[i] = value;
	}
	mapped.unmap();
}

template <vertex_layout T>
void BasicStaticMesh<T>::set_always_visible(bool flag)
{
	always_visible_ = flag;
}

template <vertex_layout T>
void BasicStaticMesh<T>::set_morph_targets(std::vector<std::vector<GenericVertex>> const &targets,
	GenericVertex::format_type format)
{
	morph_target_deltas_.clear();
	morph_weights_.clear();

	if (!vertex_array_ || base_vertices_.empty() || targets.empty())
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
	apply_morph_weights_to_vertex_array();
}

template <vertex_layout T>
void BasicStaticMesh<T>::set_morph_weights(std::vector<float> const &weights)
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
	apply_morph_weights_to_vertex_array();
}

template <vertex_layout T>
std::size_t BasicStaticMesh<T>::morph_targets_count() const
{
	return morph_target_deltas_.size();
}

template <vertex_layout T>
std::string BasicStaticMesh<T>::serialization_type() const
{
	return class_serialization_type();
}

template <vertex_layout T>
std::string BasicStaticMesh<T>::class_serialization_type()
{
	return std::string("static_mesh:") + std::to_string(static_cast<unsigned>(vertex_type::FORMAT));
}

template <vertex_layout T>
void BasicStaticMesh<T>::save_geometry(SerializedMesh &serialized) const
{
	serialized.always_visible = always_visible_;
	serialized.vertices.clear();
	for (vertex_type const &vertex : base_vertices_)
	{
		serialized.vertices.push_back(to_generic_vertex(vertex));
	}
	serialized.indices.clear();
	for (index_type const &index : base_indices_)
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

template <vertex_layout T>
void BasicStaticMesh<T>::load_geometry(SerializedMesh const &serialized, bool)
{
	if (serialized.pose_bounds)
	{
		throw std::runtime_error("the file gives a pose box to mesh '" + name() + "', which is not skinned");
	}
	load_vertices(serialized);
}

template <vertex_layout T>
void BasicStaticMesh<T>::load_vertices(SerializedMesh const &serialized)
{
	if (serialized.morph_weights.size() != serialized.morph_target_deltas.size())
	{
		throw std::runtime_error("mesh '" + name() + "' has " + std::to_string(serialized.morph_weights.size()) + " morph weights for " +
			std::to_string(serialized.morph_target_deltas.size()) + " morph targets");
	}

	always_visible_ = serialized.always_visible;
	initialized_vertex_components_ = 0;
	base_vertices_.clear();
	base_indices_.clear();
	morph_target_deltas_.clear();
	morph_weights_.clear();
	vertex_array_.reset();
	index_array_.reset();

	if (!serialized.vertices.empty())
	{
		std::vector<GenericVertex> vertices = serialized.vertices;
		this->set_vertices(vertices.data(), vertex_type::FORMAT, vertices.size());
	}

	if (!serialized.indices.empty())
	{
		std::vector<unsigned> indices = serialized.indices;
		this->set_indices(indices.data(), indices.size());
	}

	if (!serialized.morph_target_deltas.empty())
	{
		set_morph_targets(serialized.morph_target_deltas, vertex_type::FORMAT);
		set_morph_weights(serialized.morph_weights);
	}
}

template <vertex_layout T>
void BasicStaticMesh<T>::apply_morph_weights_to_vertex_array()
{
	if (!vertex_array_ || base_vertices_.empty() || morph_target_deltas_.empty())
	{
		return;
	}

	auto mapped = vertex_array_->write();
	std::span<vertex_type> const vertex_ptr = mapped.template as<vertex_type>();

	for (std::size_t vertex_index = 0; vertex_index < base_vertices_.size(); ++vertex_index)
	{
		vertex_type morphed = base_vertices_[vertex_index];

		for (std::size_t target_index = 0; target_index < morph_target_deltas_.size(); ++target_index)
		{
			float const weight = target_index < morph_weights_.size() ? morph_weights_[target_index] : 0.0f;
			if (std::abs(weight) <= ZERO_WEIGHT)
			{
				continue;
			}

			vertex_type const &delta = morph_target_deltas_[target_index][vertex_index];
			if constexpr ((vertex_type::FORMAT & GenericVertex::FORMAT_POSITION) != 0)
			{
				morphed.position += delta.position * weight;
			}
			if constexpr ((vertex_type::FORMAT & GenericVertex::FORMAT_NORMAL) != 0)
			{
				morphed.normal += delta.normal * weight;
			}
			if constexpr ((vertex_type::FORMAT & GenericVertex::FORMAT_TANGENT) != 0)
			{
				morphed.tangent += delta.tangent * weight;
			}
			if constexpr ((vertex_type::FORMAT & GenericVertex::FORMAT_BINORMAL) != 0)
			{
				morphed.binormal += delta.binormal * weight;
			}
		}

		if constexpr ((vertex_type::FORMAT & GenericVertex::FORMAT_NORMAL) != 0)
		{
			if (morphed.normal.length_sq() > NO_DIRECTION_LENGTH_SQ)
			{
				morphed.normal.normalize();
			}
		}
		if constexpr ((vertex_type::FORMAT & GenericVertex::FORMAT_TANGENT) != 0)
		{
			if (morphed.tangent.length_sq() > NO_DIRECTION_LENGTH_SQ)
			{
				morphed.tangent.normalize();
			}
		}
		if constexpr ((vertex_type::FORMAT & GenericVertex::FORMAT_BINORMAL) != 0)
		{
			if constexpr ((vertex_type::FORMAT & GenericVertex::FORMAT_NORMAL) != 0 &&
				(vertex_type::FORMAT & GenericVertex::FORMAT_TANGENT) != 0)
			{
				morphed.binormal = rove::cross(morphed.normal, morphed.tangent);
			}

			if (morphed.binormal.length_sq() > NO_DIRECTION_LENGTH_SQ)
			{
				morphed.binormal.normalize();
			}
		}

		vertex_ptr[vertex_index] = morphed;
	}

	mapped.unmap();
}

template class BasicStaticMesh<VERTEX_PNT>;
template class BasicStaticMesh<VERTEX_PTBNT>;
template class BasicStaticMesh<VERTEX_PTBNTC>;
template class BasicStaticMesh<VERTEX_PNJWT>;
template class BasicStaticMesh<VERTEX_PTBNJWT>;
template class BasicStaticMesh<VERTEX_PTBNJWTC>;

}
