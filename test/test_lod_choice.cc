// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

// Contract of the level of detail chosen by pixel error (lod_choice.h): the coarsest level whose error, where the thing is, spans no more pixels than the tolerance; a pixel's
// span from the projection and the picture's height; a model's or a mesh's levels scaled as placed.  Errors that are not a ladder from 0,
// and spans or tolerances that are not numbers, are refused.  The numbers are powers of two, so a level on the boundary is exactly on it.

#include <cmath>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <vector>

#include <boost/test/unit_test.hpp>

#include "animated_model.h"
#include "camera.h"
#include "lod_choice.h"
#if defined(EDGE_TEST_ASSET_TOOLS)
#include "mesh_lod.h"
#include "model_convert.h"
#endif
#include "null_renderer.h"
#include "scheduler.h"
#include "test_fixture.h"

namespace
{

float const not_a_number = std::numeric_limits<float>::quiet_NaN();
float const infinite = std::numeric_limits<float>::infinity();

unsigned level(std::vector<float> const &errors, float metres_per_pixel, float tolerance)
{
	return edge::lod_level(std::span<float const>(errors), metres_per_pixel, tolerance);
}

}

BOOST_AUTO_TEST_SUITE(lod_choice)

BOOST_AUTO_TEST_CASE(the_coarsest_level_within_the_tolerance_is_chosen)
{
	std::vector<float> const errors = {0.0f, 0.25f, 1.0f, 4.0f};
	// a pixel spans 0.5 m and one is allowed: a level straying 0.5 m or less
	BOOST_CHECK_EQUAL(level(errors, 0.5f, 1.0f), 1u);
	BOOST_CHECK_EQUAL(level(errors, 1.0f, 1.0f), 2u);              // exactly on the budget is within it
	BOOST_CHECK_EQUAL(level(errors, 0.5f, 2.0f), 2u);              // two pixels allowed: the same as a pixel twice as big
	BOOST_CHECK_EQUAL(level(errors, 0.125f, 1.0f), 0u);
	BOOST_CHECK_EQUAL(level(errors, 1000.0f, 1.0f), 3u);           // far away: the coarsest there is, and no further
	BOOST_CHECK_EQUAL(level({0.0f}, 1000.0f, 1.0f), 0u);           // a model without levels
}

BOOST_AUTO_TEST_CASE(nothing_is_coarsened_at_no_span_or_no_tolerance_but_a_level_that_strays_nothing)
{
	std::vector<float> const errors = {0.0f, 0.25f, 1.0f};
	BOOST_CHECK_EQUAL(level(errors, 0.0f, 1.0f), 0u);              // at the eye, or inside the thing (zero distance, or less)
	BOOST_CHECK_EQUAL(level(errors, 0.5f, 0.0f), 0u);              // zero tolerance: level 0 everywhere
	BOOST_CHECK_EQUAL(level({0.0f, 0.0f, 1.0f}, 0.0f, 1.0f), 1u);  // a level that strays nothing is as good as level 0
}

BOOST_AUTO_TEST_CASE(errors_that_are_not_a_ladder_from_zero_are_refused)
{
	BOOST_CHECK_THROW(level({}, 1.0f, 1.0f), std::invalid_argument);
	BOOST_CHECK_THROW(level({0.5f, 1.0f}, 1.0f, 1.0f), std::invalid_argument);          // level 0 strays from itself
	BOOST_CHECK_THROW(level({0.0f, 1.0f, 0.5f}, 1.0f, 1.0f), std::invalid_argument);    // a coarser level better than a finer one
	BOOST_CHECK_THROW(level({0.0f, -1.0f}, 1.0f, 1.0f), std::invalid_argument);
	BOOST_CHECK_THROW(level({0.0f, 1.0f, not_a_number}, 1000.0f, 1.0f), std::invalid_argument);  // the last, even when it would not be reached
	BOOST_CHECK_THROW(level({0.0f, not_a_number, 1.0f}, 0.0f, 1.0f), std::invalid_argument);
	BOOST_CHECK_THROW(level({0.0f, infinite}, 1.0f, 1.0f), std::invalid_argument);
}

BOOST_AUTO_TEST_CASE(a_span_or_a_tolerance_that_is_not_a_number_of_its_kind_is_refused)
{
	std::vector<float> const errors = {0.0f, 1.0f};
	for (float bad : {-1.0f, not_a_number, infinite})
	{
		BOOST_CHECK_THROW(level(errors, bad, 1.0f), std::invalid_argument);
		BOOST_CHECK_THROW(level(errors, 1.0f, bad), std::invalid_argument);
	}
}

BOOST_AUTO_TEST_CASE(a_pixel_spans_what_the_projection_makes_of_it)
{
	edge::Camera camera;
	float const fov = 1.0f;
	camera.set_perspective_projection(fov, 16.0f / 9.0f, 0.1f, 100.0f);
	float const pitch = edge::pixel_pitch(camera.projection_matrix(), 720);
	BOOST_CHECK_CLOSE(pitch, 2.0f * std::tan(fov / 2.0f) / 720.0f, 1e-4);
	// a point `pitch * depth` above another at that depth lands one pixel above it (2 / 720 in NDC)
	for (float depth : {1.0f, 7.5f, 60.0f})
	{
		rove::vec<3> a(0.0f, 0.0f, -depth), b(0.0f, pitch * depth, -depth);
		rove::vec<4> const pa = camera.projection_matrix() * rove::vec<4>(a, 1.0f);
		rove::vec<4> const pb = camera.projection_matrix() * rove::vec<4>(b, 1.0f);
		BOOST_CHECK_CLOSE(std::fabs(pb.y / pb.w - pa.y / pa.w) * 720.0f / 2.0f, 1.0f, 1e-3);
	}
	camera.set_orthographic_projection(20.0f, 10.0f, 0.1f, 100.0f);    // 10 m high onto 500 pixels: 2 cm a pixel, at any depth
	BOOST_CHECK_CLOSE(edge::pixel_pitch(camera.projection_matrix(), 500), 0.02f, 1e-4);
}

BOOST_AUTO_TEST_CASE(a_turned_over_projection_spans_the_same_and_a_picture_of_no_pixels_is_refused)
{
	edge::Camera camera;
	camera.set_perspective_projection(1.0f, 1.0f, 0.1f, 100.0f);
	rove::matrix<4,4> flipped = camera.projection_matrix();
	flipped[1][1] = -flipped[1][1];
	BOOST_CHECK_EQUAL(edge::pixel_pitch(flipped, 600), edge::pixel_pitch(camera.projection_matrix(), 600));
	BOOST_CHECK_THROW(edge::pixel_pitch(camera.projection_matrix(), 0), std::invalid_argument);
	rove::matrix<4,4> none = camera.projection_matrix();
	none[1][1] = 0.0f;
	BOOST_CHECK_THROW(edge::pixel_pitch(none, 600), std::invalid_argument);
	none[1][1] = not_a_number;
	BOOST_CHECK_THROW(edge::pixel_pitch(none, 600), std::invalid_argument);
}

// (edge_asset's converter: EDGE_ASSET_TOOLS)
#if defined(EDGE_TEST_ASSET_TOOLS)
BOOST_AUTO_TEST_CASE(a_models_and_a_meshs_levels_are_chosen_as_they_stray_where_the_model_is_placed)
{
	edge::RendererPtr renderer(new edge::NullRenderer(320, 200));
	auto scheduler = std::make_shared<edge::Scheduler>(renderer);
	std::filesystem::path const file = std::filesystem::temp_directory_path() / "edge_lod_choice.edge";
	edge::convert_gltf_to_edge(edge::TestFixture::graphic_sources_path() + "test/data/test_figure.glb", file.string(), edge::GltfOptions(),
		edge::LodOptions{{0.5f, 0.2f, 0.06f}, 0});
	auto model = std::make_shared<edge::AnimatedModelPTBNJWT>(renderer, scheduler);
	model->load_from_file(file.string());
	std::filesystem::remove(file);
	BOOST_REQUIRE_EQUAL(model->lod_count(), 4u);
	std::vector<float> errors;
	for (unsigned k = 0; k < model->lod_count(); ++k)
		errors.push_back(model->lod_error(k));
	for (float scale : {1.0f, 3.0f, 0.25f})
		for (unsigned k = 0; k < 4; ++k)
		{
			float const span = errors[k] * scale;                        // a pixel the size of level k's error, placed at `scale`
			BOOST_CHECK_EQUAL(edge::lod_level(*model, scale, span, 1.0f), k);
			BOOST_CHECK_EQUAL(edge::lod_level(*model, scale, span * 2.0f, 0.5f), k);
		}
	edge::Mesh const &mesh = *model->meshes().at(0);
	std::vector<float> mesh_errors;
	for (unsigned k = 0; k < mesh.lod_count(); ++k)
		mesh_errors.push_back(mesh.lod_error(k) * 2.0f);
	for (float span : {0.0f, mesh_errors.back() / 2.0f, mesh_errors.back()})
		BOOST_CHECK_EQUAL(edge::lod_level(mesh, 2.0f, span, 1.0f), level(mesh_errors, span, 1.0f));
	for (float bad : {0.0f, -1.0f, not_a_number, infinite})
	{
		BOOST_CHECK_THROW(edge::lod_level(*model, bad, 1.0f, 1.0f), std::invalid_argument);
		BOOST_CHECK_THROW(edge::lod_level(mesh, bad, 1.0f, 1.0f), std::invalid_argument);
	}
}
#endif


BOOST_AUTO_TEST_SUITE_END()
