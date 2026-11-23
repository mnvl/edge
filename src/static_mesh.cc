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
	return std::shared_ptr<BasicStaticMesh<T>>(new BasicStaticMesh<T>(*this));
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

	if (vertex_array_ && vertex_array_->count() != count)
	{
		// they are offsets of as many vertices as there were
		morph_target_deltas_ = std::make_shared<std::vector<std::vector<vertex_type>> const>();
	}

	own_vertex_array(count);
	auto mapped = vertex_array_->write(Contents::KEEP);
	std::span<vertex_type> const buffer = mapped.template as<vertex_type>();
	bool const whole = initialized_vertex_components_ == 0 || vertex_type::FORMAT == format;
	for (size_t i = 0; i < count; i++)
	{
		if (whole)
		{
			buffer[i] = vertices[i];
		}
		else
		{
			buffer[i].assign(vertices[i], format);
		}
	}
	mapped.unmap();

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
std::size_t BasicStaticMesh<T>::morph_targets_count() const
{
	return morph_target_deltas_->size();
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
	for (std::vector<vertex_type> const &target : *morph_target_deltas_)
	{
		serialized.morph_target_deltas.emplace_back();
		for (vertex_type const &delta : target)
		{
			serialized.morph_target_deltas.back().push_back(to_generic_vertex(delta));
		}
	}
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
	always_visible_ = serialized.always_visible;
	initialized_vertex_components_ = 0;
	morph_target_deltas_ = std::make_shared<std::vector<std::vector<vertex_type>> const>();
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
		std::vector<std::vector<vertex_type>> deltas;
		for (std::vector<GenericVertex> const &target : serialized.morph_target_deltas)
		{
			std::vector<vertex_type> &converted = deltas.emplace_back(target.size());
			std::copy(target.begin(), target.end(), converted.begin());
		}
		morph_target_deltas_ = std::make_shared<std::vector<std::vector<vertex_type>> const>(std::move(deltas));
	}
}

template class BasicStaticMesh<VERTEX_PNT>;
template class BasicStaticMesh<VERTEX_PTBNT>;
template class BasicStaticMesh<VERTEX_PTBNTC>;
template class BasicStaticMesh<VERTEX_PNJWT>;
template class BasicStaticMesh<VERTEX_PTBNJWT>;
template class BasicStaticMesh<VERTEX_PTBNJWTC>;

}
