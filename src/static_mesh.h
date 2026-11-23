// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

#include <memory>
#include <span>
#include <vector>
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

	rove::aabb<3> exact_bounds(std::vector<std::shared_ptr<Joint>> const &joints, Joint const &owner,
		rove::matrix<4,4> const &world_matrix) const override;
	rove::obb<3> conservative_bounds(std::vector<std::shared_ptr<Joint>> const &joints, Joint const &owner,
		rove::matrix<4,4> const &world_matrix) const override;
	void insert_triangles(octree_type &octree, int id, std::vector<std::shared_ptr<Joint>> const &joints,
		Joint const &owner, rove::matrix<4,4> const &world_matrix, unsigned lod) const override;
	std::shared_ptr<Mesh> shallow_copy() const override;

	BasicStaticModel<V> *model();

	void check() override;

	void draw(Scheduler &scheduler, Joint *joint_ptr, std::shared_ptr<Camera> const &camera, rove::matrix<4,4> const &world_matrix, unsigned lod = 0) override;
	void draw_instances(Scheduler &scheduler, Joint *joint_ptr, std::shared_ptr<Camera> const &camera, std::vector<rove::matrix<4,4>> const &world_matrices,
		unsigned lod = 0) override;

	void set_vertices(GenericVertex *vertices, GenericVertex::format_type format, size_t count) override;
	void set_indices(unsigned *indices, size_t count) override;
	void set_always_visible(bool flag) override;
	std::size_t morph_targets_count() const override;
	std::string serialization_type() const override;
	/// What serialization_type() says of every mesh of this type.
	static std::string class_serialization_type();
	void save_geometry(SerializedMesh &serialized) const override;
	void load_geometry(SerializedMesh const &serialized, bool pose_box_given) override;

	/// The vertices on the CPU: the vertex buffer's own copy.  None before set_vertices(); good until the mesh's geometry is next set or
	/// the mesh goes.
	std::span<vertex_type const> vertices() const;

	/// The triangle-list indices on the CPU: the index buffer's own copy, for as long as vertices() is good.
	std::span<index_type const> indices() const;

	/// The morph targets' offsets from the vertices, a vector of them a target (glTF's `targets`): data for the application, shared by
	/// the mesh's copies.  Edge does not play them.
	///
	/// It did, to November 2026, on the CPU: set_morph_weights() added every target to every vertex and sent the whole vertex buffer
	/// again.  It was taken out because
	///   - a mesh had two sets of vertices, its base and the morphed ones in its buffer, and each reader had to know which it wanted:
	///     the buffer was drawn morphed, while the bounds, the collision octree, the skinning and the file read the base, so that
	///     what was hit was not what was drawn;
	///   - the weights were the buffer's, so every instance had a buffer of its own, and the vertices on the CPU again in it (and an
	///     instance whose buffer was not its own moved all of them);
	///   - the states between went wrong: a buffer left morphed when its targets were taken away, a component set on a morphed mesh
	///     that took the others morphed for its base;
	///   - it was written twice, here and in MeshBuilder, and nothing used it: of our projects none set a weight, and the one whose
	///     models have targets reads them as data.
	///
	/// To play them again, do it in the vertex shader, as skinning is done, and leave the vertices alone:
	///   - the deltas (position, normal, tangent) once a mesh, in a storage buffer, or a texture read with texelFetch where there is
	///     none (OpenGL ES 3.0), indexed by gl_VertexIndex and the target, and shared by every instance;
	///   - the weights a draw call's, in the frame's own memory beside the joint matrices: the few largest, chosen on the CPU, with
	///     their targets' numbers (glTF asks for at least eight morphed attributes);
	///   - position += sum of weight * delta before the skinning, the normal and tangent too and made unit length again after;
	///   - the weights the instance's, set by a clip as a joint's transform is and part of what a cached pose is keyed by (PoseKey);
	///   - the bounds grown by each target's reach times its weight, and whoever collides with or saves the morphed shape given it
	///     when asked, as skinned_vertices() gives the skinned one, never kept beside the base;
	///   - levels of detail need nothing: they draw the same vertices.
	/// A test compares pixels with vertices morphed by hand, on every backend, for two instances with weights of their own.
	std::vector<std::vector<vertex_type>> const &morph_target_deltas() const { return *morph_target_deltas_; }

	/// The GPU buffers draw() binds; shallow copies share them.
	std::shared_ptr<VertexBuffer> const &vertex_buffer() const { return vertex_array_->vertex_buffer(); }
	std::shared_ptr<IndexBuffer> const &index_buffer() const { return index_array_->index_buffer(); }

protected:
	/// draw() and draw_instances(): the triangle groups' draw calls at @p world_matrix, drawn once for each of @p instances if any
	void submit(Scheduler &scheduler, Joint *joint_ptr, std::shared_ptr<Camera> const &camera, rove::matrix<4,4> const &world_matrix, unsigned lod,
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
	// never null; the mesh's copies (shallow_copy()) share it
	std::shared_ptr<std::vector<std::vector<vertex_type>> const> morph_target_deltas_ =
		std::make_shared<std::vector<std::vector<vertex_type>> const>();

	/// A vertex array of @p count vertices that no copy of the mesh shares, holding those the mesh had (the rest zero).
	void own_vertex_array(std::size_t count);

	/// Model::ensure_joints() of the mesh's model, if it has one.
	void build_model_joints() const;
};

using StaticMeshPNT = BasicStaticMesh<VERTEX_PNT>;
using StaticMeshPTBNT = BasicStaticMesh<VERTEX_PTBNT>;
using StaticMeshPTBNTC = BasicStaticMesh<VERTEX_PTBNTC>;
using StaticMeshPNJWT = BasicStaticMesh<VERTEX_PNJWT>;
using StaticMeshPTBNJWT = BasicStaticMesh<VERTEX_PTBNJWT>;
using StaticMeshPTBNJWTC = BasicStaticMesh<VERTEX_PTBNJWTC>;

}
