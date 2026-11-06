// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

// Contract of GLESContext, the seam where the host gives the renderer its OpenGL ES context (edge asks no window system for one): the
// renderer works with any context that honours it, not only SDL's.  A context here is SDL's offscreen one wrapped to count what the
// renderer asks of it and to say whether it has a window; the checks read what the renderer does through its interface.

#include <boost/test/unit_test.hpp>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <string>

#include <GLES3/gl3.h>

#include "gles_renderer.h"
#include "gles_sdl_context.h"

using namespace edge;

namespace
{

/// SDL's context seen through the interface, with a window said to be there or not (swap() is counted and not forwarded: it has none).
class SpyContext: public GLESContext
{
public:
	explicit SpyContext(bool hosted): hosted_(hosted) {}

	void make_current() override { ++made_current; inner.make_current(); }
	bool hosted() const override { return hosted_; }
	void swap() override { ++swaps; }
	void *proc_address(char const *name) override { return inner.proc_address(name); }

	std::atomic<int> made_current{0};
	std::atomic<int> swaps{0};
	GLESSDLContext inner;

private:
	bool hosted_;
};

std::shared_ptr<SpyContext> spy(bool hosted)
{
	if (!std::getenv("SDL_VIDEO_DRIVER"))
		setenv("SDL_VIDEO_DRIVER", "offscreen", 1);
	return std::make_shared<SpyContext>(hosted);
}

}

BOOST_AUTO_TEST_SUITE(gles_context)

BOOST_AUTO_TEST_CASE(a_renderer_needs_a_context)
{
	BOOST_CHECK_THROW(GLESRenderer(8, 8, ".", GLESContextPtr()), std::invalid_argument);
}

BOOST_AUTO_TEST_CASE(a_renderer_works_in_any_context_the_host_gives_it)
{
	auto context = spy(false);
	GLESRenderer renderer(8, 8, ".", context);
	BOOST_TEST(renderer.context() == context);
	BOOST_TEST(context->made_current > 0, "the renderer made the host's context current to make its own objects in it");
	auto target = renderer.create_render_target(8, 8, PixelFormat::X8R8G8B8);
	renderer.begin_frame();
	renderer.begin_pass(target, RenderPassClearState());
	renderer.end_pass();
	renderer.finish_frame();
	BOOST_TEST(context->swaps == 0, "a frame is drawn, not shown");
}

BOOST_AUTO_TEST_CASE(a_context_with_no_window_shows_nothing)
{
	auto context = spy(false);
	GLESRenderer renderer(8, 8, ".", context);
	uint8_t const frame[4] = {1, 2, 3, 0};
	BOOST_CHECK_THROW(renderer.show_frame(frame, 1, 1, 8, 8), std::logic_error);
	BOOST_CHECK_THROW(renderer.swap_window(), std::logic_error);
	BOOST_TEST(context->swaps == 0);
}

BOOST_AUTO_TEST_CASE(the_window_of_a_hosted_context_is_swapped_once_for_each_swap_window)
{
	auto context = spy(true);
	GLESRenderer renderer(8, 8, ".", context);
	renderer.swap_window();
	BOOST_TEST(context->swaps == 1);
	renderer.swap_window();
	BOOST_TEST(context->swaps == 2);
}

BOOST_AUTO_TEST_CASE(extensions_are_known_once_the_renderer_has_read_them)
{
	auto context = spy(false);
	BOOST_CHECK_THROW(context->has_extension("GL_KHR_debug"), std::logic_error);       // (nobody has read them: no answer, not "none")
	GLESRenderer renderer(8, 8, ".", context);
	BOOST_TEST(!context->has_extension("GL_no_such_extension"));
}

BOOST_AUTO_TEST_CASE(a_gl_error_pending_at_the_end_of_a_frame_fails_the_frame)
{
	// (in a browser what a frame does many times over is not checked call by call, check_gl_often: an error of it still fails its frame)
	auto context = spy(false);
	GLESRenderer renderer(8, 8, ".", context);
	auto target = renderer.create_render_target(8, 8, PixelFormat::X8R8G8B8);
	renderer.begin_frame();
	renderer.begin_pass(target, RenderPassClearState());
	glEnable(0x1234);          // GL_INVALID_ENUM, behind the renderer's back
	renderer.end_pass();
	BOOST_CHECK_THROW(renderer.finish_frame(), std::runtime_error);
	BOOST_CHECK_NO_THROW(check_gl("the next call"));          // (read once: GL keeps an error till it is read)
}

BOOST_AUTO_TEST_CASE(what_a_frame_does_often_is_checked_call_by_call_off_a_browser)
{
	auto context = spy(false);
	GLESRenderer renderer(8, 8, ".", context);
	BOOST_CHECK_NO_THROW(check_gl_often("nothing wrong"));
	glEnable(0x1234);
	try
	{
		check_gl_often("a draw call");
		BOOST_FAIL("an error pending, and check_gl_often let it be");
	}
	catch (std::runtime_error const &e)
	{
		BOOST_TEST(std::string(e.what()) == "OpenGL ES error 0x500 in a draw call");
	}
}

BOOST_AUTO_TEST_CASE(a_failing_context_fails_the_renderer_with_its_message)
{
	struct Failing: GLESContext
	{
		void make_current() override { throw std::runtime_error("the host's context is gone"); }
		bool hosted() const override { return false; }
		void swap() override {}
		void *proc_address(char const *) override { return nullptr; }
	};
	try
	{
		GLESRenderer(8, 8, ".", std::make_shared<Failing>());
		BOOST_FAIL("a renderer in a context that cannot be made current");
	}
	catch (std::runtime_error const &e)
	{
		BOOST_TEST(std::string(e.what()) == "the host's context is gone");
	}
}

BOOST_AUTO_TEST_SUITE_END()
