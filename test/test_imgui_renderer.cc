// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

// Dear ImGui drawn through edge (edge_imgui's imgui_renderer.h), checked on the pixels of a render target read back from the GPU, on
// every backend that draws: where things land, what is clipped, how alpha blends, how a framebuffer scale is applied, textures.

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

#include <boost/test/unit_test.hpp>
#include <imgui.h>

#include "imgui_renderer.h"
#include "pixel.h"
#include "render_target.h"
#include "test_backends.h"
#include "texture.h"

using namespace edge;

namespace
{

constexpr int W = 64, H = 48;
constexpr uint32_t BACKGROUND = 0x00102030u;     // what the target is cleared to, alpha left out
constexpr uint32_t RED = 0x00FF0000u;

/// An ImGui context with the default font, and edge drawing it with the backend's renderer.
template <class F> struct Ui
{
	F fixture;
	RendererPtr renderer;
	ImGuiContext *context = nullptr;
	std::unique_ptr<ImGuiRenderer> ui;
	RenderTargetPtr target;

	Ui()
	{
		BOOST_REQUIRE_MESSAGE(fixture.renderer, "renderer unavailable: " << fixture.error);
		renderer = fixture.renderer;
		context = ImGui::CreateContext();
		ImGuiIO &io = ImGui::GetIO();
		io.IniFilename = nullptr;
		io.Fonts->AddFontDefault();
		ui = std::make_unique<ImGuiRenderer>(renderer);
		target = renderer->create_render_target(W, H, PixelFormat::A8R8G8B8);
	}

	~Ui()
	{
		ui.reset();
		ImGui::DestroyContext(context);
	}

	void frame(std::function<void()> const &build, ImVec2 display = {W, H})
	{
		ImGuiIO &io = ImGui::GetIO();
		io.DisplaySize = display;
		io.DeltaTime = 1.0f / 60;
		ImGui::NewFrame();
		build();
		ImGui::Render();
	}

	/// An ImGui frame of @p display logical pixels with what @p build adds, drawn on the cleared target; its pixels, RGB only.
	std::vector<uint32_t> render(std::function<void()> const &build, ImVec2 display = {W, H})
	{
		frame(build, display);
		ui->queue(ImGui::GetDrawData(), W, H);
		RenderPassClearState clear;
		clear.clear_color_r = float((BACKGROUND >> 16) & 0xFF) / 255.0f;
		clear.clear_color_g = float((BACKGROUND >> 8) & 0xFF) / 255.0f;
		clear.clear_color_b = float(BACKGROUND & 0xFF) / 255.0f;
		renderer->begin_frame();
		renderer->begin_pass(target, clear);
		ui->draw();
		renderer->end_pass();
		renderer->finish_frame();
		TexturePtr const colour = target->color_texture();
		colour->download();
		auto colour_mapping = colour->read();
		uint32_t const *texels = colour_mapping.as<uint32_t const>().data();
		std::vector<uint32_t> pixels(texels, texels + W * H);
		colour_mapping.unmap();
		for (uint32_t &p : pixels)
			p &= 0x00FFFFFFu;
		return pixels;
	}

	void expect_rectangle(std::vector<uint32_t> const &p, int x0, int y0, int x1, int y1, uint32_t colour)
	{
		for (int y = 0; y < H; ++y)
			for (int x = 0; x < W; ++x)
				BOOST_REQUIRE_MESSAGE(p[y * W + x] == (x >= x0 && x < x1 && y >= y0 && y < y1 ? colour : BACKGROUND),
					"pixel " << x << "," << y << " is " << std::hex << p[y * W + x]);
	}
};

/// A 2 x 2 texture: red, green over blue, white.
TexturePtr quad(RendererPtr const &renderer)
{
	TexturePtr t = renderer->create_texture(2, 2, PixelFormat::A8R8G8B8);
	auto t_mapping = t->write(Contents::DISCARD);
	auto *texels = t_mapping.as<unsigned char>().data();
	unsigned char const bgra[4][4] = {{0, 0, 255, 255}, {0, 255, 0, 255}, {255, 0, 0, 255}, {255, 255, 255, 255}};
	for (int i = 0; i < 4; ++i)
		std::copy(bgra[i], bgra[i] + 4, texels + 4 * i);
	t_mapping.unmap();
	return t;
}

/// Whether the channel at @p shift (16 red, 8 green, 0 blue) is the pixel's strong one: over 150 and 70 above each of the others.
bool mostly(uint32_t pixel, int shift)
{
	int const c[3] = {int((pixel >> 16) & 0xFF), int((pixel >> 8) & 0xFF), int(pixel & 0xFF)};
	int const own = c[(16 - shift) / 8];
	for (int i = 0; i < 3; ++i)
		if (i != (16 - shift) / 8 && own < c[i] + 70)
			return false;
	return own > 150;
}

}

BOOST_AUTO_TEST_SUITE(imgui_renderer)

BOOST_AUTO_TEST_CASE_TEMPLATE(a_filled_rectangle_covers_exactly_its_pixels_from_the_top_left, F, Backends)
{
	Ui<F> u;
	auto const p = u.render([] { ImGui::GetForegroundDrawList()->AddRectFilled({10, 8}, {30, 24}, IM_COL32(255, 0, 0, 255)); });
	u.expect_rectangle(p, 10, 8, 30, 24, RED);
}

BOOST_AUTO_TEST_CASE_TEMPLATE(a_translucent_rectangle_blends_with_what_is_under_it, F, Backends)
{
	Ui<F> u;
	auto const p = u.render([] { ImGui::GetForegroundDrawList()->AddRectFilled({10, 8}, {30, 24}, IM_COL32(255, 255, 255, 128)); });
	uint32_t const mix = p[16 * W + 20];
	auto const channel = [](uint32_t c, int shift) { return int((c >> shift) & 0xFF); };
	BOOST_CHECK_LE(std::abs(channel(mix, 16) - (0x10 + (255 - 0x10) * 128 / 255)), 2);
	BOOST_CHECK_LE(std::abs(channel(mix, 8) - (0x20 + (255 - 0x20) * 128 / 255)), 2);
	BOOST_CHECK_LE(std::abs(channel(mix, 0) - (0x30 + (255 - 0x30) * 128 / 255)), 2);
}

BOOST_AUTO_TEST_CASE_TEMPLATE(the_clip_rectangle_cuts_what_is_drawn, F, Backends)
{
	Ui<F> u;
	auto const p = u.render([] {
		ImDrawList *list = ImGui::GetForegroundDrawList();
		list->PushClipRect({10, 10}, {30, 20});
		list->AddRectFilled({0, 0}, {W, H}, IM_COL32(255, 0, 0, 255));
		list->PopClipRect();
	});
	u.expect_rectangle(p, 10, 10, 30, 20, RED);
}

BOOST_AUTO_TEST_CASE_TEMPLATE(what_is_drawn_inside_a_clip_away_from_the_corner_is_where_it_was_drawn, F, Backends)
{
	Ui<F> u;
	auto const p = u.render([] {
		ImDrawList *list = ImGui::GetForegroundDrawList();
		list->PushClipRect({10, 12}, {40, 40});
		list->AddRectFilled({20, 15}, {30, 25}, IM_COL32(255, 0, 0, 255));
		list->PopClipRect();
	});
	u.expect_rectangle(p, 20, 15, 30, 25, RED);
}

BOOST_AUTO_TEST_CASE_TEMPLATE(a_target_larger_than_the_display_scales_the_picture_up, F, Backends)
{
	Ui<F> u;
	// 32 x 24 logical pixels on the 64 x 48 target: the display's 5..15 x 4..12 is the target's 10..30 x 8..24
	auto const p = u.render([] { ImGui::GetForegroundDrawList()->AddRectFilled({5, 4}, {15, 12}, IM_COL32(255, 0, 0, 255)); }, {32, 24});
	u.expect_rectangle(p, 10, 8, 30, 24, RED);
}

BOOST_AUTO_TEST_CASE_TEMPLATE(text_is_drawn_from_the_font_atlas_not_as_solid_boxes, F, Backends)
{
	Ui<F> u;
	auto const p = u.render([] { ImGui::GetForegroundDrawList()->AddText({4, 4}, IM_COL32(255, 255, 255, 255), "HIM"); });
	int lit = 0, box = 0;
	for (int y = 0; y < H; ++y)
		for (int x = 0; x < W; ++x)
		{
			bool const inside = x >= 4 && x < 40 && y >= 4 && y < 20;
			if (p[y * W + x] != BACKGROUND)
			{
				++lit;
				BOOST_REQUIRE_MESSAGE(inside, "a glyph pixel outside the text's box at " << x << "," << y);
			}
			box += inside;
		}
	BOOST_CHECK_GT(lit, 10);
	BOOST_CHECK_MESSAGE(lit < box / 2, lit << " lit pixels of the " << box << " in the box: glyphs are shapes, not boxes");
}

BOOST_AUTO_TEST_CASE_TEMPLATE(text_of_a_size_the_atlas_had_not_got_is_drawn_when_it_is_asked_for, F, Backends)
{
	Ui<F> u;
	auto lit = [&](float size, int frames) {
		std::vector<uint32_t> p;
		for (int i = 0; i < frames; ++i)           // (the atlas is made, and grown, as the frames ask)
			p = u.render([size] {
				ImGui::PushFont(nullptr, size);
				ImGui::GetForegroundDrawList()->AddText({2, 2}, IM_COL32(255, 255, 255, 255), "HIM");
				ImGui::PopFont();
			});
		return int(std::count_if(p.begin(), p.end(), [](uint32_t c) { return c != BACKGROUND; }));
	};
	int const small = lit(13, 2);
	int const large = lit(26, 2);                  // glyphs the first frames never baked
	BOOST_CHECK_GT(small, 10);
	BOOST_CHECK_GT(large, 2 * small);              // four times the area of the glyphs, whatever the edges
	BOOST_CHECK_GT(lit(13, 1), 10);                // and the small ones are still there
}

BOOST_AUTO_TEST_CASE_TEMPLATE(an_atlas_that_grows_for_many_sizes_is_drawn_whole_in_every_frame, F, Backends)
{
	Ui<F> u;
	// twenty sizes of a line of different letters in a row: the atlas has to be made bigger on the way, its pixels made again
	for (int i = 0; i < 20; ++i)
	{
		float const size = 10.0f + 1.5f * float(i);
		auto const p = u.render([size] {
			ImGui::PushFont(nullptr, size);
			ImGui::GetForegroundDrawList()->AddText({0, 0}, IM_COL32(255, 255, 255, 255), "Brethren of the Electric Sea");
			ImGui::PopFont();
		});
		int lit = 0;
		for (uint32_t c : p)
			lit += c != BACKGROUND;
		BOOST_REQUIRE_MESSAGE(lit > 20, "no text at the size " << size << " (frame " << i << ")");
		// what ImGui asked of the backend was done: every texture of the atlas made, and its glyphs sent
		for (ImTextureData const *texture : ImGui::GetPlatformIO().Textures)
			BOOST_REQUIRE_MESSAGE(texture->TexID != ImTextureID_Invalid && (texture->Status == ImTextureStatus_OK ||
					texture->Status == ImTextureStatus_WantDestroy),       // (the one the atlas left, for a bigger one)
				"the atlas's texture " << texture->UniqueID << " is not made (frame " << i << ")");
	}
	int highest = 0;
	for (int i = 0; i < 8; ++i)                    // quiet frames: what the atlas left is let go of
	{
		u.render([] {});
		for (ImTextureData const *texture : ImGui::GetPlatformIO().Textures)
			highest = std::max(highest, texture->UniqueID);
	}
	BOOST_CHECK_GT(highest, 1);                    // it did grow
	for (ImTextureData const *texture : ImGui::GetPlatformIO().Textures)
		BOOST_CHECK_MESSAGE(texture->Status == ImTextureStatus_OK, "the atlas's texture " << texture->UniqueID << " is kept, not used");
}

BOOST_AUTO_TEST_CASE_TEMPLATE(the_backend_says_it_makes_the_atlas_s_textures_as_imgui_asks, F, Backends)
{
	Ui<F> u;
	BOOST_CHECK(ImGui::GetIO().BackendFlags & ImGuiBackendFlags_RendererHasTextures);
	BOOST_CHECK(ImGui::GetIO().BackendFlags & ImGuiBackendFlags_RendererHasVtxOffset);
}

BOOST_AUTO_TEST_CASE_TEMPLATE(more_than_a_frame_held_before_is_drawn_all_the_same, F, Backends)
{
	Ui<F> u;
	u.render([] { ImGui::GetForegroundDrawList()->AddRectFilled({1, 1}, {2, 2}, IM_COL32(0, 255, 0, 255)); });
	// 5000 rectangles are 20000 vertices and 30000 indices; the last one, down at the bottom right, must be there
	auto const p = u.render([] {
		ImDrawList *list = ImGui::GetForegroundDrawList();
		for (int i = 0; i < 5000; ++i)
			list->AddRectFilled({1, 1}, {2, 2}, IM_COL32(0, 255, 0, 255));
		list->AddRectFilled({60, 44}, {62, 46}, IM_COL32(255, 0, 0, 255));
	});
	BOOST_CHECK_EQUAL(p[1 * W + 1], 0x0000FF00u);
	BOOST_CHECK_EQUAL(p[45 * W + 61], RED);
}

BOOST_AUTO_TEST_CASE_TEMPLATE(an_added_texture_is_drawn_where_the_image_is_and_its_id_is_not_the_font_s, F, Backends)
{
	Ui<F> u;
	uint64_t const id = u.ui->add_texture(quad(u.renderer));
	// (drawn, not only built: until the renderer has been told of the atlas, asking its id is an assertion of Dear ImGui's)
	u.render([] { ImGui::GetForegroundDrawList()->AddText({4, 4}, IM_COL32(255, 255, 255, 255), "font"); });
	ImDrawData const *text = ImGui::GetDrawData();
	BOOST_REQUIRE(text->CmdListsCount > 0 && text->CmdLists[0]->CmdBuffer.Size > 0);
	BOOST_CHECK_NE(id, uint64_t(text->CmdLists[0]->CmdBuffer[0].GetTexID()));       // not the font's
	BOOST_CHECK_NE(u.ui->add_texture(quad(u.renderer)), id);
	auto const p = u.render([&] { ImGui::GetForegroundDrawList()->AddImage(ImTextureID(id), {8, 8}, {24, 24}); });
	BOOST_CHECK(mostly(p[10 * W + 10], 16));     // (filtered: a pixel near a corner of the image is its texel's colour, not exactly)
	BOOST_CHECK(mostly(p[10 * W + 22], 8));
	BOOST_CHECK(mostly(p[22 * W + 10], 0));
	BOOST_CHECK_GT((p[22 * W + 22] >> 16) & 0xFF, 140u);
	BOOST_CHECK_GT((p[22 * W + 22] >> 8) & 0xFF, 140u);
	BOOST_CHECK_GT(p[22 * W + 22] & 0xFF, 140u);
	BOOST_CHECK_EQUAL(p[4 * W + 4], BACKGROUND);
	BOOST_CHECK_EQUAL(p[30 * W + 30], BACKGROUND);
}

BOOST_AUTO_TEST_CASE_TEMPLATE(nothing_queued_draws_nothing, F, Backends)
{
	Ui<F> u;
	u.renderer->begin_frame();
	u.renderer->begin_pass(u.target);
	u.ui->draw();
	u.renderer->end_pass();
	u.renderer->finish_frame();
}

BOOST_AUTO_TEST_CASE_TEMPLATE(what_is_out_of_its_order_or_unknown_is_refused, F, AllBackends)
{
	Ui<F> u;
	// a second frame queued before the first was drawn
	u.frame([] {});
	u.ui->queue(ImGui::GetDrawData(), W, H);
	u.frame([] {});
	BOOST_CHECK_THROW(u.ui->queue(ImGui::GetDrawData(), W, H), std::logic_error);
	// drawn outside a pass, or in a pass of a target of another size than it was queued for
	BOOST_CHECK_THROW(u.ui->draw(), std::logic_error);
	u.renderer->begin_frame();
	u.renderer->begin_pass(u.renderer->create_render_target(W / 2, H, PixelFormat::A8R8G8B8));
	BOOST_CHECK_THROW(u.ui->draw(), std::logic_error);
	u.renderer->end_pass();
	u.renderer->begin_pass(u.target);
	u.ui->draw();
	u.renderer->end_pass();
	// queued inside a frame: the buffers are written while the renderer is idle
	u.frame([] {});
	BOOST_CHECK_THROW(u.ui->queue(ImGui::GetDrawData(), W, H), std::logic_error);
	u.renderer->finish_frame();
	// a texture never added, a display of no area, a target of none, no texture
	u.frame([] { ImGui::GetForegroundDrawList()->AddImage(ImTextureID(uint64_t(999)), {8, 8}, {24, 24}); });
	BOOST_CHECK_THROW(u.ui->queue(ImGui::GetDrawData(), W, H), std::invalid_argument);
	u.frame([] {}, {0, 0});
	BOOST_CHECK_THROW(u.ui->queue(ImGui::GetDrawData(), W, H), std::invalid_argument);
	u.frame([] {});
	BOOST_CHECK_THROW(u.ui->queue(ImGui::GetDrawData(), 0, H), std::invalid_argument);
	BOOST_CHECK_THROW(u.ui->queue(nullptr, W, H), std::invalid_argument);
	BOOST_CHECK_THROW(u.ui->add_texture(nullptr), std::invalid_argument);
}

BOOST_AUTO_TEST_CASE_TEMPLATE(it_needs_a_renderer_and_an_imgui_context_whose_renderer_it_is_alone, F, AllBackends)
{
	F fixture;
	BOOST_REQUIRE_MESSAGE(fixture.renderer, "renderer unavailable: " << fixture.error);
	BOOST_CHECK_THROW(ImGuiRenderer{fixture.renderer}, std::logic_error);       // no context
	ImGuiContext *context = ImGui::CreateContext();
	BOOST_CHECK_THROW(ImGuiRenderer{nullptr}, std::invalid_argument);
	{
		ImGuiRenderer first(fixture.renderer);
		BOOST_CHECK(ImGui::GetIO().BackendRendererName != nullptr);
		BOOST_CHECK_THROW(ImGuiRenderer{fixture.renderer}, std::logic_error);   // the context has its renderer
	}
	BOOST_CHECK(ImGui::GetIO().BackendRendererName == nullptr);                  // given back
	{
		ImGuiRenderer again(fixture.renderer);
	}
	ImGui::DestroyContext(context);
}

BOOST_AUTO_TEST_SUITE_END()
