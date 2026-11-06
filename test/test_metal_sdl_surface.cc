// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

// Showing a MetalRenderer's frames in an SDL window (edge_sdl's metal_sdl_surface.h): the window's layer is the context's, frames are
// drawn into its drawables and presented, in a window that is never shown.

#include <cstdint>
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <vector>

#include <SDL3/SDL.h>
#include <boost/test/unit_test.hpp>

#include "metal_renderer.h"
#include "metal_sdl_surface.h"
#include "pixel.h"
#include "render_target.h"
#include "texture.h"

using namespace edge;

namespace
{

/// SDL's video (the Mac's own driver: a Metal layer is a window's) and a hidden window of it.
struct Fixture
{
	SDL_Window *window = nullptr;

	Fixture()
	{
		SDL_QuitSubSystem(SDL_INIT_VIDEO);          // (the video another test started on the offscreen driver has no layers)
		setenv("SDL_VIDEO_DRIVER", "cocoa", 1);
		BOOST_REQUIRE_MESSAGE(SDL_InitSubSystem(SDL_INIT_VIDEO), SDL_GetError());
		window = SDL_CreateWindow("edge", 64, 48, SDL_WINDOW_METAL | SDL_WINDOW_HIDDEN);
		BOOST_REQUIRE_MESSAGE(window, SDL_GetError());
	}

	~Fixture()
	{
		SDL_DestroyWindow(window);
		SDL_QuitSubSystem(SDL_INIT_VIDEO);
	}
};

}

BOOST_AUTO_TEST_SUITE(metal_sdl_surface)

BOOST_FIXTURE_TEST_CASE(a_window_made_for_metal_shows_frames, Fixture)
{
	MetalSDLSurface surface(window);
	BOOST_CHECK(surface.layer());
	MetalRenderer renderer(64, 48, EDGE_METAL_SHADER_DIR, surface.make_context());
	BOOST_CHECK(renderer.context()->hosted());
	RenderTargetPtr const target = renderer.create_render_target(64, 48, PixelFormat::A8R8G8B8);
	RenderPassClearState clear;
	clear.clear_color_r = 1.0f;
	clear.clear_color_g = clear.clear_color_b = 0.0f;
	renderer.begin_frame();
	renderer.begin_pass(target, clear);
	renderer.end_pass();
	renderer.finish_frame();
	// several frames, a texture of the renderer's and the host's bytes, at the window's size and not
	for (int i = 0; i < 5; ++i)
	{
		renderer.show_frame(target->color_texture(), 64, 48);
		renderer.swap_window();
		std::vector<uint8_t> const bytes(16 * 8 * 4, 0x80);
		renderer.show_frame(bytes.data(), 16, 8, 128, 96);
		renderer.swap_window();
	}
	BOOST_CHECK_THROW(renderer.swap_window(), std::logic_error);                 // nothing shown since the last
	renderer.begin_frame();
	BOOST_CHECK_THROW(renderer.show_frame(target->color_texture(), 64, 48), std::logic_error);       // not inside a frame
	renderer.finish_frame();
	BOOST_CHECK_THROW(renderer.show_frame(target->color_texture(), 0, 48), std::invalid_argument);
	BOOST_CHECK_THROW(renderer.show_frame(TexturePtr(), 64, 48), std::invalid_argument);
}

BOOST_AUTO_TEST_CASE(no_window_is_refused)
{
	BOOST_CHECK_THROW(MetalSDLSurface surface(nullptr), std::invalid_argument);
}

BOOST_AUTO_TEST_SUITE_END()
