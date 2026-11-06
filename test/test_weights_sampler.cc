// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include <cmath>
#include <boost/test/unit_test.hpp>

#include "weights_sampler.h"
#include "mesh.h"

BOOST_AUTO_TEST_SUITE(weights_sampler)

BOOST_AUTO_TEST_CASE(empty_samples_returns_empty)
{
	edge::WeightsSampler ws;
	std::vector<float> result = ws.get(0.0f);
	BOOST_CHECK(result.empty());
}

BOOST_AUTO_TEST_CASE(single_sample_returns_those_weights)
{
	edge::WeightsSampler ws;
	std::vector<float> w = {0.5f, 0.3f};
	ws.add_sample(0.0f, w);

	std::vector<float> result = ws.get(0.0f);
	BOOST_REQUIRE_EQUAL(result.size(), 2u);
	BOOST_CHECK_SMALL(std::abs(result[0] - 0.5f), 1e-6f);
	BOOST_CHECK_SMALL(std::abs(result[1] - 0.3f), 1e-6f);
}

BOOST_AUTO_TEST_CASE(clamp_before_first)
{
	edge::WeightsSampler ws;
	ws.add_sample(1.0f, {1.0f});
	ws.add_sample(2.0f, {2.0f});

	std::vector<float> result = ws.get(-100.0f);
	BOOST_REQUIRE_EQUAL(result.size(), 1u);
	BOOST_CHECK_SMALL(std::abs(result[0] - 1.0f), 1e-6f);
}

BOOST_AUTO_TEST_CASE(clamp_after_last)
{
	edge::WeightsSampler ws;
	ws.add_sample(0.0f, {1.0f});
	ws.add_sample(1.0f, {2.0f});

	std::vector<float> result = ws.get(100.0f);
	BOOST_REQUIRE_EQUAL(result.size(), 1u);
	BOOST_CHECK_SMALL(std::abs(result[0] - 2.0f), 1e-6f);
}

BOOST_AUTO_TEST_CASE(linear_interpolation_midpoint)
{
	edge::WeightsSampler ws;
	ws.set_interpolation(edge::WeightsSampler::Interpolation::LINEAR);
	ws.add_sample(0.0f, {0.0f, 1.0f});
	ws.add_sample(1.0f, {1.0f, 0.0f});

	std::vector<float> result = ws.get(0.5f);
	BOOST_REQUIRE_EQUAL(result.size(), 2u);
	BOOST_CHECK_SMALL(std::abs(result[0] - 0.5f), 1e-6f);
	BOOST_CHECK_SMALL(std::abs(result[1] - 0.5f), 1e-6f);
}

BOOST_AUTO_TEST_CASE(linear_interpolation_quarter)
{
	edge::WeightsSampler ws;
	ws.set_interpolation(edge::WeightsSampler::Interpolation::LINEAR);
	ws.add_sample(0.0f, {0.0f});
	ws.add_sample(1.0f, {4.0f});

	std::vector<float> result = ws.get(0.25f);
	BOOST_REQUIRE_EQUAL(result.size(), 1u);
	BOOST_CHECK_SMALL(std::abs(result[0] - 1.0f), 1e-6f);
}

BOOST_AUTO_TEST_CASE(step_interpolation_returns_left_sample)
{
	edge::WeightsSampler ws;
	ws.set_interpolation(edge::WeightsSampler::Interpolation::STEP);
	ws.add_sample(0.0f, {1.0f});
	ws.add_sample(1.0f, {2.0f});

	std::vector<float> result = ws.get(0.5f);
	BOOST_REQUIRE_EQUAL(result.size(), 1u);
	BOOST_CHECK_SMALL(std::abs(result[0] - 1.0f), 1e-6f);
}

BOOST_AUTO_TEST_CASE(step_interpolation_at_boundary)
{
	edge::WeightsSampler ws;
	ws.set_interpolation(edge::WeightsSampler::Interpolation::STEP);
	ws.add_sample(0.0f, {10.0f});
	ws.add_sample(1.0f, {20.0f});
	ws.add_sample(2.0f, {30.0f});

	std::vector<float> result = ws.get(0.99f);
	BOOST_REQUIRE_EQUAL(result.size(), 1u);
	BOOST_CHECK_SMALL(std::abs(result[0] - 10.0f), 1e-4f);
}

BOOST_AUTO_TEST_CASE(mismatched_component_counts)
{
	edge::WeightsSampler ws;
	ws.set_interpolation(edge::WeightsSampler::Interpolation::LINEAR);
	ws.add_sample(0.0f, {1.0f, 2.0f});
	ws.add_sample(1.0f, {3.0f, 4.0f, 5.0f});

	std::vector<float> result = ws.get(0.5f);
	// Result should have max(2, 3) = 3 components
	BOOST_REQUIRE_EQUAL(result.size(), 3u);
	BOOST_CHECK_SMALL(std::abs(result[0] - 2.0f), 1e-6f);
	BOOST_CHECK_SMALL(std::abs(result[1] - 3.0f), 1e-6f);
	// Third component: lerp(0, 5, 0.5) = 2.5
	BOOST_CHECK_SMALL(std::abs(result[2] - 2.5f), 1e-6f);
}

BOOST_AUTO_TEST_CASE(three_samples_second_segment)
{
	edge::WeightsSampler ws;
	ws.set_interpolation(edge::WeightsSampler::Interpolation::LINEAR);
	ws.add_sample(0.0f, {0.0f});
	ws.add_sample(1.0f, {10.0f});
	ws.add_sample(2.0f, {20.0f});

	std::vector<float> result = ws.get(1.5f);
	BOOST_REQUIRE_EQUAL(result.size(), 1u);
	BOOST_CHECK_SMALL(std::abs(result[0] - 15.0f), 1e-4f);
}

BOOST_AUTO_TEST_SUITE_END()
