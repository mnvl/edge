// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

// decode_texture(): a picture file's bytes (PNG, JPEG, ... or a DDS) as the texels of a texture, without a renderer -- what glTF loading
// and Texture::load_from_memory() make textures of.  The 8-bit texels are B, G, R, A; compression is BC1 or BC3 by the alpha; a DDS
// brings its blocks and levels as they are.

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

#include <boost/test/unit_test.hpp>

#include "image.h"
#include "pixel.h"
#include "serialized_model.h"
#include "test_fixture.h"
#include "texture.h"
#include "texture_io.h"

using namespace edge;

namespace
{

using bgra8 = rove::bgra<std::uint8_t>;

PixelFormat const BC1 = PixelFormat::BC1, BC3 = PixelFormat::BC3, BC7 = PixelFormat::BC7, X8R8G8B8 = PixelFormat::X8R8G8B8,
	A8R8G8B8 = PixelFormat::A8R8G8B8;

std::vector<char> bytes_of(std::filesystem::path const &path)
{
	std::ifstream in(path, std::ios::binary);
	BOOST_REQUIRE_MESSAGE(in, path.string());
	return std::vector<char>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::string asset(std::string const &name)
{
	return TestFixture::graphic_sources_path() + "test/data/" + name;
}

/// A PNG of @p width x @p height whose texel (x, y) is f(x, y).
template <class F> std::vector<char> png_of(int width, int height, F f)
{
	Image<bgra8> image(width, height);
	for (int y = 0; y < height; ++y)
		for (int x = 0; x < width; ++x)
			image(x, y) = f(x, y);
	std::filesystem::path const path = std::filesystem::temp_directory_path() / "edge_texture_io.png";
	save_image<bgra8>(path, image);
	std::vector<char> bytes = bytes_of(path);
	std::filesystem::remove(path);
	return bytes;
}

SerializedTexture decode(std::vector<char> const &bytes, TextureDecoding const &how = TextureDecoding())
{
	return decode_texture(bytes.data(), bytes.size(), how);
}

bgra8 colour(int x, int y)
{
	return bgra8(std::uint8_t(10 + x), std::uint8_t(100 + y), std::uint8_t(200 - x - y), 255);
}

}

BOOST_AUTO_TEST_SUITE(texture_io)

BOOST_AUTO_TEST_CASE(a_picture_s_texels_are_b_g_r_a_row_0_first)
{
	SerializedTexture const t = decode(png_of(5, 3, colour));
	BOOST_TEST(t.width == 5);
	BOOST_TEST(t.height == 3);
	BOOST_TEST(t.provided_mip_levels == 0);
	BOOST_TEST(!t.srgb);
	BOOST_TEST(!t.mipmaps);
	BOOST_REQUIRE_EQUAL(t.pixels.size(), 5u * 3u * 4u);
	for (int y = 0; y < 3; ++y)
		for (int x = 0; x < 5; ++x)
		{
			bgra8 got;
			std::memcpy(&got, &t.pixels[(size_t(y) * 5 + x) * 4], 4);
			bgra8 const want = colour(x, y);
			BOOST_TEST((got.b == want.b && got.g == want.g && got.r == want.r && got.a == want.a), "texel " << x << "," << y);
		}
}

BOOST_AUTO_TEST_CASE(the_8_bit_format_is_the_one_edge_has_always_given_such_a_texture)
{
	// X8R8G8B8 for one kept on the CPU, plain; A8R8G8B8 for an sRGB, mipmapped or GPU-only one
	std::vector<char> const png = png_of(4, 4, colour);
	BOOST_TEST(decode(png).pixel_format == X8R8G8B8);
	TextureDecoding srgb;
	srgb.srgb = true;
	BOOST_TEST(decode(png, srgb).pixel_format == A8R8G8B8);
	BOOST_TEST(decode(png, srgb).srgb);
	TextureDecoding mips;
	mips.mipmaps = true;
	BOOST_TEST(decode(png, mips).pixel_format == A8R8G8B8);
	BOOST_TEST(decode(png, mips).mipmaps);
	TextureDecoding gpu_only;
	gpu_only.keep_cpu_copy = false;
	BOOST_TEST(decode(png, gpu_only).pixel_format == A8R8G8B8);
}

BOOST_AUTO_TEST_CASE(a_picture_see_through_somewhere_keeps_its_alpha_however_it_is_decoded)
{
	// X8R8G8B8 says the fourth byte is nothing (a renderer samples it as 1, the converter's bake makes it 255): never for a picture
	// whose alpha says something, as edge_convert decodes a glTF's pictures (kept on the CPU, not compressed, not sRGB: a net's mesh
	// came out opaque)
	auto const clear = [](int x, int y) { bgra8 c = colour(x, y); c.a = std::uint8_t(x < 2 ? 0 : 40 * y + 10); return c; };
	std::vector<char> const png = png_of(4, 4, clear);
	TextureDecoding plain, srgb, mips, gpu_only;
	srgb.srgb = true;
	mips.mipmaps = true;
	gpu_only.keep_cpu_copy = false;
	for (TextureDecoding const &how : {plain, srgb, mips, gpu_only})
	{
		SerializedTexture const t = decode(png, how);
		BOOST_TEST(t.pixel_format == A8R8G8B8);
		BOOST_REQUIRE_EQUAL(t.pixels.size(), 4u * 4u * 4u);
		for (int y = 0; y < 4; ++y)
			for (int x = 0; x < 4; ++x)
				BOOST_TEST(t.pixels[(size_t(y) * 4 + size_t(x)) * 4 + 3] == clear(x, y).a, "texel " << x << "," << y);
	}
}

BOOST_AUTO_TEST_CASE(compressed_a_picture_is_bc1_when_opaque_and_bc3_when_it_has_alpha_if_its_sides_are_blocks)
{
	TextureDecoding compress;
	compress.compress = true;
	SerializedTexture const opaque = decode(png_of(8, 4, colour), compress);
	BOOST_TEST(opaque.pixel_format == BC1);
	BOOST_TEST(opaque.pixels.size() == Texture::level_size(8, 4, BC1, 0));
	SerializedTexture const clear = decode(png_of(8, 4, [](int x, int y) { bgra8 c = colour(x, y); c.a = x < 4 ? 255 : 30; return c; }), compress);
	BOOST_TEST(clear.pixel_format == BC3);
	BOOST_TEST(clear.pixels.size() == Texture::level_size(8, 4, BC3, 0));
	// a side that is not a whole block: left 8-bit
	SerializedTexture const odd = decode(png_of(6, 4, colour), compress);
	BOOST_TEST(is_block_compressed(odd.pixel_format) == false);
	BOOST_TEST(odd.pixels.size() == 6u * 4u * 4u);
}

BOOST_AUTO_TEST_CASE(a_compressed_texture_cannot_have_the_gpu_make_its_mips)
{
	TextureDecoding both;
	both.compress = true;
	both.mipmaps = true;
	BOOST_CHECK_THROW(decode(png_of(8, 8, colour), both), std::invalid_argument);
}

BOOST_AUTO_TEST_CASE(a_dds_brings_its_blocks_and_levels_as_they_are_and_its_own_srgb)
{
	std::vector<char> const dds = bytes_of(asset("test_dds_bc7_mips.dds"));
	SerializedTexture const t = decode(dds);
	BOOST_TEST(t.pixel_format == BC7);
	BOOST_TEST(t.provided_mip_levels > 1);
	BOOST_TEST(!t.mipmaps);
	size_t const data = Texture::levels_size(t.width, t.height, t.pixel_format, t.provided_mip_levels);
	BOOST_REQUIRE_EQUAL(t.pixels.size(), data);
	BOOST_CHECK(std::memcmp(t.pixels.data(), dds.data() + (dds.size() - data), data) == 0);
	// asked for sRGB, a colour block format is; BC5 (two channels of numbers) never is
	TextureDecoding srgb;
	srgb.srgb = true;
	BOOST_TEST(decode(dds, srgb).srgb);
	BOOST_TEST(!decode(bytes_of(asset("test_dds_bc5.dds")), srgb).srgb);
}

BOOST_AUTO_TEST_CASE(what_is_not_a_picture_is_refused)
{
	std::vector<char> const junk = {'n', 'o', 't', ' ', 'a', ' ', 'p', 'n', 'g'};
	BOOST_CHECK_THROW(decode(junk), std::runtime_error);
	BOOST_CHECK_THROW(decode(bytes_of(asset("test_dds_truncated.dds"))), std::runtime_error);
	BOOST_CHECK_THROW(decode_texture(nullptr, 0, TextureDecoding()), std::runtime_error);
}

BOOST_AUTO_TEST_SUITE_END()
