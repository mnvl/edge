// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

// Showing frames in an SDL window with Vulkan (edge_sdl's vulkan_sdl_surface.h, edge's frame_presenter.h), in windows of SDL's offscreen
// video driver, whose Vulkan surface is a headless one: nothing is seen, everything is made, presented and read back.

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <SDL3/SDL.h>
#include <boost/test/unit_test.hpp>

#include "frame_presenter.h"
#include "pixel.h"
#include "render_target.h"
#include "texture.h"
#include "vulkan_renderer.h"
#include "vulkan_sdl_surface.h"

using namespace edge;

namespace
{

using Bgra = std::array<uint8_t, 4>;

/// SDL's video on the offscreen driver, and a window of it to make a surface of.
struct Fixture
{
	SDL_Window *window = nullptr;

	Fixture()
	{
		setenv("SDL_VIDEO_DRIVER", "offscreen", 1);
		BOOST_REQUIRE_MESSAGE(SDL_InitSubSystem(SDL_INIT_VIDEO), SDL_GetError());
		window = SDL_CreateWindow("edge", 64, 48, SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN);
		BOOST_REQUIRE_MESSAGE(window, SDL_GetError());
	}

	~Fixture()
	{
		SDL_DestroyWindow(window);
		SDL_QuitSubSystem(SDL_INIT_VIDEO);
	}

	/// A renderer that can make this window's surface, the surface attached.
	VulkanRendererPtr attached(bool vsync = false)
	{
		auto const renderer = std::make_shared<VulkanRenderer>(64, 48, std::string(), sdl_vulkan_instance_extensions());
		attach_sdl_window(*renderer, window, vsync);
		return renderer;
	}
};

std::vector<Bgra> pixels_of(TexturePtr const &texture)
{
	texture->download();
	auto texture_mapping = texture->read();
	auto const *bytes = texture_mapping.as<Bgra const>().data();
	std::vector<Bgra> out(bytes, bytes + size_t(texture->width()) * texture->height());
	texture_mapping.unmap();
	return out;
}

bool near(Bgra const &a, Bgra const &b)
{
	for (int c = 0; c < 4; ++c)
		if (std::abs(int(a[c]) - int(b[c])) > 2)
			return false;
	return true;
}

/// B, G, R, X bytes of a frame of @p width x @p height whose pixel (x, y) is its own colour.
std::vector<uint8_t> frame_bytes(int width, int height)
{
	std::vector<uint8_t> bytes(size_t(width) * height * 4);
	for (int y = 0; y < height; ++y)
		for (int x = 0; x < width; ++x)
		{
			uint8_t *p = &bytes[(size_t(y) * width + x) * 4];
			p[0] = uint8_t(50 * x + 5);
			p[1] = uint8_t(70 * y + 5);
			p[2] = uint8_t(255 - 50 * x);
			p[3] = 0;                     // X: not alpha
		}
	return bytes;
}

}

BOOST_FIXTURE_TEST_SUITE(vulkan_sdl_surface, Fixture)

BOOST_AUTO_TEST_CASE(the_instance_extensions_of_sdl_s_surfaces_include_the_surface_extension)
{
	std::vector<std::string> const extensions = sdl_vulkan_instance_extensions();
	BOOST_CHECK(std::find(extensions.begin(), extensions.end(), "VK_KHR_surface") != extensions.end());
}

BOOST_AUTO_TEST_CASE(the_instance_extensions_need_sdl_s_video)
{
	SDL_DestroyWindow(window);
	window = nullptr;
	SDL_QuitSubSystem(SDL_INIT_VIDEO);
	BOOST_CHECK_THROW(sdl_vulkan_instance_extensions(), std::logic_error);
	BOOST_REQUIRE(SDL_InitSubSystem(SDL_INIT_VIDEO));
	window = SDL_CreateWindow("edge", 64, 48, SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN);
}

BOOST_AUTO_TEST_CASE(a_window_s_surface_is_attached_and_a_rendered_frame_presented_in_it)
{
	VulkanRendererPtr const renderer = attached();
	RenderTargetPtr const target = renderer->create_render_target(64, 48, PixelFormat::A8R8G8B8);
	renderer->begin_frame();
	renderer->begin_pass(target);
	renderer->end_pass();
	renderer->finish_frame();
	BOOST_CHECK(renderer->present(target->color_texture(), 64, 48));
	renderer->wait_frames();
}

BOOST_AUTO_TEST_CASE(a_second_surface_or_no_window_is_refused)
{
	VulkanRendererPtr const renderer = attached();
	BOOST_CHECK_THROW(attach_sdl_window(*renderer, window, false), std::logic_error);
	BOOST_CHECK_THROW(attach_sdl_window(*renderer, nullptr, false), std::invalid_argument);
	// detached, the window's surface can be had again
	renderer->detach_surface();
	attach_sdl_window(*renderer, window, true);
	BOOST_CHECK(renderer->vsync());
}

BOOST_AUTO_TEST_CASE(a_renderer_without_sdl_s_extensions_cannot_take_its_surface)
{
	VulkanRenderer renderer(64, 48);
	BOOST_CHECK_THROW(attach_sdl_window(renderer, window, false), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(bytes_presented_are_the_picture_shown_stretched_to_the_window_and_opaque)
{
	VulkanRendererPtr const renderer = attached();
	FramePresenter presenter(renderer);
	std::vector<uint8_t> const bytes = frame_bytes(4, 3);
	BOOST_REQUIRE(presenter.present(bytes.data(), 4, 3, 64, 48));
	renderer->wait_frames();
	TexturePtr const picture = presenter.picture();
	BOOST_REQUIRE_EQUAL(picture->width(), 64);
	BOOST_REQUIRE_EQUAL(picture->height(), 48);
	std::vector<Bgra> const shown = pixels_of(picture);
	auto const byte_at = [&](int x, int y) { Bgra b; std::memcpy(b.data(), &bytes[(size_t(y) * 4 + x) * 4], 4); b[3] = 255; return b; };
	BOOST_CHECK(near(shown[0], byte_at(0, 0)));                         // top left
	BOOST_CHECK(near(shown[63], byte_at(3, 0)));                        // top right
	BOOST_CHECK(near(shown[size_t(47) * 64], byte_at(0, 2)));           // bottom left
	BOOST_CHECK(near(shown[size_t(47) * 64 + 63], byte_at(3, 2)));      // bottom right
}

BOOST_AUTO_TEST_CASE(a_frame_of_the_window_s_size_is_shown_as_it_is)
{
	VulkanRendererPtr const renderer = attached();
	FramePresenter presenter(renderer);
	std::vector<uint8_t> const bytes = frame_bytes(5, 4);
	BOOST_REQUIRE(presenter.present(bytes.data(), 5, 4, 5, 4));
	renderer->wait_frames();
	std::vector<Bgra> const shown = pixels_of(presenter.picture());
	for (int i = 0; i < 20; ++i)
	{
		Bgra expected;
		std::memcpy(expected.data(), &bytes[size_t(i) * 4], 4);
		expected[3] = 255;
		BOOST_CHECK(near(shown[size_t(i)], expected));
	}
}

BOOST_AUTO_TEST_CASE(a_texture_is_shown_with_what_is_drawn_over_it_in_the_window_s_pass)
{
	VulkanRendererPtr const renderer = attached();
	FramePresenter presenter(renderer);
	TexturePtr const frame = renderer->create_render_target(8, 8, PixelFormat::A8R8G8B8)->color_texture();
	bool drawn_over = false;
	BOOST_REQUIRE(presenter.present(frame, 32, 16, [&] {
		BOOST_CHECK(renderer->in_pass());
		BOOST_CHECK_EQUAL(renderer->pass_target()->width(), 32);
		drawn_over = true;
	}));
	BOOST_CHECK(drawn_over);
	BOOST_CHECK(!renderer->in_frame());
	BOOST_CHECK_EQUAL(presenter.picture()->width(), 32);
	// another window size: another picture
	BOOST_REQUIRE(presenter.present(frame, 20, 10));
	BOOST_CHECK_EQUAL(presenter.picture()->width(), 20);
	BOOST_CHECK_EQUAL(presenter.picture()->height(), 10);
	renderer->wait_frames();
}

BOOST_AUTO_TEST_CASE(what_goes_wrong_drawing_over_the_frame_is_the_caller_s_and_the_renderer_is_left_out_of_a_frame)
{
	VulkanRendererPtr const renderer = attached();
	FramePresenter presenter(renderer);
	TexturePtr const frame = renderer->create_render_target(8, 8, PixelFormat::A8R8G8B8)->color_texture();
	BOOST_CHECK_THROW(presenter.present(frame, 32, 16, [] { throw std::out_of_range("the UI's"); }), std::out_of_range);
	BOOST_CHECK(!renderer->in_frame());
	BOOST_CHECK(presenter.present(frame, 32, 16));           // and it goes on
	renderer->wait_frames();
}

BOOST_AUTO_TEST_CASE(what_cannot_be_shown_is_refused)
{
	VulkanRendererPtr const renderer = attached();
	FramePresenter presenter(renderer);
	BOOST_CHECK_THROW(presenter.picture(), std::logic_error);           // nothing presented yet
	std::vector<uint8_t> const bytes = frame_bytes(4, 3);
	BOOST_CHECK_THROW(presenter.present(nullptr, 4, 3, 64, 48), std::invalid_argument);
	for (auto const &[w, h, ww, wh] : std::vector<std::array<int, 4>>{{0, 3, 64, 48}, {4, -1, 64, 48}, {4, 3, 0, 48}, {4, 3, 64, -2}})
		BOOST_CHECK_THROW(presenter.present(bytes.data(), w, h, ww, wh), std::invalid_argument);
	BOOST_CHECK_THROW(presenter.present(TexturePtr(), 64, 48), std::invalid_argument);
	BOOST_CHECK_THROW(FramePresenter(nullptr), std::invalid_argument);
	BOOST_CHECK(!renderer->in_frame());
}

BOOST_AUTO_TEST_CASE(a_renderer_without_a_surface_cannot_present_and_is_left_out_of_a_frame)
{
	auto const renderer = std::make_shared<VulkanRenderer>(64, 48);
	FramePresenter presenter(renderer);
	std::vector<uint8_t> const bytes = frame_bytes(4, 3);
	BOOST_CHECK_THROW(presenter.present(bytes.data(), 4, 3, 64, 48), std::logic_error);
	BOOST_CHECK(!renderer->in_frame());
}

BOOST_AUTO_TEST_SUITE_END()
