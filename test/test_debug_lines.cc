// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

// DebugLines: segments, boxes, spheres and axes drawn as strips of a width in world units facing the camera, one draw for each
// colour, collected between draws.

#include <cmath>
#include <memory>
#include <stdexcept>

#include <boost/test/unit_test.hpp>
#include <rove/aabb.h>
#include <rove/obb.h>

#include "camera.h"
#include "debug_lines.h"
#include "image.h"
#include "null_renderer.h"
#include "pixel.h"
#include "renderer.h"
#include "scheduler.h"
#include "test_backends.h"
#include "test_fixture.h"
#include "texture.h"

using namespace edge;

namespace
{

int const SIZE = 64;

/// [-1, 1] on x and y seen from +z: pixel (x, y) of the target is world (x, y)
CameraPtr view()
{
	CameraPtr camera = std::make_shared<Camera>();
	camera->set_orthographic_projection(2.0f, 2.0f, 0.0f, 10.0f);
	camera->look_at(rove::vec<3>(0, 0, 5), rove::vec<3>(0, 0, 0), rove::vec<3>(0, 1, 0));
	return camera;
}

Image<rove::rgba<float>> draw(RendererPtr const &renderer, DebugLines &lines, CameraPtr const &camera)
{
	auto const scheduler = std::make_shared<Scheduler>(renderer);
	lines.draw(*scheduler, camera);
	RenderTargetPtr const target = renderer->create_render_target(SIZE, SIZE, PixelFormat::R32G32B32A32F);
	RenderPassClearState clear;
	clear.clear_color_r = clear.clear_color_g = clear.clear_color_b = clear.clear_color_a = 0;
	renderer->begin_frame();
	renderer->begin_pass(target, clear);
	scheduler->flush();
	renderer->end_pass();
	renderer->finish_frame();
	return read_image<rove::rgba<float>>(*target->color_texture());
}

rove::rgba<float> at(Image<rove::rgba<float>> const &image, float x, float y)
{
	return image.at(std::min(SIZE - 1, int((x + 1) / 2 * SIZE)), std::min(SIZE - 1, int((1 - y) / 2 * SIZE)));
}

bool red(rove::rgba<float> const &p) { return p.r > 0.99f && p.g < 0.01f && p.b < 0.01f; }
bool green(rove::rgba<float> const &p) { return p.g > 0.99f && p.r < 0.01f && p.b < 0.01f; }
bool empty(rove::rgba<float> const &p) { return p.r == 0 && p.g == 0 && p.b == 0; }

}

BOOST_AUTO_TEST_SUITE(debug_lines)

BOOST_AUTO_TEST_CASE_TEMPLATE(a_line_is_a_strip_of_its_width_along_it, F, Backends)
{
	F fixture;
	auto const &renderer = fixture.renderer;
	BOOST_REQUIRE_MESSAGE(renderer, "renderer unavailable: " << fixture.error);
	DebugLines lines(renderer, 0.1f);
	lines.line(rove::vec<3>(-0.8f, 0, 0), rove::vec<3>(0.8f, 0, 0), rove::vec<4>(1, 0, 0, 1));
	lines.line(rove::vec<3>(0.5f, -0.8f, 0), rove::vec<3>(0.5f, 0.8f, 0), rove::vec<4>(0, 1, 0, 1));
	Image<rove::rgba<float>> const image = draw(renderer, lines, view());
	BOOST_CHECK(red(at(image, -0.5f, 0)));
	BOOST_CHECK(red(at(image, -0.5f, 0.03f)));          // within half the width
	BOOST_CHECK(empty(at(image, -0.5f, 0.1f)));         // past it
	BOOST_CHECK(empty(at(image, -0.9f, 0)));            // past the end
	BOOST_CHECK(green(at(image, 0.5f, 0.5f)));
	BOOST_CHECK(empty(at(image, 0.6f, 0.5f)));
}

BOOST_AUTO_TEST_CASE_TEMPLATE(a_box_is_its_edges_and_a_sphere_its_three_great_circles, F, Backends)
{
	F fixture;
	auto const &renderer = fixture.renderer;
	BOOST_REQUIRE_MESSAGE(renderer, "renderer unavailable: " << fixture.error);
	DebugLines lines(renderer, 0.05f);
	lines.box(rove::aabb<3>(rove::vec<3>(-0.9f, -0.9f, -0.5f), rove::vec<3>(-0.1f, -0.1f, 0.5f)), rove::vec<4>(1, 0, 0, 1));
	lines.sphere(rove::vec<3>(0.5f, 0.5f, 0), 0.3f, rove::vec<4>(0, 1, 0, 1));
	Image<rove::rgba<float>> const image = draw(renderer, lines, view());
	BOOST_CHECK(red(at(image, -0.5f, -0.9f)));          // the bottom edge
	BOOST_CHECK(red(at(image, -0.9f, -0.5f)));          // the left
	BOOST_CHECK(empty(at(image, -0.5f, -0.5f)));        // the middle of a face: no face, only edges
	BOOST_CHECK(green(at(image, 0.8f, 0.5f)));          // on the circle in the view's plane
	BOOST_CHECK(green(at(image, 0.5f, 0.2f)));
	BOOST_CHECK(empty(at(image, 0.65f, 0.65f)));        // inside it, off the other circles (seen edge-on they lie on its axes)
}

BOOST_AUTO_TEST_CASE(one_draw_for_each_colour_and_a_draw_starts_afresh)
{
	auto const renderer = std::make_shared<NullRenderer>(64, 64);
	auto const scheduler = std::make_shared<Scheduler>(renderer);
	CameraPtr const camera = view();
	DebugLines lines(renderer, 0.05f);
	lines.line(rove::vec<3>(0, 0, 0), rove::vec<3>(1, 0, 0), rove::vec<4>(1, 0, 0, 1));
	lines.line(rove::vec<3>(0, 0, 0), rove::vec<3>(0, 1, 0), rove::vec<4>(1, 0, 0, 1));
	lines.box(rove::obb<3>(rove::aabb<3>(rove::vec<3>(0, 0, 0), rove::vec<3>(1, 1, 1))), rove::vec<4>(0, 0, 1, 1));
	lines.axes(rove::matrix<4, 4>(), 1.0f);           // red, green and blue: red and blue join their colours
	BOOST_CHECK_EQUAL(lines.segments(), 2u + 12u + 3u);
	lines.draw(*scheduler, camera);
	flush_in_a_pass(scheduler);
	BOOST_CHECK_EQUAL(scheduler->visible_calls_count(), 3u);            // red, blue, green
	BOOST_CHECK_EQUAL(lines.segments(), 0u);
	lines.draw(*scheduler, camera);                   // nothing since
	flush_in_a_pass(scheduler);
	BOOST_CHECK_EQUAL(scheduler->visible_calls_count(), 0u);
	// a line of no length is nothing to draw, and no width is refused
	lines.line(rove::vec<3>(1, 1, 1), rove::vec<3>(1, 1, 1), rove::vec<4>(1, 1, 1, 1));
	BOOST_CHECK_EQUAL(lines.segments(), 0u);
	BOOST_CHECK_THROW(DebugLines(renderer, 0.0f), std::invalid_argument);
	BOOST_CHECK_THROW(DebugLines(renderer, -1.0f), std::invalid_argument);
	BOOST_CHECK_THROW(lines.sphere(rove::vec<3>(0, 0, 0), 1.0f, rove::vec<4>(1, 1, 1, 1), 2), std::invalid_argument);
}

BOOST_AUTO_TEST_CASE(colours_alike_but_for_one_component_are_drawn_apart)
{
	// The colours are told apart by every component, the last as the first: red, red with green in it, half-transparent red, and red
	// again (joining the first).
	auto const renderer = std::make_shared<NullRenderer>(64, 64);
	auto const scheduler = std::make_shared<Scheduler>(renderer);
	DebugLines lines(renderer, 0.05f);
	lines.line(rove::vec<3>(0, 0, 0), rove::vec<3>(1, 0, 0), rove::vec<4>(1, 0, 0, 1));
	lines.line(rove::vec<3>(0, 0, 0), rove::vec<3>(1, 0, 0), rove::vec<4>(1, 0.5f, 0, 1));
	lines.line(rove::vec<3>(0, 0, 0), rove::vec<3>(1, 0, 0), rove::vec<4>(1, 0, 0, 0.5f));
	lines.line(rove::vec<3>(0, 0, 0), rove::vec<3>(0, 1, 0), rove::vec<4>(1, 0, 0, 1));
	BOOST_CHECK_EQUAL(lines.segments(), 4u);
	lines.draw(*scheduler, view());
	flush_in_a_pass(scheduler);
	BOOST_CHECK_EQUAL(scheduler->visible_calls_count(), 3u);
}

BOOST_AUTO_TEST_SUITE_END()
