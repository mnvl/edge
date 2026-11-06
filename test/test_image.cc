// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include <boost/test/unit_test.hpp>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

#include <rove/vec.h>
#include "stb_image_write.h"

#include "image.h"
#include "font.h"
#include "null_renderer.h"
#include "pixel.h"
#include "render_target.h"
#include "test_fixture.h"
#include "test_backends.h"
#include "texture.h"

// make_text_texture and make_text_draw_call are deprecated; these tests keep what they do.
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

namespace
{

using rgb8 = rove::rgb<uint8_t>;
using bgr8 = rove::bgr<uint8_t>;
using rgba8 = rove::rgba<uint8_t>;
using bgra8 = rove::bgra<uint8_t>;
using bytes4 = rove::vec<4, uint8_t>;

// A directory of this test's own, gone with it.
struct Scratch
{
	std::filesystem::path directory;

	Scratch(): directory(std::filesystem::temp_directory_path() / "edge_test_image")
	{
		std::filesystem::remove_all(directory);
		std::filesystem::create_directories(directory);
	}

	~Scratch()
	{
		std::filesystem::remove_all(directory);
	}

	std::filesystem::path write(std::string const &name, std::string const &bytes) const
	{
		std::filesystem::path const path = directory / name;
		std::ofstream file(path, std::ios::binary);
		file.write(bytes.data(), std::streamsize(bytes.size()));
		BOOST_REQUIRE(file.good());
		return path;
	}

	// 3 wide, 2 high, every component of every pixel different: binary PPM,
	// which is its header and then the bytes R, G, B from the top left.
	std::filesystem::path colours() const
	{
		return write("colours.ppm", std::string("P6\n3 2\n255\n") +
			"\x0A\x14\x1E" "\x28\x32\x3C" "\x46\x50\x5A"
			"\x64\x6E\x78" "\x82\x8C\x96" "\xA0\xAA\xB4");
	}

	// 2 wide, 2 high: binary PGM, one byte a pixel.
	std::filesystem::path greys() const
	{
		return write("greys.pgm", std::string("P5\n2 2\n255\n") + "\x01\x7F\x80\xFE");
	}

	// 2 wide, 1 high, with alpha: R, G, B, A = (10, 20, 30, 40), (50, 60, 70, 80).
	std::filesystem::path translucent() const
	{
		std::filesystem::path const path = directory / "translucent.png";
		unsigned char const rgba[] = {10, 20, 30, 40, 50, 60, 70, 80};
		BOOST_REQUIRE(stbi_write_png(path.string().c_str(), 2, 1, 4, rgba, 8));
		return path;
	}
};

template<class V, class P> concept views_as = std::is_convertible_v<V, edge::ImageView<P>>;
template<class T, class P> concept has_pixels = requires(T t) { t.template pixels<P>(); };

void write_bytes(edge::Texture &texture, std::vector<uint8_t> const &bytes)
{
	BOOST_REQUIRE_EQUAL(bytes.size(), edge::pixel_data_size(texture.width(), texture.height(),
		texture.pixel_format()));
	auto texture_mapping = texture.write(edge::Contents::DISCARD);
	void *to = texture_mapping.data();
	BOOST_REQUIRE(to);
	std::memcpy(to, bytes.data(), bytes.size());
	texture_mapping.unmap();
}

edge::RendererPtr null_renderer()
{
	edge::RendererPtr renderer(new edge::NullRenderer(320, 200));
	return renderer;
}

}

BOOST_AUTO_TEST_SUITE(image)

// --- Image -------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(an_image_is_its_size_filled)
{
	edge::Image<bgr8> const image(3, 2, bgr8(1, 2, 3));
	BOOST_CHECK_EQUAL(image.width(), 3);
	BOOST_CHECK_EQUAL(image.height(), 2);
	BOOST_CHECK_EQUAL(image.count(), 6u);
	for (int y = 0; y < 2; ++y)
		for (int x = 0; x < 3; ++x)
			BOOST_TEST((image(x, y) == bgr8(1, 2, 3)));

	edge::Image<float> const zeros(4, 5);
	for (size_t i = 0; i < zeros.count(); ++i)
		BOOST_TEST(zeros.pixels()[i] == 0.0f);
	edge::Image<bgra8> const black(2, 2);
	BOOST_TEST((black(1, 1) == bgra8(0, 0, 0, 0)));
}

BOOST_AUTO_TEST_CASE(an_image_of_no_size_is_refused)
{
	BOOST_CHECK_THROW(edge::Image<uint8_t>(0, 4), std::invalid_argument);
	BOOST_CHECK_THROW(edge::Image<uint8_t>(4, 0), std::invalid_argument);
	BOOST_CHECK_THROW(edge::Image<uint8_t>(0, 0), std::invalid_argument);
	BOOST_CHECK_THROW(edge::Image<uint8_t>(-1, 4), std::invalid_argument);
	BOOST_CHECK_THROW(edge::Image<uint8_t>(4, -1), std::invalid_argument);
	// -2 * -2 is 4 pixels to who only multiplies
	BOOST_CHECK_THROW(edge::Image<uint8_t>(-2, -2), std::invalid_argument);
}

BOOST_AUTO_TEST_CASE(an_image_not_given_a_size_is_empty)
{
	edge::Image<bgr8> image;
	BOOST_CHECK_EQUAL(image.width(), 0);
	BOOST_CHECK_EQUAL(image.height(), 0);
	BOOST_CHECK_EQUAL(image.count(), 0u);
	BOOST_CHECK_THROW(image.at(0, 0), std::out_of_range);
	edge::ImageView<bgr8> const view = image;
	BOOST_CHECK_EQUAL(view.width(), 0);
	BOOST_CHECK_EQUAL(view.height(), 0);
	BOOST_CHECK_THROW(view.at(0, 0), std::out_of_range);
}

BOOST_AUTO_TEST_CASE(pixels_lie_row_after_row_from_the_top)
{
	edge::Image<uint8_t> image(3, 2);
	for (int y = 0; y < 2; ++y)
		for (int x = 0; x < 3; ++x)
			image(x, y) = uint8_t(10 * y + x);
	uint8_t const expected[] = {0, 1, 2, 10, 11, 12};
	BOOST_CHECK_EQUAL_COLLECTIONS(image.pixels(), image.pixels() + image.count(), expected, expected + 6);
	BOOST_TEST((&image.at(2, 1) == &image(2, 1)));
	BOOST_TEST((&image.at(2, 1) == image.pixels() + 5));

	// the bytes of its pixels, nothing between them
	edge::Image<bgr8> colours(2, 1);
	colours(0, 0) = bgr8(1, 2, 3);
	colours(1, 0) = bgr8(4, 5, 6);
	uint8_t bytes[6];
	std::memcpy(bytes, colours.pixels(), 6);
	uint8_t const expected_bytes[] = {1, 2, 3, 4, 5, 6};
	BOOST_CHECK_EQUAL_COLLECTIONS(bytes, bytes + 6, expected_bytes, expected_bytes + 6);
}

BOOST_AUTO_TEST_CASE(at_refuses_what_is_off_the_image)
{
	edge::Image<uint8_t> image(3, 2);
	edge::Image<uint8_t> const &constant = image;
	BOOST_CHECK_NO_THROW(image.at(0, 0));
	BOOST_CHECK_NO_THROW(image.at(2, 1));
	BOOST_CHECK_THROW(image.at(3, 0), std::out_of_range);
	BOOST_CHECK_THROW(image.at(0, 2), std::out_of_range);
	BOOST_CHECK_THROW(image.at(-1, 0), std::out_of_range);
	BOOST_CHECK_THROW(image.at(0, -1), std::out_of_range);
	// width and height the other way round
	BOOST_CHECK_THROW(image.at(1, 2), std::out_of_range);
	BOOST_CHECK_THROW(constant.at(3, 1), std::out_of_range);
	BOOST_CHECK_NO_THROW(constant.at(2, 1));
}

BOOST_AUTO_TEST_CASE(a_copy_is_its_own_and_a_move_leaves_nothing)
{
	edge::Image<uint8_t> image(2, 2, 7);
	edge::Image<uint8_t> copy(image);
	copy(0, 0) = 9;
	BOOST_CHECK_EQUAL(image(0, 0), 7);
	BOOST_TEST((copy.pixels() != image.pixels()));

	uint8_t const *pixels = image.pixels();
	edge::Image<uint8_t> moved(std::move(image));
	BOOST_TEST((moved.pixels() == pixels));
	BOOST_CHECK_EQUAL(moved.width(), 2);
	BOOST_CHECK_EQUAL(image.width(), 0);
	BOOST_CHECK_EQUAL(image.height(), 0);
	BOOST_CHECK_THROW(image.at(0, 0), std::out_of_range);

	edge::Image<uint8_t> assigned(5, 5);
	assigned = std::move(moved);
	BOOST_CHECK_EQUAL(assigned.width(), 2);
	BOOST_CHECK_EQUAL(assigned.height(), 2);
	BOOST_CHECK_EQUAL(assigned(1, 1), 7);
	BOOST_CHECK_EQUAL(moved.width(), 0);
	BOOST_CHECK_EQUAL(moved.count(), 0u);
}

// --- ImageView ---------------------------------------------------------------

BOOST_AUTO_TEST_CASE(a_view_of_an_image_is_the_image)
{
	edge::Image<bgr8> image(3, 2);
	edge::ImageView<bgr8> const view = image;
	BOOST_CHECK_EQUAL(view.width(), 3);
	BOOST_CHECK_EQUAL(view.height(), 2);
	BOOST_CHECK_EQUAL(view.pitch(), 9);
	BOOST_TEST((view.pixels() == image.pixels()));

	view(2, 1) = bgr8(1, 2, 3);
	BOOST_TEST((image(2, 1) == bgr8(1, 2, 3)));
	image(0, 1).g = 77;
	BOOST_CHECK_EQUAL(view.at(0, 1).g, 77);
	BOOST_TEST((view.row(1) == &image(0, 1)));
}

BOOST_AUTO_TEST_CASE(a_view_is_const_as_what_it_looks_at)
{
	static_assert(views_as<edge::Image<uint8_t> &, uint8_t>);
	static_assert(views_as<edge::Image<uint8_t> &, uint8_t const>);
	static_assert(views_as<edge::Image<uint8_t> const &, uint8_t const>);
	static_assert(!views_as<edge::Image<uint8_t> const &, uint8_t>);
	static_assert(views_as<edge::ImageView<uint8_t>, uint8_t const>);
	static_assert(!views_as<edge::ImageView<uint8_t const>, uint8_t>);
	// a view of one pixel type is not a view of another
	static_assert(!views_as<edge::ImageView<bgr8>, rgb8>);
	static_assert(!views_as<edge::Image<bgra8> &, rgba8>);
	static_assert(!std::is_assignable_v<uint8_t const &, uint8_t>);
	static_assert(!std::is_assignable_v<decltype(std::declval<edge::ImageView<uint8_t const>>()(0, 0)), uint8_t>);

	edge::Image<uint8_t> image(2, 2, 5);
	edge::ImageView<uint8_t> const view = image;
	edge::ImageView<uint8_t const> const constant = view;
	BOOST_TEST((constant.pixels() == image.pixels()));
	BOOST_CHECK_EQUAL(constant.pitch(), 2);
	image(1, 1) = 6;
	BOOST_CHECK_EQUAL(constant(1, 1), 6);
}

// Rows pitch bytes apart, which is more than their pixels when whoever owns
// them pads them: a window's surface does.
BOOST_AUTO_TEST_CASE(a_views_rows_are_its_pitch_apart)
{
	std::vector<uint8_t> bytes(3 * 5, 0xEE);        // 3 rows of 5 bytes: 2 pixels of 2, and a byte of padding
	using pair = rove::vec<2, uint8_t>;
	edge::ImageView<pair> const view(reinterpret_cast<pair *>(bytes.data()), 2, 3, 5);
	BOOST_CHECK_EQUAL(view.pitch(), 5);
	for (int y = 0; y < 3; ++y)
		for (int x = 0; x < 2; ++x)
			view(x, y) = pair(uint8_t(10 * y + x), uint8_t(100 + 10 * y + x));
	uint8_t const expected[] = {
		0, 100, 1, 101, 0xEE,
		10, 110, 11, 111, 0xEE,
		20, 120, 21, 121, 0xEE};
	BOOST_CHECK_EQUAL_COLLECTIONS(bytes.begin(), bytes.end(), expected, expected + 15);
	BOOST_TEST((static_cast<void *>(view.row(2)) == bytes.data() + 10));
	BOOST_TEST((static_cast<void *>(&view.at(1, 2)) == bytes.data() + 12));
}

BOOST_AUTO_TEST_CASE(a_view_at_refuses_what_is_off_it)
{
	edge::Image<uint8_t> image(3, 2);
	edge::ImageView<uint8_t const> const view = image;
	BOOST_CHECK_NO_THROW(view.at(2, 1));
	BOOST_CHECK_THROW(view.at(3, 0), std::out_of_range);
	BOOST_CHECK_THROW(view.at(0, 2), std::out_of_range);
	BOOST_CHECK_THROW(view.at(-1, 0), std::out_of_range);
	BOOST_CHECK_THROW(view.at(0, -1), std::out_of_range);
	BOOST_CHECK_THROW(view.at(1, 2), std::out_of_range);
}

BOOST_AUTO_TEST_CASE(a_view_that_cannot_be_is_refused)
{
	uint8_t bytes[64] = {};
	BOOST_CHECK_NO_THROW(edge::ImageView<uint8_t>(bytes, 8, 8, 8));
	BOOST_CHECK_THROW(edge::ImageView<uint8_t>(bytes, -1, 8, 8), std::invalid_argument);
	BOOST_CHECK_THROW(edge::ImageView<uint8_t>(bytes, 8, -1, 8), std::invalid_argument);
	// rows that would overlap
	BOOST_CHECK_THROW(edge::ImageView<uint8_t>(bytes, 8, 8, 7), std::invalid_argument);
	BOOST_CHECK_THROW(edge::ImageView<bgra8>(reinterpret_cast<bgra8 *>(bytes), 4, 4, 15), std::invalid_argument);
	BOOST_CHECK_THROW(edge::ImageView<uint8_t>(bytes, 8, 8, -8), std::invalid_argument);
	// pixels nowhere
	BOOST_CHECK_THROW(edge::ImageView<uint8_t>(nullptr, 8, 8, 8), std::invalid_argument);
	BOOST_CHECK_NO_THROW(edge::ImageView<uint8_t>(nullptr, 0, 0, 0));

	// floats that would not be where a float can be
	alignas(float) uint8_t aligned[64] = {};
	BOOST_CHECK_NO_THROW(edge::ImageView<float>(reinterpret_cast<float *>(aligned), 2, 2, 8));
	BOOST_CHECK_NO_THROW(edge::ImageView<float>(reinterpret_cast<float *>(aligned), 2, 2, 12));
	BOOST_CHECK_THROW(edge::ImageView<float>(reinterpret_cast<float *>(aligned), 2, 2, 9), std::invalid_argument);
	BOOST_CHECK_THROW(edge::ImageView<float>(reinterpret_cast<float *>(aligned + 1), 2, 2, 8), std::invalid_argument);
}

BOOST_AUTO_TEST_CASE(an_image_of_a_view_is_a_copy_without_the_padding)
{
	std::vector<uint8_t> bytes = {
		1, 2, 3, 0xEE, 0xEE,
		4, 5, 6, 0xEE, 0xEE};
	edge::ImageView<uint8_t const> const view(bytes.data(), 3, 2, 5);
	edge::Image<uint8_t> image(view);
	BOOST_CHECK_EQUAL(image.width(), 3);
	BOOST_CHECK_EQUAL(image.height(), 2);
	uint8_t const expected[] = {1, 2, 3, 4, 5, 6};
	BOOST_CHECK_EQUAL_COLLECTIONS(image.pixels(), image.pixels() + image.count(), expected, expected + 6);
	bytes[0] = 9;
	BOOST_CHECK_EQUAL(image(0, 0), 1);

	edge::Image<uint8_t> const nothing{edge::ImageView<uint8_t const>(nullptr, 0, 0, 0)};
	BOOST_CHECK_EQUAL(nothing.count(), 0u);
}

// --- files -------------------------------------------------------------------

BOOST_FIXTURE_TEST_CASE(a_file_is_read_in_the_order_asked_for, Scratch)
{
	edge::Image<rgb8> const rgb = edge::load_image<rgb8>(colours());
	BOOST_REQUIRE_EQUAL(rgb.width(), 3);
	BOOST_REQUIRE_EQUAL(rgb.height(), 2);
	BOOST_TEST((rgb(0, 0).r == 0x0A && rgb(0, 0).g == 0x14 && rgb(0, 0).b == 0x1E));
	BOOST_TEST((rgb(2, 0).r == 0x46 && rgb(2, 0).g == 0x50 && rgb(2, 0).b == 0x5A));
	BOOST_TEST((rgb(0, 1).r == 0x64 && rgb(0, 1).g == 0x6E && rgb(0, 1).b == 0x78));
	BOOST_TEST((rgb(2, 1).r == 0xA0 && rgb(2, 1).g == 0xAA && rgb(2, 1).b == 0xB4));
	// red first in memory
	BOOST_CHECK_EQUAL(rgb(0, 0).i[0], 0x0A);

	edge::Image<bgr8> const bgr = edge::load_image<bgr8>(colours());
	BOOST_REQUIRE_EQUAL(bgr.width(), 3);
	BOOST_REQUIRE_EQUAL(bgr.height(), 2);
	for (int y = 0; y < 2; ++y)
		for (int x = 0; x < 3; ++x)
		{
			BOOST_CHECK_EQUAL(bgr(x, y).r, rgb(x, y).r);
			BOOST_CHECK_EQUAL(bgr(x, y).g, rgb(x, y).g);
			BOOST_CHECK_EQUAL(bgr(x, y).b, rgb(x, y).b);
		}
	// blue first in memory
	BOOST_CHECK_EQUAL(bgr(0, 0).i[0], 0x1E);
	BOOST_CHECK_EQUAL(bgr(0, 0).i[2], 0x0A);
}

BOOST_FIXTURE_TEST_CASE(a_file_without_alpha_is_opaque, Scratch)
{
	edge::Image<rgba8> const rgba = edge::load_image<rgba8>(colours());
	BOOST_TEST((rgba(1, 1) == rgba8(0x82, 0x8C, 0x96, 0xFF)));
	edge::Image<bgra8> const bgra = edge::load_image<bgra8>(colours());
	BOOST_TEST((bgra(1, 1) == bgra8(0x96, 0x8C, 0x82, 0xFF)));
	BOOST_TEST((bgra(1, 1).r == 0x82 && bgra(1, 1).a == 0xFF));
}

BOOST_FIXTURE_TEST_CASE(a_files_alpha_is_kept_or_dropped_as_asked, Scratch)
{
	edge::Image<rgba8> const rgba = edge::load_image<rgba8>(translucent());
	BOOST_REQUIRE_EQUAL(rgba.width(), 2);
	BOOST_REQUIRE_EQUAL(rgba.height(), 1);
	BOOST_TEST((rgba(0, 0) == rgba8(10, 20, 30, 40)));
	BOOST_TEST((rgba(1, 0) == rgba8(50, 60, 70, 80)));

	edge::Image<bgra8> const bgra = edge::load_image<bgra8>(translucent());
	BOOST_TEST((bgra(0, 0).r == 10 && bgra(0, 0).g == 20 && bgra(0, 0).b == 30 && bgra(0, 0).a == 40));
	BOOST_TEST((bgra(1, 0) == bgra8(70, 60, 50, 80)));

	// not blended by it
	edge::Image<bgr8> const bgr = edge::load_image<bgr8>(translucent());
	BOOST_TEST((bgr(0, 0).r == 10 && bgr(0, 0).g == 20 && bgr(0, 0).b == 30));
	edge::Image<rgb8> const rgb = edge::load_image<rgb8>(translucent());
	BOOST_TEST((rgb(1, 0) == rgb8(50, 60, 70)));
}

BOOST_FIXTURE_TEST_CASE(a_grey_file_is_read_as_it_is, Scratch)
{
	edge::Image<uint8_t> const grey = edge::load_image<uint8_t>(greys());
	BOOST_REQUIRE_EQUAL(grey.width(), 2);
	BOOST_REQUIRE_EQUAL(grey.height(), 2);
	uint8_t const expected[] = {0x01, 0x7F, 0x80, 0xFE};
	BOOST_CHECK_EQUAL_COLLECTIONS(grey.pixels(), grey.pixels() + 4, expected, expected + 4);

	// and as a colour: the grey in each
	edge::Image<bgr8> const bgr = edge::load_image<bgr8>(greys());
	BOOST_TEST((bgr(1, 1) == bgr8(0xFE, 0xFE, 0xFE)));
}

BOOST_FIXTURE_TEST_CASE(what_is_not_an_image_is_refused, Scratch)
{
	BOOST_CHECK_THROW(edge::load_image<bgr8>(directory / "no_such.png"), std::runtime_error);
	BOOST_CHECK_THROW(edge::load_image<uint8_t>(directory / "no_such.png"), std::runtime_error);
	BOOST_CHECK_THROW(edge::load_image<bgr8>(directory), std::runtime_error);
	BOOST_CHECK_THROW(edge::load_image<bgra8>(write("words.png", "not an image at all")), std::runtime_error);
	BOOST_CHECK_THROW(edge::load_image<rgb8>(write("empty.png", "")), std::runtime_error);
	// a header that promises pixels the file does not have
	BOOST_CHECK_THROW(edge::load_image<rgb8>(write("short.ppm", "P6\n3 2\n255\n\x01\x02\x03")), std::runtime_error);

	try
	{
		edge::load_image<bgr8>(directory / "no_such.png");
		BOOST_FAIL("read a file that is not there");
	}
	catch (std::runtime_error const &error)
	{
		BOOST_TEST(std::string(error.what()).find("no_such.png") != std::string::npos);
	}
}

// --- textures ------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(a_textures_pixels_are_its_cpu_copy)
{
	edge::RendererPtr const renderer = null_renderer();
	edge::TexturePtr const texture = renderer->create_texture(3, 2, edge::PixelFormat::A8R8G8B8);
	write_bytes(*texture, {
		1, 2, 3, 4,  5, 6, 7, 8,  9, 10, 11, 12,
		13, 14, 15, 16,  17, 18, 19, 20,  21, 22, 23, 24});

	edge::Texture const &constant = *texture;
	edge::ImageView<bgra8 const> const view = constant.pixels<bgra8>();
	BOOST_CHECK_EQUAL(view.width(), 3);
	BOOST_CHECK_EQUAL(view.height(), 2);
	BOOST_CHECK_EQUAL(view.pitch(), 12);
	BOOST_TEST((static_cast<void const *>(view.pixels()) == static_cast<void const *>(constant.read().data())));
	// B, G, R, A as edge keeps its 8-bit texels
	BOOST_TEST((view(0, 0).b == 1 && view(0, 0).g == 2 && view(0, 0).r == 3 && view(0, 0).a == 4));
	BOOST_TEST((view(2, 0).b == 9 && view(2, 0).r == 11));
	BOOST_TEST((view(0, 1).b == 13 && view(0, 1).a == 16));
	BOOST_TEST((view.at(2, 1).b == 21 && view.at(2, 1).g == 22 && view.at(2, 1).r == 23 && view.at(2, 1).a == 24));

	// X8R8G8B8 lies the same
	edge::TexturePtr const opaque = renderer->create_texture(1, 1, edge::PixelFormat::X8R8G8B8);
	write_bytes(*opaque, {1, 2, 3, 4});
	BOOST_TEST((std::as_const(*opaque).pixels<bgra8>()(0, 0).r == 3));
	// and as four bytes that are no colour
	BOOST_TEST((std::as_const(*opaque).pixels<bytes4>()(0, 0) == bytes4(1, 2, 3, 4)));
}

BOOST_AUTO_TEST_CASE(a_textures_floats_are_red_first)
{
	edge::RendererPtr const renderer = null_renderer();
	float const floats[] = {0.25f, 0.5f, 0.75f, 1.0f, 2, 3, 4, 5};
	std::vector<uint8_t> bytes(sizeof(floats));
	std::memcpy(bytes.data(), floats, sizeof(floats));

	edge::TexturePtr const colour = renderer->create_texture(2, 1, edge::PixelFormat::R32G32B32A32F);
	write_bytes(*colour, bytes);
	edge::ImageView<rove::rgba<float> const> const view = std::as_const(*colour).pixels<rove::rgba<float>>();
	BOOST_CHECK_EQUAL(view.pitch(), 32);
	BOOST_TEST((view(0, 0).r == 0.25f && view(0, 0).g == 0.5f && view(0, 0).b == 0.75f && view(0, 0).a == 1.0f));
	BOOST_TEST((view(1, 0) == rove::rgba<float>(2, 3, 4, 5)));
	BOOST_TEST((std::as_const(*colour).pixels<rove::vec<4, float>>()(1, 0) == rove::vec<4, float>(2, 3, 4, 5)));

	edge::TexturePtr const depth = renderer->create_texture(4, 2, edge::PixelFormat::R32F);
	write_bytes(*depth, bytes);
	edge::ImageView<float const> const depths = std::as_const(*depth).pixels<float>();
	BOOST_CHECK_EQUAL(depths.width(), 4);
	BOOST_CHECK_EQUAL(depths.pitch(), 16);
	BOOST_CHECK_EQUAL(depths(3, 0), 1.0f);
	BOOST_CHECK_EQUAL(depths(0, 1), 2.0f);

	uint16_t const halves[] = {0x3400, 0x3800, 0x3A00, 0x3C00};     // 0.25, 0.5, 0.75, 1
	std::vector<uint8_t> half_bytes(sizeof(halves));
	std::memcpy(half_bytes.data(), halves, sizeof(halves));
	edge::TexturePtr const half = renderer->create_texture(1, 1, edge::PixelFormat::R16G16B16A16F);
	write_bytes(*half, half_bytes);
	rove::rgba<uint16_t> const texel = std::as_const(*half).pixels<rove::rgba<uint16_t>>()(0, 0);
	BOOST_TEST((texel.r == 0x3400 && texel.g == 0x3800 && texel.b == 0x3A00 && texel.a == 0x3C00));
}

// A texel read as what it is not is the wrong picture and no error, so it
// is an error: by the format, not by the size (a float and four bytes are
// both four bytes).
BOOST_AUTO_TEST_CASE(a_textures_pixels_are_refused_as_what_they_are_not)
{
	edge::RendererPtr const renderer = null_renderer();
	edge::TexturePtr const bytes_texture = renderer->create_texture(4, 4, edge::PixelFormat::X8R8G8B8);
	edge::TexturePtr const floats_texture = renderer->create_texture(4, 4, edge::PixelFormat::R32G32B32A32F);
	edge::TexturePtr const depth_texture = renderer->create_texture(4, 4, edge::PixelFormat::R32F);
	edge::TexturePtr const halves_texture = renderer->create_texture(4, 4, edge::PixelFormat::R16G16B16A16F);
	edge::Texture const &bytes = *bytes_texture, &floats = *floats_texture, &depth = *depth_texture, &halves = *halves_texture;

	BOOST_CHECK_NO_THROW(bytes.pixels<bgra8>());
	BOOST_CHECK_THROW(bytes.pixels<float>(), std::logic_error);
	BOOST_CHECK_THROW(bytes.pixels<rove::rgba<float>>(), std::logic_error);
	BOOST_CHECK_THROW(bytes.pixels<rove::rgba<uint16_t>>(), std::logic_error);

	BOOST_CHECK_THROW(floats.pixels<bgra8>(), std::logic_error);
	BOOST_CHECK_THROW(floats.pixels<float>(), std::logic_error);
	BOOST_CHECK_THROW(floats.pixels<rove::rgba<uint16_t>>(), std::logic_error);

	BOOST_CHECK_THROW(depth.pixels<bgra8>(), std::logic_error);
	BOOST_CHECK_THROW(depth.pixels<bytes4>(), std::logic_error);
	BOOST_CHECK_THROW(depth.pixels<rove::rgba<float>>(), std::logic_error);

	BOOST_CHECK_THROW(halves.pixels<rove::rgba<float>>(), std::logic_error);
	BOOST_CHECK_THROW(halves.pixels<bgra8>(), std::logic_error);

	// the orders edge has no format of are not there to ask for
	static_assert(has_pixels<edge::Texture const &, bgra8>);
	static_assert(has_pixels<edge::Texture const &, rove::rgba<float>>);
	static_assert(!has_pixels<edge::Texture const &, rgba8>);
	static_assert(!has_pixels<edge::Texture const &, rove::bgra<float>>);
	static_assert(!has_pixels<edge::Texture const &, rove::bgra<uint16_t>>);
	static_assert(!has_pixels<edge::Texture const &, bgr8>);
	static_assert(!has_pixels<edge::Texture const &, uint8_t>);
	static_assert(!has_pixels<edge::Texture const &, double>);
}

BOOST_AUTO_TEST_CASE(a_texture_without_a_cpu_copy_has_no_pixels)
{
	edge::RendererPtr const renderer = null_renderer();
	edge::TexturePtr const compressed_texture = renderer->create_texture(8, 8, edge::PixelFormat::BC1);
	edge::Texture const &compressed = *compressed_texture;
	BOOST_CHECK_THROW(compressed.pixels<bgra8>(), std::logic_error);
	BOOST_CHECK_THROW(compressed.pixels<bytes4>(), std::logic_error);
}

BOOST_AUTO_TEST_CASE(a_textures_pixels_are_written_where_it_is_not_const)
{
	edge::RendererPtr const renderer = null_renderer();
	edge::TexturePtr const texture = renderer->create_texture(2, 2, edge::PixelFormat::X8R8G8B8);
	write_bytes(*texture, std::vector<uint8_t>(16, 0));

	static_assert(std::is_same_v<decltype(texture->pixels<bgra8>()), edge::ImageView<bgra8>>);
	static_assert(std::is_same_v<decltype(std::as_const(*texture).pixels<bgra8>()), edge::ImageView<bgra8 const>>);

	edge::ImageView<bgra8> const view = texture->pixels<bgra8>();
	view(1, 1) = bgra8(1, 2, 3, 4);
	view(0, 0).r = 200;
	auto texture_mapping = texture->read();
	uint8_t const *bytes = texture_mapping.as<uint8_t const>().data();
	uint8_t const expected[] = {0, 0, 200, 0,  0, 0, 0, 0,  0, 0, 0, 0,  1, 2, 3, 4};
	BOOST_CHECK_EQUAL_COLLECTIONS(bytes, bytes + 16, expected, expected + 16);
	BOOST_CHECK_THROW(texture->pixels<float>(), std::logic_error);
}

// --- sRGB, and saving -------------------------------------------------------------

BOOST_AUTO_TEST_CASE(srgb_is_the_standard_curve_and_its_inverse)
{
	BOOST_CHECK_EQUAL(edge::srgb_decode(0.0f), 0.0f);
	BOOST_CHECK_CLOSE(edge::srgb_decode(1.0f), 1.0f, 1e-4);
	BOOST_CHECK_CLOSE(edge::srgb_decode(0.5f), 0.2140411f, 1e-3);
	BOOST_CHECK_CLOSE(edge::srgb_decode(0.04045f), 0.04045f / 12.92f, 1e-3);      // the linear toe ends where the power begins
	BOOST_CHECK_CLOSE(edge::srgb_encode(0.2140411f), 0.5f, 1e-3);
	BOOST_CHECK_CLOSE(edge::srgb_encode(0.0031308f), 0.0031308f * 12.92f, 1e-3);
	float previous = -1.0f;
	for (int i = 0; i <= 1000; ++i)
	{
		float const x = float(i) / 1000.0f;
		float const linear = edge::srgb_decode(x);
		BOOST_REQUIRE_GT(linear, previous);                    // rising, nowhere flat
		previous = linear;
		BOOST_REQUIRE_SMALL(edge::srgb_encode(linear) - x, 1e-5f);
	}
}

namespace
{

/// A 3 x 2 picture inside a view of rows 5 pixels apart (a pitch that is not the width's), every pixel its own.
template <class P> struct Pitched
{
	std::vector<P> storage = std::vector<P>(5 * 2);
	edge::ImageView<P> view{storage.data(), 3, 2, 5 * std::ptrdiff_t(sizeof(P))};
};

std::filesystem::path out_png(std::string const &name)
{
	return std::filesystem::temp_directory_path() / ("edge_test_save_" + name + ".png");
}

}

BOOST_AUTO_TEST_CASE(a_saved_png_loads_back_as_it_was_whatever_the_order_of_the_channels)
{
	Pitched<bgra8> bgra;
	Pitched<rgb8> rgb;
	Pitched<uint8_t> grey;
	for (int y = 0; y < 2; ++y)
		for (int x = 0; x < 3; ++x)
		{
			uint8_t const v = uint8_t(40 * y + 10 * x);
			bgra.view(x, y) = bgra8(v, uint8_t(v + 1), uint8_t(v + 2), uint8_t(255 - v));   // b, g, r, a
			rgb.view(x, y) = rgb8(uint8_t(v + 2), uint8_t(v + 1), v);
			grey.view(x, y) = v;
		}
	edge::save_image(out_png("bgra"), edge::ImageView<bgra8 const>(bgra.view));
	edge::save_image(out_png("rgb"), edge::ImageView<rgb8 const>(rgb.view));
	edge::save_image(out_png("grey"), edge::ImageView<uint8_t const>(grey.view));
	auto const as_rgba = edge::load_image<rgba8>(out_png("bgra"));
	auto const as_bgr = edge::load_image<bgr8>(out_png("rgb"));
	auto const as_grey = edge::load_image<uint8_t>(out_png("grey"));
	BOOST_REQUIRE_EQUAL(as_rgba.width(), 3);
	BOOST_REQUIRE_EQUAL(as_rgba.height(), 2);
	for (int y = 0; y < 2; ++y)
		for (int x = 0; x < 3; ++x)
		{
			uint8_t const v = uint8_t(40 * y + 10 * x);
			BOOST_TEST_INFO("pixel " << x << ", " << y);
			// red is red whichever order the memory holds it in
			BOOST_CHECK(as_rgba(x, y) == rgba8(uint8_t(v + 2), uint8_t(v + 1), v, uint8_t(255 - v)));
			BOOST_CHECK(as_bgr(x, y) == bgr8(v, uint8_t(v + 1), uint8_t(v + 2)));
			BOOST_CHECK_EQUAL(as_grey(x, y), v);
		}
	for (char const *name : {"bgra", "rgb", "grey"})
		std::filesystem::remove(out_png(name));
}

BOOST_AUTO_TEST_CASE(an_image_is_saved_as_a_png_or_not_at_all)
{
	Pitched<rgba8> rgba;
	edge::ImageView<rgba8 const> const view(rgba.view);
	// a JPEG named .jpg is not what this writes: not a PNG under another name either
	BOOST_CHECK_THROW(edge::save_image(std::filesystem::temp_directory_path() / "edge_test_save.jpg", view), std::invalid_argument);
	try
	{
		edge::save_image("/nonexistent/edge_test_save.png", view);
		BOOST_ERROR("an image was saved where no directory is");
	}
	catch (std::runtime_error const &e)
	{
		BOOST_CHECK_MESSAGE(std::string(e.what()).find("/nonexistent/edge_test_save.png") != std::string::npos, e.what());
	}
	BOOST_CHECK_THROW(edge::save_image(out_png("empty"), edge::ImageView<rgba8 const>(nullptr, 0, 0, 0)), std::invalid_argument);
}

// --- textures on a GPU -----------------------------------------------------------

BOOST_AUTO_TEST_SUITE(gpu)

namespace
{

edge::RenderPassClearState clear_to(float r, float g, float b, float a)
{
	edge::RenderPassClearState clear;
	clear.clear_color = true;
	clear.clear_color_r = r;
	clear.clear_color_g = g;
	clear.clear_color_b = b;
	clear.clear_color_a = a;
	return clear;
}

}

// What the names are for: a target cleared to a red, a green and a blue
// reads back with each under its own name, whichever way round the format
// keeps them.
BOOST_AUTO_TEST_CASE_TEMPLATE(what_is_rendered_reads_back_under_its_names, F, edge::Backends)
{
	F fixture;
	auto const &renderer = fixture.renderer;
	BOOST_REQUIRE_MESSAGE(renderer, fixture.error);
	edge::RenderTargetPtr const bytes = renderer->create_render_target(8, 4, edge::PixelFormat::X8R8G8B8);
	edge::RenderTargetPtr const halves = renderer->create_render_target(8, 4, edge::PixelFormat::R16G16B16A16F);
	edge::RenderTargetPtr const floats = renderer->create_render_target(8, 4, edge::PixelFormat::R32G32B32A32F);
	edge::RenderTargetPtr const depth = renderer->create_render_target(8, 4, edge::PixelFormat::R32F);

	renderer->begin_frame();
	renderer->begin_pass(bytes, clear_to(1.0f, 0.4f, 0.2f, 1.0f));
	renderer->end_pass();
	renderer->begin_pass(halves, clear_to(0.25f, 0.5f, 0.75f, 1.0f));
	renderer->end_pass();
	renderer->begin_pass(floats, clear_to(0.125f, 0.375f, 0.625f, 0.875f));
	renderer->end_pass();
	renderer->begin_pass(depth, clear_to(7.5f, 0, 0, 0));
	renderer->end_pass();
	renderer->finish_frame();

	edge::Image<bgra8> const image = edge::read_image<bgra8>(*bytes->color_texture());
	BOOST_REQUIRE_EQUAL(image.width(), 8);
	BOOST_REQUIRE_EQUAL(image.height(), 4);
	for (int y = 0; y < 4; ++y)
		for (int x = 0; x < 8; ++x)
		{
			BOOST_CHECK_EQUAL(image(x, y).r, 255);
			BOOST_CHECK_EQUAL(image(x, y).g, 102);
			BOOST_CHECK_EQUAL(image(x, y).b, 51);
		}

	edge::Image<rove::rgba<uint16_t>> const half = edge::read_image<rove::rgba<uint16_t>>(*halves->color_texture());
	BOOST_TEST((half(7, 3).r == 0x3400 && half(7, 3).g == 0x3800 && half(7, 3).b == 0x3A00 && half(7, 3).a == 0x3C00));

	edge::Image<rove::rgba<float>> const wide = edge::read_image<rove::rgba<float>>(*floats->color_texture());
	BOOST_TEST((wide(0, 0) == rove::rgba<float>(0.125f, 0.375f, 0.625f, 0.875f)));
	BOOST_TEST((wide(7, 3).r == 0.125f && wide(7, 3).g == 0.375f && wide(7, 3).b == 0.625f && wide(7, 3).a == 0.875f));

	edge::Image<float> const far = edge::read_image<float>(*depth->color_texture());
	BOOST_REQUIRE_EQUAL(far.count(), 32u);
	BOOST_CHECK_EQUAL(far(0, 0), 7.5f);
	BOOST_CHECK_EQUAL(far(7, 3), 7.5f);

	BOOST_CHECK_THROW(edge::read_image<float>(*bytes->color_texture()), std::logic_error);
	BOOST_CHECK_THROW(edge::read_image<bgra8>(*depth->color_texture()), std::logic_error);
}

// pixels() is the CPU's copy and stays there: what is written through it
// does not reach the GPU, and nothing is read from the GPU until download().
BOOST_AUTO_TEST_CASE_TEMPLATE(pixels_neither_upload_nor_download, F, edge::Backends)
{
	F fixture;
	auto const &renderer = fixture.renderer;
	BOOST_REQUIRE_MESSAGE(renderer, fixture.error);
	edge::TexturePtr const texture = renderer->create_texture(2, 2, edge::PixelFormat::A8R8G8B8);
	std::vector<uint8_t> uploaded(16);
	for (size_t i = 0; i < 16; ++i)
		uploaded[i] = uint8_t(10 + i);
	write_bytes(*texture, uploaded);                 // on the GPU

	edge::ImageView<bgra8> const view = texture->pixels<bgra8>();
	for (int y = 0; y < 2; ++y)
		for (int x = 0; x < 2; ++x)
			view(x, y) = bgra8(200, 201, 202, 203);  // on the CPU only
	BOOST_TEST((std::as_const(*texture).pixels<bgra8>()(1, 1) == bgra8(200, 201, 202, 203)));

	// a copy made without asking the GPU is the CPU's
	BOOST_TEST((edge::Image<bgra8>(std::as_const(*texture).pixels<bgra8>())(0, 0) == bgra8(200, 201, 202, 203)));

	// read_image asks the GPU, which still has what was uploaded
	edge::Image<bgra8> const image = edge::read_image<bgra8>(*texture);
	BOOST_CHECK_EQUAL_COLLECTIONS(reinterpret_cast<uint8_t const *>(image.pixels()),
		reinterpret_cast<uint8_t const *>(image.pixels()) + 16, uploaded.begin(), uploaded.end());
	// and so has the CPU again, and the view of it, which is still the same copy
	BOOST_TEST((view(0, 0) == bgra8(10, 11, 12, 13)));
	BOOST_TEST((view(1, 1).b == 22 && view(1, 1).g == 23 && view(1, 1).r == 24 && view(1, 1).a == 25));
}

// The font's colours are (r, g, b, a), whatever the texture keeps first.
BOOST_AUTO_TEST_CASE_TEMPLATE(a_fonts_colours_are_red_first, F, edge::Backends)
{
	F fixture;
	auto const &renderer = fixture.renderer;
	BOOST_REQUIRE_MESSAGE(renderer, fixture.error);

	std::string font_path;
	for (char const *candidate : {"/System/Library/Fonts/Supplemental/Arial.ttf", "/System/Library/Fonts/Helvetica.ttc",
		"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "/usr/share/fonts/TTF/DejaVuSans.ttf"})
		if (std::filesystem::is_regular_file(candidate))
		{
			font_path = candidate;
			break;
		}
	if (char const *given = std::getenv("EDGE_TEST_FONT"))
		font_path = given;
	BOOST_REQUIRE_MESSAGE(!font_path.empty(), "no font to test with: set EDGE_TEST_FONT to a .ttf or .ttc");
	edge::Font const font(font_path, 48);

	// no text: a texel of the background
	edge::TexturePtr const background = edge::make_text_texture(renderer, font, "", rove::vec<4>(1, 1, 1, 1), rove::vec<4>(0.2f, 0.4f, 0.6f, 0.8f));
	bgra8 const texel = std::as_const(*background).pixels<bgra8>().at(0, 0);
	BOOST_TEST((texel.r == 51 && texel.g == 102 && texel.b == 153 && texel.a == 204));

	// text on nothing: where the ink is solid, the text's colour; the corner, the background's
	edge::TexturePtr const text = edge::make_text_texture(renderer, font, "HHH", rove::vec<4>(0.2f, 0.4f, 0.6f, 0.8f), rove::vec<4>(0, 0, 0, 0));
	edge::ImageView<bgra8 const> const inked = std::as_const(*text).pixels<bgra8>();
	size_t solid = 0;
	for (int y = 0; y < inked.height(); ++y)
		for (int x = 0; x < inked.width(); ++x)
		{
			bgra8 const &p = inked(x, y);
			// every texel is the colour scaled by its coverage: red the least, blue the most of the three
			BOOST_REQUIRE((p.r <= p.g && p.g <= p.b && p.b <= p.a));
			if (p.a == 204)
			{
				++solid;
				BOOST_REQUIRE((p.r == 51 && p.g == 102 && p.b == 153));
			}
		}
	BOOST_CHECK_GT(solid, 0u);
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE_END()
