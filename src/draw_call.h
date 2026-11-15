// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>
#include <rove/matrix.h>
#include <rove/obb.h>
#include <rove/frustum.h>
#include "forward.h"

namespace edge
{

/// A list of matrices shared, not copied, by the draw calls it is handed to (a model's mesh hands one to each call it makes).
using SharedMatrices = std::shared_ptr<std::vector<rove::matrix<4,4>> const>;

/// A draw: what is drawn, set on the call, and where, worked out by commit().  execute() gives the call itself to its renderer
/// (Renderer::do_draw), which reads it through the getters below.
class DrawCall: public std::enable_shared_from_this<DrawCall> {
public:
	DrawCall(DrawCall const &) = delete;
	DrawCall &operator=(DrawCall const &) = delete;
	DrawCall(std::shared_ptr<Renderer> const &renderer);
	virtual ~DrawCall();

	void set_priority(int priority);
	int priority() const { return priority_; }
	void set_always_visible(bool always_visible);
	void set_obb(rove::obb<3> const &bounds);
	void set_vertex_buffer(std::shared_ptr<VertexBuffer> const &vertex_buffer);
	void set_index_buffer(std::shared_ptr<IndexBuffer> const &index_buffer);
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
	void set_instances(SharedMatrices const &instances);
	void set_camera(std::shared_ptr<Camera> const &camera);
	/// Works out the matrices the shader reads and which instances are in view, from the camera's and the call's state as they are now:
	/// commit again after changing either.  It reads the call and its camera alone.  std::logic_error without a vertex buffer, a
	/// material or a camera.
	void commit();

	std::shared_ptr<VertexBuffer> const &vertex_buffer() const { return vertex_buffer_; }
	/// None: the vertices from minimal_vertex_index(), three a triangle.
	std::shared_ptr<IndexBuffer> const &index_buffer() const { return index_buffer_; }
	std::shared_ptr<Material> const &material() const { return material_; }
	std::shared_ptr<Camera> const &camera() const { return camera_; }
	size_t minimal_vertex_index() const { return minimal_vertex_index_; }
	size_t first_index_offset() const { return first_index_offset_; }
	size_t triangles_count() const { return triangles_count_; }
	/// The matrices commit() worked out for the shader: the camera's view-projection times the world matrix, and the world matrix; for
	/// an instanced call the view-projection alone and the identity (each instance's matrix places it, edge_instance.glsl).
	rove::matrix<4,4> const &shader_world_view_projection() const { return shader_world_view_projection_; }
	rove::matrix<4,4> const &shader_world() const { return shader_world_; }
	bool is_instanced() const { return instances_ != nullptr; }
	/// The instances commit() found in view (all of them for a call that is always visible), each as its world; none for a call that
	/// is not instanced.
	std::vector<rove::matrix<4,4>> const &visible_instances() const { return visible_instances_; }
	/// The uber draw call's: the built-in shader's flags of what is drawn (texture blending, a skin) are set.
	virtual bool is_uber() const { return false; }
	/// The skin's joint matrices, read as the call is drawn (those set after commit() too); none (nullptr): not skinned.
	virtual std::vector<rove::matrix<4,4>> const *joint_matrices() const { return nullptr; }

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
	std::shared_ptr<Renderer> const renderer_;
	int priority_;
	bool always_visible_;
	rove::obb<3> bounds_;
	std::shared_ptr<VertexBuffer> vertex_buffer_;
	std::shared_ptr<IndexBuffer> index_buffer_;
	std::shared_ptr<Material> material_;
	size_t minimal_vertex_index_, first_index_offset_, triangles_count_;
	rove::matrix<4,4> world_matrix_;
	std::shared_ptr<Camera> camera_;
	rove::matrix<4,4> shader_world_view_projection_, shader_world_;      // commit()
	rove::frustum<> frustum_;
	bool committed_ = false;
	SharedMatrices instances_;                             // set_instances(); none: not instanced (shared, never copied again)
	std::vector<rove::matrix<4,4>> visible_instances_;    // commit(): each in view, as its world
	float view_depth_ = 0;
};

}
