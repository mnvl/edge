// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

#include "gltf_loader.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <memory>
#include <rove/octree.h>
#include <rove/triangle.h>
#include "animated_mesh_cache.h"
#include "joint.h"
#include "animation.h"
#include "mesh.h"
#include "material.h"

namespace edge
{

class Camera;
struct SerializedModel;
class Renderer;
class Scheduler;

class Model: public std::enable_shared_from_this<Model> {
public:
	using joints_type = std::vector<std::shared_ptr<Joint>>;
	using animations_type = std::vector<std::shared_ptr<Animation>>;
	using meshes_type = std::vector<std::shared_ptr<Mesh>>;
	using materials_type = std::vector<std::shared_ptr<Material>>;

	/// What the model is made of, in the order it was loaded or built: joints (a joint's index is its place), meshes (each placed by a
	/// joint), clips and materials.  The model changes them (load, add_joint, add_mesh, clear); the pointers give the parts to change.
	joints_type const &joints() const { return joints_; }
	meshes_type const &meshes() const { return meshes_; }
	animations_type const &animations() const { return animations_; }
	materials_type const &materials() const { return materials_; }

	/// An empty model whose GPU buffers and textures live on @p renderer_ptr; it draws into any scheduler of that renderer (draw()).
	/// Throws std::invalid_argument for a null @p renderer_ptr.
	explicit Model(std::shared_ptr<Renderer> const &renderer_ptr);

	/// An instance of @p prototype: shallow copies of its meshes (sharing their GPU buffers), clones of its materials (sharing their
	/// textures), deep copies of its joints, and copies of its animations that share their keyframes (Sampler).  Whatever a caller
	/// changes on one -- pose, material settings, mesh flags, morph weights -- stays on that instance.  The instance holds
	/// @p prototype for as long as it lives (prototype()).  Call bind_animations() on the new model once it is owned by a shared_ptr
	/// (ModelCache does).  Throws std::invalid_argument for a null @p prototype.
	explicit Model(std::shared_ptr<Model const> const &prototype);
	Model(Model const &) = delete;
	Model &operator=(Model const &) = delete;

	/// The model this one is an instance of, held for as long as this instance lives.  Null for a model loaded on its own and
	/// after clear().
	std::shared_ptr<Model const> const &prototype() const { return prototype_; }
	virtual ~Model();

	std::shared_ptr<Renderer> const &renderer() const { return renderer_; }

	/// The exact world-space AABB of the meshes where draw(camera,
	/// @p world_matrix) puts them under the joints' current matrices -- the
	/// union of Mesh::exact_bounds(): a static mesh's vertices through its
	/// owner joint, a skinned mesh's vertices skinned as vertex_skin.glsl
	/// skins them.  Follows the pose Animation::pose() last set.  Throws if
	/// the model has no meshes.
	/// @param world_matrix  Model-to-world transform applied on top of joints.
	virtual rove::aabb<3> bounds(rove::matrix<4,4> const &world_matrix) const;

	/// (Both build the joints of a cached pose first: see ensure_joints().)
	std::shared_ptr<Joint> joint_by_id(std::string const &name);
	std::shared_ptr<Joint> joint_by_name(std::string const &name);
	std::shared_ptr<Mesh> mesh_by_id(std::string const &id);
	std::shared_ptr<Mesh> mesh_by_name(std::string const &name);
	std::shared_ptr<Material> material_by_name(std::string const &name);

	/// Points every animation at this model, so Animation::pose()
	/// rebuilds these joints.  Needs this model to be owned by a shared_ptr.
	void bind_animations();

	virtual std::shared_ptr<Joint> add_joint();
	virtual std::shared_ptr<Mesh> add_mesh() = 0;
	/// What serialization_type() says of the meshes add_mesh() makes: the type a .edge file's meshes must be of for this model.
	virtual std::string mesh_serialization_type() const = 0;
	virtual void clear();

	void load_from_gltf(std::string const &filename, GltfOptions const &options = GltfOptions());
	/// Writes the model as a .edge file (version MODEL_BINARY_VERSION): materials and their textures, joints, meshes with their
	/// custom attributes and pose boxes, and the clips.  Throws std::runtime_error for a texture that has no CPU copy, std::logic_error for a
	/// skinned mesh whose pose box was never computed.
	void save_to_file(std::string const &filename) const;
	/// What save_to_file() writes, as structures (to be changed, as add_lods() does, before write_file()).  Throws what save_to_file() does.
	SerializedModel serialize() const;
	/// Writes @p serialized as a .edge file.  Throws std::runtime_error when the file cannot be opened.
	static void write_file(SerializedModel const &serialized, std::string const &filename);
	/// Replaces the model with the one in a .edge file.  The textures keep a CPU copy of their texels when @p keep_cpu_copy (what
	/// glTF loading calls GltfOptions::keep_cpu_copy); everything else about them is the file's.  Throws std::runtime_error naming
	/// the file for a file that is missing, damaged (a material with occlusion and no metallic-roughness texture, say), of another
	/// version, or with bytes after the model.
	void load_from_file(std::string const &filename, bool keep_cpu_copy = true);
	/// The model a .edge file holds, as structures; throws what load_from_file() does for the file.
	static SerializedModel read_file(std::string const &filename);
	/// The same of a .edge file's bytes, kept wherever a program keeps them (an archive of its own, say); @p name names them in the
	/// errors, as read_file() names the file.
	static SerializedModel read_bytes(std::string_view bytes, std::string const &name);
	/// load_from_file() of a .edge file's bytes (read_bytes()).
	void load_from_bytes(std::string_view bytes, std::string const &name, bool keep_cpu_copy = true);
	/// Replaces the model with @p serialized (a .edge file's, read_file(), or a glTF's, GltfLoader::read()), its skinned meshes' pose
	/// boxes the ones it gives: load_from_file() is this of read_file(), load_from_gltf() of a glTF's, which works the boxes out.
	/// Throws std::runtime_error for what does not hold together (an index of something that is not there, a mesh of another type
	/// than this model's, a skinned mesh without a pose box ...).
	void load(SerializedModel const &serialized, bool keep_cpu_copy);

	// checks that model have been loaded correctly
	virtual void check();

	/// Works out what the model's meshes need from its joints and clips before they are drawn: for a skinned mesh, its pose box
	/// (BasicAnimatedMesh::pose_bounds()), grown by @p margin as GltfOptions::pose_margin says.  load_from_gltf() does it; a
	/// model put together by hand calls it once built.  Nothing to do for a model that has no skinned meshes.
	virtual void compute_pose_bounds(float margin = 0);

	/// Recomputes each joint's world-space matrix by traversing the joint tree from
	/// the root joints down.  Must be called after writing to any joint's local
	/// transforms (e.g. after Animation::pose() when bypassing pose()).
	virtual void rebuild_joints();

	/// The pose the joints were last built into, as a number: every rebuild_joints() takes a new one, never given to any model
	/// before (a new instance starts with its own), so what is worked out from a pose (a skinned mesh's palette) holds until it changes.
	std::uint64_t pose_serial() const { return pose_serial_; }

	/// Poses the model in clip @p animation at the hundredth of a second @p time falls in (PoseKey), as every instance of it from a
	/// ModelCache does, so that what a skinned mesh has in the pose (BasicAnimatedMesh::skin_matrices(), posed_bounds()) is worked out
	/// once for them all: the pose is sampled only when a mesh finds nobody has worked it out yet, or when somebody reads the joints
	/// (ensure_joints()).  Cheap otherwise.  The pose is the clip at the key's time, not at @p time.
	/// Any other pose -- a clip's pose(), a mix, a joint written by hand and rebuild_joints() -- takes the model out of this for
	/// good (can_pose_cached()), as the joints no longer are the prototype's but for the clip's.  Throws std::logic_error for such a
	/// model, std::invalid_argument for a clip it does not have and for a @p time that is NaN, negative or after the clip's end.
	void set_cached_pose(std::size_t animation, rove::scalar time);

	/// The pose set_cached_pose() left, until something else poses the model.
	std::optional<PoseKey> cached_pose() const { return cached_pose_; }

	/// Whether set_cached_pose() may pose the model: it was not posed any other way since it was loaded or copied.
	bool can_pose_cached() const { return !posed_by_hand_; }
	/// Whether the model was posed by hand (its joints rebuilt other than into a cached pose) since it was loaded or copied.
	bool posed_by_hand() const { return posed_by_hand_; }

	/// Builds the joints of the cached pose if nobody has: a cached pose does not sample or build them until they are read.  What reads them
	/// -- joints[i]->matrix, a mix on top of the pose -- calls this first; joint_by_*, bounds() and build_octree() do.  The joints
	/// are logically the pose's, not state: so it is const.  Nothing to do when the joints are built.
	void ensure_joints() const;

	/// Convenience: evaluates all animations at time @p t and calls rebuild_joints().
	/// For models with a single animation this is the simplest correct usage.
	/// For models with multiple animations, drive them individually:
	/// @code
	///   animations[i]->pose(t);  // also calls rebuild_joints() internally
	/// @endcode
	/// @param t  Animation time in seconds.
	virtual void pose(rove::scalar t);

	/// Whether set_always_visible(true) was the last word on the model.
	bool always_visible() const { return always_visible_; }

	/// Sets the always-visible flag on every mesh in the model and on the model
	/// itself.  When true, Model::draw() skips the model-level frustum test so
	/// all per-mesh draw calls are always submitted regardless of camera pose.
	/// @param flag  true to disable culling, false to re-enable it.
	virtual void set_always_visible(bool flag);

	/// Returns the duration of the longest animation in the model (seconds).
	/// Returns 0 if the model has no animations.
	double animation_duration() const;

	/// Sets @p program as the shader program on every material in the model.
	void set_materials_shader_program(std::shared_ptr<ShaderProgram> const &program);

	/// Resets the shader program to null on every material in the model.
	void clear_materials_shader_program();

	/// Sets extra parameter @p index to @p value on every material in the model.
	void set_materials_extra_parameter(size_t index, rove::vec<4> const &value);

	/// Submits the model's draw calls to @p scheduler, its meshes at level of detail @p lod (0 the full one; a mesh with fewer levels
	/// draws its last, and a model with none draws itself).  The model keeps nothing of @p scheduler: it draws into as many as it is
	/// given.  Throws std::invalid_argument for a scheduler of another renderer than the model's (or of one that is gone), and then
	/// submits nothing.
	virtual void draw(Scheduler &scheduler, std::shared_ptr<Camera> const &camera, rove::matrix<4,4> const &world_matrix, unsigned lod = 0);

	/// The level of detail of a mesh given by whoever draws: asked for each mesh with the box that holds it placed by @p world_matrix, so that the
	/// tiles of a ground are each drawn at the level their distance asks for.
	using LodChooser = std::function<unsigned int (const Mesh &, const rove::obb<3> &)>;
	virtual void draw(Scheduler &scheduler, std::shared_ptr<Camera> const &camera, rove::matrix<4,4> const &world_matrix, LodChooser const &chooser);

	/// Submits the model once for each of @p world_matrices, as draw() at each but in one instanced draw for each triangle group (the
	/// static and skinned meshes; Mesh::draw_instances), every instance in the model's pose and at level @p lod, each culled on its
	/// own.  No world matrices draws nothing.  Refuses @p scheduler as draw() does.
	virtual void draw_instances(Scheduler &scheduler, std::shared_ptr<Camera> const &camera, std::vector<rove::matrix<4,4>> const &world_matrices,
		unsigned lod = 0);

	/// The levels of detail the model has, level 0 included: the most any of its meshes has.
	unsigned lod_count() const;
	/// How far level @p lod strays from level 0, in the model's units: the largest error of its meshes' levels (0 for level 0).
	/// A level past the last is the last.
	float lod_error(unsigned lod) const;

	using octree_type = Mesh::octree_type;

	/// A collision octree of the triangles draw(camera, @p world_matrix) would
	/// draw, at the pose Animation::pose() last set (Mesh::insert_triangles):
	/// a skinned mesh skinned, not in its bind pose.  Each triangle's payload
	/// is the index of its mesh among the joints that hold one, in joint order;
	/// the root box is bounds(@p world_matrix).
	/// @throws std::runtime_error if the model has no triangles.
	octree_type build_octree(rove::matrix<4,4> const &world_matrix) const;

protected:
	std::shared_ptr<Renderer> const renderer_;

	joints_type joints_;
	meshes_type meshes_;
	animations_type animations_;
	materials_type materials_;

	/// When @c true, Model::draw() skips the model-level frustum test and always
	/// submits per-mesh draw calls.  Mirrors the per-mesh always_visible flags.
	/// Set/cleared by always_visible(); reset to @c false by clear().
	bool always_visible_;

	/// Called when a model has been put together: what was done to its joints so far was loading (its bind pose), not posing.
	void forget_posing() { posed_by_hand_ = false; }
	/// load(), the skinned meshes' pose boxes those of @p serialized when @p pose_boxes_given, to be worked out (compute_pose_bounds())
	/// otherwise: then a skinned mesh of @p serialized has none.
	void load(SerializedModel const &serialized, bool keep_cpu_copy, bool pose_boxes_given);

private:
	/// Throws std::invalid_argument unless @p scheduler is of the model's renderer.
	void check_scheduler(Scheduler const &scheduler) const;
	static std::uint64_t next_pose_serial();
	std::uint64_t pose_serial_ = next_pose_serial();
	std::optional<PoseKey> cached_pose_;
	bool joints_stale_ = false;            // cached_pose_ is set and the joints are not built into it
	bool building_cached_pose_ = false;    // ensure_joints() is rebuilding: that is not posing by hand
	bool posed_by_hand_ = false;
	std::shared_ptr<Model const> prototype_;
};

}
