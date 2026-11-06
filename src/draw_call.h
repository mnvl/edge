// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>
#include <rove/obb.h>
#include <rove/frustum.h>
#include "forward.h"
#include "motion.h"

namespace edge
{

/// A draw: what is drawn, set on the call, and where, worked out by commit().  execute() gives the call itself to its renderer
/// (Renderer::do_draw), which reads it through the getters below.
class DrawCall: public std::enable_shared_from_this<DrawCall> {
public:
	DrawCall(DrawCall const &) = delete;
	DrawCall &operator=(DrawCall const &) = delete;
	DrawCall(RendererPtr const &renderer);
	virtual ~DrawCall();

	void set_priority(int priority);
	int priority() const { return priority_; }
	void set_always_visible(bool always_visible);
	void set_obb(rove::obb<3> const &bounds);
	void set_vertex_buffer(VertexBufferPtr const &vertex_buffer);
	void set_index_buffer(IndexBufferPtr const &index_buffer);
	void set_material(std::shared_ptr<Material> const &material);
	void set_minimal_vertex_index(size_t minimal_vertex_index);
	void set_first_index_offset(size_t first_index_offset);
	void set_triangles_count(size_t triangles_count);
	void set_world_matrix(rove::matrix<4,4> const &world_matrix);
	/// Draws the call once for each matrix (an instanced draw): instance i at instances[i] * world matrix, its bounds the call's
	/// bounds (obb, where the world matrix puts them) moved by instances[i].  commit() culls each instance on its own, and only
	/// those in view are drawn.  The shaders place an instance by edge_instance_matrix() (edge_instance.glsl), as the built-in ones
	/// do.  No instances is std::invalid_argument.
	void set_instances(std::vector<rove::matrix<4,4>> const &instances);
	/// The same of a list shared, not copied (a model's mesh hands one to each call it makes).
	void set_instances(Placement::Matrices const &instances);
	void set_camera(CameraPtr const &camera);
	/// Works out the matrices the shader reads, this frame's and the previous frame's, and which instances are in view.  The camera's and
	/// the call's state as they are now: commit again after changing either.  std::logic_error without a vertex buffer, a material or a
	/// camera, or for what previous_* gave that does not fit the call (previous instances of a call not instanced, previous joint
	/// matrices of one not skinned); std::invalid_argument for previous joint matrices of another length than the call's.
	void commit();

	// The previous frame's matrices (motion vectors: TAA, motion blur; edge_motion.glsl).  The frame before is the camera's: the last
	// of the renderer's frames (Renderer::frame_index()) in which a call was committed through it (Renderer::previous_frame()), so
	// each camera that commits the call has its own, whichever commits first.  commit() works them out from:
	// - what placed the call (its world matrix, instances and joint matrices) at its commits in that frame.  A call committed again
	//   in a frame at the same placement (another pass, another camera) is one thing and keeps that; committed at two placements in a
	//   frame it is two things, and where either was is not known.  Not known either for a call not committed in that frame (seen the
	//   first time, or hidden then), nor in the last PlacementHistory::FRAMES (4) frames it was committed in before this one (a camera
	//   that draws it more rarely than one in four of its frames), reset since (reset_previous()), or placed otherwise than then
	//   (instanced and not, joint matrices of another length): it was where it is now;
	// - the camera's view-projection at its last commit, by any call of the renderer, in that frame: where there is none (a camera seen
	//   the first time), or the camera was cut since (Camera::cut()), the one it has now.  A camera moved between the passes of a frame
	//   (one camera for a cube's faces) has only its last: each view that writes motion wants a camera of its own.
	// A call made afresh each frame (a model's, Model::draw) remembers nothing: what previous_* give stands in for what it would,
	// for its commits in the frame they are given in (the open one, or between frames the next one).  A call drawn (execute()) in a
	// frame it was not committed in has not moved since (commit() is called again after the call or its camera changes): its previous
	// matrices are then its own, this frame's.

	/// The world matrix the call had the frame before, in place of what it remembers.
	void set_previous_world_matrix(rove::matrix<4,4> const &world_matrix);
	/// The instances the call had the frame before, in place of what it remembers: instance i was at instances[i]; one past them is new
	/// (where it is now).  std::invalid_argument for none.
	void set_previous_instances(std::vector<rove::matrix<4,4>> const &instances);
	/// All of what placed the call the frame before (world matrix, instances, joint matrices), in place of what it remembers: what a
	/// model's mesh gives the calls it makes afresh.  std::invalid_argument for an empty list of instances.
	void set_previous(Placement const &previous);
	/// Forgets where the call was: from its next commit on it was where it is (the call draws another object now, the object jumped).
	/// What was given for this frame goes too.  The camera's motion stays.
	void reset_previous();

	VertexBufferPtr const &vertex_buffer() const { return vertex_buffer_; }
	/// None: the vertices from minimal_vertex_index(), three a triangle.
	IndexBufferPtr const &index_buffer() const { return index_buffer_; }
	MaterialPtr const &material() const { return material_; }
	CameraPtr const &camera() const { return camera_; }
	size_t minimal_vertex_index() const { return minimal_vertex_index_; }
	size_t first_index_offset() const { return first_index_offset_; }
	size_t triangles_count() const { return triangles_count_; }
	/// The matrices commit() worked out for the shader: the camera's view-projection times the world matrix, and the world matrix; for
	/// an instanced call the view-projection alone and the identity (each instance's matrix places it, edge_instance.glsl).
	rove::matrix<4,4> const &shader_world_view_projection() const { return shader_world_view_projection_; }
	rove::matrix<4,4> const &shader_world() const { return shader_world_; }
	/// The same of the previous frame (commit() says which): the camera's view-projection then times the world matrix then (an instanced
	/// call's: the view-projection alone and the identity), and the world matrix then.  For a call seen the first time through a camera
	/// seen the first time, this frame's.
	rove::matrix<4,4> const &shader_previous_world_view_projection() const { return shader_previous_world_view_projection_; }
	rove::matrix<4,4> const &shader_previous_world() const { return shader_previous_world_; }
	bool is_instanced() const { return instances_ != nullptr; }
	/// The instances commit() found in view (all of them for a call that is always visible), each as its world; none for a call that
	/// is not instanced.
	std::vector<rove::matrix<4,4>> const &visible_instances() const { return visible_instances_; }
	/// Where each of visible_instances() was the frame before, as its world (the instance then times the world matrix then); a new
	/// one where it is.  None for a call that is not instanced.
	std::vector<rove::matrix<4,4>> const &previous_visible_instances() const { return previous_visible_instances_; }
	/// The uber draw call's: the built-in shader's flags of what is drawn (texture blending, a skin) are set.
	virtual bool is_uber() const { return false; }
	/// The skin's joint matrices; none (nullptr): not skinned.
	virtual std::vector<rove::matrix<4,4>> const *joint_matrices() const { return nullptr; }
	/// The same as the call holds them, for what keeps them without a copy (the call's history); none: not skinned.
	virtual Placement::Matrices shared_joint_matrices() const { return nullptr; }
	/// The skin's joint matrices the frame before; none (nullptr): not skinned.  As many as joint_matrices() has, but where joint
	/// matrices of another length were set after commit() (they are read as the call is drawn): drawn in the commit's frame, that is
	/// std::logic_error (their previous ones are not known); drawn in a later frame, the call did not move and they are their own.
	std::vector<rove::matrix<4,4>> const *previous_joint_matrices() const
	{
		return joint_matrices() ? previous_joint_matrices_.get() : nullptr;
	}
	/// Whether the previous matrices are known: false where they are this frame's for want of them (seen the first time, at two
	/// placements or placed otherwise in the frame before, not committed then, reset, through a camera seen the first time or cut);
	/// true for a call told its placement in full (previous()), and for one drawn in a frame after its commit (it did not move).
	/// The shader sees it as EDGE_FLAG_PREVIOUS_UNKNOWN, for a TAA pass to throw its history away.  An instance past the frame before's
	/// instances is new and its own previous one, which the flag does not say.
	bool has_previous() const { return previous_known_; }

	bool is_visible() const;
	/// The instances commit() found in view (all of them for a call that is always visible); 1 for a call that is not instanced.
	size_t instance_count() const;
	/// The instances given, in view or not; 1 for a call that is not instanced.
	size_t offered_instance_count() const;
	/// How far in front of the camera commit() found the call: its bounds' centre along the view direction (for an instanced call,
	/// the mean centre of the instances in view).  What Scheduler::sort_blended orders blended calls by.
	float view_depth() const { return view_depth_; }

	/// Draws, in the renderer's pass (Renderer::draw); nothing for no triangles.  std::logic_error before commit().
	void execute();

protected:
	RendererPtr const renderer_;
	int priority_;
	bool always_visible_;
	rove::obb<3> bounds_;
	VertexBufferPtr vertex_buffer_;
	IndexBufferPtr index_buffer_;
	std::shared_ptr<Material> material_;
	size_t minimal_vertex_index_, first_index_offset_, triangles_count_;
	rove::matrix<4,4> world_matrix_;
	CameraPtr camera_;
	rove::matrix<4,4> shader_world_view_projection_, shader_world_;      // commit()
	rove::frustum<> frustum_;
	bool committed_ = false;
	Placement::Matrices instances_;                        // set_instances(); none: not instanced (shared, never copied again)
	std::vector<rove::matrix<4,4>> visible_instances_;    // commit(): each in view, as its world
	float view_depth_ = 0;

	/// The joint matrices the frame before, in place of what the call remembers (UberDrawCall::previous_joint_matrix_array).
	void give_previous_joint_matrices(std::vector<rove::matrix<4,4>> const &joint_matrices);

private:
	PlacementHistory history_;                              // where the call was placed the frames before
	rove::matrix<4,4> shader_previous_world_view_projection_, shader_previous_world_;      // commit()
	std::vector<rove::matrix<4,4>> previous_visible_instances_;
	Placement::Matrices previous_joint_matrices_;           // shared with what told or kept them, or this frame's; none: not skinned
	/// What previous_* gave, for the commits of given_frame_
	std::uint64_t given_frame_ = 0;
	std::optional<rove::matrix<4,4>> given_world_;
	Placement::Matrices given_instances_, given_joint_matrices_;
	/// The frame of the last commit; drawn in another (not committed again: it did not move) the previous matrices are the call's own
	std::uint64_t committed_frame_ = 0;
	bool previous_known_ = false;
	/// Forgets what was given for a frame other than @p frame.
	void keep_given_of(std::uint64_t frame);
};

}
