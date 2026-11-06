// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

// FullscreenCopy draws a texture over the whole target of the pass it is drawn in: row 0 at the top, stretched to the target's
// size, opaque whatever the texture's alpha, and its far edge not wrapped onto the near one.  On every backend that draws; the
// null renderer for what it refuses.

#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <vector>

#include <boost/test/unit_test.hpp>

#include "fullscreen_copy.h"
#include "pixel.h"
#include "render_target.h"
#include "test_backends.h"
#include "texture.h"

using namespace edge;

namespace
{

using Bgra = std::array<uint8_t, 4>;

/// A texture of @p width x @p height whose texel (x, y) is @p texel(x, y), B, G, R, A.
template <class F> TexturePtr texture_of(RendererPtr const &renderer, int width, int height, F texel)
{
	TexturePtr const texture = renderer->create_texture(width, height, PixelFormat::A8R8G8B8);
	auto texture_mapping = texture->write(Contents::DISCARD);
	auto *bytes = texture_mapping.as<uint8_t>().data();
	for (int y = 0; y < height; ++y)
		for (int x = 0; x < width; ++x)
		{
			Bgra const t = texel(x, y);
			std::memcpy(bytes + (size_t(y) * width + x) * 4, t.data(), 4);
		}
	texture_mapping.unmap();
	return texture;
}

/// What the target holds, row 0 first.
std::vector<Bgra> pixels_of(RenderTargetPtr const &target)
{
	TexturePtr const colour = target->color_texture();
	colour->download();
	auto colour_mapping = colour->read();
	auto const *bytes = colour_mapping.as<Bgra const>().data();
	std::vector<Bgra> out(bytes, bytes + size_t(target->width()) * target->height());
	colour_mapping.unmap();
	return out;
}

/// @p source copied into @p target, a frame of its own.
void copy(RendererPtr const &renderer, FullscreenCopy &copier, TexturePtr const &source, RenderTargetPtr const &target)
{
	renderer->begin_frame();
	renderer->begin_pass(target);
	copier.draw(source);
	renderer->end_pass();
	renderer->finish_frame();
}

bool near(Bgra const &a, Bgra const &b, int tolerance = 2)
{
	for (int c = 0; c < 4; ++c)
		if (std::abs(int(a[c]) - int(b[c])) > tolerance)
			return false;
	return true;
}

/// A texel of its own colour for every (x, y) of a small texture, alpha 0: a copy that kept alpha would show it.
Bgra pattern(int x, int y)
{
	return Bgra{uint8_t(40 * x + 10), uint8_t(100 * y + 20), uint8_t(200 - 30 * x), 0};
}

}

BOOST_AUTO_TEST_SUITE(fullscreen_copy)

BOOST_AUTO_TEST_CASE_TEMPLATE(a_copy_of_the_target_s_size_is_the_texture_texel_for_texel_and_opaque, F, Backends)
{
	F fixture;
	BOOST_REQUIRE_MESSAGE(fixture.renderer, "renderer unavailable: " << fixture.error);
	RendererPtr const renderer = fixture.renderer;
	FullscreenCopy copier(renderer);
	RenderTargetPtr const target = renderer->create_render_target(4, 3, PixelFormat::A8R8G8B8);
	copy(renderer, copier, texture_of(renderer, 4, 3, pattern), target);
	std::vector<Bgra> const got = pixels_of(target);
	for (int y = 0; y < 3; ++y)
		for (int x = 0; x < 4; ++x)
		{
			Bgra expected = pattern(x, y);
			expected[3] = 255;
			Bgra const g = got[size_t(y) * 4 + x];
			BOOST_CHECK_MESSAGE(near(g, expected), "texel " << x << ", " << y << ": " << int(g[0]) << " " << int(g[1]) << " " << int(g[2])
				<< " " << int(g[3]));
		}
}

BOOST_AUTO_TEST_CASE_TEMPLATE(a_copy_is_stretched_over_a_larger_target_and_its_edges_do_not_wrap, F, Backends)
{
	F fixture;
	BOOST_REQUIRE_MESSAGE(fixture.renderer, "renderer unavailable: " << fixture.error);
	RendererPtr const renderer = fixture.renderer;
	FullscreenCopy copier(renderer);
	// black but for a white last column: a copy that wrapped would bleed white into the first column of the target
	TexturePtr const source = texture_of(renderer, 4, 2, [](int x, int) { return x == 3 ? Bgra{255, 255, 255, 255} : Bgra{0, 0, 0, 255}; });
	RenderTargetPtr const target = renderer->create_render_target(16, 8, PixelFormat::A8R8G8B8);
	copy(renderer, copier, source, target);
	std::vector<Bgra> const got = pixels_of(target);
	for (int y = 0; y < 8; ++y)
	{
		BOOST_CHECK(near(got[size_t(y) * 16 + 0], Bgra{0, 0, 0, 255}));
		BOOST_CHECK(near(got[size_t(y) * 16 + 15], Bgra{255, 255, 255, 255}));
		BOOST_CHECK(near(got[size_t(y) * 16 + 6], Bgra{0, 0, 0, 255}));       // the middle of a black texel's block
	}
}

BOOST_AUTO_TEST_CASE_TEMPLATE(row_0_of_the_texture_is_the_top_of_the_target_whatever_their_sizes, F, Backends)
{
	F fixture;
	BOOST_REQUIRE_MESSAGE(fixture.renderer, "renderer unavailable: " << fixture.error);
	RendererPtr const renderer = fixture.renderer;
	FullscreenCopy copier(renderer);
	TexturePtr const source = texture_of(renderer, 2, 2, [](int, int y) { return y == 0 ? Bgra{0, 0, 255, 255} : Bgra{255, 0, 0, 255}; });
	RenderTargetPtr const target = renderer->create_render_target(6, 10, PixelFormat::A8R8G8B8);
	copy(renderer, copier, source, target);
	std::vector<Bgra> const got = pixels_of(target);
	BOOST_CHECK(near(got[0], Bgra{0, 0, 255, 255}));                // red at the top
	BOOST_CHECK(near(got[size_t(9) * 6 + 5], Bgra{255, 0, 0, 255})); // blue at the bottom
}

BOOST_AUTO_TEST_CASE_TEMPLATE(two_copies_in_one_frame_each_draw_their_own_texture, F, Backends)
{
	F fixture;
	BOOST_REQUIRE_MESSAGE(fixture.renderer, "renderer unavailable: " << fixture.error);
	RendererPtr const renderer = fixture.renderer;
	FullscreenCopy copier(renderer);
	TexturePtr const red = texture_of(renderer, 2, 2, [](int, int) { return Bgra{0, 0, 255, 255}; });
	TexturePtr const green = texture_of(renderer, 2, 2, [](int, int) { return Bgra{0, 255, 0, 255}; });
	RenderTargetPtr const a = renderer->create_render_target(4, 4, PixelFormat::A8R8G8B8);
	RenderTargetPtr const b = renderer->create_render_target(4, 4, PixelFormat::A8R8G8B8);
	renderer->begin_frame();
	renderer->begin_pass(a);
	copier.draw(red);
	renderer->end_pass();
	renderer->begin_pass(b);
	copier.draw(green);
	renderer->end_pass();
	renderer->finish_frame();
	BOOST_CHECK(near(pixels_of(a)[5], Bgra{0, 0, 255, 255}));
	BOOST_CHECK(near(pixels_of(b)[5], Bgra{0, 255, 0, 255}));
}

BOOST_AUTO_TEST_CASE_TEMPLATE(a_copy_is_drawn_in_a_pass_of_a_texture_and_by_a_renderer, F, AllBackends)
{
	F fixture;
	BOOST_REQUIRE_MESSAGE(fixture.renderer, "renderer unavailable: " << fixture.error);
	RendererPtr const renderer = fixture.renderer;
	BOOST_CHECK_THROW(FullscreenCopy(nullptr), std::invalid_argument);
	FullscreenCopy copier(renderer);
	TexturePtr const source = texture_of(renderer, 2, 2, [](int, int) { return Bgra{0, 0, 0, 255}; });
	BOOST_CHECK_THROW(copier.draw(source), std::logic_error);
	renderer->begin_frame();
	BOOST_CHECK_THROW(copier.draw(source), std::logic_error);
	renderer->begin_pass(renderer->create_render_target(4, 4, PixelFormat::A8R8G8B8));
	BOOST_CHECK_THROW(copier.draw(nullptr), std::invalid_argument);
	copier.draw(source);
	renderer->end_pass();
	renderer->finish_frame();
}

BOOST_AUTO_TEST_SUITE_END()
