// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <rove/aabb.h>
#include <rove/matrix.h>
#include <rove/obb.h>
#include <rove/octree.h>
#include <rove/triangle.h>
#include "material.h"
#include "generic_vertex.h"
#include "motion.h"
#include "triangle_iterator.h"

namespace edge {

struct SerializedMesh;

static size_t constexpr MAX_JOINT_MATRICES = 256;

class Mesh: public std::enable_shared_from_this<Mesh> {
public:
	struct Triangles
	{
		MaterialPtr material_ptr;
		size_t minimal_vertex_index;
		size_t vertices_count;
		size_t first_index_offset;
		size_t triangles_count;
	};

	using triangles_list_type = std::vector<Triangles>;
	using inverted_bind_matrices_type = std::vector<rove::matrix<4, 4>>;
    using joint_index_transform_type = std::vector<size_t>;

	/// A level of detail: the groups of a coarser version of the mesh, one for each group of triangles_list in its order, over the same
	/// vertices and indices (their indices follow the others', so base_indices_ holds every level; collision and the pose box
	/// only ever see triangles_list).  `error` is how far the level's surface may be from level 0's, in the mesh's units.
	struct Lod
	{
		float error = 0;
		triangles_list_type triangles;
	};

	std::string const &id() const { return id_; }
	void set_id(std::string id) { id_ = std::move(id); }
	std::string const &name() const { return name_; }
	void set_name(std::string name) { name_ = std::move(name); }

	/// The groups of triangles of level 0, each of a material.
	triangles_list_type const &triangles() const { return triangles_; }
	void set_triangles(triangles_list_type triangles) { triangles_ = std::move(triangles); }
	/// Levels 1, 2, ... (level 0 is triangles()), each with fewer triangles than the one before.  Empty for a mesh that has no levels.
	std::vector<Lod> const &lods() const { return lods_; }
	void set_lods(std::vector<Lod> lods) { lods_ = std::move(lods); }

	/// The levels the mesh has, level 0 included.
	std::size_t lod_count() const { return lods_.size() + 1; }
	/// How far level @p lod strays from level 0 (Lod::error; 0 for level 0); a level past the last is the last.
	float lod_error(unsigned lod) const { return lod == 0 || lods_.empty() ? 0.0f : lods_[std::min<std::size_t>(lod, lods_.size()) - 1].error; }
	/// The groups of level @p lod; a level past the last is the last.
	triangles_list_type const &triangles_of_lod(unsigned lod) const
	{
		return lod == 0 || lods_.empty() ? triangles_ : lods_[std::min<std::size_t>(lod, lods_.size()) - 1].triangles;
	}

	/// A skinned mesh's: the matrix its vertices are given in (glTF's is the identity), the inverse bind matrix of each joint of its
	/// skin, and the index among the model's joints of each joint of its skin.
	rove::matrix<4,4> const &bind_shape_matrix() const { return bind_shape_matrix_; }
	void set_bind_shape_matrix(rove::matrix<4,4> const &matrix) { bind_shape_matrix_ = matrix; }
	inverted_bind_matrices_type const &inverted_bind_matrices() const { return inverted_bind_matrices_; }
	void set_inverted_bind_matrices(inverted_bind_matrices_type matrices) { inverted_bind_matrices_ = std::move(matrices); }
	joint_index_transform_type const &joint_index_transform() const { return joint_index_transform_; }
	void set_joint_index_transform(joint_index_transform_type indices) { joint_index_transform_ = std::move(indices); }

	/// An application-defined vertex attribute of the glTF: one whose name starts with an underscore, which glTF leaves to the
	/// application.  edge does not know what it means; it keeps it with the mesh (and in the .edge file) for the application to use.
	/// Read as floats: an integer is its value and a normalised integer is normalised as glTF says.  Only scalars and vectors (SCALAR
	/// to VEC4); a matrix is a load error.
	struct CustomAttribute
	{
		unsigned components = 1;            // 1 to 4
		std::vector<float> values;          // `components` of them per vertex, vertex after vertex

		std::size_t count() const { return values.size() / components; }
	};
	/// The custom attributes of the mesh by name (`_ID`...).  All the primitives of a glTF mesh become one mesh, so each attribute
	/// runs through all of them: a primitive that lacks one the others have, or has it with another width, is a load error.
	std::map<std::string, CustomAttribute> const &custom_attributes() const { return custom_attributes_; }
	void set_custom_attributes(std::map<std::string, CustomAttribute> attributes) { custom_attributes_ = std::move(attributes); }

	Mesh(Model *model_ptr = nullptr);
	virtual ~Mesh();

	/// The collision octree Model::build_octree() fills: triangles, each
	/// carrying the index of the mesh it came from.
	using octree_type = rove::octree<rove::triangle<3>, int>;

	/// Where draw(owner, camera, world_matrix) puts the mesh, for the joints'
	/// current matrices: a static mesh at its owner joint, a skinned one where
	/// its skin puts it (the owner does not move it).  The three below agree
	/// on that placement; `joints` are the model's joints.
	///
	/// The exact AABB of the placed vertices.  O(vertices).
	virtual rove::aabb<3> exact_bounds(std::vector<JointPtr> const &joints, Joint const &owner,
		rove::matrix<4,4> const &world_matrix) const = 0;

	/// A box that encloses the placed mesh, cheap enough to cull with on every
	/// draw; larger than the exact bounds, never smaller.
	virtual rove::obb<3> conservative_bounds(std::vector<JointPtr> const &joints, Joint const &owner,
		rove::matrix<4,4> const &world_matrix) const = 0;

	/// Inserts the placed triangles into `octree` with payload `id`.
	virtual void insert_triangles(octree_type &octree, int id, std::vector<JointPtr> const &joints,
		Joint const &owner, rove::matrix<4,4> const &world_matrix) const = 0;

	virtual MeshPtr shallow_copy() const = 0;

	void set_model(Model *model);
	Model *model() const;

	virtual void set_vertices(GenericVertex *vertices, GenericVertex::format_type format, size_t count) = 0;
	virtual void set_indices(unsigned *indices, size_t count) = 0;
	virtual void set_always_visible(bool flag) = 0;
	virtual void set_morph_targets(std::vector<std::vector<GenericVertex>> const &targets,
		GenericVertex::format_type format);
	virtual void set_morph_weights(std::vector<float> const &weights);
	virtual std::size_t morph_targets_count() const;

	virtual void check();

	/// Submits the mesh's draw calls for level of detail @p lod (a level past the last draws the last).
	virtual void draw(Joint *joint_ptr, CameraPtr const &camera, rove::matrix<4,4> const &world_matrix, unsigned lod = 0);
	/// Submits the mesh's draw calls once for each of @p world_matrices, as draw() at each.  The static and skinned meshes make
	/// instanced draws (DrawCall::instances); a mesh that does not (one of a game's own) is drawn by draw() at each, which is the
	/// same picture.
	virtual void draw_instances(Joint *joint_ptr, CameraPtr const &camera, std::vector<rove::matrix<4,4>> const &world_matrices,
		unsigned lod = 0);
	// The static and skinned meshes tell each draw call they make where the thing it draws was the frame before (motion vectors,
	// DrawCall::set_previous_world_matrix()): the mesh's draw from a joint is one thing, placed by the world matrix (a static mesh's
	// times the joint's), the instances and the skin's joint matrices; drawn again in a frame at the same placement (a shadow pass, then
	// the view) it is the same thing, drawn at two placements in a frame (a prototype drawn at two places) it is two, and where either
	// was is not known.  A copy of the mesh (a model's instance) is a thing of its own.  A mesh of a game's own that draws instances by
	// draw() at each has none.

	/// Forgets where the mesh's draws were: from the next draw on, each was where it is (Model::reset_previous()).
	void reset_previous() { previous_.by_owner.clear(); }

	virtual std::string serialization_type() const;
	/// Puts what a .edge file keeps of the mesh's own into @p serialized: its vertices, indices, morph targets and weights, the
	/// always-visible flag and a skinned mesh's pose box (the rest is the model's to fill).  Throws std::logic_error for a
	/// skinned mesh whose pose box was never worked out, std::runtime_error for a mesh of a type that is not saved.
	virtual void save_geometry(SerializedMesh &serialized) const;
	/// save_geometry() undone, the pose box of a skinned mesh the one @p serialized has where @p pose_box_given, and none (to be worked
	/// out, Model::compute_pose_bounds()) otherwise.  Throws std::runtime_error when what it has is not what the mesh needs: a pose box
	/// for a mesh that has no use for one, none for a skinned mesh that is given one or one that is not, or morph weights of another
	/// count than the targets.
	virtual void load_geometry(SerializedMesh const &serialized, bool pose_box_given);

protected:
	Model *model_;

	std::string id_, name_;
	triangles_list_type triangles_;
	std::vector<Lod> lods_;
	rove::matrix<4,4> bind_shape_matrix_;
	inverted_bind_matrices_type inverted_bind_matrices_;
	joint_index_transform_type joint_index_transform_;
	std::map<std::string, CustomAttribute> custom_attributes_;

	/// Where the mesh's draw from @p owner was the frame before (that of @p camera, Renderer::previous_frame()), the draw being at
	/// @p placement now (PlacementHistory::use()); none: where it is.  Valid until the next draw from @p owner.
	Placement const *previous_placement(Joint const *owner, CameraPtr const &camera, Placement placement);
	/// Tells @p call where its thing was the frame before, @p previous (previous_placement()); nothing for none.
	static void give_previous(UberDrawCall &call, Placement const *previous);

private:
	/// Each owner joint's history.  A copy of the mesh starts with none: it draws another thing.
	struct Previous
	{
		Previous() = default;
		Previous(Previous const &) {}
		Previous &operator=(Previous const &) { by_owner.clear(); return *this; }
		std::unordered_map<Joint const *, PlacementHistory> by_owner;
	};
	Previous previous_;
};

}
