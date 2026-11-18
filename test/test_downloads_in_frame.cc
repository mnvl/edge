// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

// Downloads that ride in a frame, the same on every renderer: download_in_frame() takes a texture as the frame has drawn it at that
// point (what the frame draws after is not in it), with no submit of its own and no wait; finish_downloads() waits for that frame and
// puts the texels in the CPU copy, which is left as it was until then.  With rectangles, only those are taken.  A texture has one
// download pending at a time, and what is out of place is refused with the renderer left as it was.
//
// The pictures are passes that clear their target: every renderer draws them alike.

#include <algorithm>
#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
#include <vector>

#include <boost/test/unit_test.hpp>

#include "pixel.h"
#include "render_target.h"
#include "test_backends.h"
#include "texture.h"

using namespace edge;

namespace
{

int const W = 64, H = 32;      // (not square: a download that swaps x and y shows)

struct Colour
{
	float r, g, b;
	uint32_t texel;      // as A8R8G8B8 lies in memory: B, G, R, A
};
Colour const RED{1, 0, 0, 0xFFFF0000u}, GREEN{0, 1, 0, 0xFF00FF00u}, BLUE{0, 0, 1, 0xFF0000FFu};

void clear(Renderer &renderer, std::shared_ptr<RenderTarget> const &target, Colour const &colour)
{
	RenderPassClearState state;
	state.clear_color_r = colour.r;
	state.clear_color_g = colour.g;
	state.clear_color_b = colour.b;
	state.clear_color_a = 1.0f;
	renderer.begin_pass(target, state);
	renderer.end_pass();
}

std::vector<uint32_t> texels(std::shared_ptr<RenderTarget> const &target)
{
	auto mapping = target->color_texture()->read();
	std::span<uint32_t const> const texels = mapping.as<uint32_t const>();
	BOOST_REQUIRE_EQUAL(texels.size(), size_t(W) * H);
	return std::vector<uint32_t>(texels.begin(), texels.end());
}

size_t count(std::vector<uint32_t> const &texels, uint32_t texel)
{
	return size_t(std::count(texels.begin(), texels.end(), texel));
}

/// Two frames submitted, each with a download; the first's target drawn over by the second while its download is pending.
void two_frames_in_flight(Renderer &renderer)
{
	std::shared_ptr<RenderTarget> const a = renderer.create_render_target(W, H, PixelFormat::A8R8G8B8);
	std::shared_ptr<RenderTarget> const b = renderer.create_render_target(W, H, PixelFormat::A8R8G8B8);

	renderer.begin_frame();
	clear(renderer, a, RED);
	renderer.download_in_frame({a->color_texture()});
	renderer.submit_frame();
	uint64_t const first = renderer.frame_serial();
	renderer.begin_frame();
	clear(renderer, a, GREEN);               // a drawn over while its download is pending: the download is the first frame's
	clear(renderer, b, BLUE);
	renderer.download_in_frame({b->color_texture()});
	renderer.submit_frame();
	uint64_t const second = renderer.frame_serial();
	BOOST_REQUIRE_EQUAL(second, first + 1);

	// the later one first
	renderer.finish_downloads({b->color_texture()});
	BOOST_CHECK(renderer.frame_drawn(second));
	BOOST_CHECK_EQUAL(count(texels(b), BLUE.texel), size_t(W) * H);
	renderer.finish_downloads({a->color_texture()});
	BOOST_CHECK_EQUAL(count(texels(a), RED.texel), size_t(W) * H);
	renderer.wait_frame(first);              // drawn already: nothing
	BOOST_CHECK(renderer.frame_drawn(first));
}

}

BOOST_AUTO_TEST_SUITE(downloads_in_frame)

BOOST_AUTO_TEST_CASE_TEMPLATE(a_download_is_the_texture_as_the_frame_had_drawn_it_then, F, Backends)
{
	F fixture;
	auto const &renderer = fixture.renderer;
	BOOST_REQUIRE_MESSAGE(renderer, "renderer unavailable: " << fixture.error);
	std::shared_ptr<RenderTarget> const target = renderer->create_render_target(W, H, PixelFormat::A8R8G8B8);
	std::vector<uint32_t> const before = texels(target);

	renderer->begin_frame();
	clear(*renderer, target, RED);
	renderer->download_in_frame({target->color_texture()});
	clear(*renderer, target, GREEN);          // after the download: not in it
	renderer->submit_frame();
	BOOST_CHECK(texels(target) == before);   // the CPU copy is as it was until the download is finished
	renderer->finish_downloads({target->color_texture()});
	BOOST_CHECK_EQUAL(count(texels(target), RED.texel), size_t(W) * H);
}

BOOST_AUTO_TEST_CASE_TEMPLATE(frames_in_flight_are_each_downloaded_as_they_drew, F, Backends)
{
	F fixture;
	BOOST_REQUIRE_MESSAGE(fixture.renderer, "renderer unavailable: " << fixture.error);
	two_frames_in_flight(*fixture.renderer);
}

BOOST_AUTO_TEST_CASE_TEMPLATE(a_download_of_rectangles_takes_them_and_leaves_the_rest, F, Backends)
{
	F fixture;
	auto const &renderer = fixture.renderer;
	BOOST_REQUIRE_MESSAGE(renderer, "renderer unavailable: " << fixture.error);
	std::shared_ptr<RenderTarget> const target = renderer->create_render_target(W, H, PixelFormat::A8R8G8B8);
	std::vector<std::shared_ptr<Texture>> const textures{target->color_texture()};
	renderer->begin_frame();
	clear(*renderer, target, RED);
	renderer->download_in_frame(textures);
	renderer->finish_frame();
	renderer->finish_downloads(textures);
	BOOST_REQUIRE_EQUAL(count(texels(target), RED.texel), size_t(W) * H);
	{
		// the CPU copy made blue and not sent: what it was is not what the GPU or any earlier download has
		auto mapping = target->color_texture()->write();
		std::span<uint32_t> const copy = mapping.as<uint32_t>();
		std::fill(copy.begin(), copy.end(), BLUE.texel);
		mapping.release();
	}

	// drawn over green; two rectangles taken -- one inside, one a strip ending at the last texel -- and the rest still blue
	std::vector<TextureRect> const rects{{8, 4, 16, 8}, {40, H - 3, W - 40, 3}};
	renderer->begin_frame();
	clear(*renderer, target, GREEN);
	renderer->download_in_frame(textures, rects);
	renderer->finish_frame();
	renderer->finish_downloads(textures);
	std::vector<uint32_t> const mixed = texels(target);
	size_t wrong = 0;
	for (int y = 0; y < H; ++y)
		for (int x = 0; x < W; ++x)
		{
			bool inside = false;
			for (TextureRect const &r : rects)
				inside = inside || (x >= r.x && x < r.x + r.width && y >= r.y && y < r.y + r.height);
			wrong += mixed[size_t(y) * W + size_t(x)] != (inside ? GREEN.texel : BLUE.texel);
		}
	BOOST_CHECK_EQUAL(wrong, 0u);
	BOOST_CHECK_EQUAL(count(mixed, GREEN.texel), size_t(16 * 8 + (W - 40) * 3));
}

BOOST_AUTO_TEST_CASE_TEMPLATE(what_is_out_of_place_is_refused, F, AllBackends)
{
	F fixture;
	auto const &renderer = fixture.renderer;
	BOOST_REQUIRE_MESSAGE(renderer, "renderer unavailable: " << fixture.error);
	std::shared_ptr<RenderTarget> const target = renderer->create_render_target(W, H, PixelFormat::A8R8G8B8);
	std::vector<std::shared_ptr<Texture>> const textures{target->color_texture()};
	TextureUsage no_copy;
	no_copy.keep_cpu_copy = false;
	std::shared_ptr<Texture> const without_copy = renderer->create_texture(4, 4, PixelFormat::A8R8G8B8, no_copy);
	std::shared_ptr<Renderer> const other = std::make_shared<NullRenderer>(64, 64);
	std::shared_ptr<Texture> const others = other->create_texture(4, 4, PixelFormat::A8R8G8B8);

	BOOST_CHECK_THROW(renderer->download_in_frame(textures), std::logic_error);         // outside a frame
	BOOST_CHECK_THROW(renderer->wait_frame(0), std::logic_error);
	BOOST_CHECK_THROW(renderer->wait_frame(1), std::logic_error);                       // not begun
	BOOST_CHECK_THROW(renderer->frame_drawn(1), std::logic_error);
	renderer->begin_frame();
	BOOST_CHECK_THROW(renderer->wait_frame(renderer->frame_serial()), std::logic_error);    // the open frame
	BOOST_CHECK_THROW(renderer->frame_drawn(renderer->frame_serial()), std::logic_error);
	BOOST_CHECK_THROW(renderer->download_in_frame({std::shared_ptr<Texture>()}), std::invalid_argument);
	BOOST_CHECK_THROW(renderer->download_in_frame({textures[0], textures[0]}), std::invalid_argument);
	BOOST_CHECK_THROW(renderer->download_in_frame({others}), std::invalid_argument);
	{
		// another renderer of the same kind: its target's colour is known by the backend alone (no owner recorded), and refusing it
		// after the first texture leaves neither pending
		F second;
		BOOST_REQUIRE_MESSAGE(second.renderer, "renderer unavailable: " << second.error);
		std::shared_ptr<Texture> const foreign = second.renderer->create_render_target(W, H, PixelFormat::A8R8G8B8)->color_texture();
		BOOST_CHECK_THROW(renderer->download_in_frame({textures[0], foreign}), std::invalid_argument);
	}
	BOOST_CHECK_THROW(renderer->download_in_frame({without_copy}), std::logic_error);
	for (TextureRect const &r : {TextureRect{0, 0, W + 1, 1}, TextureRect{0, H - 1, 1, 2}, TextureRect{0, 0, 0, 1}, TextureRect{-1, 0, 1, 1}})
		BOOST_CHECK_THROW(renderer->download_in_frame(textures, {r}), std::out_of_range);
	renderer->begin_pass(target);
	BOOST_CHECK_THROW(renderer->download_in_frame(textures), std::logic_error);         // inside a pass
	renderer->end_pass();
	BOOST_CHECK_THROW(renderer->finish_downloads(textures), std::logic_error);          // none pending: every refusal left none

	renderer->download_in_frame(textures);
	BOOST_CHECK_THROW(renderer->download_in_frame(textures), std::logic_error);         // one pending at a time
	BOOST_CHECK_THROW(renderer->finish_downloads(textures), std::logic_error);          // its frame is open
	renderer->submit_frame();
	BOOST_CHECK_THROW(textures[0]->download(), std::logic_error);                       // the download pending is the texture's
	BOOST_CHECK_THROW(renderer->batch_download_textures(textures), std::logic_error);
	BOOST_CHECK_THROW(renderer->finish_downloads({std::shared_ptr<Texture>()}), std::invalid_argument);
	renderer->finish_downloads(textures);
	BOOST_CHECK_THROW(renderer->finish_downloads(textures), std::logic_error);          // taken already

	// and again, as if nothing had been refused
	textures[0]->download();
	renderer->begin_frame();
	renderer->download_in_frame(textures);
	renderer->finish_frame();
	renderer->finish_downloads(textures);
}

#ifdef EDGE_USE_VULKAN
BOOST_AUTO_TEST_CASE(frames_in_flight_on_vulkan_are_each_downloaded_as_they_drew)
{
	// (the fixture's renderer keeps one frame in flight: a frame begun waits for the last)
	std::shared_ptr<VulkanRenderer> renderer;
	try
	{
		renderer = std::make_shared<VulkanRenderer>(64, 64, std::string(), std::vector<std::string>(), 3);
	}
	catch (std::exception const &error)
	{
		BOOST_FAIL(std::string("renderer unavailable: ") + error.what());
	}
	two_frames_in_flight(*renderer);
}

BOOST_AUTO_TEST_CASE(a_vulkan_download_in_the_frame_submits_nothing_of_its_own)
{
	VulkanFixture fixture;
	auto const &renderer = fixture.renderer;
	BOOST_REQUIRE_MESSAGE(renderer, "renderer unavailable: " << fixture.error);
	std::shared_ptr<RenderTarget> const target = renderer->create_render_target(W, H, PixelFormat::A8R8G8B8);
	uint64_t const submits = renderer->single_time_submit_count();
	renderer->begin_frame();
	clear(*renderer, target, BLUE);
	renderer->download_in_frame({target->color_texture()});
	renderer->submit_frame();
	renderer->finish_downloads({target->color_texture()});
	BOOST_CHECK_EQUAL(renderer->single_time_submit_count() - submits, 1u);      // the frame's own
	BOOST_CHECK_EQUAL(count(texels(target), BLUE.texel), size_t(W) * H);
}
#endif

BOOST_AUTO_TEST_SUITE_END()
