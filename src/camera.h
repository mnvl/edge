// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

#include <cstdint>
#include <memory>
#include <rove/vec.h>
#include <rove/matrix.h>
#include "forward.h"

namespace edge
{

/**
 * @brief Camera with view and projection matrices for rendering and frustum culling.
 *
 * Camera owns two independent transforms:
 *   - **View matrix** — positions and orients the camera in world space.
 *     Set via look_at() or set_view_matrix().
 *   - **Projection matrix** — defines the visible volume (frustum).
 *     Set via set_perspective_projection() or set_orthographic_projection().
 *
 * ## Frustum culling
 *
 * When a DrawCall is committed (DrawCall::commit()), it builds a
 * view-projection matrix from this camera and extracts six frustum planes
 * via the Gribb-Hartmann method (rove::frustum::load()).  During
 * Scheduler::flush(), each draw call's OBB is tested against these planes
 * and skipped if entirely outside.
 *
 * The @p z_far parameter in set_perspective_projection() directly controls
 * the far frustum plane distance.  Any geometry beyond @p z_far is culled
 * and never reaches the GPU.  Lowering @p z_far is the primary knob for
 * reducing draw call count and improving rendering performance.  It also
 * improves depth buffer precision (less z-fighting) because the [0,1]
 * depth range is distributed over a smaller interval.
 */
class Camera: public std::enable_shared_from_this<Camera> {
public:
	/// Constructs a camera with a default 45° perspective projection
	/// (z_near=1, z_far=10000) looking along +Z from the origin.
	Camera();

	/// @name View matrix accessors
	/// @{

	/// View matrix (world → view space).
	rove::matrix<4,4> const &view_matrix() const {
		return view_matrix_;
	}

	/// Inverse view matrix (view → world space).
	rove::matrix<4,4> const &inverted_view_matrix() const {
		return inverted_view_matrix_;
	}

	/// Billboard matrix (inverse view with translation zeroed out).
	rove::matrix<4,4> const &billboard_matrix() const {
		return billboard_matrix_;
	}

	/// @}
	/// @name Projection matrix accessors
	/// @{

	/// Projection matrix (view → clip space).
	rove::matrix<4,4> const &projection_matrix() const {
		return projection_matrix_;
	}

	/// Inverse projection matrix (clip → view space).
	rove::matrix<4,4> const &inverted_projection_matrix() const {
		return inverted_projection_matrix_;
	}

	/// @}
	/// @name Derived view vectors (extracted from the inverse view matrix)
	/// @{

	/// Camera position in world space.
	rove::vec<3> const &eye_point() const { return eye_point_; }
	/// Camera up direction in world space.
	rove::vec<3> const &up_dir() const { return up_dir_; }
	/// Camera forward direction in world space.
	rove::vec<3> const &view_dir() const { return view_dir_; }
	/// Camera right direction in world space.
	rove::vec<3> const &cross_dir() const { return cross_dir_; }

	/// @}
	/// @name Projection setup
	/// @{

	/**
	 * @brief Set a symmetric perspective projection.
	 * @param fov    Vertical field of view in radians.
	 * @param aspect Aspect ratio (width / height).
	 * @param z_near Near clipping plane distance.  Must be > 0.
	 * @param z_far  Far clipping plane distance.  Geometry beyond this
	 *               distance is frustum-culled and never drawn.  Keeping
	 *               this as small as practical reduces draw calls and
	 *               improves depth buffer precision.
	 */
	void set_perspective_projection(rove::scalar fov, rove::scalar aspect, rove::scalar z_near,
		rove::scalar z_far);

	/**
	 * @brief Set an orthographic projection.
	 * @param width  View width in world units.
	 * @param height View height in world units.
	 * @param z_near Near clipping plane distance.
	 * @param z_far  Far clipping plane distance.  Same culling role as in
	 *               set_perspective_projection().
	 */
	void set_orthographic_projection(rove::scalar width, rove::scalar height, rove::scalar z_near,
		rove::scalar z_far);

	/// @}
	/// @name View setup
	/// @{

	/// Set the view matrix directly.  The inverse, billboard matrix, and
	/// derived vectors (eye, up, view, cross) are recomputed.
	void set_view_matrix(rove::matrix<4,4> const &tf);

	/// Build a look-at view matrix (rove::matrix::lookat: right = forward x up, a mirror; README, Coordinate System).
	/// @param eye_pt    Camera position in world space.
	/// @param lookat_pt Point the camera is aimed at.
	/// @param up_dir    World up hint (will be orthogonalised).
	void look_at(rove::vec<3> const &eye_pt, rove::vec<3> const &lookat_pt, rove::vec<3> const &up_dir);

	/// The camera that draws face @p face (0 to 5: +x, -x, +y, -y, +z, -z) of a cube render target (RenderTargetFormat::cube) from
	/// @p eye: a square perspective of a right angle (the projection too: set_perspective_projection(pi / 2, 1, z_near, z_far)), its
	/// up and right those that make the faces a cube a shader samples in world direction d from the eye by edge_cube_direction(d)
	/// (edge_cube.glsl).  Edge's images are drawn top row first, so the faces hold the world mirrored in y: face 2 looks along -y, face 3
	/// along +y, the others along their own axis.  std::out_of_range for another face.
	void set_cube_face(rove::vec<3> const &eye, int face, rove::scalar z_near, rove::scalar z_far);

	/// @}
	/// @name Motion
	/// @{

	/// A cut: what the camera sees from now on is not seen from where it looked, so the draws committed through it from now on, to the
	/// end of the frame they are in, take its view-projection as they commit it for the previous frame's too
	/// (DrawCall::shader_previous_world_view_projection()): no motion of the camera's.  What they see keeps its own.  The frame
	/// after has the camera's motion again.
	void cut() { ++cuts_; }
	/// How many times cut() was called.
	std::uint64_t cuts() const { return cuts_; }

	/// @}

private:
	rove::vec<3> eye_point_;
	rove::vec<3> up_dir_;
	rove::vec<3> view_dir_;
	rove::vec<3> cross_dir_;
	rove::matrix<4,4> view_matrix_;
	rove::matrix<4,4> inverted_view_matrix_;
	rove::matrix<4,4> billboard_matrix_;

	rove::scalar field_of_view_;
	rove::scalar aspect_ratio_;
	rove::scalar near_plane_;
	rove::scalar far_plane_;
	rove::matrix<4,4> projection_matrix_;
	rove::matrix<4,4> inverted_projection_matrix_;
	std::uint64_t cuts_ = 0;
};

}
