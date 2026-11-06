// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

#include <memory>
#include "vertex.h"
#include "index.h"
#include "mesh.h"
#include "model.h"

namespace edge
{

class ModelBuilder;
class Renderer;

using MeshBuilderPtr = std::shared_ptr<class MeshBuilder>;

// MeshBuilder is for constructing meshes — it cannot render.
// Save via ModelBuilder and load with StaticModel/AnimatedModel for rendering.
class MeshBuilder: public Mesh {
public:
    using vertex_type = VERTEX_PTBNT;
	using index_type = INDEX_U32;

	MeshBuilder(ModelBuilder *m);
	~MeshBuilder() override;

	rove::aabb<3> exact_bounds(std::vector<JointPtr> const &joints, Joint const &owner,
		rove::matrix<4,4> const &world_matrix) const override;
	rove::obb<3> conservative_bounds(std::vector<JointPtr> const &joints, Joint const &owner,
		rove::matrix<4,4> const &world_matrix) const override;
	void insert_triangles(octree_type &octree, int id, std::vector<JointPtr> const &joints,
		Joint const &owner, rove::matrix<4,4> const &world_matrix) const override;
	MeshPtr shallow_copy() const override;

	std::vector<vertex_type> const &vertex_buffer() const { return vertices_; }
	std::vector<index_type> const &index_buffer() const { return indices_; }

	// High-level geometry helpers for model construction workflows.
	void clear_geometry();
	void set_geometry(std::vector<vertex_type> const &vertices, std::vector<index_type> const &indices);
	void append_geometry(std::vector<vertex_type> const &vertices, std::vector<index_type> const &indices,
		bool add_default_triangles = true);
	void clear_triangles();
	void add_triangles(MaterialPtr const &material_ptr, size_t first_index_offset, size_t triangles_count);

	/// std::invalid_argument for joints or weights: a MeshBuilder is saved as a static mesh, which has no skin.
	void set_vertices(GenericVertex *vertices, GenericVertex::format_type format, size_t count) override;
	void set_indices(unsigned *indices, size_t count) override;
	void set_always_visible(bool flag) override;
	void set_morph_targets(std::vector<std::vector<GenericVertex>> const &targets,
		GenericVertex::format_type format) override;
	void set_morph_weights(std::vector<float> const &weights) override;
	std::size_t morph_targets_count() const override;
	std::string serialization_type() const override;
	/// What serialization_type() says of every MeshBuilder.
	static std::string class_serialization_type();
	void save_geometry(SerializedMesh &serialized) const override;
	void load_geometry(SerializedMesh const &serialized, bool pose_box_given) override;


private:
	friend class ModelBuilder;

	GenericVertex::format_type initialized_vertex_components_;
	std::vector<vertex_type> vertices_;
	std::vector<index_type> indices_;
	std::vector<vertex_type> base_vertices_;
	std::vector<std::vector<vertex_type>> morph_target_deltas_;
	std::vector<float> morph_weights_;
	mutable rove::aabb<3> bounds_;
	mutable bool bounds_valid_ = false;

	/// The base vertices' box, computed once.
	rove::aabb<3> const &local_bounds() const;

void apply_morph_weights();
};

}
