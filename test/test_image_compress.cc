// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

// Contract of compress_bc7(): the bytes are the blocks of a BC7 texture of the image's size, and decoded they are the image, to the
// error BC7 has, with no channel swapped, no row or column of a neighbour's block, and no junk where the image's edge is not a
// block's.  The decoder is bc7enc's own, a different code from the encoder.

#include <boost/test/unit_test.hpp>

#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <vector>

#include <rove/vec.h>

#include "image.h"
#include "image_compress.h"
#include "pixel.h"
#include "test_astc.h"
#include "test_bc7.h"
#include "texture.h"

namespace
{

using bgra8 = rove::bgra<std::uint8_t>;

edge::Image<bgra8> decode(std::vector<std::uint8_t> const &blocks, int width, int height)
{
	return edge::decode_bc7(blocks.data(), blocks.size(), width, height);
}

/// Peak signal to noise of the colour channels of two images, in dB.
double psnr(edge::Image<bgra8> const &a, edge::Image<bgra8> const &b)
{
	BOOST_REQUIRE(a.width() == b.width() && a.height() == b.height());
	double sum = 0;
	for (int y = 0; y < a.height(); ++y)
		for (int x = 0; x < a.width(); ++x)
			for (int c = 0; c < 3; ++c)
			{
				double const d = double(a(x, y).i[c]) - double(b(x, y).i[c]);
				sum += d * d;
			}
	double const mse = sum / (3.0 * a.width() * a.height());
	return mse == 0 ? 200.0 : 10 * std::log10(255.0 * 255.0 / mse);
}

bool close(bgra8 const &a, bgra8 const &b, int tolerance)
{
	for (int i = 0; i < 4; ++i)
		if (std::abs(int(a.i[i]) - int(b.i[i])) > tolerance)
			return false;
	return true;
}

/// A smooth picture: gradients in each channel, different ways, which a block of 16 texels takes well.
edge::Image<bgra8> gradients(int width, int height)
{
	edge::Image<bgra8> image(width, height);
	for (int y = 0; y < height; ++y)
		for (int x = 0; x < width; ++x)
			image(x, y) = bgra8(std::uint8_t(255 * x / (width - 1)), std::uint8_t(255 * y / (height - 1)),
				std::uint8_t(255 * (x + y) / (width + height - 2)), 255);
	return image;
}

}

BOOST_AUTO_TEST_SUITE(image_compress)

BOOST_AUTO_TEST_CASE(the_bytes_are_the_size_of_a_BC7_texture_of_the_image)
{
	for (auto const &size : {std::pair{4, 4}, {8, 8}, {5, 7}, {1, 1}, {3, 16}, {33, 2}})
	{
		auto const blocks = edge::compress_bc7(edge::Image<bgra8>(size.first, size.second, bgra8(9, 9, 9, 255)), edge::BlockContent::Data);
		BOOST_TEST(blocks.size() == edge::pixel_data_size(size.first, size.second, edge::PixelFormat::BC7),
			size.first << " x " << size.second);
		BOOST_TEST(blocks.size() == edge::Texture::level_size(size.first, size.second, edge::PixelFormat::BC7, 0));
	}
}

BOOST_AUTO_TEST_CASE(an_image_of_no_pixels_is_refused)
{
	BOOST_CHECK_THROW(edge::compress_bc7(edge::ImageView<bgra8 const>(nullptr, 0, 0, 0), edge::BlockContent::Data), std::invalid_argument);
}

BOOST_AUTO_TEST_CASE(a_flat_colour_decodes_to_itself_with_every_channel_where_it_was)
{
	// four different channels: a swap of two shows
	for (auto content : {edge::BlockContent::Colour, edge::BlockContent::Data})
		for (bgra8 const &c : {bgra8(10, 120, 250, 255), bgra8(250, 10, 120, 255), bgra8(0, 0, 0, 255), bgra8(255, 255, 255, 255), bgra8(7, 77, 177, 31)})
		{
			auto const decoded = decode(edge::compress_bc7(edge::Image<bgra8>(8, 8, c), content), 8, 8);
			for (int y = 0; y < 8; ++y)
				for (int x = 0; x < 8; ++x)
					BOOST_REQUIRE_MESSAGE(close(decoded(x, y), c, 1), "texel " << x << "," << y << " of colour " << int(c.b) << "," << int(c.g) << "," << int(c.r) << "," << int(c.a)
						<< " came out " << int(decoded(x, y).b) << "," << int(decoded(x, y).g) << "," << int(decoded(x, y).r) << "," << int(decoded(x, y).a));
		}
}

BOOST_AUTO_TEST_CASE(a_smooth_picture_decodes_close_to_itself)
{
	auto const image = gradients(32, 24);
	for (auto content : {edge::BlockContent::Colour, edge::BlockContent::Data})
		BOOST_TEST(psnr(image, decode(edge::compress_bc7(image, content), 32, 24)) > 38.0);
}

BOOST_AUTO_TEST_CASE(a_texel_is_in_the_block_of_its_place_whatever_the_blocks_around)
{
	// left half red, right half green, top rows blue: blocks differ in a way that a block order mistake would swap
	edge::Image<bgra8> image(16, 8);
	for (int y = 0; y < 8; ++y)
		for (int x = 0; x < 16; ++x)
			image(x, y) = y < 4 ? bgra8(250, 0, 0, 255) : x < 8 ? bgra8(0, 0, 250, 255) : bgra8(0, 250, 0, 255);
	auto const decoded = decode(edge::compress_bc7(image, edge::BlockContent::Data), 16, 8);
	for (int y = 0; y < 8; ++y)
		for (int x = 0; x < 16; ++x)
			BOOST_REQUIRE_MESSAGE(close(decoded(x, y), image(x, y), 2), "texel " << x << "," << y);
}

BOOST_AUTO_TEST_CASE(alpha_is_kept_where_it_is_opaque_and_where_it_is_clear)
{
	edge::Image<bgra8> image(8, 4);
	for (int y = 0; y < 4; ++y)
		for (int x = 0; x < 8; ++x)
			image(x, y) = x < 4 ? bgra8(40, 80, 200, 255) : bgra8(40, 80, 200, 0);
	auto const decoded = decode(edge::compress_bc7(image, edge::BlockContent::Colour), 8, 4);
	for (int y = 0; y < 4; ++y)
	{
		BOOST_TEST(decoded(1, y).a >= 254);        // (an opaque block may come out 254: the mode shares one bit among all four channels)
		BOOST_TEST(decoded(6, y).a == 0);
		BOOST_TEST(decoded(1, y).r >= 198);
	}
}

BOOST_AUTO_TEST_CASE(a_side_that_is_not_a_block_has_its_last_texels_repeated_so_the_edge_stays_clean)
{
	// the image's edge is the colour of its last column and row, not black and not the first row's
	edge::Image<bgra8> image(5, 7, bgra8(30, 60, 90, 255));
	for (int x = 0; x < 5; ++x)
		image(x, 0) = bgra8(250, 250, 250, 255);
	auto const decoded = decode(edge::compress_bc7(image, edge::BlockContent::Data), 5, 7);
	for (int y = 0; y < 7; ++y)
		for (int x = 0; x < 5; ++x)
			BOOST_REQUIRE_MESSAGE(close(decoded(x, y), image(x, y), 2), "texel " << x << "," << y);
}

BOOST_AUTO_TEST_CASE(the_same_image_gives_the_same_bytes_and_a_big_one_too_across_the_cores)
{
	auto const image = gradients(256, 130);
	auto const a = edge::compress_bc7(image, edge::BlockContent::Colour);
	BOOST_CHECK(a == edge::compress_bc7(image, edge::BlockContent::Colour));
	// the picture's last rows are not the first's: the parts the cores had are put together in order
	BOOST_TEST(psnr(image, decode(a, 256, 130)) > 38.0);
}

BOOST_AUTO_TEST_SUITE_END()

// compress_astc(): the bytes are the blocks of an ASTC texture of the image's size and the block asked for, and decoded (astc-encoder's
// decoder) they are the image, to the error ASTC has at that size, with no channel swapped and no block out of its place.

BOOST_AUTO_TEST_SUITE(image_compress_astc)

BOOST_AUTO_TEST_CASE(the_bytes_are_the_size_of_an_ASTC_texture_of_the_image_and_the_block)
{
	for (auto const &[block, format] : {std::pair{4, edge::PixelFormat::ASTC_4x4}, {6, edge::PixelFormat::ASTC_6x6}, {8, edge::PixelFormat::ASTC_8x8}})
		for (auto const &size : {std::pair{4, 4}, {8, 8}, {5, 7}, {1, 1}, {3, 16}, {33, 2}, {12, 12}})
		{
			auto const blocks = edge::compress_astc(edge::Image<bgra8>(size.first, size.second, bgra8(9, 9, 9, 255)), block, false);
			BOOST_TEST(blocks.size() == edge::pixel_data_size(size.first, size.second, format), block << ": " << size.first << " x " << size.second);
			BOOST_TEST(blocks.size() == edge::Texture::level_size(size.first, size.second, format, 0));
		}
}

BOOST_AUTO_TEST_CASE(a_block_astc_has_not_or_an_image_of_no_pixels_is_refused)
{
	edge::Image<bgra8> const image(8, 8, bgra8(9, 9, 9, 255));
	for (int block : {0, 3, 5, 10, 12})
		BOOST_CHECK_THROW(edge::compress_astc(image, block, false), std::invalid_argument);
	BOOST_CHECK_THROW(edge::compress_astc(edge::ImageView<bgra8 const>(nullptr, 0, 0, 0), 4, false), std::invalid_argument);
}

BOOST_AUTO_TEST_CASE(a_flat_colour_decodes_to_itself_with_every_channel_where_it_was)
{
	for (int block : {4, 6, 8})
		for (bool srgb : {false, true})
			for (bgra8 const &c : {bgra8(10, 120, 250, 255), bgra8(250, 10, 120, 255), bgra8(0, 0, 0, 255), bgra8(255, 255, 255, 255), bgra8(7, 77, 177, 31)})
			{
				auto const blocks = edge::compress_astc(edge::Image<bgra8>(12, 12, c), block, srgb);
				auto const decoded = edge::decode_astc(blocks.data(), blocks.size(), 12, 12, block, srgb);
				for (int y = 0; y < 12; ++y)
					for (int x = 0; x < 12; ++x)
						BOOST_REQUIRE_MESSAGE(close(decoded(x, y), c, 2), "block " << block << (srgb ? " sRGB" : "") << ", texel " << x << "," << y
							<< " of colour " << int(c.b) << "," << int(c.g) << "," << int(c.r) << "," << int(c.a) << " came out " << int(decoded(x, y).b)
							<< "," << int(decoded(x, y).g) << "," << int(decoded(x, y).r) << "," << int(decoded(x, y).a));
			}
}

BOOST_AUTO_TEST_CASE(a_smooth_picture_decodes_close_to_itself_the_closer_the_smaller_the_block)
{
	auto const image = gradients(48, 36);
	double last = 1000;
	for (int block : {4, 6, 8})
	{
		auto const blocks = edge::compress_astc(image, block, false);
		double const quality = psnr(image, edge::decode_astc(blocks.data(), blocks.size(), 48, 36, block, false));
		BOOST_TEST(quality > 36.0, block << "x" << block << ": " << quality << " dB");
		BOOST_TEST(quality <= last + 0.5);
		last = quality;
	}
}

BOOST_AUTO_TEST_CASE(a_texel_is_in_the_block_of_its_place_whatever_the_blocks_around)
{
	// quadrants of their own colour, each a whole number of 4x4 blocks: a block order mistake would swap them
	edge::Image<bgra8> image(16, 8);
	for (int y = 0; y < 8; ++y)
		for (int x = 0; x < 16; ++x)
			image(x, y) = y < 4 ? (x < 8 ? bgra8(250, 0, 0, 255) : bgra8(0, 0, 0, 255)) : x < 8 ? bgra8(0, 0, 250, 255) : bgra8(0, 250, 0, 255);
	auto const blocks = edge::compress_astc(image, 4, false);
	auto const decoded = edge::decode_astc(blocks.data(), blocks.size(), 16, 8, 4, false);
	for (int y = 0; y < 8; ++y)
		for (int x = 0; x < 16; ++x)
			BOOST_REQUIRE_MESSAGE(close(decoded(x, y), image(x, y), 3), "texel " << x << "," << y);
}

BOOST_AUTO_TEST_CASE(srgb_is_the_curve_the_error_is_judged_on_and_the_texture_is_sampled_with)
{
	// a dark gradient: encoded for sRGB and decoded as sRGB it is close; decoded as linear it is not what was asked
	edge::Image<bgra8> image(24, 24);
	for (int y = 0; y < 24; ++y)
		for (int x = 0; x < 24; ++x)
			image(x, y) = bgra8(std::uint8_t(x * 2), std::uint8_t(y * 2), std::uint8_t(x + y), 255);
	auto const srgb = edge::compress_astc(image, 6, true);
	BOOST_TEST(psnr(image, edge::decode_astc(srgb.data(), srgb.size(), 24, 24, 6, true)) > 36.0);
	BOOST_CHECK(srgb != edge::compress_astc(image, 6, false));
}

BOOST_AUTO_TEST_CASE(the_same_image_gives_the_same_bytes_and_a_big_one_too_across_the_cores)
{
	auto const image = gradients(256, 130);
	auto const a = edge::compress_astc(image, 6, true);
	BOOST_CHECK(a == edge::compress_astc(image, 6, true));
	BOOST_TEST(psnr(image, edge::decode_astc(a.data(), a.size(), 256, 130, 6, true)) > 36.0);
}

BOOST_AUTO_TEST_SUITE_END()
