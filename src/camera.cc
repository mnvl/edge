// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include <stdexcept>
#include <string>
#include "camera.h"

namespace
{
constexpr float UNIT_TOLERANCE = 1.0e-6f;          // how far from 1 the squared length of an axis of the view may be
}

namespace edge
{

Camera::Camera()
{
	set_perspective_projection(rove::PI/4, 1.0f, 1.0f, 10000.0f);
	look_at(rove::vec<3>(0, 0, 0), rove::vec<3>(0, 0, 1), rove::vec<3>(0, 1, 0));
}

void Camera::set_perspective_projection(rove::scalar fov, rove::scalar aspect, rove::scalar z_near, rove::scalar z_far)
{
	field_of_view_ = fov;
	aspect_ratio_ = aspect;
	near_plane_ = z_near;
	far_plane_ = z_far;

	projection_matrix_.perspective(fov, aspect, z_near, z_far);

	if (!projection_matrix_.inverse(inverted_projection_matrix_))
		throw std::runtime_error("can't invert projection matrix");
}

void Camera::set_orthographic_projection(rove::scalar width, rove::scalar height, rove::scalar z_near, rove::scalar z_far)
{
	field_of_view_ = rove::PI/4;
	aspect_ratio_ = width / height;
	near_plane_ = z_near;
	far_plane_ = z_far;

	projection_matrix_.ortho(width, height, z_near, z_far);

	if (!projection_matrix_.inverse(inverted_projection_matrix_))
		throw std::runtime_error("can't invert projection matrix");
}

void Camera::set_view_matrix(rove::matrix<4,4> const &tf)
{
	if (!tf.inverse(inverted_view_matrix_))
		throw std::runtime_error("can't invert view matrix");
	view_matrix_ = tf;
	billboard_matrix_ = inverted_view_matrix_;

	{
		rove::matrix<4,4> &bb = billboard_matrix_;

		cross_dir_.set(bb.ij[0][0], bb.ij[0][1], bb.ij[0][2]);
		up_dir_.set(bb.ij[1][0], bb.ij[1][1], bb.ij[1][2]);
		view_dir_.set(bb.ij[2][0], bb.ij[2][1], bb.ij[2][2]);
		eye_point_.set(bb.ij[3][0], bb.ij[3][1], bb.ij[3][2]);

		bb.ij[3][0] = 0;
		bb.ij[3][1] = 0;
		bb.ij[3][2] = 0;
	}

	// view matrices with scaling are not supported
	assert (rove::abs(cross_dir_.length_sq() - 1.0f) < UNIT_TOLERANCE);
	assert (rove::abs(up_dir_.length_sq() - 1.0f) < UNIT_TOLERANCE);
	assert (rove::abs(view_dir_.length_sq() - 1.0f) < UNIT_TOLERANCE);
}

void Camera::look_at(rove::vec<3> const &eye_pt, rove::vec<3> const &lookat_pt, rove::vec<3> const &up_dir)
{
	rove::matrix<4,4> tf;
	tf.lookat(eye_pt, lookat_pt, up_dir);
	set_view_matrix(tf);
}

void Camera::set_cube_face(rove::vec<3> const &eye, int face, rove::scalar z_near, rove::scalar z_far)
{
	// Face i of a cube is sampled (Vulkan's and OpenGL's table) with s across and t down its image along these axes; edge's cameras
	// put right = forward x up across and up up the image, which is the table mirrored.  Mirrored once more in y (edge_cube_direction),
	// the faces are these turns of the camera: what each looks along, and its up.
	static float const views[6][2][3] = {
		{{1, 0, 0}, {0, -1, 0}},
		{{-1, 0, 0}, {0, -1, 0}},
		{{0, -1, 0}, {0, 0, -1}},
		{{0, 1, 0}, {0, 0, 1}},
		{{0, 0, 1}, {0, -1, 0}},
		{{0, 0, -1}, {0, -1, 0}},
	};
	if (face < 0 || face >= 6)
		throw std::out_of_range("cube face " + std::to_string(face) + ": a cube has faces 0 to 5");
	rove::vec<3> const forward(views[face][0][0], views[face][0][1], views[face][0][2]);
	rove::vec<3> const up(views[face][1][0], views[face][1][1], views[face][1][2]);
	set_perspective_projection(rove::PI / 2, 1, z_near, z_far);
	look_at(eye, eye + forward, up);
}

}
