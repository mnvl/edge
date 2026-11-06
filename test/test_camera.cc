// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include <boost/test/unit_test.hpp>
#include "camera.h"

namespace
{
constexpr float TOLERANCE = 1.0e-6f;          // how close (squared) the tests take a computed vector to be to the one expected
}

BOOST_AUTO_TEST_SUITE(camera)

BOOST_AUTO_TEST_CASE(translation_rotation_1)
{
	edge::Camera c;
	c.set_translation_rotation(rove::vec<3>(0, 0, 0), rove::vec<3>(0, 0, 0));

	BOOST_REQUIRE (((c.view_matrix() * rove::vec<3>(0, 0, 0)) - rove::vec<3>(0, 0, 0)).length_sq() < TOLERANCE);
	BOOST_REQUIRE ((rove::vec<3>(0, 0, 0) - c.eye_point()).length_sq() < TOLERANCE);
	BOOST_REQUIRE ((rove::vec<3>(0, 0, 1) - c.view_dir()).length_sq() < TOLERANCE);
	BOOST_REQUIRE ((rove::vec<3>(0, 1, 0) - c.up_dir()).length_sq() < TOLERANCE);
	BOOST_REQUIRE ((rove::vec<3>(1, 0, 0) - c.cross_dir()).length_sq() < TOLERANCE);
}

BOOST_AUTO_TEST_CASE(translation_rotation_2)
{
	edge::Camera c;
	c.set_translation_rotation(rove::vec<3>(-123, -456, -789), rove::vec<3>(0, 0, 0));

	BOOST_REQUIRE (((c.view_matrix() * rove::vec<3>(0, 0, 0)) + rove::vec<3>(123, 456, 789)).length_sq() < TOLERANCE);
	BOOST_REQUIRE ((rove::vec<3>(123, 456, 789) - c.eye_point()).length_sq() < TOLERANCE);
	BOOST_REQUIRE ((rove::vec<3>(0, 0, 1) - c.view_dir()).length_sq() < TOLERANCE);
	BOOST_REQUIRE ((rove::vec<3>(0, 1, 0) - c.up_dir()).length_sq() < TOLERANCE);
	BOOST_REQUIRE ((rove::vec<3>(1, 0, 0) - c.cross_dir()).length_sq() < TOLERANCE);
}

BOOST_AUTO_TEST_CASE(translation_rotation_3)
{
	edge::Camera c;
	c.set_translation_rotation(rove::vec<3>(0, 0, 0), rove::vec<3>(-rove::PI/4, 0, 0));

	BOOST_REQUIRE (((c.view_matrix() * rove::vec<3>(0, 0, 0)) + rove::vec<3>(0, 0, 0)).length_sq() < TOLERANCE);
	BOOST_REQUIRE ((rove::vec<3>(0, 0, 0) - c.eye_point()).length_sq() < TOLERANCE);
	BOOST_REQUIRE ((rove::vec<3>(0, -0.707106f, 0.707106f) - c.view_dir()).length_sq() < TOLERANCE);
	BOOST_REQUIRE ((rove::vec<3>(0, 0.707106f, 0.707106f) - c.up_dir()).length_sq() < TOLERANCE);
	BOOST_REQUIRE ((rove::vec<3>(1, 0, 0) - c.cross_dir()).length_sq() < TOLERANCE);
}

BOOST_AUTO_TEST_CASE(translation_rotation_4)
{
	edge::Camera c;
	c.set_translation_rotation(rove::vec<3>(0, 0, 0), rove::vec<3>(rove::PI/2, 0, 0));

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
