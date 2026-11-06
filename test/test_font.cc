// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <set>
#include <fstream>
#include <stdexcept>
#include <vector>
#include <boost/test/unit_test.hpp>
#include "test_fixture.h"
#include "font.h"
#include "material.h"
#include "vertex_array.h"
#include "index_array.h"
#include "uber_draw_call.h"
#include "test_backends.h"
#include "test_vulkan_fixture.h"

// make_text_texture and make_text_draw_call are deprecated; these tests keep what they do.
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

BOOST_AUTO_TEST_SUITE(font)

namespace
{

std::string resolve_test_font_path()
{
	char const *env_font = std::getenv("EDGE_TEST_FONT");
	if (env_font && env_font[0] != '\0')
	{
		return std::string(env_font);
	}

	std::vector<std::string> candidates;

	std::string const src = edge::TestFixture::graphic_sources_path();
	candidates.push_back(src + "test/data/arial.ttf");
	candidates.push_back(src + "test/data/DejaVuSans.ttf");
	candidates.push_back(src + "test/data/Roboto-Regular.ttf");

#if defined(__APPLE__)
	candidates.push_back("/System/Library/Fonts/Supplemental/Arial.ttf");
	candidates.push_back("/Library/Fonts/Arial.ttf");
	candidates.push_back("/System/Library/Fonts/Supplemental/Helvetica.ttc");
	candidates.push_back("/System/Library/Fonts/SFNS.ttf");
#elif defined(_WIN32)
	candidates.push_back("C:/Windows/Fonts/arial.ttf");
	candidates.push_back("C:/Windows/Fonts/segoeui.ttf");
#else
	candidates.push_back("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf");
	candidates.push_back("/usr/share/fonts/dejavu/DejaVuSans.ttf");
	candidates.push_back("/usr/local/share/fonts/DejaVuSans.ttf");
#endif

	for (auto const &candidate : candidates)
	{
		std::ifstream test_file(candidate.c_str(), std::ios::binary);
		if (test_file.good())
		{
			return candidate;
		}
	}

	throw std::runtime_error(
		"No usable font file found for test_font.cc; set EDGE_TEST_FONT to an absolute .ttf/.ttc path");
}

}

// --- the font alone: no renderer, no GPU -------------------------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(a_missing_file_or_one_that_is_not_a_font_is_refused)
{
	BOOST_CHECK_THROW(edge::Font("/no/such/font.ttf", 14), std::runtime_error);
	BOOST_CHECK_THROW(edge::Font(edge::TestFixture::graphic_sources_path() + "test/data/test_directions.png", 14),
		std::runtime_error);
}

BOOST_AUTO_TEST_CASE(a_line_is_a_mask_as_wide_as_its_advance_and_as_high_as_the_font)
{
	edge::Font const font(resolve_test_font_path(), 20);
	BOOST_CHECK_GT(font.ascent(), 0);
	BOOST_CHECK_GT(font.descent(), 0);
	BOOST_CHECK_LT(font.ascent() + font.descent(), 40);
	for (char const *line : {"", " ", "H", "Hello, world", "gjpqy"})
	{
		edge::TextMask const m = font.rasterise(line);
		BOOST_TEST_CONTEXT(line)
		{
			BOOST_CHECK_EQUAL(m.coverage.width(), int(std::ceil(font.advance(line))) + 2);
			BOOST_CHECK_EQUAL(m.coverage.height(), font.ascent() + font.descent() + 1);
		}
	}
	BOOST_CHECK_EQUAL(font.advance(""), 0.0f);
	BOOST_CHECK_GT(font.advance("HH"), font.advance("H"));
	BOOST_CHECK_GT(font.advance("H"), font.advance(" "));
}

BOOST_AUTO_TEST_CASE(the_ink_bounds_are_exactly_where_the_mask_has_coverage)
{
	edge::Font const font(resolve_test_font_path(), 20);
	for (char const *line : {"H", "Hello, world", "gjpqy", "0123456789"})
	{
		edge::TextMask const m = font.rasterise(line);
		int left = 1 << 30, top = 1 << 30, right = 0, bottom = 0;
		bool strong = false;
		for (int y = 0; y < m.coverage.height(); ++y)
			for (int x = 0; x < m.coverage.width(); ++x)
				if (m.coverage(x, y))
				{
					left = std::min(left, x);
					top = std::min(top, y);
					right = std::max(right, x + 1);
					bottom = std::max(bottom, y + 1);
					strong |= m.coverage(x, y) > 200;
				}
		BOOST_TEST_CONTEXT(line)
		{
			BOOST_CHECK_EQUAL(m.left, left);
			BOOST_CHECK_EQUAL(m.top, top);
			BOOST_CHECK_EQUAL(m.right, right);
			BOOST_CHECK_EQUAL(m.bottom, bottom);
			BOOST_CHECK(strong);
		}
	}
}

BOOST_AUTO_TEST_CASE(a_line_of_spaces_or_nothing_has_no_ink)
{
	edge::Font const font(resolve_test_font_path(), 20);
	for (char const *line : {"", " ", "   "})
	{
		edge::TextMask const m = font.rasterise(line);
		BOOST_TEST_CONTEXT("\"" << line << "\"")
		{
			BOOST_CHECK_EQUAL(m.left, 0);
			BOOST_CHECK_EQUAL(m.top, 0);
			BOOST_CHECK_EQUAL(m.right, 0);
			BOOST_CHECK_EQUAL(m.bottom, 0);
			for (int y = 0; y < m.coverage.height(); ++y)
				for (int x = 0; x < m.coverage.width(); ++x)
					BOOST_REQUIRE_EQUAL(m.coverage(x, y), 0);
		}
	}
}

BOOST_AUTO_TEST_CASE(capitals_stand_on_the_baseline_and_descenders_hang_under_it)
{
	edge::Font const font(resolve_test_font_path(), 24);
	edge::TextMask const h = font.rasterise("H"), g = font.rasterise("g");
	BOOST_CHECK_LE(std::abs(h.bottom - font.ascent()), 1);
	BOOST_CHECK_GT(g.bottom, font.ascent() + 1);
	BOOST_CHECK_LE(g.bottom, font.ascent() + font.descent() + 1);
	BOOST_CHECK_LT(h.top, font.ascent() / 2 + 2);       // a capital is more than half of the ascent high
}

BOOST_AUTO_TEST_CASE(a_larger_font_has_larger_ink_and_the_same_text_the_same_pixels)
{
	edge::Font const small(resolve_test_font_path(), 14), big(resolve_test_font_path(), 28);
	edge::TextMask const a = small.rasterise("Hello"), b = big.rasterise("Hello"), again = small.rasterise("Hello");
	BOOST_CHECK_GT(b.right - b.left, 3 * (a.right - a.left) / 2);
	BOOST_CHECK_GT(b.bottom - b.top, 3 * (a.bottom - a.top) / 2);
	BOOST_CHECK_GT(big.advance("Hello"), 3 * small.advance("Hello") / 2);
	BOOST_REQUIRE_EQUAL(a.coverage.width(), again.coverage.width());
	for (int y = 0; y < a.coverage.height(); ++y)
		for (int x = 0; x < a.coverage.width(); ++x)
			BOOST_REQUIRE_EQUAL(a.coverage(x, y), again.coverage(x, y));
}

BOOST_AUTO_TEST_CASE(glyphs_sit_at_fractions_of_a_pixel_so_a_wide_line_is_not_a_row_of_the_same_stamp)
{
	// "iiiiiiii": the advance is not a whole number of pixels, so the stems' coverage differs from one to the next.
	edge::Font const font(resolve_test_font_path(), 13);
	BOOST_REQUIRE_NE(font.advance("i"), std::floor(font.advance("i")));
	edge::TextMask const m = font.rasterise("iiiiiiii");
	int const y = (m.top + m.bottom) / 2;
	std::vector<int> row;
	for (int x = m.left; x < m.right; ++x)
		row.push_back(m.coverage(x, y));
	int const stamp = int(std::lround(font.advance("i")));
	std::set<std::vector<int>> stamps;
	for (size_t at = 0; at + stamp <= row.size(); at += stamp)
		stamps.emplace(row.begin() + at, row.begin() + at + stamp);
	BOOST_CHECK_GT(stamps.size(), 1u);
}

BOOST_AUTO_TEST_CASE(glyphs_that_touch_keep_the_more_covered_texel)
{
	edge::Font const font(resolve_test_font_path(), 40);
	// kerned in, the V's box lies over the A's feet, where its own texels are empty
	BOOST_REQUIRE_LT(font.advance("AV"), font.advance("A") + font.advance("V") - 1);
	edge::TextMask const a = font.rasterise("A"), av = font.rasterise("AV");
	int overlap = 0;
	for (int y = 0; y < a.coverage.height(); ++y)
		for (int x = 0; x < a.coverage.width() && x < av.coverage.width(); ++x)
		{
			BOOST_REQUIRE_GE(av.coverage(x, y), a.coverage(x, y));
			overlap += a.coverage(x, y) && av.coverage(x, y);
		}
	BOOST_CHECK_GT(overlap, 0);
}

// --- on a renderer ---------------------------------------------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE_TEMPLATE(make_texture, F, edge::Backends)
{
	F fixture;
	BOOST_REQUIRE_MESSAGE(fixture.renderer, "renderer unavailable: " << fixture.error);
	edge::RendererPtr const renderer = fixture.renderer;


	edge::Font const font(resolve_test_font_path(), 72);

	edge::TexturePtr texture = edge::make_text_texture(renderer, font, "bonjour", rove::vec<4>(1, 1, 1, 0), rove::vec<4>(0, 0.2f, 0.2f, 0));
	BOOST_REQUIRE(texture);
	BOOST_CHECK_GT(texture->width(), 0);
	BOOST_CHECK_GT(texture->height(), 0);
}

BOOST_AUTO_TEST_CASE_TEMPLATE(make_draw_call, F, edge::Backends)
{
	F fixture;
	BOOST_REQUIRE_MESSAGE(fixture.renderer, "renderer unavailable: " << fixture.error);
	edge::RendererPtr const renderer = fixture.renderer;

	std::string const src = edge::TestFixture::graphic_sources_path();

	edge::Font const font(resolve_test_font_path(), 72);

	std::shared_ptr<edge::Camera> camera(new edge::Camera());
	camera->set_orthographic_projection(renderer->screen_width(), renderer->screen_height(), -1, 1000);

	rove::matrix<4,4> view_matrix;
	view_matrix.translation(-renderer->screen_width() / 2, -renderer->screen_height() / 2, 0);
	view_matrix.scale(1, -1, 1);
	camera->set_view_matrix(view_matrix);

	edge::MaterialPtr hacked_material = renderer->create_material();
	hacked_material->set_diffuse_texture(edge::Texture::load_from_file(renderer,
		src + "test/data/test_directions.png"));

	edge::DrawCallPtr draw_call = edge::make_text_draw_call(renderer, font, rove::vec<3>(0, 0, 0), rove::vec<3>(100, 0, 0),
		rove::vec<3>(100, 100, 0), rove::vec<3>(0, 100, 0), "bonjour", rove::vec<4>(1, 1, 1, 0), rove::vec<4>(0, 0.2f, 0.2f, 0));
	draw_call->set_camera(camera);
	draw_call->set_always_visible(true);
	draw_call->commit();

	edge::DrawCallPtr hacked_draw_call = edge::make_text_draw_call(renderer, font, rove::vec<3>(0, 100, 0), rove::vec<3>(100, 100, 0),
		rove::vec<3>(100, 200, 0), rove::vec<3>(0, 200, 0), "bonjour", rove::vec<4>(1, 1, 1, 0), rove::vec<4>(0, 0.2f, 0.2f, 0));
	hacked_draw_call->set_material(hacked_material);
	hacked_draw_call->set_camera(camera);
	hacked_draw_call->commit();

	for (int i = 0; i < 20; ++i)
	{

			renderer->begin_frame();
			renderer->begin_pass(renderer->default_render_target());

			draw_call->execute();
			hacked_draw_call->execute();

			renderer->end_pass();
			renderer->finish_frame();
		}
}

// Text is UTF-8: a byte over 127 was taken as a Latin-1 character, so "é" in UTF-8 (C3 A9) drew as two glyphs, "Ã©".
BOOST_AUTO_TEST_CASE(text_is_utf8_so_an_accented_letter_is_one_glyph)
{
	edge::Font const font(resolve_test_font_path(), 24);
	std::string const e_acute = "\xC3\xA9";                 // é
	BOOST_CHECK_CLOSE(font.advance(e_acute), font.advance("e"), 1.0);                      // as wide as e, as the font draws it
	BOOST_CHECK_LT(font.advance(e_acute), font.advance("\xC3\x83\xC2\xA9") - 5);            // not the two glyphs of "Ã©"
	edge::TextMask const accented = font.rasterise(e_acute), plain = font.rasterise("e");
	BOOST_CHECK_LT(accented.top, plain.top);                                                 // the accent stands above the e
	BOOST_CHECK_EQUAL(accented.bottom, plain.bottom);
	// three- and four-byte characters are one each too
	BOOST_CHECK_CLOSE(font.advance("a\xE2\x80\x94" "b"), font.advance("a") + font.advance("\xE2\x80\x94") + font.advance("b"), 5.0);   // an em dash
	BOOST_CHECK_GT(font.advance("\xF0\x9F\x99\x82"), 0.0f);                                                                          // U+1F642
}

BOOST_AUTO_TEST_CASE(text_that_is_not_utf8_is_refused)
{
	edge::Font const font(resolve_test_font_path(), 24);
	for (std::string const bad : {std::string("\xE9"),                // Latin-1's é: a lead byte with nothing after
		                          std::string("\xA9"),                // a continuation with no lead
		                          std::string("ab\xC3"),              // cut off
		                          std::string("\xC0\xAF"),            // an overlong '/'
		                          std::string("\xED\xA0\x80"),        // a surrogate half
		                          std::string("\xF4\x90\x80\x80"),    // past U+10FFFF
		                          std::string("\xE2\x28\xA1")})       // a lead whose continuation is not one
	{
		BOOST_CHECK_THROW(font.advance(bad), std::invalid_argument);
		BOOST_CHECK_THROW(font.rasterise(bad), std::invalid_argument);
	}
}

BOOST_AUTO_TEST_SUITE_END()
