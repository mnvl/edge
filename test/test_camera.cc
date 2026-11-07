// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include <boost/test/unit_test.hpp>
#include "camera.h"

namespace
{
constexpr float TOLERANCE = 1.0e-6f;          // how close (squared) the tests take a computed vector to be to the one expected

/// A view matrix of a translation, then Euler angles
rove::matrix<4, 4> moved_and_turned(rove::vec<3> const &translation, rove::vec<3> const &angles)
{
	rove::matrix<4, 4> view;
	view.translation(translation);
	view.rotate(angles.x, angles.y, angles.z);
	return view;
}

template<class C>
concept takes_translation_and_angles = requires(C c, rove::vec<3> v) { c.set_translation_rotation(v, v); };
}

BOOST_AUTO_TEST_SUITE(camera)

// A view matrix is built by rove and given whole: Euler angles in a vector were never a thing to keep
BOOST_AUTO_TEST_CASE(a_camera_is_not_placed_by_a_vector_of_angles)
{
	static_assert(!takes_translation_and_angles<edge::Camera>);
}

BOOST_AUTO_TEST_CASE(view_matrix_of_no_move_or_turn)
{
	edge::Camera c;
	c.set_view_matrix(moved_and_turned(rove::vec<3>(0, 0, 0), rove::vec<3>(0, 0, 0)));

	BOOST_REQUIRE (((c.view_matrix() * rove::vec<3>(0, 0, 0)) - rove::vec<3>(0, 0, 0)).length_sq() < TOLERANCE);
	BOOST_REQUIRE ((rove::vec<3>(0, 0, 0) - c.eye_point()).length_sq() < TOLERANCE);
	BOOST_REQUIRE ((rove::vec<3>(0, 0, 1) - c.view_dir()).length_sq() < TOLERANCE);
	BOOST_REQUIRE ((rove::vec<3>(0, 1, 0) - c.up_dir()).length_sq() < TOLERANCE);
	BOOST_REQUIRE ((rove::vec<3>(1, 0, 0) - c.cross_dir()).length_sq() < TOLERANCE);
}

BOOST_AUTO_TEST_CASE(view_matrix_of_a_move)
{
	edge::Camera c;
	c.set_view_matrix(moved_and_turned(rove::vec<3>(-123, -456, -789), rove::vec<3>(0, 0, 0)));

	BOOST_REQUIRE (((c.view_matrix() * rove::vec<3>(0, 0, 0)) + rove::vec<3>(123, 456, 789)).length_sq() < TOLERANCE);
	BOOST_REQUIRE ((rove::vec<3>(123, 456, 789) - c.eye_point()).length_sq() < TOLERANCE);
	BOOST_REQUIRE ((rove::vec<3>(0, 0, 1) - c.view_dir()).length_sq() < TOLERANCE);
	BOOST_REQUIRE ((rove::vec<3>(0, 1, 0) - c.up_dir()).length_sq() < TOLERANCE);
	BOOST_REQUIRE ((rove::vec<3>(1, 0, 0) - c.cross_dir()).length_sq() < TOLERANCE);
}

BOOST_AUTO_TEST_CASE(view_matrix_of_a_turn_down)
{
	edge::Camera c;
	c.set_view_matrix(moved_and_turned(rove::vec<3>(0, 0, 0), rove::vec<3>(-rove::PI/4, 0, 0)));

	BOOST_REQUIRE (((c.view_matrix() * rove::vec<3>(0, 0, 0)) + rove::vec<3>(0, 0, 0)).length_sq() < TOLERANCE);
	BOOST_REQUIRE ((rove::vec<3>(0, 0, 0) - c.eye_point()).length_sq() < TOLERANCE);
	BOOST_REQUIRE ((rove::vec<3>(0, -0.707106f, 0.707106f) - c.view_dir()).length_sq() < TOLERANCE);
	BOOST_REQUIRE ((rove::vec<3>(0, 0.707106f, 0.707106f) - c.up_dir()).length_sq() < TOLERANCE);
	BOOST_REQUIRE ((rove::vec<3>(1, 0, 0) - c.cross_dir()).length_sq() < TOLERANCE);
}

BOOST_AUTO_TEST_CASE(view_matrix_of_a_quarter_turn)
{
	edge::Camera c;
	c.set_view_matrix(moved_and_turned(rove::vec<3>(0, 0, 0), rove::vec<3>(rove::PI/2, 0, 0)));

	BOOST_REQUIRE (((c.view_matrix() * rove::vec<3>(0, 0, 0)) + rove::vec<3>(0, 0, 0)).length_sq() < TOLERANCE);
	BOOST_REQUIRE ((rove::vec<3>(0, 0, 0) - c.eye_point()).length_sq() < TOLERANCE);
	BOOST_REQUIRE ((rove::vec<3>(0, 1, 0) - c.view_dir()).length_sq() < TOLERANCE);
	BOOST_REQUIRE ((rove::vec<3>(0, 0, -1) - c.up_dir()).length_sq() < TOLERANCE);
	BOOST_REQUIRE ((rove::vec<3>(1, 0, 0) - c.cross_dir()).length_sq() < TOLERANCE);
}

BOOST_AUTO_TEST_CASE(eye_lookat_up_1)
{
	edge::Camera c;
	c.look_at(rove::vec<3>(0, 0, 0), rove::vec<3>(0, 0, 1), rove::vec<3>(0, 1, 0));

	BOOST_REQUIRE (((c.view_matrix() * rove::vec<3>(0, 0, 0)) - rove::vec<3>(0, 0, 0)).length_sq() < TOLERANCE);
	BOOST_REQUIRE ((rove::vec<3>(0, 0, 0) - c.eye_point()).length_sq() < TOLERANCE);
	BOOST_REQUIRE ((rove::vec<3>(0, 0, 1) - c.view_dir()).length_sq() < TOLERANCE);
	BOOST_REQUIRE ((rove::vec<3>(0, 1, 0) - c.up_dir()).length_sq() < TOLERANCE);
	BOOST_REQUIRE ((rove::vec<3>(-1, 0, 0) - c.cross_dir()).length_sq() < TOLERANCE);
}

BOOST_AUTO_TEST_CASE(eye_lookat_up_2)
{
	edge::Camera c;
	c.look_at(rove::vec<3>(123, 456, 789), rove::vec<3>(123, 456, 790), rove::vec<3>(0, 1, 0));

	BOOST_REQUIRE (((c.view_matrix() * rove::vec<3>(0, 0, 0)) + rove::vec<3>(-123, 456, 789)).length_sq() < TOLERANCE);
	BOOST_REQUIRE ((rove::vec<3>(123, 456, 789) - c.eye_point()).length_sq() < TOLERANCE);
	BOOST_REQUIRE ((rove::vec<3>(0, 0, 1) - c.view_dir()).length_sq() < TOLERANCE);
	BOOST_REQUIRE ((rove::vec<3>(0, 1, 0) - c.up_dir()).length_sq() < TOLERANCE);
	BOOST_REQUIRE ((rove::vec<3>(-1, 0, 0) - c.cross_dir()).length_sq() < TOLERANCE);
}

BOOST_AUTO_TEST_SUITE_END()
