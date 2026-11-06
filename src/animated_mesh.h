// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <vector>
#include <rove/aabb.h>
#include "animated_mesh_cache.h"
#include "static_mesh.h"

namespace edge
{

template<skinned_vertex_layout V> class BasicAnimatedModel;

template<skinned_vertex_layout V = VERTEX_PNJWT>
class BasicAnimatedMesh: public BasicStaticMesh<V> {
public:
    BasicAnimatedMesh(Model *model_ptr);
	~BasicAnimatedMesh() override;

	MeshPtr shallow_copy() const override;

    BasicAnimatedModel<V> *model();

	void check() override;
	std::string serialization_type() const override;
	/// What serialization_type() says of every mesh of this type.
	static std::string class_serialization_type();

    void draw(Joint *joint_ptr, CameraPtr const &camera, rove::matrix<4,4> const &world_matrix, unsigned lod = 0) override;
	/// One instanced draw for each triangle group: every instance in the pose the model is in (a crowd in step, as a cached pose
	/// shared by the instances of one prototype is).
	void draw_instances(Joint *joint_ptr, CameraPtr const &camera, std::vector<rove::matrix<4,4>> const &world_matrices,
		unsigned lod = 0) override;
	void set_vertices(GenericVertex *vertices, GenericVertex::format_type format, size_t count) override;

	/// Every vertex skinned on the CPU as vertex_skin.glsl skins it, under
	/// world_matrix; the owner joint does not move a skinned mesh.
	rove::aabb<3> exact_bounds(std::vector<JointPtr> const &joints, Joint const &owner,
		rove::matrix<4,4> const &world_matrix) const override;

	/// The box that holds the mesh in the pose its model was set to with Model::set_cached_pose() (posed_bounds()); in the pose its
	/// model was posed in by hand (joints set and rebuilt, a clip's time set: Model::posed_by_hand()), AnimatedMeshCache::bounds_in its
	/// palette (skin_matrices()), worked out once per pose; and otherwise in the bind pose and in every pose of its model's clips (see
	/// pose_bounds()); through @p world_matrix.  For culling.
	rove::obb<3> conservative_bounds(std::vector<JointPtr> const &joints, Joint const &owner,
		rove::matrix<4,4> const &world_matrix) const override;

	/// The model-space box that holds the mesh in the bind pose and in every pose of its model's clips (any time from 0 to the last key):
	/// the union of its vertices over them, grown by half the farthest any vertex moves between two successive samples of a clip (so that
	/// the motion between samples is inside) and by the pose margin.  Worked out by BasicAnimatedModel::compute_pose_bounds().  Throws std::logic_error before it was.
	rove::aabb<3> const &pose_bounds() const;

	/// The box that holds the mesh in the pose Model::set_cached_pose() set (model space), from the cache of the mesh's poses: worked out
	/// by the first instance to ask, for all of them.  Throws std::logic_error for a mesh with no model or a model with no cached pose.
	rove::aabb<3> const &posed_bounds() const;

	/// The poses of this mesh and of every copy of it (shallow_copy(), so every instance of a ModelCache's model): shared by them, made
	/// when first asked or first copied.  A mesh that is not a copy of another has its own.  Throws std::runtime_error for vertices
	/// weighted to a joint the skin does not have.  vertices() and load_geometry() drop it.
	std::shared_ptr<AnimatedMeshCache> const &pose_cache() const;

	/// Sets the box, for a model whose joints are posed by something other than its clips, which hold the box of the poses it makes
	/// instead (BasicAnimatedModel::compute_pose_bounds() has no way to know them).  vertices() forgets it.
	void set_pose_bounds(rove::aabb<3> const &bounds) { pose_bounds_ = bounds; }

	void save_geometry(SerializedMesh &serialized) const override;
	void load_geometry(SerializedMesh const &serialized, bool pose_box_given) override;

	/// The base vertices in the pose the joints have now, in model space: the
	/// position, normal, tangent and binormal as vertex_skin.glsl blends them
	/// (the normal-like ones renormalised); texcoords, joints and weights as
	/// they are.  A vertex with no joint weight lands on the origin, as there.
	std::vector<V> skinned_vertices(std::vector<JointPtr> const &joints) const;

	/// The triangles of the skinned vertices: the pose draw() shows.
	void insert_triangles(Mesh::octree_type &octree, int id, std::vector<JointPtr> const &joints,
		Joint const &owner, rove::matrix<4,4> const &world_matrix) const override;

	/// The matrices draw() hands vertex_skin.glsl, one per skin joint.  For a model set to a cached pose (Model::set_cached_pose()) the
	/// palette in the mesh's pose cache, worked out by the first instance to stand in that pose.  Otherwise for the pose the model's joints
	/// were last built into (Model::rebuild_joints): worked out once per pose (Model::pose_serial), however many passes draw the mesh; a
	/// joint's matrix written without a rebuild is not seen.  Throws std::logic_error for a mesh with no model.
	std::vector<rove::matrix<4,4>> const &skin_matrices() const;

private:
	friend class BasicAnimatedModel<V>;

	mutable std::vector<rove::matrix<4,4>> palette_;
	mutable std::uint64_t palette_pose_ = 0;      // the pose palette_ is of; 0, no pose has that number: none yet
	mutable rove::aabb<3> hand_bounds_;           // the box of the pose the model was posed in by hand (model space) ...
	mutable std::uint64_t hand_bounds_pose_ = 0;  // ... that pose's number

	/// Set by set_pose_bounds() and load_geometry(); empty until then, and after vertices().
	std::optional<rove::aabb<3>> pose_bounds_;

	mutable std::shared_ptr<AnimatedMeshCache> pose_cache_;

	/// The mesh's entry in pose_cache() for the model's cached pose, worked out (from the joints, which it builds) if nobody has.
	CachedPose const &cached_pose() const;

	rove::obb<3> placed_pose_bounds(rove::matrix<4,4> const &world_matrix) const;

	/// The matrices draw() hands vertex_skin.glsl, one per skin joint:
	/// joints[j]->matrix * inverted_bind_matrices[j] * bind_shape_matrix for
	/// every j in joint_index_transform.
	std::vector<rove::matrix<4,4>> skin_matrices(std::vector<JointPtr> const &joints) const;

	struct SkinnedVertex
	{
		rove::vec<3> position;
	};

	/// The base vertices where vertex_skin.glsl puts them (model space); a
	/// vertex without any joint weight lands on the origin, as there.
	std::vector<SkinnedVertex> skin(std::vector<JointPtr> const &joints) const;
};

using AnimatedMeshPNJWT = BasicAnimatedMesh<VERTEX_PNJWT>;
using AnimatedMeshPTBNJWT = BasicAnimatedMesh<VERTEX_PTBNJWT>;
using AnimatedMeshPTBNJWTC = BasicAnimatedMesh<VERTEX_PTBNJWTC>;

}
