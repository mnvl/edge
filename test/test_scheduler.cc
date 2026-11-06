// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include <algorithm>
#include <functional>
#include <memory>
#include <vector>

#include <boost/test/unit_test.hpp>

#include "test_fixture.h"

#include "scheduler.h"
#include "draw_call.h"
#include "camera.h"
#include "material.h"
#include "null_renderer.h"

namespace
{

struct SchedulerFixture
{
	edge::RendererPtr renderer;
	edge::SchedulerPtr scheduler;

	SchedulerFixture()
	{
		renderer = std::make_shared<edge::NullRenderer>(320, 200);
		scheduler = std::make_shared<edge::Scheduler>(renderer);
	}

	edge::DrawCallPtr make_visible_draw_call(int priority = 0)
	{
		edge::DrawCallPtr dc = renderer->create_draw_call();
		dc->set_always_visible(true);
		dc->set_priority(priority);
		dc->set_vertex_buffer(renderer->create_vertex_buffer(nullptr, 1));
		dc->set_material(renderer->create_material());

		edge::CameraPtr camera(new edge::Camera());
		camera->set_perspective_projection(1.0f, 1.0f, 0.1f, 100.0f);
		camera->look_at(
			rove::vec<3>(0, 0, 5),
			rove::vec<3>(0, 0, 0),
			rove::vec<3>(0, 1, 0));
		dc->set_camera(camera);
		dc->commit();
		return dc;
	}

	edge::DrawCallPtr make_invisible_draw_call(int priority = 0)
	{
		edge::DrawCallPtr dc = renderer->create_draw_call();
		dc->set_always_visible(false);
		dc->set_priority(priority);
		dc->set_vertex_buffer(renderer->create_vertex_buffer(nullptr, 1));
		dc->set_material(renderer->create_material());

		edge::CameraPtr camera(new edge::Camera());
		camera->set_perspective_projection(1.0f, 1.0f, 0.1f, 100.0f);
		camera->look_at(
			rove::vec<3>(0, 0, 5),
			rove::vec<3>(0, 0, 0),
			rove::vec<3>(0, 1, 0));
		dc->set_camera(camera);

		// Place OBB far outside the camera frustum via obb
		rove::obb<3> far_obb(
			rove::vec<3>(10000.0f, 10000.0f, 10000.0f),
			rove::vec<3>(1.0f, 0.0f, 0.0f),
			rove::vec<3>(0.0f, 1.0f, 0.0f),
			rove::vec<3>(0.0f, 0.0f, 1.0f));
		dc->set_obb(far_obb);
		dc->commit();
		return dc;
	}
};

}

BOOST_AUTO_TEST_SUITE(scheduler)

BOOST_FIXTURE_TEST_CASE(empty_flush, SchedulerFixture)
{
	edge::flush_in_a_pass(scheduler);
	BOOST_CHECK_EQUAL(scheduler->visible_calls_count(), 0u);
	BOOST_CHECK_EQUAL(scheduler->invisible_calls_count(), 0u);
}

BOOST_FIXTURE_TEST_CASE(visible_call_counted, SchedulerFixture)
{
	scheduler->add(make_visible_draw_call());
	edge::flush_in_a_pass(scheduler);
	BOOST_CHECK_EQUAL(scheduler->visible_calls_count(), 1u);
	BOOST_CHECK_EQUAL(scheduler->invisible_calls_count(), 0u);
}

BOOST_FIXTURE_TEST_CASE(invisible_call_counted, SchedulerFixture)
{
	scheduler->add(make_invisible_draw_call());
	edge::flush_in_a_pass(scheduler);
	BOOST_CHECK_EQUAL(scheduler->visible_calls_count(), 0u);
	BOOST_CHECK_EQUAL(scheduler->invisible_calls_count(), 1u);
}

BOOST_FIXTURE_TEST_CASE(mixed_visible_invisible, SchedulerFixture)
{
	scheduler->add(make_visible_draw_call());
	scheduler->add(make_visible_draw_call());
	scheduler->add(make_invisible_draw_call());
	edge::flush_in_a_pass(scheduler);
	BOOST_CHECK_EQUAL(scheduler->visible_calls_count(), 2u);
	BOOST_CHECK_EQUAL(scheduler->invisible_calls_count(), 1u);
}

BOOST_FIXTURE_TEST_CASE(flush_clears_queue, SchedulerFixture)
{
	scheduler->add(make_visible_draw_call());
	edge::flush_in_a_pass(scheduler);
	BOOST_CHECK_EQUAL(scheduler->visible_calls_count(), 1u);

	// Second flush with no new adds
	edge::flush_in_a_pass(scheduler);
	BOOST_CHECK_EQUAL(scheduler->visible_calls_count(), 0u);
	BOOST_CHECK_EQUAL(scheduler->invisible_calls_count(), 0u);
}

namespace
{

/// What it is given to draw, in the order it is given it.
struct RecordingRenderer: edge::NullRenderer
{
	std::vector<edge::Material const *> drawn;

	void do_draw(edge::DrawCall const &call) override { drawn.push_back(call.material().get()); }
};

}

/// Lower priority first; within a priority, the order the calls were added.
/// Which of two coplanar surfaces a pixel shows depends on the order they
/// are drawn in, so any other tie-break -- the material's address, as it
/// used to be -- renders one scene differently from one run to the next.
BOOST_FIXTURE_TEST_CASE(calls_execute_by_priority_then_in_the_order_added, SchedulerFixture)
{
	int const priorities[] = {1, 0, 1, 0, 1, 1};
	std::size_t const count = sizeof(priorities) / sizeof(priorities[0]);

	// Materials handed out highest address first, so ordering by material
	// pointer would reverse the order within each priority.
	std::vector<edge::MaterialPtr> materials;
	for (std::size_t i = 0; i < count; ++i)
		materials.push_back(renderer->create_material());
	std::sort(materials.begin(), materials.end(), std::greater<edge::MaterialPtr>());

	auto const recording = std::make_shared<RecordingRenderer>();
	auto const recorded = std::make_shared<edge::Scheduler>(recording);
	edge::CameraPtr camera(new edge::Camera());
	for (std::size_t i = 0; i < count; ++i)
	{
		edge::DrawCallPtr dc = recording->create_draw_call();
		dc->set_priority(priorities[i]);
		dc->set_always_visible(true);
		dc->set_vertex_buffer(recording->create_vertex_buffer(nullptr, 1));
		dc->set_material(materials[i]);
		dc->set_camera(camera);
		dc->set_triangles_count(1);
		dc->commit();
		recorded->add(dc);
	}
	edge::flush_in_a_pass(recorded);
	std::vector<int> log;
	for (edge::Material const *drawn : recording->drawn)
		log.push_back(int(std::find_if(materials.begin(), materials.end(), [&](auto const &m) { return m.get() == drawn; }) - materials.begin()));

	std::vector<int> const expected = {1, 3, 0, 2, 4, 5};
	BOOST_CHECK_EQUAL_COLLECTIONS(log.begin(), log.end(), expected.begin(), expected.end());
}

BOOST_FIXTURE_TEST_CASE(multiple_calls_sorted_by_priority, SchedulerFixture)
{
	scheduler->add(make_visible_draw_call(2));
	scheduler->add(make_visible_draw_call(0));
	scheduler->add(make_visible_draw_call(1));
	edge::flush_in_a_pass(scheduler);
	// All visible regardless of priority
	BOOST_CHECK_EQUAL(scheduler->visible_calls_count(), 3u);
}

BOOST_FIXTURE_TEST_CASE(executed_triangles_count_visible_calls_only, SchedulerFixture)
{
	BOOST_CHECK_EQUAL(scheduler->executed_triangles_count(), 0u);
	edge::DrawCallPtr const seen = make_visible_draw_call();
	seen->set_triangles_count(12);
	edge::DrawCallPtr const culled = make_invisible_draw_call();
	culled->set_triangles_count(1000);
	scheduler->add(seen);
	scheduler->add(culled);
	edge::flush_in_a_pass(scheduler);
	BOOST_CHECK_EQUAL(scheduler->executed_triangles_count(), 12u);
}

BOOST_FIXTURE_TEST_CASE(executed_triangles_add_up_over_flushes, SchedulerFixture)
{
	// the count is of every flush since the scheduler was made (a frame's several passes), not of the last one
	edge::DrawCallPtr const dc = make_visible_draw_call();
	dc->set_triangles_count(5);
	scheduler->add(dc);
	edge::flush_in_a_pass(scheduler, false);
	edge::flush_in_a_pass(scheduler, false);
	edge::flush_in_a_pass(scheduler);
	BOOST_CHECK_EQUAL(scheduler->executed_triangles_count(), 15u);
	edge::flush_in_a_pass(scheduler);      // nothing queued any more
	BOOST_CHECK_EQUAL(scheduler->executed_triangles_count(), 15u);
}

BOOST_FIXTURE_TEST_CASE(executed_calls_add_up_over_flushes, SchedulerFixture)
{
	scheduler->add(make_visible_draw_call());
	scheduler->add(make_visible_draw_call());
	edge::flush_in_a_pass(scheduler, false);
	edge::flush_in_a_pass(scheduler);
	BOOST_CHECK_EQUAL(scheduler->executed_calls_count(), 4u);
}

BOOST_FIXTURE_TEST_CASE(offered_counts_include_culled_calls, SchedulerFixture)
{
	// "offered" is everything a flush was given, the culled calls too: drawn / offered is what culling saved
	BOOST_CHECK_EQUAL(scheduler->offered_calls_count(), 0u);
	BOOST_CHECK_EQUAL(scheduler->offered_triangles_count(), 0u);
	edge::DrawCallPtr const seen = make_visible_draw_call();
	seen->set_triangles_count(12);
	edge::DrawCallPtr const culled = make_invisible_draw_call();
	culled->set_triangles_count(1000);
	scheduler->add(seen);
	scheduler->add(culled);
	edge::flush_in_a_pass(scheduler, false);
	BOOST_CHECK_EQUAL(scheduler->offered_calls_count(), 2u);
	BOOST_CHECK_EQUAL(scheduler->offered_triangles_count(), 1012u);
	BOOST_CHECK_EQUAL(scheduler->executed_calls_count(), 1u);
	BOOST_CHECK_EQUAL(scheduler->executed_triangles_count(), 12u);
	edge::flush_in_a_pass(scheduler);     // the same two calls again, as a second pass over the same geometry
	BOOST_CHECK_EQUAL(scheduler->offered_calls_count(), 4u);
	BOOST_CHECK_EQUAL(scheduler->offered_triangles_count(), 2024u);
	edge::flush_in_a_pass(scheduler);     // nothing queued
	BOOST_CHECK_EQUAL(scheduler->offered_triangles_count(), 2024u);
}

BOOST_AUTO_TEST_SUITE_END()
