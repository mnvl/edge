// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

// Contract of next_mip(): each texel of the level under an image is the mean of the area it covers, averaged the way the texture's
// kind says -- as stored, as light (weighed by the coverage in alpha, the plain mean where nothing is covered), or as a direction -- and the
// sizes are the ones the GPU's mip chains have.

#include <boost/test/unit_test.hpp>

#include <cmath>
#include <cstdint>
#include <stdexcept>

#include <rove/vec.h>

#include "image.h"
#include "image_mips.h"

namespace
{

using bgra8 = rove::bgra<std::uint8_t>;

edge::Image<bgra8> filled(int width, int height, bgra8 const &c)
{
	return edge::Image<bgra8>(width, height, c);
}

/// Every component of both within @p tolerance.
bool close(bgra8 const &a, bgra8 const &b, int tolerance)
{
	for (int i = 0; i < 4; ++i)
		if (std::abs(int(a.i[i]) - int(b.i[i])) > tolerance)
			return false;
	return true;
}

}

BOOST_AUTO_TEST_SUITE(image_mips)

BOOST_AUTO_TEST_CASE(a_level_is_half_the_size_down_to_one_texel_on_each_side)
{
	auto size = [](int w, int h, edge::MipKind kind = edge::MipKind::Linear) {
		auto const level = edge::next_mip(filled(w, h, bgra8(1, 2, 3, 4)), kind);
		return std::make_pair(level.width(), level.height());
	};
	BOOST_TEST((size(8, 8) == std::make_pair(4, 4)));
	BOOST_TEST((size(8, 2) == std::make_pair(4, 1)));
	BOOST_TEST((size(1, 8) == std::make_pair(1, 4)));
	BOOST_TEST((size(2, 1) == std::make_pair(1, 1)));
	BOOST_TEST((size(5, 3) == std::make_pair(2, 1)));
	BOOST_TEST((size(7, 7, edge::MipKind::Srgb) == std::make_pair(3, 3)));
	BOOST_TEST((size(6, 6, edge::MipKind::Normal) == std::make_pair(3, 3)));
}

BOOST_AUTO_TEST_CASE(a_level_of_one_texel_has_none_under_it)
{
	BOOST_CHECK_THROW(edge::next_mip(filled(1, 1, bgra8()), edge::MipKind::Linear), std::invalid_argument);
}

BOOST_AUTO_TEST_CASE(a_flat_image_stays_the_colour_it_is_in_every_kind)
{
	// the extremes and something between, in every channel, odd sides and even
	for (bgra8 const &c : {bgra8(0, 0, 0, 0), bgra8(255, 255, 255, 255), bgra8(10, 120, 250, 77)})
		for (int side : {2, 3, 5, 8})
		{
			BOOST_TEST((edge::next_mip(filled(side, side, c), edge::MipKind::Linear)(0, 0) == c));
			// a round trip through light may be one off in the dark, where sRGB steps are the finest
			BOOST_TEST(close(edge::next_mip(filled(side, side, c), edge::MipKind::Srgb)(0, 0), c, 1));
		}
	// a direction made of a unit vector stays it (a sideways one, up, and one tilted)
	for (bgra8 const &c : {bgra8(255, 128, 128, 255), bgra8(128, 128, 255, 255), bgra8(230, 128, 204, 255)})
		BOOST_TEST(close(edge::next_mip(filled(4, 4, c), edge::MipKind::Normal)(1, 1), c, 1));
}

BOOST_AUTO_TEST_CASE(linear_averages_the_values_as_stored_each_channel_by_itself)
{
	edge::Image<bgra8> image(2, 2);
	image(0, 0) = bgra8(0, 0, 0, 0);
	image(1, 0) = bgra8(100, 0, 255, 0);
	image(0, 1) = bgra8(0, 40, 255, 0);
	image(1, 1) = bgra8(100, 0, 255, 255);
	auto const level = edge::next_mip(image, edge::MipKind::Linear);
	// (b, g, r, a) means: 50, 10, 191.25 (to 191), 63.75 (to 64)
	BOOST_TEST(close(level(0, 0), bgra8(50, 10, 191, 64), 1));
}

BOOST_AUTO_TEST_CASE(each_texel_averages_its_own_area_not_its_neighbours)
{
	edge::Image<bgra8> image(4, 2);
	for (int x = 0; x < 4; ++x)
		for (int y = 0; y < 2; ++y)
			image(x, y) = bgra8(std::uint8_t(x < 2 ? 0 : 200), 0, 0, 255);
	auto const level = edge::next_mip(image, edge::MipKind::Linear);
	BOOST_TEST(level(0, 0).b == 0);
	BOOST_TEST(level(1, 0).b == 200);
}

BOOST_AUTO_TEST_CASE(an_odd_side_gives_the_middle_texel_to_both_neighbours_by_the_part_of_it_each_area_covers)
{
	// 5 texels into 2: the first covers texels 0 and 1 and half of 2, the second half of 2 and 3 and 4
	edge::Image<bgra8> image(5, 1);
	std::uint8_t const values[] = {0, 0, 100, 200, 200};
	for (int x = 0; x < 5; ++x)
		image(x, 0) = bgra8(values[x], 0, 0, 255);
	auto const level = edge::next_mip(image, edge::MipKind::Linear);
	BOOST_TEST(close(level(0, 0), bgra8(20, 0, 0, 255), 1));      // (0 + 0 + 50) / 2.5
	BOOST_TEST(close(level(1, 0), bgra8(180, 0, 0, 255), 1));     // (50 + 200 + 200) / 2.5

	// 3 texels into 1: all three, a third each
	edge::Image<bgra8> three(3, 1);
	three(0, 0) = bgra8(0, 0, 0, 255);
	three(1, 0) = bgra8(0, 0, 0, 255);
	three(2, 0) = bgra8(255, 0, 0, 255);
	BOOST_TEST(edge::next_mip(three, edge::MipKind::Linear)(0, 0).b == 85);
}

BOOST_AUTO_TEST_CASE(srgb_averages_light_so_black_and_white_make_the_grey_of_half_the_light)
{
	edge::Image<bgra8> image(2, 1);
	image(0, 0) = bgra8(0, 0, 0, 255);
	image(1, 0) = bgra8(255, 255, 255, 255);
	auto const level = edge::next_mip(image, edge::MipKind::Srgb);
	// half the light is 0.5 linear, which sRGB writes as 0.7354 (188 of 255), not as 128
	BOOST_TEST(close(level(0, 0), bgra8(188, 188, 188, 255), 1));
}

BOOST_AUTO_TEST_CASE(srgb_averages_alpha_as_it_is)
{
	edge::Image<bgra8> image(2, 1);
	image(0, 0) = bgra8(0, 0, 0, 0);
	image(1, 0) = bgra8(0, 0, 0, 200);
	BOOST_TEST(edge::next_mip(image, edge::MipKind::Srgb)(0, 0).a == 100);
}

BOOST_AUTO_TEST_CASE(srgb_weighs_the_light_by_the_coverage_so_a_clear_texel_lends_no_colour)
{
	// a cut-out (a leaf card) beside the clear texels round it: the clear ones are not seen, so their colour (black, in an atlas's empty space)
	// must not darken the covered one's in the smaller levels, where the shader keeps the cut-out by raising its alpha
	edge::Image<bgra8> image(2, 1);
	image(0, 0) = bgra8(255, 255, 255, 255);
	image(1, 0) = bgra8(0, 0, 0, 0);
	BOOST_TEST(close(edge::next_mip(image, edge::MipKind::Srgb)(0, 0), bgra8(255, 255, 255, 128), 1));
	// covered by a third: a third of its light in the mean of the two (1 x 1 + 0 x 1/3) / (1 + 1/3) = 0.75 linear, 225 in sRGB
	image(1, 0) = bgra8(0, 0, 0, 85);
	BOOST_TEST(close(edge::next_mip(image, edge::MipKind::Srgb)(0, 0), bgra8(225, 225, 225, 170), 1));
}

BOOST_AUTO_TEST_CASE(srgb_where_nothing_is_covered_keeps_the_plain_mean_of_the_light)
{
	// a cut-out's clear texels are bled with its colour so that its edge does not fringe: an area all clear keeps that colour for the levels under it
	edge::Image<bgra8> image(2, 1);
	image(0, 0) = bgra8(0, 0, 0, 0);
	image(1, 0) = bgra8(255, 255, 255, 0);
	BOOST_TEST(close(edge::next_mip(image, edge::MipKind::Srgb)(0, 0), bgra8(188, 188, 188, 0), 1));
}

BOOST_AUTO_TEST_CASE(resizing_weighs_srgb_by_the_coverage_too_and_linear_not)
{
	edge::Image<bgra8> image(4, 1, bgra8(0, 0, 0, 0));
	image(0, 0) = bgra8(255, 255, 255, 255);
	BOOST_TEST(close(edge::resize(image, 1, 1, edge::MipKind::Srgb)(0, 0), bgra8(255, 255, 255, 64), 1));
	BOOST_TEST(close(edge::resize(image, 1, 1, edge::MipKind::Linear)(0, 0), bgra8(64, 64, 64, 64), 1));
}

BOOST_AUTO_TEST_CASE(normals_two_opposite_tilts_average_to_straight_up_at_unit_length)
{
	// directions (+x tilted from up) and (-x tilted from up) in bgra: blue is z, green y, red x
	edge::Image<bgra8> image(2, 1);
	image(0, 0) = bgra8(220, 128, 240, 255);       // x +0.88, z +0.72 (not unit: the file's rounding)
	image(1, 0) = bgra8(220, 128, 16, 255);        // x -0.88
	auto const level = edge::next_mip(image, edge::MipKind::Normal);
	BOOST_TEST(close(level(0, 0), bgra8(255, 128, 128, 255), 1));
}

BOOST_AUTO_TEST_CASE(normals_come_out_unit_length_whatever_went_in)
{
	// four tilted four ways: the plain mean of the encoded values would be shorter than any of them
	edge::Image<bgra8> image(2, 2);
	image(0, 0) = bgra8(200, 128, 230, 255);
	image(1, 0) = bgra8(200, 230, 128, 255);
	image(0, 1) = bgra8(200, 128, 26, 255);
	image(1, 1) = bgra8(150, 26, 150, 255);
	auto const p = edge::next_mip(image, edge::MipKind::Normal)(0, 0);
	double const x = p.r / 255.0 * 2 - 1, y = p.g / 255.0 * 2 - 1, z = p.b / 255.0 * 2 - 1;
	BOOST_TEST(std::sqrt(x * x + y * y + z * z) == 1.0, boost::test_tools::tolerance(0.02));
	BOOST_TEST(z > 0.8);
}

BOOST_AUTO_TEST_CASE(normals_that_cancel_exactly_have_no_mean_direction_and_face_up)
{
	edge::Image<bgra8> opposite(2, 1);
	opposite(0, 0) = bgra8(255, 255, 255, 255);    // (1, 1, 1)
	opposite(1, 0) = bgra8(0, 0, 0, 255);          // (-1, -1, -1)
	auto const p = edge::next_mip(opposite, edge::MipKind::Normal)(0, 0);
	BOOST_TEST(close(p, bgra8(255, 128, 128, 255), 1));
}

BOOST_AUTO_TEST_CASE(normals_average_alpha_as_it_is)
{
	edge::Image<bgra8> image(2, 1, bgra8(255, 128, 128, 40));
	image(1, 0).a = 80;
	BOOST_TEST(edge::next_mip(image, edge::MipKind::Normal)(0, 0).a == 60);
}

BOOST_AUTO_TEST_CASE(a_view_with_a_pitch_is_read_by_its_rows_not_the_pixels_after_them)
{
	// 2 x 2 of a 4 x 2 image: the right half is 255 and must not be seen
	edge::Image<bgra8> image(4, 2, bgra8(255, 255, 255, 255));
	for (int x = 0; x < 2; ++x)
		for (int y = 0; y < 2; ++y)
			image(x, y) = bgra8(10, 10, 10, 10);
	edge::ImageView<bgra8 const> const left(image.pixels(), 2, 2, 4 * sizeof(bgra8));
	BOOST_TEST((edge::next_mip(left, edge::MipKind::Linear)(0, 0) == bgra8(10, 10, 10, 10)));
}

BOOST_AUTO_TEST_CASE(resizing_to_the_same_size_changes_nothing)
{
	edge::Image<bgra8> image(5, 3);
	for (int y = 0; y < 3; ++y)
		for (int x = 0; x < 5; ++x)
			image(x, y) = bgra8(std::uint8_t(x * 40 + y), std::uint8_t(y * 70), std::uint8_t(200 - x * 9), std::uint8_t(x + y * 50));
	for (auto kind : {edge::MipKind::Linear, edge::MipKind::Srgb})
	{
		auto const same = edge::resize(image, 5, 3, kind);
		for (int y = 0; y < 3; ++y)
			for (int x = 0; x < 5; ++x)
				BOOST_REQUIRE_MESSAGE(close(same(x, y), image(x, y), 1), "texel " << x << "," << y);
	}
}

BOOST_AUTO_TEST_CASE(resizing_to_half_is_the_mip)
{
	edge::Image<bgra8> image(6, 4);
	for (int y = 0; y < 4; ++y)
		for (int x = 0; x < 6; ++x)
			image(x, y) = bgra8(std::uint8_t(x * 40), std::uint8_t(y * 70), std::uint8_t(x * y * 9), 255);
	auto const a = edge::resize(image, 3, 2, edge::MipKind::Srgb);
	auto const b = edge::next_mip(image, edge::MipKind::Srgb);
	for (int y = 0; y < 2; ++y)
		for (int x = 0; x < 3; ++x)
			BOOST_TEST((a(x, y) == b(x, y)));
}

BOOST_AUTO_TEST_CASE(a_larger_image_is_interpolated_between_texel_centres_and_keeps_its_edge)
{
	// 2 texels into 4: the new centres are at 1/8, 3/8, 5/8, 7/8 of the width; the old at 1/4 and 3/4
	edge::Image<bgra8> image(2, 1);
	image(0, 0) = bgra8(0, 0, 0, 255);
	image(1, 0) = bgra8(200, 0, 0, 255);
	auto const big = edge::resize(image, 4, 1, edge::MipKind::Linear);
	BOOST_TEST(big(0, 0).b == 0);                      // before the first centre: the edge repeated
	BOOST_TEST(close(big(1, 0), bgra8(50, 0, 0, 255), 1));
	BOOST_TEST(close(big(2, 0), bgra8(150, 0, 0, 255), 1));
	BOOST_TEST(big(3, 0).b == 200);
}

BOOST_AUTO_TEST_CASE(resizing_one_side_up_and_the_other_down_does_each_its_own_way)
{
	edge::Image<bgra8> image(4, 2);
	for (int x = 0; x < 4; ++x)
	{
		image(x, 0) = bgra8(std::uint8_t(x * 60), 0, 0, 255);
		image(x, 1) = bgra8(std::uint8_t(x * 60), 100, 0, 255);
	}
	auto const r = edge::resize(image, 2, 4, edge::MipKind::Linear);
	BOOST_REQUIRE(r.width() == 2 && r.height() == 4);
	BOOST_TEST(close(r(0, 0), bgra8(30, 0, 0, 255), 1));        // the mean of texels 0 and 1, rows 0
	BOOST_TEST(close(r(1, 3), bgra8(150, 100, 0, 255), 1));     // the mean of 2 and 3, row 1
	BOOST_TEST(close(r(0, 1), bgra8(30, 25, 0, 255), 1));       // between the rows' centres: a quarter of the way down
}

BOOST_AUTO_TEST_CASE(resizing_to_nothing_is_refused)
{
	edge::Image<bgra8> image(2, 2);
	BOOST_CHECK_THROW(edge::resize(image, 0, 2, edge::MipKind::Linear), std::invalid_argument);
	BOOST_CHECK_THROW(edge::resize(image, 2, -1, edge::MipKind::Linear), std::invalid_argument);
	BOOST_CHECK_THROW(edge::resize(edge::ImageView<bgra8 const>(nullptr, 0, 0, 0), 2, 2, edge::MipKind::Linear), std::invalid_argument);
}

BOOST_AUTO_TEST_CASE(a_texel_goes_to_numbers_and_back_in_every_kind)
{
	for (auto kind : {edge::MipKind::Linear, edge::MipKind::Srgb})
		for (int v : {0, 1, 17, 128, 254, 255})
		{
			bgra8 const t(std::uint8_t(v), std::uint8_t(255 - v), std::uint8_t(v / 2), std::uint8_t(v));
			BOOST_TEST((edge::encode_texel(edge::decode_texel(t, kind), kind) == t));
		}
}

namespace
{

/// 4 x 2: texel (x, y) has blue 40 * x + 100 * y, so that every texel differs and a neighbour is told from another.
edge::Image<bgra8> ramp()
{
	edge::Image<bgra8> image(4, 2);
	for (int y = 0; y < 2; ++y)
		for (int x = 0; x < 4; ++x)
			image(x, y) = bgra8(std::uint8_t(40 * x + 100 * y), 0, 0, 255);
	return image;
}

}

BOOST_AUTO_TEST_CASE(a_chain_goes_down_to_one_texel)
{
	edge::MipChain const chain(ramp(), edge::MipKind::Linear);
	BOOST_REQUIRE_EQUAL(chain.levels(), 3);
	BOOST_TEST(chain.level(0).width() == 4);
	BOOST_TEST(chain.level(1).width() == 2);
	BOOST_TEST(chain.level(1).height() == 1);
	BOOST_TEST(chain.level(2).width() == 1);
	BOOST_TEST(edge::MipChain(edge::Image<bgra8>(1, 1), edge::MipKind::Srgb).levels() == 1);
}

BOOST_AUTO_TEST_CASE(sampling_a_texel_centre_gives_the_texel)
{
	edge::MipChain const chain(ramp(), edge::MipKind::Linear);
	for (int y = 0; y < 2; ++y)
		for (int x = 0; x < 4; ++x)
			BOOST_TEST((chain.sample((float(x) + 0.5f) / 4, (float(y) + 0.5f) / 2, 0) == chain.level(0)(x, y)), "texel " << x << "," << y);
}

BOOST_AUTO_TEST_CASE(sampling_between_centres_blends_the_nearest_texels_by_how_near)
{
	edge::MipChain const chain(ramp(), edge::MipKind::Linear);
	// u between texel 1 (40) and texel 2 (80) at a quarter of the way: 50; v half way between the rows (+50)
	BOOST_TEST(close(chain.sample((1.5f + 0.25f) / 4, 0.5f / 2, 0), bgra8(50, 0, 0, 255), 1));
	BOOST_TEST(close(chain.sample((1.5f + 0.25f) / 4, 1.0f / 2, 0), bgra8(100, 0, 0, 255), 1));
}

BOOST_AUTO_TEST_CASE(sampling_repeats_the_image_in_every_direction)
{
	edge::MipChain const chain(ramp(), edge::MipKind::Linear);
	for (float shift : {1.0f, 2.0f, -1.0f, -3.0f, 10.0f})
	{
		BOOST_TEST((chain.sample(0.375f + shift, 0.75f + shift, 0) == chain.sample(0.375f, 0.75f, 0)), "shifted by " << shift);
		BOOST_TEST((chain.sample(0.0f + shift, 0.25f - shift, 0) == chain.sample(0.0f, 0.25f, 0)), "shifted by " << shift);
	}
	// across the seam: the last texel (120) and the first (0) are neighbours: half way between them is their mean
	BOOST_TEST(close(chain.sample(1.0f, 0.25f, 0), bgra8(60, 0, 0, 255), 1));
}

BOOST_AUTO_TEST_CASE(a_level_of_detail_is_the_level_and_between_two_the_blend)
{
	edge::MipChain const chain(ramp(), edge::MipKind::Linear);
	BOOST_TEST((chain.sample(0.25f, 0.5f, 1) == chain.level(1)(0, 0)));
	BOOST_TEST((chain.sample(0.5f, 0.5f, 2) == chain.level(2)(0, 0)));
	bgra8 const a = chain.level(1)(0, 0), b = chain.level(2)(0, 0);
	BOOST_TEST(close(chain.sample(0.25f, 0.5f, 1.5f), bgra8(std::uint8_t((a.b + b.b) / 2), 0, 0, 255), 2));
}

BOOST_AUTO_TEST_CASE(a_level_of_detail_past_the_last_level_is_the_last_and_one_below_the_first_is_the_first)
{
	edge::MipChain const chain(ramp(), edge::MipKind::Linear);
	BOOST_TEST((chain.sample(0.3f, 0.3f, 9) == chain.level(2)(0, 0)));
	BOOST_TEST((chain.sample(0.375f, 0.25f, -3) == chain.sample(0.375f, 0.25f, 0)));
}

BOOST_AUTO_TEST_SUITE_END()
