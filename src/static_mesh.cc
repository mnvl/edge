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
#include <utility>

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
	std::span<vertex_type const> const vertices = this->vertices();
	if (vertices.empty())
	{
		throw std::runtime_error("failed to compute bounds: no vertices in mesh '" + name() + "'");
	}

	rove::aabb<3> bounds(rove::transform_point(transform, vertices[0].position));

	for (std::size_t i = 1; i < vertices.size(); ++i)
	{
		bounds.extend(rove::transform_point(transform, vertices[i].position));
	}

	return bounds;
}

template <vertex_layout T>
rove::aabb<3> BasicStaticMesh<T>::exact_bounds(std::vector<std::shared_ptr<Joint>> const &, Joint const &owner,
	rove::matrix<4,4> const &world_matrix) const
{
	build_model_joints();
	return transformed_bounds(world_matrix * owner.matrix);
}

template <vertex_layout T>
rove::obb<3> BasicStaticMesh<T>::conservative_bounds(std::vector<std::shared_ptr<Joint>> const &, Joint const &owner,
	rove::matrix<4,4> const &world_matrix) const
{
	build_model_joints();
	rove::obb<3> bounds(bounds_);
	bounds.transform(world_matrix * owner.matrix);
	return bounds;
}

template <vertex_layout T>
void BasicStaticMesh<T>::insert_triangles(octree_type &octree, int id, std::vector<std::shared_ptr<Joint>> const &,
	Joint const &owner, rove::matrix<4,4> const &world_matrix, unsigned lod) const
{
	build_model_joints();
	TriangleIterator<T, index_type, Mesh::Triangles>
		tris(vertices(), indices(), this->triangles_of_lod(lod), world_matrix * owner.matrix);
	for (auto const &tri : tris)
	{
		octree.insert(tri, id);
	}
}

template <vertex_layout T>
std::shared_ptr<Mesh> BasicStaticMesh<T>::shallow_copy() const
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
		template create_vertex_array<vertex_type>(morph_base_.size());
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
void BasicStaticMesh<T>::draw(Scheduler &scheduler, Joint *joint_ptr, std::shared_ptr<Camera> const &camera, rove::matrix<4,4> const &world_matrix, unsigned lod)
{
	submit(scheduler, joint_ptr, camera, world_matrix, lod, nullptr);
}

template <vertex_layout T>
void BasicStaticMesh<T>::draw_instances(Scheduler &scheduler, Joint *joint_ptr, std::shared_ptr<Camera> const &camera, std::vector<rove::matrix<4,4>> const &world_matrices,
	unsigned lod)
{
	if (world_matrices.empty())
		return;
	// the draw calls at the model's own space, each instance placing them (and their bounds) by its world
	submit(scheduler, joint_ptr, camera, rove::matrix<4,4>(), lod, &world_matrices);
}

template <vertex_layout T>
void BasicStaticMesh<T>::submit(Scheduler &scheduler, Joint *joint_ptr, std::shared_ptr<Camera> const &camera, rove::matrix<4,4> const &world_matrix, unsigned lod,
	std::vector<rove::matrix<4,4>> const *instances)
{
	build_model_joints();
	rove::matrix<4,4> const transform = world_matrix * joint_ptr->matrix;
	rove::obb<3> const bounds = conservative_bounds(Mesh::model()->joints(), *joint_ptr, world_matrix);

	// one list of instances for the calls (shared, not copied for each)
	SharedMatrices const shared_instances = instances ? std::make_shared<std::vector<rove::matrix<4,4>> const>(*instances) : nullptr;

	triangles_list_type const &level = triangles_of_lod(lod);
	for (auto const &triangles : level)
	{
		std::shared_ptr<UberDrawCall> draw_call = Mesh::model()->renderer()->create_uber_draw_call();
		draw_call->set_obb(bounds);

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

		scheduler.add(draw_call);
	}
}

template <vertex_layout T>
std::span<T const> BasicStaticMesh<T>::vertices() const
{
	if (!morph_base_.empty())
	{
		return std::span<T const>(morph_base_.data(), morph_base_.size());
	}
	if (!vertex_array_)
	{
		return {};
	}
	// (the mapping reads the buffer's CPU copy where it is, and ending it does nothing)
	return std::as_const(*vertex_array_).read().template as<T const>();
}

template <vertex_layout T>
std::span<typename BasicStaticMesh<T>::index_type const> BasicStaticMesh<T>::indices() const
{
	if (!index_array_)
	{
		return {};
	}
	return std::as_const(*index_array_).read().template as<index_type const>();
}

template <vertex_layout T>
void BasicStaticMesh<T>::own_vertex_array(std::size_t count)
{
	if (vertex_array_ && vertex_array_.use_count() == 1 && vertex_array_->count() == count)
	{
		return;
	}
	std::shared_ptr<vertex_array_type> const had = vertex_array_;
	vertex_array_ = Mesh::model()->renderer()->
		template create_vertex_array<vertex_type>(count);
	if (!had)
	{
		return;      // (a buffer is made zero)
	}
	auto mapped = vertex_array_->write(Contents::DISCARD);
	std::span<vertex_type> const own = mapped.template as<vertex_type>();
	std::memset(static_cast<void *>(own.data()), 0, own.size_bytes());
	auto const before = std::as_const(*had).read();
	std::span<vertex_type const> const old = before.template as<vertex_type const>();
	std::copy_n(old.begin(), std::min(old.size(), own.size()), own.begin());
	mapped.unmap();
}

template <vertex_layout V>
void BasicStaticMesh<V>::set_vertices(GenericVertex *vertices, GenericVertex::format_type format, size_t count)
{
	EDGE_LOG_DEBUG("vertices: count = {}, format = {:x}", count, format);

	if (!morph_base_.empty() && morph_base_.size() != count)
	{
		drop_morph_targets();      // they are of as many vertices as there were
	}

	auto const assign = [&](std::span<vertex_type> const out) {
		bool const whole = initialized_vertex_components_ == 0 || vertex_type::FORMAT == format;
		for (size_t i = 0; i < count; i++)
		{
			if (whole)
			{
				out[i] = vertices[i];
			}
			else
			{
				out[i].assign(vertices[i], format);
			}
		}
	};

	if (!morph_base_.empty())
	{
		assign(morph_base_.write());
	}
	else
	{
		own_vertex_array(count);
		auto mapped = vertex_array_->write(Contents::KEEP);
		assign(mapped.template as<vertex_type>());
		mapped.unmap();
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

	apply_morph_weights_to_vertex_array();
}

template <vertex_layout T>
void BasicStaticMesh<T>::set_indices(unsigned *buffer, size_t count)
{
	EDGE_LOG_DEBUG("indices: count = {}", count);

	index_array_ = Mesh::model()->renderer()->
		template create_index_array<index_type>(count);

	auto mapped = index_array_->write(Contents::DISCARD);
	std::span<index_type> const indices = mapped.template as<index_type>();
	for (size_t i = 0; i < count; i++)
	{
		indices[i] = index_type(buffer[i]);
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
	drop_morph_targets();

	std::span<vertex_type const> const base = vertices();
	if (base.empty() || targets.empty())
	{
		return;
	}

	std::vector<std::vector<vertex_type>> &deltas = morph_target_deltas_.write();
	deltas.resize(targets.size());
	for (std::size_t target_index = 0; target_index < targets.size(); ++target_index)
	{
		std::vector<GenericVertex> const &target = targets[target_index];
		if (target.size() != base.size())
		{
			morph_target_deltas_.clear();
			return;
		}

		std::vector<vertex_type> &converted_target = deltas[target_index];
		converted_target.resize(base.size());
		for (std::size_t vertex_index = 0; vertex_index < target.size(); ++vertex_index)
		{
			vertex_type delta;
			std::memset(&delta, 0, sizeof(delta));
			delta.assign(target[vertex_index], format);
			converted_target[vertex_index] = delta;
		}
	}

	morph_base_.write().assign(base.begin(), base.end());
	own_vertex_array(base.size());      // the weights are written into it
	morph_weights_.assign(morph_target_deltas_.size(), 0.0f);
	apply_morph_weights_to_vertex_array();
}

template <vertex_layout T>
void BasicStaticMesh<T>::drop_morph_targets()
{
	morph_target_deltas_.clear();
	morph_weights_.clear();
	if (morph_base_.empty())
	{
		return;
	}
	auto mapped = vertex_array_->write(Contents::DISCARD);
	std::copy_n(morph_base_.begin(), morph_base_.size(), mapped.template as<vertex_type>().begin());
	mapped.unmap();
	morph_base_.clear();
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
	for (vertex_type const &vertex : vertices())
	{
		serialized.vertices.push_back(to_generic_vertex(vertex));
	}
	serialized.indices.clear();
	for (index_type const &index : indices())
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
	morph_base_.clear();
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
	if (!vertex_array_ || morph_base_.empty() || morph_target_deltas_.empty())
	{
		return;
	}

	auto mapped = vertex_array_->write();
	std::span<vertex_type> const vertex_ptr = mapped.template as<vertex_type>();

	for (std::size_t vertex_index = 0; vertex_index < morph_base_.size(); ++vertex_index)
	{
		vertex_type morphed = morph_base_[vertex_index];

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
