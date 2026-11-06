// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include <cmath>
#include <stdexcept>
#include <boost/test/unit_test.hpp>
#include <rove/scalar.h>

#include "animation.h"
#include "sampler.h"
#include "joint.h"

namespace
{

rove::quaternion<> const NO_ROTATION(0, 0, 0, 1);
rove::vec<3> const UNIT_SCALE(1, 1, 1);

edge::Sampler::Key translation_key(float time, float x, float y, float z)
{
	return {time, rove::vec<3>(x, y, z), NO_ROTATION, UNIT_SCALE};
}

rove::quaternion<> rotation_z(float angle_rad)
{
	return rove::axis_angle(rove::vec<3>(0, 0, 1), angle_rad);
}

rove::matrix<4,4> make_translation(float x, float y, float z)
{
	rove::matrix<4,4> m;
	m.identity();
	m.ij[3][0] = x;
	m.ij[3][1] = y;
	m.ij[3][2] = z;
	return m;
}

float matrix_max_delta(rove::matrix<4,4> const &a, rove::matrix<4,4> const &b)
{
	float max_delta = 0.0f;
	for (std::size_t col = 0; col < 4; ++col)
		for (std::size_t row = 0; row < 4; ++row)
			max_delta = std::max(max_delta, std::abs(a.ij[col][row] - b.ij[col][row]));
	return max_delta;
}

// where the matrix takes a point
rove::vec<3> apply(rove::matrix<4,4> const &m, float x, float y, float z)
{
	return m * rove::vec<3>(x, y, z);
}

void check_point(rove::vec<3> const &got, float x, float y, float z)
{
	BOOST_CHECK_SMALL(std::abs(got.x - x), 1e-4f);
	BOOST_CHECK_SMALL(std::abs(got.y - y), 1e-4f);
	BOOST_CHECK_SMALL(std::abs(got.z - z), 1e-4f);
}

}

BOOST_AUTO_TEST_SUITE(sampler)

BOOST_AUTO_TEST_CASE(single_keyframe_returns_that_keyframe)
{
	edge::Sampler s;
	rove::matrix<4,4> m = make_translation(1.0f, 2.0f, 3.0f);
	s.add_sample(translation_key(0.0f, 1.0f, 2.0f, 3.0f));

	rove::matrix<4,4> result = s.get(0.0f);
	BOOST_CHECK_SMALL(matrix_max_delta(result, m), 1e-4f);
}

BOOST_AUTO_TEST_CASE(time_before_first_clamps_to_first)
{
	edge::Sampler s;
	rove::matrix<4,4> m0 = make_translation(1.0f, 0.0f, 0.0f);
	rove::matrix<4,4> m1 = make_translation(2.0f, 0.0f, 0.0f);
	s.add_sample(translation_key(1.0f, 1.0f, 0.0f, 0.0f));
	s.add_sample(translation_key(2.0f, 2.0f, 0.0f, 0.0f));

	rove::matrix<4,4> result = s.get(-100.0f);
	BOOST_CHECK_SMALL(matrix_max_delta(result, m0), 1e-4f);
}

BOOST_AUTO_TEST_CASE(time_after_last_clamps_to_last)
{
	edge::Sampler s;
	rove::matrix<4,4> m0 = make_translation(1.0f, 0.0f, 0.0f);
	rove::matrix<4,4> m1 = make_translation(2.0f, 0.0f, 0.0f);
	s.add_sample(translation_key(0.0f, 1.0f, 0.0f, 0.0f));
	s.add_sample(translation_key(1.0f, 2.0f, 0.0f, 0.0f));

	rove::matrix<4,4> result = s.get(100.0f);
	BOOST_CHECK_SMALL(matrix_max_delta(result, m1), 1e-4f);
}

BOOST_AUTO_TEST_CASE(linear_translation_interpolation)
{
	edge::Sampler s;
	s.add_sample(translation_key(0.0f, 0.0f, 0.0f, 0.0f));
	s.add_sample(translation_key(1.0f, 10.0f, 0.0f, 0.0f));

	rove::matrix<4,4> result = s.get(0.5f);
	BOOST_CHECK_SMALL(std::abs(result.ij[3][0] - 5.0f), 1e-4f);
	BOOST_CHECK_SMALL(std::abs(result.ij[3][1]), 1e-4f);
	BOOST_CHECK_SMALL(std::abs(result.ij[3][2]), 1e-4f);
}

BOOST_AUTO_TEST_CASE(translation_at_quarter)
{
	edge::Sampler s;
	s.add_sample(translation_key(0.0f, 0.0f, 0.0f, 0.0f));
	s.add_sample(translation_key(1.0f, 4.0f, 8.0f, 12.0f));

	rove::matrix<4,4> result = s.get(0.25f);
	BOOST_CHECK_SMALL(std::abs(result.ij[3][0] - 1.0f), 1e-4f);
	BOOST_CHECK_SMALL(std::abs(result.ij[3][1] - 2.0f), 1e-4f);
	BOOST_CHECK_SMALL(std::abs(result.ij[3][2] - 3.0f), 1e-4f);
}

BOOST_AUTO_TEST_CASE(rotation_slerp_90_degrees)
{
	edge::Sampler s;
	s.add_sample({0.0f, rove::vec<3>(0, 0, 0), NO_ROTATION, UNIT_SCALE});
	s.add_sample({1.0f, rove::vec<3>(0, 0, 0), rotation_z(rove::PI / 2), UNIT_SCALE});

	rove::matrix<4,4> result = s.get(0.5f);
	// At midpoint of 0-90 degree rotation, should be ~45 degrees
	float expected_cos = std::cos(rove::PI / 4);
	BOOST_CHECK_SMALL(std::abs(result.ij[0][0] - expected_cos), 2e-3f);
}

BOOST_AUTO_TEST_CASE(three_keyframes)
{
	edge::Sampler s;
	s.add_sample(translation_key(0.0f, 0.0f, 0.0f, 0.0f));
	s.add_sample(translation_key(1.0f, 10.0f, 0.0f, 0.0f));
	s.add_sample(translation_key(2.0f, 10.0f, 20.0f, 0.0f));

	// Between first two keyframes
	rove::matrix<4,4> r1 = s.get(0.5f);
	BOOST_CHECK_SMALL(std::abs(r1.ij[3][0] - 5.0f), 1e-4f);

	// Between second and third keyframes
	rove::matrix<4,4> r2 = s.get(1.5f);
	BOOST_CHECK_SMALL(std::abs(r2.ij[3][0] - 10.0f), 1e-4f);
	BOOST_CHECK_SMALL(std::abs(r2.ij[3][1] - 10.0f), 1e-4f);
}

BOOST_AUTO_TEST_CASE(apply_updates_joint)
{
	edge::JointPtr joint(new edge::Joint(0));
	joint->transforms.push_back(edge::Joint::Transform{"translate", make_translation(0.0f, 0.0f, 0.0f)});

	edge::Sampler s;
	s.set_target(joint, "translate");
	s.add_sample(translation_key(0.0f, 1.0f, 2.0f, 3.0f));
	s.add_sample(translation_key(1.0f, 5.0f, 6.0f, 7.0f));

	s.apply(0.0f);
	rove::matrix<4,4> const &t = joint->transform_by_sid("translate");
	BOOST_CHECK_SMALL(std::abs(t.ij[3][0] - 1.0f), 1e-4f);
	BOOST_CHECK_SMALL(std::abs(t.ij[3][1] - 2.0f), 1e-4f);
	BOOST_CHECK_SMALL(std::abs(t.ij[3][2] - 3.0f), 1e-4f);
}

BOOST_AUTO_TEST_CASE(exact_keyframe_times)
{
	edge::Sampler s;
	s.add_sample(translation_key(0.0f, 1.0f, 0.0f, 0.0f));
	s.add_sample(translation_key(1.0f, 2.0f, 0.0f, 0.0f));
	s.add_sample(translation_key(2.0f, 3.0f, 0.0f, 0.0f));

	rove::matrix<4,4> r0 = s.get(0.0f);
	rove::matrix<4,4> r1 = s.get(1.0f);
	rove::matrix<4,4> r2 = s.get(2.0f);

	BOOST_CHECK_SMALL(std::abs(r0.ij[3][0] - 1.0f), 1e-4f);
	BOOST_CHECK_SMALL(std::abs(r1.ij[3][0] - 2.0f), 1e-4f);
	BOOST_CHECK_SMALL(std::abs(r2.ij[3][0] - 3.0f), 1e-4f);
}

BOOST_AUTO_TEST_CASE(scale_is_interpolated_between_keys)
{
	// not only at the keys: a node grown from 1 to 3 is 2 halfway
	edge::Sampler s;
	s.add_sample({0.0f, rove::vec<3>(0, 0, 0), NO_ROTATION, rove::vec<3>(1, 1, 1)});
	s.add_sample({1.0f, rove::vec<3>(0, 0, 0), NO_ROTATION, rove::vec<3>(3, 3, 3)});
	check_point(apply(s.get(0.5f), 1, 0, 0), 2, 0, 0);
	check_point(apply(s.get(0.5f), 0, 1, 0), 0, 2, 0);
	check_point(apply(s.get(1.0f), 0, 0, 1), 0, 0, 3);
}

BOOST_AUTO_TEST_CASE(a_key_is_scaled_then_rotated_then_translated)
{
	// glTF's T * R * S: (1, 0, 0) scaled by 2 along x, turned 90 degrees about z, moved 10 along x
	edge::Sampler s;
	s.add_sample({0.0f, rove::vec<3>(10, 0, 0), rotation_z(rove::PI / 2), rove::vec<3>(2, 1, 1)});
	s.add_sample({1.0f, rove::vec<3>(10, 0, 0), rotation_z(rove::PI / 2), rove::vec<3>(2, 1, 1)});
	check_point(apply(s.get(0.0f), 1, 0, 0), 10, 2, 0);
	check_point(apply(s.get(0.5f), 1, 0, 0), 10, 2, 0);
	check_point(apply(s.get(0.5f), 0, 1, 0), 9, 0, 0);
}

BOOST_AUTO_TEST_CASE(a_key_scaled_to_nothing_keeps_its_rotation)
{
	// a part hidden by a scale of 0 grows back turned as its keys say, which a matrix of zeros cannot tell
	edge::Sampler s;
	s.add_sample({0.0f, rove::vec<3>(0, 0, 0), rotation_z(rove::PI / 2), rove::vec<3>(0, 0, 0)});
	s.add_sample({1.0f, rove::vec<3>(0, 0, 0), rotation_z(rove::PI / 2), rove::vec<3>(1, 1, 1)});
	check_point(apply(s.get(0.5f), 1, 0, 0), 0, 0.5f, 0);
}

BOOST_AUTO_TEST_CASE(a_sampler_returns_its_keys_as_given)
{
	edge::Sampler s;
	s.add_sample({0.5f, rove::vec<3>(1, 2, 3), rotation_z(0.3f), rove::vec<3>(4, 5, 6)});
	BOOST_REQUIRE_EQUAL(s.samples_count(), 1u);
	edge::Sampler::Key const &key = s.sample(0);
	BOOST_CHECK_EQUAL(key.time, 0.5f);
	BOOST_CHECK_EQUAL(key.translation.y, 2.0f);
	BOOST_CHECK_EQUAL(key.rotation.z, rotation_z(0.3f).z);
	BOOST_CHECK_EQUAL(key.scale.z, 6.0f);
}

BOOST_AUTO_TEST_CASE(a_sampler_without_keys_has_nothing_to_give)
{
	edge::Sampler s;
	BOOST_CHECK_THROW((void)s.get(0.0f), std::logic_error);
}

BOOST_AUTO_TEST_CASE(keys_come_in_time_order)
{
	edge::Sampler s;
	s.add_sample(translation_key(1.0f, 0, 0, 0));
	BOOST_CHECK_THROW(s.add_sample(translation_key(0.5f, 0, 0, 0)), std::invalid_argument);
	BOOST_CHECK_THROW(s.add_sample(translation_key(1.0f, 0, 0, 0)), std::invalid_argument);
}

BOOST_AUTO_TEST_CASE(a_clip_lasts_from_time_0_to_its_last_key)
{
	// glTF's times are the clip's own from 0: one whose first key is at 1 s and last at 3 s plays for 3 s (holding the first key's pose
	// before it), not 2
	edge::Sampler s;
	s.add_sample(translation_key(1.0f, 0, 0, 0));
	s.add_sample(translation_key(3.0f, 1, 0, 0));
	BOOST_CHECK_EQUAL(s.duration(), 3.0f);
	edge::Animation clip;
	clip.samplers.push_back(s);
	BOOST_CHECK_EQUAL(clip.duration(), 3.0f);
	edge::Sampler empty;
	BOOST_CHECK_EQUAL(empty.duration(), 0.0f);
}

BOOST_AUTO_TEST_SUITE_END()
