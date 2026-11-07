// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>
#include <rove/obb.h>
#include "tolerance.h"
#include "log.h"
#include "uber_draw_call.h"
#include "animated_model.h"
#include "renderer.h"
#include "scheduler.h"
#include "animated_mesh.h"
#include "serialized_model.h"

namespace edge
{

template <skinned_vertex_layout V>
BasicAnimatedMesh<V>::BasicAnimatedMesh(Model *model_ptr):
    BasicStaticMesh<V>(model_ptr)
{
}

template <skinned_vertex_layout V>
BasicAnimatedMesh<V>::~BasicAnimatedMesh() = default;

template <skinned_vertex_layout V>
MeshPtr BasicAnimatedMesh<V>::shallow_copy() const
{
	std::shared_ptr<BasicAnimatedMesh<V>> copy(new BasicAnimatedMesh(*this));
	copy->unshare_morphed_vertex_array();
	copy->palette_.clear();
	copy->palette_pose_ = 0;
	copy->pose_cache_ = pose_cache();      // (made now if this is the first copy: the prototype is never posed)
	return copy;
}

template <skinned_vertex_layout V>
BasicAnimatedModel<V> *BasicAnimatedMesh<V>::model()
{
    return dynamic_cast<BasicAnimatedModel<V> *>(Mesh::model());
}

template <skinned_vertex_layout V>
void BasicAnimatedMesh<V>::check()
{
   	if (this->joint_index_transform().size() > MAX_JOINT_MATRICES)
	{
		EDGE_LOG_WARN("mesh '{}' has {} skin joints, but the maximum supported is {}; "
			"joints beyond this limit will not animate correctly",
			this->name(), this->joint_index_transform().size(), MAX_JOINT_MATRICES);
	}

	if (this->vertex_array_->size())
	{
		if (rove::abs(this->bind_shape_matrix().determinant()) < ZERO_DETERMINANT)
		{
			EDGE_LOG_WARN("bind shape matrix is zero");
		}

		size_t vertices_with_zero_joins = 0;
		size_t vertices_with_zero_weights = 0;

		auto const vertices = std::as_const(*this->vertex_array_).read();
		for (V const &vertex : vertices.template as<V const>())
		{
			if (vertex.joints.is_zero())
			{
				vertices_with_zero_joins++;
			}

			if (vertex.weights.is_zero())
			{
				vertices_with_zero_weights++;
			}
		}

		if (vertices_with_zero_joins > 0 || vertices_with_zero_weights > 0) {
			EDGE_LOG_WARN("mesh id = {}, name = {}: vertices with zero joints = {}, zero weights = {}, total = {}",
				this->id(), this->name(),
				vertices_with_zero_joins, vertices_with_zero_weights,
				this->vertex_array_->count());
		}
	}
}

template <skinned_vertex_layout V>
std::string BasicAnimatedMesh<V>::serialization_type() const
{
	return class_serialization_type();
}

template <skinned_vertex_layout V>
std::string BasicAnimatedMesh<V>::class_serialization_type()
{
	return std::string("animated_mesh:") + std::to_string(static_cast<unsigned>(V::FORMAT));
}

template <skinned_vertex_layout V>
void BasicAnimatedMesh<V>::set_vertices(GenericVertex *vertices, GenericVertex::format_type format, size_t count)
{
	BasicStaticMesh<V>::set_vertices(vertices, format, count);
	pose_bounds_.reset();      // the box was of the old vertices
	pose_cache_.reset();
}

template <skinned_vertex_layout V>
rove::aabb<3> const &BasicAnimatedMesh<V>::pose_bounds() const
{
	if (!pose_bounds_)
	{
		throw std::logic_error("mesh '" + this->name() + "' has no pose box: Model::compute_pose_bounds() was not called");
	}
	return *pose_bounds_;
}

template <skinned_vertex_layout V>
void BasicAnimatedMesh<V>::save_geometry(SerializedMesh &serialized) const
{
	BasicStaticMesh<V>::save_geometry(serialized);
	serialized.pose_bounds = pose_bounds();
}

template <skinned_vertex_layout V>
void BasicAnimatedMesh<V>::load_geometry(SerializedMesh const &serialized, bool pose_box_given)
{
	if (pose_box_given && !serialized.pose_bounds)
	{
		throw std::runtime_error("the file gives no pose box to skinned mesh '" + this->name() + "'");
	}
	if (!pose_box_given && serialized.pose_bounds)
	{
		throw std::runtime_error("skinned mesh '" + this->name() + "' comes with a pose box where its box is to be worked out");
	}
	this->load_vertices(serialized);
	pose_bounds_ = serialized.pose_bounds;
	pose_cache_.reset();
}

template <skinned_vertex_layout V>
rove::obb<3> BasicAnimatedMesh<V>::placed_pose_bounds(rove::matrix<4,4> const &world_matrix) const
{
	Model const *model = Mesh::model();
	if (model && model->cached_pose())
	{
		rove::obb<3> bounds(posed_bounds());
		bounds.transform(world_matrix);
		return bounds;
	}
	if (model && model->posed_by_hand())          // (poses no clip makes, perhaps: the box of this one, worked out once per pose)
	{
		if (hand_bounds_pose_ != model->pose_serial())
		{
			hand_bounds_ = pose_cache()->bounds_in(skin_matrices());
			hand_bounds_pose_ = model->pose_serial();
		}
		rove::obb<3> bounds(hand_bounds_);
		bounds.transform(world_matrix);
		return bounds;
	}
	rove::obb<3> bounds(pose_bounds());
	bounds.transform(world_matrix);
	return bounds;
}

template <skinned_vertex_layout V>
rove::obb<3> BasicAnimatedMesh<V>::conservative_bounds(std::vector<JointPtr> const &, Joint const &,
	rove::matrix<4,4> const &world_matrix) const
{
	return placed_pose_bounds(world_matrix);
}

template <skinned_vertex_layout V>
rove::aabb<3> const &BasicAnimatedMesh<V>::posed_bounds() const
{
	Model const *model = Mesh::model();
	if (!model)
		throw std::logic_error("BasicAnimatedMesh::posed_bounds: a mesh with no model");
	if (!model->cached_pose())
		throw std::logic_error("BasicAnimatedMesh::posed_bounds: the model of mesh '" + this->name() + "' has no cached pose");
	return cached_pose().bounds;
}

template <skinned_vertex_layout V>
std::vector<rove::matrix<4,4>> BasicAnimatedMesh<V>::skin_matrices(std::vector<JointPtr> const &joints) const
{
	// (what skins on the CPU reads the joints: a model in a cached pose builds them now)
	if (Model const *model = Mesh::model())
		model->ensure_joints();
	std::vector<rove::matrix<4,4>> result;
	result.reserve(this->joint_index_transform().size());
	for (std::size_t const joint_index : this->joint_index_transform())
	{
		result.push_back(joints[joint_index]->matrix *
			this->inverted_bind_matrices()[joint_index] *
			this->bind_shape_matrix());
	}
	return result;
}

template <skinned_vertex_layout V>
std::vector<rove::matrix<4,4>> const &BasicAnimatedMesh<V>::skin_matrices() const
{
	Model const *model = Mesh::model();
	if (!model)
		throw std::logic_error("BasicAnimatedMesh::skin_matrices: a mesh with no model");
	if (model->cached_pose())
		return cached_pose().palette;
	if (palette_pose_ != model->pose_serial())
	{
		palette_ = skin_matrices(model->joints());
		palette_pose_ = model->pose_serial();
	}
	return palette_;
}

namespace
{

/// Calls @p f(skin joint, weight) for every skin joint that pulls @p vertex, as vertex_skin.glsl weighs them.  Throws
/// std::runtime_error for a joint the skin does not have (@p joints_count of them).
template<class V, class F>
void for_each_influence(V const &vertex, std::size_t joints_count, std::string const &mesh_name, F &&f)
{
	for (std::size_t j = 0; j < 8; ++j)
	{
		rove::scalar const weight = vertex.weights.i[j];
		if (weight == 0.0f)
		{
			continue;
		}
		std::size_t const joint = vertex.joints.i[j];
		if (joint >= joints_count)
		{
			throw std::runtime_error("mesh '" + mesh_name + "' has vertices weighted to skin joint " + std::to_string(joint) +
				" but the skin has only " + std::to_string(joints_count) + " joints");
		}
		f(joint, weight);
	}
}

/// A blended direction, unit length again (a zero one stays zero: the shader's sanitising treats it the same way).
void renormalize(rove::vec<3> &v)
{
	rove::scalar const length = v.length();
	if (length > 1e-5f)
	{
		v = v / length;
	}
}

}

template <skinned_vertex_layout V>
std::shared_ptr<AnimatedMeshCache> const &BasicAnimatedMesh<V>::pose_cache() const
{
	if (!pose_cache_)
	{
		rove::aabb<3> null_box;
		null_box.null();
		std::vector<rove::aabb<3>> boxes(this->joint_index_transform().size(), null_box);
		for (V const &vertex : this->base_vertices_)
			for_each_influence(vertex, boxes.size(), this->name(), [&](std::size_t joint, rove::scalar) {
				boxes[joint].extend(vertex.position);
			});
		pose_cache_ = std::make_shared<AnimatedMeshCache>(std::move(boxes));
	}
	return pose_cache_;
}

template <skinned_vertex_layout V>
CachedPose const &BasicAnimatedMesh<V>::cached_pose() const
{
	Model const *model = Mesh::model();
	if (!model)
		throw std::logic_error("BasicAnimatedMesh: a mesh with no model has no cached pose");
	std::optional<PoseKey> const key = model->cached_pose();
	if (!key)
		throw std::logic_error("BasicAnimatedMesh: the model of mesh '" + this->name() + "' has no cached pose");
	AnimatedMeshCache &cache = *pose_cache();
	if (CachedPose const *found = cache.find(*key))
		return *found;
	model->ensure_joints();
	CachedPose pose;
	pose.palette = skin_matrices(model->joints());
	pose.bounds = cache.bounds_in(pose.palette);
	return cache.insert(*key, std::move(pose));
}

template <skinned_vertex_layout V>
std::vector<typename BasicAnimatedMesh<V>::SkinnedVertex> BasicAnimatedMesh<V>::skin(
	std::vector<JointPtr> const &joints) const
{
	std::vector<rove::matrix<4,4>> const skin_matrices = this->skin_matrices(joints);

	std::vector<SkinnedVertex> result;
	result.reserve(this->base_vertices_.size());
	for (typename BasicAnimatedMesh<V>::vertex_type const &vertex : this->base_vertices_)
	{
		// vertex_skin.glsl: the weighted sum of the joints' images of the
		// position; a vertex with no weight at all lands on the origin.
		SkinnedVertex skinned{rove::vec<3>(0, 0, 0)};
		for_each_influence(vertex, skin_matrices.size(), this->name(), [&](std::size_t joint, rove::scalar weight) {
			rove::matrix<4,4> const &m = skin_matrices[joint];
			skinned.position += rove::transform_point(m, vertex.position) * weight;
		});
		result.push_back(skinned);
	}
	return result;
}

template <skinned_vertex_layout V>
std::vector<V> BasicAnimatedMesh<V>::skinned_vertices(std::vector<JointPtr> const &joints) const
{
	std::vector<rove::matrix<4,4>> const skin_matrices = this->skin_matrices(joints);

	std::vector<V> result;
	result.reserve(this->base_vertices_.size());
	for (V const &vertex : this->base_vertices_)
	{
		V out = vertex;
		out.position = rove::vec<3>(0, 0, 0);
		out.normal = rove::vec<3>(0, 0, 0);
		if constexpr ((V::FORMAT & GenericVertex::FORMAT_TANGENT) != 0)
		{
			out.tangent = rove::vec<3>(0, 0, 0);
			out.binormal = rove::vec<3>(0, 0, 0);
		}
		for_each_influence(vertex, skin_matrices.size(), this->name(), [&](std::size_t joint, rove::scalar weight) {
			rove::matrix<4,4> const &m = skin_matrices[joint];
			out.position += rove::transform_point(m, vertex.position) * weight;

			auto turned = [&](rove::vec<3> const &direction) {
				// a point: the matrix's translation is subtracted
				return rove::transform_point(m, direction) - rove::transform_point(m, rove::vec<3>(0, 0, 0));
			};
			out.normal += turned(vertex.normal) * weight;
			if constexpr ((V::FORMAT & GenericVertex::FORMAT_TANGENT) != 0)
			{
				out.tangent += turned(vertex.tangent) * weight;
				out.binormal += turned(vertex.binormal) * weight;
			}
		});
		renormalize(out.normal);
		if constexpr ((V::FORMAT & GenericVertex::FORMAT_TANGENT) != 0)
		{
			renormalize(out.tangent);
			renormalize(out.binormal);
		}
		result.push_back(out);
	}
	return result;
}

template <skinned_vertex_layout V>
rove::aabb<3> BasicAnimatedMesh<V>::exact_bounds(std::vector<JointPtr> const &joints, Joint const &,
	rove::matrix<4,4> const &world_matrix) const
{
	if (this->base_vertices_.empty())
	{
		throw std::runtime_error("failed to compute bounds: no vertices in mesh '" + this->name() + "'");
	}

	rove::aabb<3> bounds;
	bounds.null();
	for (SkinnedVertex const &vertex : skin(joints))
	{
		bounds.extend(rove::transform_point(world_matrix, vertex.position));
	}
	return bounds;
}

template <skinned_vertex_layout V>
void BasicAnimatedMesh<V>::insert_triangles(Mesh::octree_type &octree, int id, std::vector<JointPtr> const &joints,
	Joint const &, rove::matrix<4,4> const &world_matrix) const
{
	std::vector<SkinnedVertex> const vertices = skin(joints);
	TriangleIterator<SkinnedVertex, typename BasicAnimatedMesh<V>::index_type, Mesh::Triangles>
		tris(vertices, this->base_indices_, this->triangles(), world_matrix);
	for (auto const &tri : tris)
	{
		octree.insert(tri, id);
	}
}

template <skinned_vertex_layout V>
void BasicAnimatedMesh<V>::draw(Joint *joint_ptr, CameraPtr const &camera,
	rove::matrix<4,4> const &world_matrix, unsigned lod)
{
	// one palette for the calls and what the mesh keeps of the frame (shared, not copied)
	Placement::Matrices const joint_matrix_array = std::make_shared<std::vector<rove::matrix<4,4>> const>(skin_matrices());

	// Cull against the box that holds the mesh in every pose, not the bind pose: a walking foot swings well outside it.
	rove::obb<3> const bounds = placed_pose_bounds(world_matrix);
	Placement const *const previous = this->previous_placement(joint_ptr, camera, Placement{world_matrix, nullptr, joint_matrix_array});

	typename Mesh::triangles_list_type const &level = this->triangles_of_lod(lod);
	for (auto const &triangles : level)
	{
		UberDrawCallPtr draw_call = Mesh::model()->renderer()->create_uber_draw_call();
		Mesh::give_previous(*draw_call, previous);

		draw_call->set_obb(bounds);
		draw_call->set_vertex_buffer(this->vertex_array_->vertex_buffer());
		draw_call->set_index_buffer(this->index_array_->index_buffer());
		draw_call->set_material(triangles.material_ptr);
		draw_call->set_minimal_vertex_index(triangles.minimal_vertex_index);
		draw_call->set_first_index_offset(triangles.first_index_offset);
		draw_call->set_triangles_count(triangles.triangles_count);
		draw_call->set_world_matrix(world_matrix);
		draw_call->set_camera(camera);
		draw_call->set_joint_matrix_array(joint_matrix_array);
		draw_call->set_always_visible(this->always_visible_);
		draw_call->commit();

		Mesh::model()->scheduler()->add(draw_call);
	}
}

template <skinned_vertex_layout V>
void BasicAnimatedMesh<V>::draw_instances(Joint *joint_ptr, CameraPtr const &camera,
	std::vector<rove::matrix<4,4>> const &world_matrices, unsigned lod)
{
	if (world_matrices.empty())
		return;
	Placement::Matrices const joint_matrix_array = std::make_shared<std::vector<rove::matrix<4,4>> const>(skin_matrices());
	Placement::Matrices const instances = std::make_shared<std::vector<rove::matrix<4,4>> const>(world_matrices);
	// the pose's box in the model's own space, each instance placing it by its world
	rove::obb<3> const bounds = placed_pose_bounds(rove::matrix<4,4>());
	Placement const *const previous = this->previous_placement(joint_ptr, camera, Placement{rove::matrix<4,4>(), instances, joint_matrix_array});

	typename Mesh::triangles_list_type const &level = this->triangles_of_lod(lod);
	for (auto const &triangles : level)
	{
		UberDrawCallPtr draw_call = Mesh::model()->renderer()->create_uber_draw_call();
		Mesh::give_previous(*draw_call, previous);
		draw_call->set_obb(bounds);
		draw_call->set_vertex_buffer(this->vertex_array_->vertex_buffer());
		draw_call->set_index_buffer(this->index_array_->index_buffer());
		draw_call->set_material(triangles.material_ptr);
		draw_call->set_minimal_vertex_index(triangles.minimal_vertex_index);
		draw_call->set_first_index_offset(triangles.first_index_offset);
		draw_call->set_triangles_count(triangles.triangles_count);
		draw_call->set_instances(instances);
		draw_call->set_camera(camera);
		draw_call->set_joint_matrix_array(joint_matrix_array);
		draw_call->set_always_visible(this->always_visible_);
		draw_call->commit();

		Mesh::model()->scheduler()->add(draw_call);
	}
}

template class BasicAnimatedMesh<VERTEX_PNJWT>;
template class BasicAnimatedMesh<VERTEX_PTBNJWT>;
template class BasicAnimatedMesh<VERTEX_PTBNJWTC>;

}
