// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

// GPU timing, the same on every renderer (gpu_timer.h, Renderer::gpu_trace_enabled): a frame's pass durations come once the GPU has
// drawn it, asked for by the frame's number, never before the frame is ended; a renderer that cannot time says so and refuses to be
// asked to; a frame the GPU's clock was disturbed in has no durations at all, never numbers.

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <boost/test/unit_test.hpp>

#include "camera.h"
#include "draw_call.h"
#include "gpu_timer.h"
#include "material.h"
#include "pixel.h"
#include "render_target.h"
#include "test_backends.h"
#include "vertex.h"
#include "vertex_traits.h"
#ifdef EDGE_USE_GLES
#include "gles_gpu_timer.h"
#include "gles_sdl_context.h"
#endif

using namespace edge;

namespace
{

/// A timer whose GPU is a script: the test says what each frame's durations were, or that its clock was disturbed.
class ScriptedTimer: public GpuTimer
{
public:
	explicit ScriptedTimer(std::string const &unavailable = std::string()): GpuTimer(unavailable) {}
	using GpuTimer::record;
	using GpuTimer::record_disjoint;
	int starts = 0, stops = 0;

protected:
	void start() override { ++starts; }
	void stop() override { ++stops; }
};

/// A square over the whole view of a default camera (which looks down +z from the origin), white, blended over what is there.
DrawCallPtr cover(Renderer &renderer)
{
	VertexBufferPtr const vb = renderer.create_vertex_buffer(VertexTraits::of<VERTEX_PT>(), sizeof(VERTEX_PT) * 6);
	auto vb_mapping = vb->write(Contents::DISCARD);
	auto *v = vb_mapping.as<VERTEX_PT>().data();
	float const corners[6][2] = {{-50, -50}, {50, -50}, {50, 50}, {-50, -50}, {50, 50}, {-50, 50}};
	for (int i = 0; i < 6; ++i)
	{
		v[i].position = rove::vec<3>(corners[i][0], corners[i][1], 5.0f);
		v[i].texcoords = rove::vec<2>(0, 0);
	}
	vb_mapping.unmap();
	MaterialPtr const material = renderer.create_material();
	material->set_cull_mode(Material::CullMode::NONE);
	material->set_alpha_blend_enabled(true);
	material->set_base_color_factor(rove::vec<4>(1, 1, 1, 0.01f));
	DrawCallPtr const draw = renderer.create_draw_call();
	draw->set_vertex_buffer(vb);
	draw->set_material(material);
	draw->set_camera(std::make_shared<Camera>());
	draw->set_triangles_count(2);
	draw->set_always_visible(true);
	draw->commit();
	return draw;
}

/// One frame of `passes` passes into `target`, none drawing anything; its number.
uint64_t empty_frame(Renderer &renderer, RenderTargetPtr const &target, int passes)
{
	renderer.begin_frame();
	uint64_t const frame = renderer.frame_serial();
	for (int i = 0; i < passes; ++i)
	{
		renderer.begin_pass(target);
		renderer.end_pass();
	}
	renderer.finish_frame();
	return frame;
}

/// The durations of `frame`, drawing empty frames until they come: none if they have not after as many frames as are kept.
std::optional<std::vector<double>> times_of(Renderer &renderer, RenderTargetPtr const &target, uint64_t frame)
{
	for (size_t waited = 0; waited < GpuTimer::HISTORY / 2; ++waited)
	{
		if (std::optional<std::vector<double>> times = renderer.pass_times_us(frame))
			return times;
		empty_frame(renderer, target, 0);
	}
	return renderer.pass_times_us(frame);
}

}

BOOST_AUTO_TEST_SUITE(gpu_timer)

BOOST_AUTO_TEST_CASE(a_timer_that_cannot_time_refuses_to_be_turned_on_and_says_why)
{
	ScriptedTimer timer("no timestamps on this queue");
	BOOST_CHECK(!timer.available());
	BOOST_CHECK_EQUAL(timer.unavailable_reason(), "no timestamps on this queue");
	try
	{
		timer.set_enabled(true);
		BOOST_ERROR("turned on");
	}
	catch (std::runtime_error const &e)
	{
		BOOST_CHECK_MESSAGE(std::string(e.what()).find("no timestamps on this queue") != std::string::npos, e.what());
	}
	BOOST_CHECK(!timer.enabled());
	BOOST_CHECK_EQUAL(timer.starts, 0);
	timer.set_enabled(false);        // it is off: nothing to refuse
	BOOST_CHECK(!timer.enabled());
	BOOST_CHECK(timer.last_pass_times_us().empty());
	BOOST_CHECK(!timer.pass_times_us(1));
}

BOOST_AUTO_TEST_CASE(a_timer_is_started_and_stopped_when_it_is_switched)
{
	ScriptedTimer timer;
	BOOST_CHECK(timer.available());
	BOOST_CHECK(timer.unavailable_reason().empty());
	BOOST_CHECK(!timer.enabled());
	timer.set_enabled(true);
	timer.set_enabled(true);
	BOOST_CHECK(timer.enabled());
	BOOST_CHECK_EQUAL(timer.starts, 1);
	BOOST_CHECK_EQUAL(timer.stops, 0);
	timer.set_enabled(false);
	timer.set_enabled(false);
	BOOST_CHECK_EQUAL(timer.stops, 1);
	timer.set_enabled(true);
	BOOST_CHECK_EQUAL(timer.starts, 2);
}

BOOST_AUTO_TEST_CASE(durations_are_kept_by_frame_and_the_last_are_those_of_the_latest_frame)
{
	ScriptedTimer timer;
	timer.set_enabled(true);
	BOOST_CHECK_EQUAL(timer.last_frame(), 0u);
	timer.record(5, {1.0, 2.0});
	timer.record(4, {9.0});       // read back after a later frame (frames in flight finish in order, but a caller may not ask in order)
	BOOST_REQUIRE(timer.pass_times_us(5));
	BOOST_CHECK(*timer.pass_times_us(5) == std::vector<double>({1.0, 2.0}));
	BOOST_REQUIRE(timer.pass_times_us(4));
	BOOST_CHECK(*timer.pass_times_us(4) == std::vector<double>({9.0}));
	BOOST_CHECK_EQUAL(timer.last_frame(), 5u);
	BOOST_CHECK(timer.last_pass_times_us() == std::vector<double>({1.0, 2.0}));
	BOOST_CHECK(!timer.pass_times_us(6));
	BOOST_CHECK(!timer.disjoint(5));
}

BOOST_AUTO_TEST_CASE(a_disjoint_frame_has_no_durations_and_leaves_the_last_ones)
{
	ScriptedTimer timer;
	timer.set_enabled(true);
	timer.record(5, {1.0, 2.0});
	timer.record_disjoint(6);
	BOOST_CHECK(timer.disjoint(6));
	BOOST_CHECK(!timer.pass_times_us(6));
	BOOST_CHECK(!timer.disjoint(5));
	BOOST_CHECK(!timer.disjoint(7));        // not read back: not known to be disjoint
	BOOST_CHECK_EQUAL(timer.last_frame(), 5u);
	BOOST_CHECK(timer.last_pass_times_us() == std::vector<double>({1.0, 2.0}));
	timer.record(7, {3.0});
	BOOST_CHECK_EQUAL(timer.last_frame(), 7u);
	BOOST_CHECK(timer.disjoint(6));
}

BOOST_AUTO_TEST_CASE(only_the_last_history_frames_are_kept_disjoint_ones_among_them)
{
	ScriptedTimer timer;
	timer.set_enabled(true);
	timer.record(1, {1.0});
	timer.record_disjoint(2);
	for (uint64_t frame = 3; frame < 1 + GpuTimer::HISTORY; ++frame)
		timer.record(frame, {double(frame)});
	BOOST_CHECK(timer.pass_times_us(1));          // HISTORY frames read back: the first is still there
	BOOST_CHECK(timer.disjoint(2));
	timer.record(1 + GpuTimer::HISTORY, {0.5});
	BOOST_CHECK(!timer.pass_times_us(1));
	BOOST_CHECK(timer.disjoint(2));
	timer.record(2 + GpuTimer::HISTORY, {0.5});
	BOOST_CHECK(!timer.disjoint(2));
	BOOST_CHECK(timer.pass_times_us(3));
}

BOOST_AUTO_TEST_CASE(turning_a_timer_off_drops_what_it_kept)
{
	ScriptedTimer timer;
	timer.set_enabled(true);
	timer.record(5, {1.0});
	timer.record_disjoint(6);
	timer.set_enabled(false);
	BOOST_CHECK(timer.last_pass_times_us().empty());
	BOOST_CHECK_EQUAL(timer.last_frame(), 0u);
	BOOST_CHECK(!timer.pass_times_us(5));
	BOOST_CHECK(!timer.disjoint(6));
}

BOOST_AUTO_TEST_CASE(the_null_renderer_has_no_gpu_timer_and_says_so)
{
	NullFixture fixture;
	Renderer &renderer = *fixture.renderer;
	BOOST_CHECK(!renderer.has_gpu_timer());
	BOOST_CHECK_THROW(renderer.set_gpu_trace_enabled(true), std::runtime_error);
	BOOST_CHECK(!renderer.gpu_trace_enabled());
	renderer.set_gpu_trace_enabled(false);
	RenderTargetPtr const target = renderer.create_render_target(8, 8, PixelFormat::A8R8G8B8);
	uint64_t const frame = empty_frame(renderer, target, 2);
	BOOST_CHECK_EQUAL(frame, 1u);
	BOOST_CHECK_EQUAL(empty_frame(renderer, target, 0), 2u);
	BOOST_CHECK(!renderer.pass_times_us(frame));
	BOOST_CHECK(renderer.last_pass_times_us().empty());
	BOOST_CHECK_EQUAL(renderer.last_pass_times_frame(), 0u);
}

BOOST_AUTO_TEST_CASE_TEMPLATE(frames_are_numbered_from_one_on_every_renderer, F, AllBackends)
{
	F fixture;
	auto const &renderer = fixture.renderer;
	BOOST_REQUIRE_MESSAGE(renderer, "renderer unavailable: " << fixture.error);
	BOOST_CHECK_EQUAL(renderer->frame_serial(), 0u);
	renderer->begin_frame();
	BOOST_CHECK_EQUAL(renderer->frame_serial(), 1u);
	renderer->submit_frame();
	BOOST_CHECK_EQUAL(renderer->frame_serial(), 1u);
	BOOST_CHECK_THROW(renderer->finish_frame(), std::logic_error);     // refused: not a frame
	renderer->begin_frame();
	renderer->finish_frame();
	BOOST_CHECK_EQUAL(renderer->frame_serial(), 2u);
}

BOOST_AUTO_TEST_CASE_TEMPLATE(tracing_is_switched_between_frames_only, F, AllBackends)
{
	F fixture;
	auto const &renderer = fixture.renderer;
	BOOST_REQUIRE_MESSAGE(renderer, "renderer unavailable: " << fixture.error);
	renderer->begin_frame();
	BOOST_CHECK_THROW(renderer->set_gpu_trace_enabled(true), std::logic_error);
	BOOST_CHECK_THROW(renderer->set_gpu_trace_enabled(false), std::logic_error);
	renderer->finish_frame();
	BOOST_CHECK(!renderer->gpu_trace_enabled());
}

BOOST_AUTO_TEST_CASE_TEMPLATE(a_gpu_renderer_has_a_timer_here, F, Backends)
{
	// (this machine's GPUs: Vulkan's queue writes timestamps, and its OpenGL ES has GL_EXT_disjoint_timer_query)
	F fixture;
	auto const &renderer = fixture.renderer;
	BOOST_REQUIRE_MESSAGE(renderer, "renderer unavailable: " << fixture.error);
	BOOST_CHECK(renderer->has_gpu_timer());
	BOOST_CHECK(!renderer->gpu_trace_enabled());
	renderer->set_gpu_trace_enabled(true);
	BOOST_CHECK(renderer->gpu_trace_enabled());
}

BOOST_AUTO_TEST_CASE_TEMPLATE(a_timed_frame_s_durations_come_after_it_is_ended_one_for_each_pass, F, Backends)
{
	F fixture;
	auto const &renderer = fixture.renderer;
	BOOST_REQUIRE_MESSAGE(renderer, "renderer unavailable: " << fixture.error);
	RenderTargetPtr const target = renderer->create_render_target(64, 64, PixelFormat::A8R8G8B8);
	renderer->set_gpu_trace_enabled(true);
	renderer->begin_frame();
	uint64_t const frame = renderer->frame_serial();
	for (int i = 0; i < 3; ++i)
	{
		renderer->begin_pass(target);
		renderer->end_pass();
	}
	BOOST_CHECK(!renderer->pass_times_us(frame));        // not drawn: it is not even submitted
	renderer->finish_frame();
	std::optional<std::vector<double>> const times = times_of(*renderer, target, frame);
	BOOST_REQUIRE_MESSAGE(times, "the durations of frame " << frame << " never came");
	BOOST_REQUIRE_EQUAL(times->size(), 3u);
	for (double t : *times)
	{
		BOOST_CHECK(std::isfinite(t));
		BOOST_CHECK_GE(t, 0.0);
	}
	BOOST_CHECK(!renderer->is_gpu_time_disjoint(frame));
	BOOST_CHECK_GE(renderer->last_pass_times_frame(), frame);
	BOOST_CHECK(*renderer->pass_times_us(renderer->last_pass_times_frame()) == renderer->last_pass_times_us());
}

BOOST_AUTO_TEST_CASE_TEMPLATE(a_frame_begun_untimed_never_gets_durations, F, Backends)
{
	F fixture;
	auto const &renderer = fixture.renderer;
	BOOST_REQUIRE_MESSAGE(renderer, "renderer unavailable: " << fixture.error);
	RenderTargetPtr const target = renderer->create_render_target(64, 64, PixelFormat::A8R8G8B8);
	uint64_t const untimed = empty_frame(*renderer, target, 2);
	renderer->set_gpu_trace_enabled(true);
	uint64_t const timed = empty_frame(*renderer, target, 2);
	BOOST_CHECK(times_of(*renderer, target, timed));
	BOOST_CHECK(!renderer->pass_times_us(untimed));
	BOOST_CHECK(!renderer->is_gpu_time_disjoint(untimed));
}

BOOST_AUTO_TEST_CASE_TEMPLATE(passes_past_the_most_a_frame_times_are_drawn_untimed, F, Backends)
{
	F fixture;
	auto const &renderer = fixture.renderer;
	BOOST_REQUIRE_MESSAGE(renderer, "renderer unavailable: " << fixture.error);
	RenderTargetPtr const target = renderer->create_render_target(64, 64, PixelFormat::A8R8G8B8);
	renderer->set_gpu_trace_enabled(true);
	uint64_t const many = empty_frame(*renderer, target, GpuTimer::MAX_PASSES + 9);
	uint64_t const few = empty_frame(*renderer, target, 2);
	std::optional<std::vector<double>> const many_times = times_of(*renderer, target, many);
	BOOST_REQUIRE(many_times);
	BOOST_CHECK_EQUAL(many_times->size(), size_t(GpuTimer::MAX_PASSES));
	std::optional<std::vector<double>> const few_times = times_of(*renderer, target, few);
	BOOST_REQUIRE(few_times);
	BOOST_CHECK_EQUAL(few_times->size(), 2u);
}

BOOST_AUTO_TEST_CASE_TEMPLATE(turning_tracing_off_drops_the_durations_and_times_nothing, F, Backends)
{
	F fixture;
	auto const &renderer = fixture.renderer;
	BOOST_REQUIRE_MESSAGE(renderer, "renderer unavailable: " << fixture.error);
	RenderTargetPtr const target = renderer->create_render_target(64, 64, PixelFormat::A8R8G8B8);
	renderer->set_gpu_trace_enabled(true);
	uint64_t const timed = empty_frame(*renderer, target, 2);
	BOOST_REQUIRE(times_of(*renderer, target, timed));
	renderer->set_gpu_trace_enabled(false);
	BOOST_CHECK(!renderer->pass_times_us(timed));
	BOOST_CHECK(renderer->last_pass_times_us().empty());
	BOOST_CHECK_EQUAL(renderer->last_pass_times_frame(), 0u);
	uint64_t const untimed = empty_frame(*renderer, target, 2);
	empty_frame(*renderer, target, 0);
	BOOST_CHECK(!renderer->pass_times_us(untimed));
	BOOST_CHECK(renderer->last_pass_times_us().empty());
	// and on again
	renderer->set_gpu_trace_enabled(true);
	BOOST_CHECK(times_of(*renderer, target, empty_frame(*renderer, target, 1)));
}

#ifdef EDGE_USE_GLES

namespace
{

/// SDL's context, giving the renderer no function it asks for by name: an OpenGL ES whose timer extension is listed but whose
/// glGetQueryObjectui64vEXT cannot be had.
class NoProcContext: public GLESContext
{
public:
	void make_current() override { inner.make_current(); }
	bool hosted() const override { return false; }
	void swap() override { throw std::logic_error("no window"); }
	void *proc_address(char const *) override { return nullptr; }
	GLESSDLContext inner;
};

/// OpenGL ES's timer, its queries real, whose GPU's clock is disturbed when the test says (GL_GPU_DISJOINT_EXT cannot be made to be
/// set).
class DisturbedTimer: public GLESGpuTimer
{
public:
	using GLESGpuTimer::GLESGpuTimer;
	bool disturb = false;

protected:
	bool disjoint_happened() override { return GLESGpuTimer::disjoint_happened() || std::exchange(disturb, false); }
};

/// A frame of `passes` empty passes timed by `timer` in the current context, done (glFinish) when it ends.
void gles_frame(GLESGpuTimer &timer, uint64_t frame, int passes)
{
	timer.begin_frame(frame);
	for (int i = 0; i < passes; ++i)
	{
		timer.begin_pass();
		glClear(GL_COLOR_BUFFER_BIT);
		timer.end_pass();
	}
	glFinish();
	timer.end_frame();
}

}

BOOST_AUTO_TEST_CASE(a_gles_frame_the_gpu_clock_was_disturbed_in_is_disjoint_and_has_no_durations)
{
	if (!std::getenv("SDL_VIDEO_DRIVER"))
		setenv("SDL_VIDEO_DRIVER", "offscreen", 1);
	GLESSDLContext context;
	context.make_current();
	context.load_extensions();
	DisturbedTimer timer(context);
	BOOST_REQUIRE_MESSAGE(timer.available(), timer.unavailable_reason());
	timer.set_enabled(true);
	gles_frame(timer, 1, 2);
	BOOST_REQUIRE(timer.pass_times_us(1));        // done when it ended: read then
	BOOST_CHECK_EQUAL(timer.pass_times_us(1)->size(), 2u);
	timer.disturb = true;
	gles_frame(timer, 2, 2);
	BOOST_CHECK(timer.disjoint(2));
	BOOST_CHECK(!timer.pass_times_us(2));
	BOOST_CHECK_EQUAL(timer.last_frame(), 1u);
	gles_frame(timer, 3, 1);
	BOOST_REQUIRE(timer.pass_times_us(3));
	BOOST_CHECK_EQUAL(timer.pass_times_us(3)->size(), 1u);
	BOOST_CHECK(!timer.disjoint(3));
	BOOST_CHECK(timer.disjoint(2));
	// a frame still waiting when the disturbance is read, when the next begins, is disjoint; the next has its durations
	timer.begin_frame(4);
	timer.begin_pass();
	timer.end_pass();
	timer.end_frame();                              // (no glFinish: maybe not done, and then read when the next begins)
	timer.disturb = true;
	gles_frame(timer, 5, 1);
	BOOST_CHECK(timer.pass_times_us(4) || timer.disjoint(4));     // read when it ended, or disjoint
	BOOST_CHECK(!(timer.pass_times_us(4) && timer.disjoint(4)));
	BOOST_CHECK(timer.pass_times_us(5));
	BOOST_CHECK(!timer.disjoint(5));
}

BOOST_AUTO_TEST_CASE(a_gles_context_without_the_timer_query_function_has_no_gpu_timer_and_says_why)
{
	if (!std::getenv("SDL_VIDEO_DRIVER"))
		setenv("SDL_VIDEO_DRIVER", "offscreen", 1);
	auto const context = std::make_shared<NoProcContext>();
	GLESRenderer renderer(64, 64, EDGE_GLES_SHADER_DIR, context);
	BOOST_CHECK(!renderer.has_gpu_timer());
	try
	{
		renderer.set_gpu_trace_enabled(true);
		BOOST_ERROR("turned on");
	}
	catch (std::runtime_error const &e)
	{
		BOOST_CHECK_MESSAGE(std::string(e.what()).find("glGetQueryObjectui64vEXT") != std::string::npos, e.what());
	}
	BOOST_CHECK(!renderer.gpu_trace_enabled());
	RenderTargetPtr const target = renderer.create_render_target(8, 8, PixelFormat::A8R8G8B8);
	uint64_t const frame = empty_frame(renderer, target, 2);
	BOOST_CHECK(!renderer.pass_times_us(frame));
}

#endif

BOOST_AUTO_TEST_SUITE_END()

// Not among ctest's tests, as dds_speed is not (test/CMakeLists.txt; the target edge_performance_tests runs it): what it measures is a
// GPU's clock against its work.  llvmpipe's GL_TIME_ELAPSED does not measure rasterization (8.9 ms for 200 full-screen blends on 4 CPU
// cores, against 4.3 ms for an empty pass), so on the software renderers CI has, it fails with nothing wrong.
BOOST_AUTO_TEST_SUITE(gpu_timer_speed)

BOOST_AUTO_TEST_CASE_TEMPLATE(a_pass_that_draws_more_takes_measurably_longer, F, Backends)
{
	// three passes into a 1024 x 1024 target, the middle one blending a square over all of it 200 times, the others drawing nothing:
	// the middle one is the longest by far, and it is the one in the middle of the durations (begin_pass order).  The first is the
	// empty one compared: on Vulkan the pass after one that draws may take in its end (its TOP_OF_PIPE timestamp does not wait for the
	// fragments before it), as it did once in ten frames here.
	F fixture;
	auto const &renderer = fixture.renderer;
	BOOST_REQUIRE_MESSAGE(renderer, "renderer unavailable: " << fixture.error);
	RenderTargetPtr const target = renderer->create_render_target(1024, 1024, PixelFormat::A8R8G8B8);
	DrawCallPtr const square = cover(*renderer);
	renderer->set_gpu_trace_enabled(true);
	std::vector<double> empty, drawn;
	for (int round = 0; round < 5; ++round)
	{
		renderer->begin_frame();
		uint64_t const frame = renderer->frame_serial();
		renderer->begin_pass(target);
		renderer->end_pass();
		renderer->begin_pass(target);
		for (int i = 0; i < 200; ++i)
			square->execute();
		renderer->end_pass();
		renderer->begin_pass(target);
		renderer->end_pass();
		renderer->finish_frame();
		std::optional<std::vector<double>> const times = times_of(*renderer, target, frame);
		BOOST_REQUIRE_MESSAGE(times, "the durations of frame " << frame << " never came");
		BOOST_REQUIRE_EQUAL(times->size(), 3u);
		BOOST_TEST_MESSAGE("passes took " << (*times)[0] << ", " << (*times)[1] << ", " << (*times)[2] << " us");
		empty.push_back((*times)[0]);
		drawn.push_back((*times)[1]);
	}
	std::sort(empty.begin(), empty.end());
	std::sort(drawn.begin(), drawn.end());
	double const empty_median = empty[empty.size() / 2], drawn_median = drawn[drawn.size() / 2];
	BOOST_CHECK_MESSAGE(drawn_median > 10.0 * empty_median && drawn_median > empty_median + 200.0,
		"the pass that draws took " << drawn_median << " us, the empty ones " << empty_median << " us");
}

BOOST_AUTO_TEST_SUITE_END()
