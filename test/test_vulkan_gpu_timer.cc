// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

// The GPU timing contract of VulkanRenderer: one duration per begin_pass/end_pass pair, available after finish_frame,
// and nothing at all while tracing is off.

#include <boost/test/unit_test.hpp>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "pixel.h"
#include "test_vulkan_fixture.h"
#include "vulkan_gpu_timer.h"
#include "vulkan_renderer.h"

using namespace edge;

namespace
{

void render_passes(VulkanRenderer &renderer, int count)
{
	RenderTargetPtr const target = renderer.create_render_target(64, 64, PixelFormat::A8R8G8B8);
	renderer.begin_frame();
	for (int i = 0; i < count; ++i)
	{
		renderer.begin_pass(target);
		renderer.end_pass();
	}
	renderer.finish_frame();
}

}

BOOST_AUTO_TEST_SUITE(vulkan_gpu_timer)

BOOST_AUTO_TEST_CASE(tracing_is_off_by_default_and_reports_nothing)
{
	VulkanFixture f;
	BOOST_REQUIRE_MESSAGE(f.renderer, f.error);
	BOOST_CHECK(!f.renderer->gpu_trace_enabled());
	render_passes(*f.renderer, 2);
	BOOST_CHECK(f.renderer->last_pass_times_us().empty());
}

BOOST_AUTO_TEST_CASE(every_pass_gets_one_finite_non_negative_duration)
{
	VulkanFixture f;
	BOOST_REQUIRE_MESSAGE(f.renderer, f.error);
	f.renderer->set_gpu_trace_enabled(true);
	BOOST_CHECK(f.renderer->gpu_trace_enabled());
	for (int passes = 1; passes <= 4; ++passes)
	{
		render_passes(*f.renderer, passes);
		std::vector<double> const times = f.renderer->last_pass_times_us();
		BOOST_REQUIRE_EQUAL(times.size(), static_cast<size_t>(passes));
		for (double t : times)
		{
			BOOST_CHECK(std::isfinite(t));
			BOOST_CHECK_GE(t, 0.0);
		}
	}
}

BOOST_AUTO_TEST_CASE(the_times_belong_to_the_last_frame_only)
{
	VulkanFixture f;
	BOOST_REQUIRE_MESSAGE(f.renderer, f.error);
	f.renderer->set_gpu_trace_enabled(true);
	render_passes(*f.renderer, 3);
	BOOST_REQUIRE_EQUAL(f.renderer->last_pass_times_us().size(), 3u);
	render_passes(*f.renderer, 1);
	BOOST_CHECK_EQUAL(f.renderer->last_pass_times_us().size(), 1u);
}

BOOST_AUTO_TEST_CASE(passes_beyond_the_query_capacity_are_not_reported_and_do_not_break_the_frame)
{
	VulkanFixture f;
	BOOST_REQUIRE_MESSAGE(f.renderer, f.error);
	f.renderer->set_gpu_trace_enabled(true);
	render_passes(*f.renderer, 100);
	// two queries a pass
	BOOST_CHECK_EQUAL(f.renderer->last_pass_times_us().size(), size_t(VulkanGpuTimer::QUERY_CAPACITY / 2));
	render_passes(*f.renderer, 2);
	BOOST_CHECK_EQUAL(f.renderer->last_pass_times_us().size(), 2u);
}

/// The queries are reset when a frame begins: a frame that was begun untimed has none to write to.
BOOST_AUTO_TEST_CASE(tracing_is_switched_between_frames_only)
{
	VulkanFixture f;
	BOOST_REQUIRE_MESSAGE(f.renderer, f.error);
	RenderTargetPtr const target = f.renderer->create_render_target(64, 64, PixelFormat::A8R8G8B8);
	f.renderer->begin_frame();
	BOOST_CHECK_THROW(f.renderer->set_gpu_trace_enabled(true), std::logic_error);
	f.renderer->begin_pass(target);
	BOOST_CHECK_THROW(f.renderer->set_gpu_trace_enabled(true), std::logic_error);
	f.renderer->end_pass();
	f.renderer->finish_frame();
	BOOST_CHECK(!f.renderer->gpu_trace_enabled());
	f.renderer->set_gpu_trace_enabled(true);
	f.renderer->begin_frame();
	BOOST_CHECK_THROW(f.renderer->set_gpu_trace_enabled(false), std::logic_error);
	f.renderer->finish_frame();
	BOOST_CHECK(f.renderer->gpu_trace_enabled());
}

BOOST_AUTO_TEST_CASE(disabling_clears_the_times_and_stops_reporting)
{
	VulkanFixture f;
	BOOST_REQUIRE_MESSAGE(f.renderer, f.error);
	f.renderer->set_gpu_trace_enabled(true);
	render_passes(*f.renderer, 2);
	BOOST_REQUIRE_EQUAL(f.renderer->last_pass_times_us().size(), 2u);
	f.renderer->set_gpu_trace_enabled(false);
	BOOST_CHECK(f.renderer->last_pass_times_us().empty());
	render_passes(*f.renderer, 2);
	BOOST_CHECK(f.renderer->last_pass_times_us().empty());
}

BOOST_AUTO_TEST_CASE(tracing_can_be_turned_on_again_after_off)
{
	VulkanFixture f;
	BOOST_REQUIRE_MESSAGE(f.renderer, f.error);
	f.renderer->set_gpu_trace_enabled(true);
	f.renderer->set_gpu_trace_enabled(false);
	f.renderer->set_gpu_trace_enabled(true);
	render_passes(*f.renderer, 2);
	BOOST_CHECK_EQUAL(f.renderer->last_pass_times_us().size(), 2u);
}

/// A frame submitted is read back when the renderer waits for it, not before: its times come later than its submit_frame().
BOOST_AUTO_TEST_CASE(with_frames_in_flight_a_frame_s_times_come_when_it_is_waited_for)
{
	auto const renderer = std::make_shared<VulkanRenderer>(64, 64, std::string(), std::vector<std::string>(), 3);
	RenderTargetPtr const target = renderer->create_render_target(64, 64, PixelFormat::A8R8G8B8);
	renderer->set_gpu_trace_enabled(true);
	std::vector<uint64_t> frames;
	for (int i = 0; i < 2; ++i)
	{
		renderer->begin_frame();
		frames.push_back(renderer->frame_serial());
		renderer->begin_pass(target);
		renderer->end_pass();
		renderer->submit_frame();
	}
	// two of three shares used: neither frame was waited for
	BOOST_CHECK(!renderer->pass_times_us(frames[0]));
	BOOST_CHECK(!renderer->pass_times_us(frames[1]));
	BOOST_CHECK_EQUAL(renderer->last_pass_times_frame(), 0u);
	BOOST_CHECK(renderer->last_pass_times_us().empty());
	renderer->wait_frames();
	for (uint64_t frame : frames)
	{
		BOOST_REQUIRE(renderer->pass_times_us(frame));
		BOOST_CHECK_EQUAL(renderer->pass_times_us(frame)->size(), 1u);
	}
	BOOST_CHECK_EQUAL(renderer->last_pass_times_frame(), frames[1]);
}

/// A frame begun untimed and read back after tracing is turned on has no durations: it wrote no queries.
BOOST_AUTO_TEST_CASE(a_frame_in_flight_begun_untimed_gets_no_durations_when_tracing_is_on_by_its_read_back)
{
	auto const renderer = std::make_shared<VulkanRenderer>(64, 64, std::string(), std::vector<std::string>(), 2);
	RenderTargetPtr const target = renderer->create_render_target(64, 64, PixelFormat::A8R8G8B8);
	renderer->begin_frame();
	uint64_t const untimed = renderer->frame_serial();
	renderer->begin_pass(target);
	renderer->end_pass();
	renderer->submit_frame();
	renderer->set_gpu_trace_enabled(true);
	renderer->wait_frames();
	BOOST_CHECK(!renderer->pass_times_us(untimed));
	BOOST_CHECK_EQUAL(renderer->last_pass_times_frame(), 0u);
}

/// Turning tracing off drops the frames still to be read back (gpu_timer.h): frames timed and in flight when it is turned off (two, in
/// shares of their own) never get durations, though tracing is on again when they are read back.
BOOST_AUTO_TEST_CASE(frames_in_flight_when_tracing_is_turned_off_never_get_durations)
{
	auto const renderer = std::make_shared<VulkanRenderer>(64, 64, std::string(), std::vector<std::string>(), 3);
	RenderTargetPtr const target = renderer->create_render_target(64, 64, PixelFormat::A8R8G8B8);
	auto const frame = [&]
	{
		renderer->begin_frame();
		uint64_t const serial = renderer->frame_serial();
		renderer->begin_pass(target);
		renderer->end_pass();
		renderer->submit_frame();
		return serial;
	};
	renderer->set_gpu_trace_enabled(true);
	std::vector<uint64_t> const dropped = {frame(), frame()};
	renderer->set_gpu_trace_enabled(false);
	renderer->set_gpu_trace_enabled(true);
	std::vector<uint64_t> timed;
	for (int i = 0; i < 4; ++i)
		timed.push_back(frame());
	renderer->wait_frames();
	for (uint64_t serial : dropped)
	{
		BOOST_CHECK_MESSAGE(!renderer->pass_times_us(serial), "frame " << serial << ", in flight when tracing was off, has durations");
		BOOST_CHECK(!renderer->is_gpu_time_disjoint(serial));
	}
	for (uint64_t serial : timed)
		BOOST_CHECK_MESSAGE(renderer->pass_times_us(serial), "frame " << serial << " begun with tracing on has no durations");
	BOOST_CHECK_EQUAL(renderer->last_pass_times_frame(), timed.back());
}

BOOST_AUTO_TEST_SUITE_END()
