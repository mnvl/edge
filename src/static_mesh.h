// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

#include <rove/aabb.h>
#include "vertex.h"
#include "vertex_array.h"
#include "index.h"
#include "index_array.h"
#include "mesh.h"

namespace edge
{

template<vertex_layout V> class BasicStaticModel;

template<vertex_layout V = VERTEX_PNT>
class BasicStaticMesh: public Mesh {
public:
	using vertex_type = V;
	using index_type = INDEX_U32;

	BasicStaticMesh(Model *model_ptr);
	~BasicStaticMesh() override;

	rove::aabb<3> exact_bounds(std::vector<JointPtr> const &joints, Joint const &owner,
		rove::matrix<4,4> const &world_matrix) const override;
	rove::obb<3> conservative_bounds(std::vector<JointPtr> const &joints, Joint const &owner,
		rove::matrix<4,4> const &world_matrix) const override;
	void insert_triangles(octree_type &octree, int id, std::vector<JointPtr> const &joints,
		Joint const &owner, rove::matrix<4,4> const &world_matrix) const override;
	MeshPtr shallow_copy() const override;

	BasicStaticModel<V> *model();

	void check() override;

	void draw(Joint *joint_ptr, CameraPtr const &camera, rove::matrix<4,4> const &world_matrix, unsigned lod = 0) override;
	void draw_instances(Joint *joint_ptr, CameraPtr const &camera, std::vector<rove::matrix<4,4>> const &world_matrices,
		unsigned lod = 0) override;

	void set_vertices(GenericVertex *vertices, GenericVertex::format_type format, size_t count) override;
	void set_indices(unsigned *indices, size_t count) override;
	void set_always_visible(bool flag) override;
	void set_morph_targets(std::vector<std::vector<GenericVertex>> const &targets,
		GenericVertex::format_type format) override;
	void set_morph_weights(std::vector<float> const &weights) override;
	std::size_t morph_targets_count() const override;
	std::string serialization_type() const override;
	/// What serialization_type() says of every mesh of this type.
	static std::string class_serialization_type();
	void save_geometry(SerializedMesh &serialized) const override;
	void load_geometry(SerializedMesh const &serialized, bool pose_box_given) override;

	/// Returns the CPU-side vertex buffer (base vertices before morph targets).
	std::vector<vertex_type> const &vertices() const { return base_vertices_; }

	/// Returns the CPU-side index buffer (triangle-list indices).
	std::vector<index_type> const &indices() const { return base_indices_; }

	/// The morph targets' offsets from the base vertices, a vector of them a target.
	std::vector<std::vector<vertex_type>> const &morph_target_deltas() const { return morph_target_deltas_; }

	/// The GPU buffers draw() binds; shallow copies share them, except a
	/// mesh with morph targets, whose weights are written into its own.
	VertexBufferPtr const &vertex_buffer() const { return vertex_array_->vertex_buffer(); }
	IndexBufferPtr const &index_buffer() const { return index_array_->index_buffer(); }

protected:
	/// draw() and draw_instances(): the triangle groups' draw calls at @p world_matrix, drawn once for each of @p instances if any
	void submit(Joint *joint_ptr, CameraPtr const &camera, rove::matrix<4,4> const &world_matrix, unsigned lod,
		std::vector<rove::matrix<4,4>> const *instances);
	using vertex_array_type = VertexArray<V>;
	using index_array_type = IndexArray32;

	// AABB over positions of all vertices before applyed transform
	rove::aabb<3> bounds_;

	/// What load_geometry() loads but the pose box.  Throws std::runtime_error for morph weights of another count than the targets.
	void load_vertices(SerializedMesh const &serialized);

	/// The base vertices' positions under `transform`, exactly.
	rove::aabb<3> transformed_bounds(rove::matrix<4,4> const &transform) const;

	GenericVertex::format_type initialized_vertex_components_;
	std::shared_ptr<vertex_array_type> vertex_array_;
	std::shared_ptr<index_array_type> index_array_;
	bool always_visible_;
	std::vector<vertex_type> base_vertices_;
	std::vector<index_type> base_indices_;
	std::vector<std::vector<vertex_type>> morph_target_deltas_;
	std::vector<float> morph_weights_;

	void apply_morph_weights_to_vertex_array();

	/// Model::ensure_joints() of the mesh's model, if it has one.
	void build_model_joints() const;

	/// Gives a shallow copy with morph targets a vertex buffer of its own:
	/// set_morph_weights() writes into it, which would move every copy.
	void unshare_morphed_vertex_array();
};

using StaticMeshPNT = BasicStaticMesh<VERTEX_PNT>;
using StaticMeshPTBNT = BasicStaticMesh<VERTEX_PTBNT>;
using StaticMeshPTBNTC = BasicStaticMesh<VERTEX_PTBNTC>;
using StaticMeshPNJWT = BasicStaticMesh<VERTEX_PNJWT>;
using StaticMeshPTBNJWT = BasicStaticMesh<VERTEX_PTBNJWT>;
using StaticMeshPTBNJWTC = BasicStaticMesh<VERTEX_PTBNJWTC>;

}
