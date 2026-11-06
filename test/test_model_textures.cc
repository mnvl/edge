// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

// Contract of bake_textures(): the 8-bit textures of a model become BC7 with their whole mip chain, each averaged the way its use
// says, the materials pointing at the right ones afterwards; and a model that cannot be baked is left as it was.

#include <boost/test/unit_test.hpp>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

#include <rove/vec.h>

#include "model_textures.h"
#include "pixel.h"
#include "serialized_model.h"
#include "image_compress.h"
#include "test_astc.h"
#include "test_bc7.h"
#include "texture.h"

namespace
{

using bgra8 = rove::bgra<std::uint8_t>;
using edge::PixelFormat;
using edge::SerializedModel;
using edge::SerializedTexture;

PixelFormat const BC7 = PixelFormat::BC7;

/// A texture of A8R8G8B8 whose texel (x, y) is f(x, y).
template <class F>
SerializedTexture texture_of(int width, int height, bool srgb, F f, PixelFormat format = PixelFormat::A8R8G8B8)
{
	SerializedTexture t;
	t.filename = "t" + std::to_string(width) + "x" + std::to_string(height);
	t.width = width;
	t.height = height;
	t.pixel_format = format;
	t.srgb = srgb;
	t.pixels.resize(size_t(width) * height * 4);
	for (int y = 0; y < height; ++y)
		for (int x = 0; x < width; ++x)
		{
			bgra8 const c = f(x, y);
			std::memcpy(&t.pixels[(size_t(y) * width + x) * 4], &c, 4);
		}
	return t;
}

SerializedTexture flat(int width, int height, bgra8 const &c, bool srgb = false)
{
	return texture_of(width, height, srgb, [&](int, int) { return c; });
}

/// Black and white by turns: a level under it averages to the grey its kind says.
SerializedTexture checker(int size, bool srgb)
{
	return texture_of(size, size, srgb, [](int x, int y) { return ((x + y) & 1) ? bgra8(255, 255, 255, 255) : bgra8(0, 0, 0, 255); });
}

edge::SerializedMaterial material(std::int64_t diffuse = -1, std::int64_t normal = -1, std::int64_t metallic_roughness = -1, std::int64_t emissive = -1)
{
	edge::SerializedMaterial m;
	m.name = "m";
	if (diffuse >= 0)
		m.diffuse_textures.push_back(diffuse);
	m.normal_map = normal;
	m.metallic_roughness = metallic_roughness;
	m.emissive = emissive;
	return m;
}

SerializedModel model_of(std::vector<SerializedTexture> textures, std::vector<edge::SerializedMaterial> materials)
{
	SerializedModel model;
	model.textures = std::move(textures);
	model.materials = std::move(materials);
	return model;
}

/// Level @p level of a baked texture as an image.
edge::Image<bgra8> level_of(SerializedTexture const &t, int level)
{
	BOOST_REQUIRE_EQUAL(t.pixel_format, BC7);
	size_t const offset = level == 0 ? 0 : edge::Texture::levels_size(t.width, t.height, BC7, level);
	int const w = std::max(1, t.width >> level), h = std::max(1, t.height >> level);
	size_t const size = edge::Texture::level_size(t.width, t.height, BC7, level);
	BOOST_REQUIRE_LE(offset + size, t.pixels.size());
	return edge::decode_bc7(t.pixels.data() + offset, size, w, h);
}

int levels_of(int width, int height)
{
	int n = 1;
	for (int s = std::max(width, height); s > 1; s /= 2)
		++n;
	return n;
}

bool close(bgra8 const &a, bgra8 const &b, int tolerance)
{
	for (int i = 0; i < 4; ++i)
		if (std::abs(int(a.i[i]) - int(b.i[i])) > tolerance)
			return false;
	return true;
}

}

BOOST_AUTO_TEST_SUITE(model_textures)

BOOST_AUTO_TEST_CASE(a_texture_becomes_BC7_with_the_whole_chain_of_mips_in_the_size_a_texture_of_it_has)
{
	for (auto const &size : {std::pair{16, 16}, {64, 8}, {6, 5}, {1, 1}, {4, 1}, {37, 21}})
	{
		SerializedModel model = model_of({flat(size.first, size.second, bgra8(10, 20, 30, 255))}, {material(0)});
		edge::bake_textures(model);
		auto const &t = model.textures.at(0);
		std::string const what = std::to_string(size.first) + " x " + std::to_string(size.second);
		BOOST_TEST(t.pixel_format == BC7, what);
		BOOST_TEST(t.width == size.first, what);
		BOOST_TEST(t.height == size.second, what);
		BOOST_TEST(t.provided_mip_levels == levels_of(size.first, size.second), what);
		BOOST_TEST(!t.mipmaps, what);
		BOOST_TEST(t.pixels.size() == edge::Texture::levels_size(size.first, size.second, BC7, t.provided_mip_levels), what);
		BOOST_TEST(t.filename == "t" + what.substr(0, what.find(' ')) + "x" + what.substr(what.rfind(' ') + 1), what);
	}
}

BOOST_AUTO_TEST_CASE(level_zero_is_the_picture_and_the_last_level_its_average)
{
	SerializedModel model = model_of({texture_of(8, 8, false, [](int x, int y) { (void) y; return bgra8(std::uint8_t(x * 30), 100, std::uint8_t(x * 30), 255); })}, {material(0)});
	edge::bake_textures(model);
	auto const &t = model.textures.at(0);
	auto const top = level_of(t, 0);
	BOOST_TEST(close(top(7, 0), bgra8(210, 100, 210, 255), 6));
	BOOST_TEST(close(top(0, 7), bgra8(0, 100, 0, 255), 6));
	// the 1 x 1 level: the mean (b 105, g 100, r 105)
	BOOST_TEST(close(level_of(t, 3)(0, 0), bgra8(105, 100, 105, 255), 4));
}

BOOST_AUTO_TEST_CASE(a_colour_texture_that_is_sRGB_is_averaged_in_light_and_one_that_is_not_as_it_is)
{
	SerializedModel model = model_of({checker(8, true), checker(8, false)}, {material(0), material(1)});
	edge::bake_textures(model);
	BOOST_TEST(model.textures.at(0).srgb);
	BOOST_TEST(!model.textures.at(1).srgb);
	BOOST_TEST(close(level_of(model.textures.at(0), 1)(1, 1), bgra8(188, 188, 188, 255), 4));
	BOOST_TEST(close(level_of(model.textures.at(1), 1)(1, 1), bgra8(128, 128, 128, 255), 4));
}

BOOST_AUTO_TEST_CASE(emissive_is_colour_too)
{
	SerializedModel model = model_of({checker(8, true)}, {material(-1, -1, -1, 0)});
	edge::bake_textures(model);
	BOOST_TEST(close(level_of(model.textures.at(0), 1)(0, 0), bgra8(188, 188, 188, 255), 4));
}

BOOST_AUTO_TEST_CASE(a_normal_map_is_averaged_as_directions_and_metallic_roughness_as_numbers)
{
	// tilted left and right by turns, up on average
	auto tilts = [](int x, int) { return x & 1 ? bgra8(220, 128, 240, 255) : bgra8(220, 128, 16, 255); };
	SerializedModel model = model_of({texture_of(8, 8, false, tilts), texture_of(8, 8, false, tilts)}, {material(-1, 0, 1)});
	edge::bake_textures(model);
	auto const &normal = model.textures.at(size_t(model.materials.at(0).normal_map));
	auto const &data = model.textures.at(size_t(model.materials.at(0).metallic_roughness));
	// as directions: straight up, of unit length (blue 255); as numbers: blue stays 220
	BOOST_TEST(close(level_of(normal, 1)(1, 1), bgra8(255, 128, 128, 255), 4));
	BOOST_TEST(close(level_of(data, 1)(1, 1), bgra8(220, 128, 128, 255), 4));
}

BOOST_AUTO_TEST_CASE(blend_weights_are_numbers_and_the_diffuse_textures_of_a_blend_are_colour)
{
	edge::SerializedMaterial m = material(0);
	m.diffuse_textures.push_back(1);
	m.texture_blending_weights = 2;
	SerializedModel model = model_of({checker(8, true), checker(8, true), checker(8, false)}, {m});
	edge::bake_textures(model);
	auto const &baked = model.materials.at(0);
	BOOST_REQUIRE_EQUAL(baked.diffuse_textures.size(), 2u);
	BOOST_TEST(close(level_of(model.textures.at(size_t(baked.diffuse_textures[0])), 1)(0, 0), bgra8(188, 188, 188, 255), 4));
	BOOST_TEST(close(level_of(model.textures.at(size_t(baked.diffuse_textures[1])), 1)(0, 0), bgra8(188, 188, 188, 255), 4));
	BOOST_TEST(close(level_of(model.textures.at(size_t(baked.texture_blending_weights)), 1)(0, 0), bgra8(128, 128, 128, 255), 4));
}

BOOST_AUTO_TEST_CASE(a_texture_of_blend_weights_and_metallic_roughness_is_one_both_being_numbers)
{
	edge::SerializedMaterial m = material(-1, -1, 0);
	m.texture_blending_weights = 0;
	SerializedModel model = model_of({checker(8, false)}, {m});
	edge::bake_textures(model);
	BOOST_TEST(model.textures.size() == 1u);
	BOOST_TEST(model.materials.at(0).metallic_roughness == model.materials.at(0).texture_blending_weights);
	// as ASTC the blocks differ: two
	SerializedModel phone = model_of({checker(8, false)}, {m});
	edge::bake_textures(phone, edge::TextureCodec::ASTC);
	BOOST_TEST(phone.textures.size() == 2u);
}

BOOST_AUTO_TEST_CASE(a_texture_the_materials_share_stays_one_and_materials_keep_their_places)
{
	SerializedModel model = model_of({flat(8, 8, bgra8(1, 2, 3, 255), true), flat(4, 4, bgra8(9, 9, 9, 255))}, {material(0, 1), material(0), material(-1, 1)});
	edge::bake_textures(model);
	BOOST_REQUIRE_EQUAL(model.textures.size(), 2u);
	auto const &m = model.materials;
	BOOST_TEST(m.at(0).diffuse_textures.at(0) == m.at(1).diffuse_textures.at(0));
	BOOST_TEST(m.at(0).normal_map == m.at(2).normal_map);
	BOOST_TEST(model.textures.at(size_t(m.at(0).diffuse_textures.at(0))).width == 8);
	BOOST_TEST(model.textures.at(size_t(m.at(0).normal_map)).width == 4);
	// no texture stays none
	BOOST_TEST(m.at(1).normal_map == -1);
	BOOST_TEST(m.at(2).diffuse_textures.empty());
	BOOST_TEST(m.at(2).emissive == -1);
	BOOST_TEST(m.at(2).metallic_roughness == -1);
	BOOST_TEST(m.at(2).texture_blending_weights == -1);
}

BOOST_AUTO_TEST_CASE(a_texture_used_as_colour_and_as_a_normal_map_is_two_textures_each_averaged_its_way)
{
	auto tilts = [](int x, int) { return x & 1 ? bgra8(220, 128, 240, 255) : bgra8(220, 128, 16, 255); };
	SerializedModel model = model_of({texture_of(8, 8, false, tilts)}, {material(0, 0)});
	edge::bake_textures(model);
	BOOST_REQUIRE_EQUAL(model.textures.size(), 2u);
	auto const &m = model.materials.at(0);
	BOOST_TEST(m.diffuse_textures.at(0) != m.normal_map);
	BOOST_TEST(close(level_of(model.textures.at(size_t(m.diffuse_textures.at(0))), 1)(0, 0), bgra8(220, 128, 128, 255), 4));
	BOOST_TEST(close(level_of(model.textures.at(size_t(m.normal_map)), 1)(0, 0), bgra8(255, 128, 128, 255), 4));
}

BOOST_AUTO_TEST_CASE(block_compressed_textures_are_left_as_they_are)
{
	SerializedTexture dds;
	dds.filename = "from a dds";
	dds.width = 8;
	dds.height = 8;
	dds.pixel_format = BC7;
	dds.srgb = true;
	dds.provided_mip_levels = 4;
	dds.pixels.assign(edge::Texture::levels_size(8, 8, BC7, 4), 0x5A);
	SerializedModel model = model_of({dds, checker(8, true)}, {material(0), material(1)});
	edge::bake_textures(model);
	BOOST_REQUIRE_EQUAL(model.textures.size(), 2u);
	auto const &t = model.textures.at(size_t(model.materials.at(0).diffuse_textures.at(0)));
	BOOST_TEST(t.filename == "from a dds");
	BOOST_TEST(t.provided_mip_levels == 4);
	BOOST_CHECK(t.pixels == dds.pixels);
}

BOOST_AUTO_TEST_CASE(a_texture_without_alpha_is_opaque_whatever_its_unused_byte_says)
{
	SerializedModel model = model_of({texture_of(8, 8, false, [](int, int) { return bgra8(10, 20, 30, 0); }, PixelFormat::X8R8G8B8)}, {material(0)});
	edge::bake_textures(model);
	for (int level = 0; level < 4; ++level)
		BOOST_TEST(level_of(model.textures.at(0), level)(0, 0).a >= 254);        // (the encoder's opaque block may have 254, as its mode has one bit for all four channels)
}

BOOST_AUTO_TEST_CASE(a_translucent_texture_keeps_its_alpha_down_the_chain)
{
	SerializedModel model = model_of({flat(8, 8, bgra8(10, 20, 30, 100))}, {material(0)});
	edge::bake_textures(model);
	for (int level = 0; level < 4; ++level)
		BOOST_TEST(close(level_of(model.textures.at(0), level)(0, 0), bgra8(10, 20, 30, 100), 2));
}

BOOST_AUTO_TEST_CASE(a_model_that_cannot_be_baked_is_left_as_it_was)
{
	auto unchanged = [](SerializedModel model, std::string const &why) {
		SerializedModel const before = model;
		BOOST_CHECK_THROW(edge::bake_textures(model), std::runtime_error);
		BOOST_CHECK_MESSAGE(model.textures.size() == before.textures.size() && model.textures.at(0).pixels == before.textures.at(0).pixels
			&& model.textures.at(0).pixel_format == before.textures.at(0).pixel_format && model.materials.size() == before.materials.size()
			&& model.materials.at(0).diffuse_textures == before.materials.at(0).diffuse_textures, why << ": the model changed");
	};
	// a texture of a pixel format the converter does not know, after a good one in the table
	unchanged(model_of({flat(4, 4, bgra8()), texture_of(4, 4, false, [](int, int) { return bgra8(); }, PixelFormat::R32F)}, {material(0), material(1)}), "R32F");
	// a texture no material uses
	unchanged(model_of({flat(4, 4, bgra8()), flat(4, 4, bgra8())}, {material(0)}), "unused texture");
	// texels fewer than its size needs
	SerializedTexture short_one = flat(4, 4, bgra8());
	short_one.pixels.pop_back();
	unchanged(model_of({flat(4, 4, bgra8()), short_one}, {material(0), material(1)}), "short texels");
	// a material that names a texture that is not there
	unchanged(model_of({flat(4, 4, bgra8())}, {material(0), material(3)}), "dangling index");
}

BOOST_AUTO_TEST_CASE(baking_twice_changes_nothing_the_second_time)
{
	SerializedModel model = model_of({checker(8, true)}, {material(0)});
	edge::bake_textures(model);
	SerializedModel const once = model;
	edge::bake_textures(model);
	BOOST_REQUIRE_EQUAL(model.textures.size(), 1u);
	BOOST_CHECK(model.textures.at(0).pixels == once.textures.at(0).pixels);
	BOOST_TEST(model.textures.at(0).provided_mip_levels == once.textures.at(0).provided_mip_levels);
}

BOOST_AUTO_TEST_CASE(unused_textures_are_removed_and_the_rest_keep_their_order_and_their_materials)
{
	SerializedModel model = model_of({flat(4, 4, bgra8(1, 0, 0, 255)), flat(5, 5, bgra8(2, 0, 0, 255)), flat(6, 6, bgra8(3, 0, 0, 255)), flat(7, 7, bgra8(4, 0, 0, 255))},
		{material(3, 1), material(-1, -1, 3)});
	edge::remove_unused_textures(model);
	BOOST_REQUIRE_EQUAL(model.textures.size(), 2u);
	BOOST_TEST(model.textures.at(0).width == 5);
	BOOST_TEST(model.textures.at(1).width == 7);
	BOOST_TEST(model.materials.at(0).normal_map == 0);
	BOOST_TEST(model.materials.at(0).diffuse_textures.at(0) == 1);
	BOOST_TEST(model.materials.at(1).metallic_roughness == 1);
	BOOST_TEST(model.materials.at(1).emissive == -1);
}

BOOST_AUTO_TEST_CASE(a_material_that_names_a_texture_that_is_not_there_is_an_error_whoever_asks)
{
	SerializedModel model = model_of({flat(4, 4, bgra8())}, {material(2)});
	BOOST_CHECK_THROW(edge::remove_unused_textures(model), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(without_emissive_no_material_glows_and_the_textures_only_it_used_are_gone)
{
	SerializedModel model = model_of({flat(4, 4, bgra8(1, 0, 0, 255)), flat(5, 5, bgra8(2, 0, 0, 255)), flat(6, 6, bgra8(3, 0, 0, 255))},
		{material(0, -1, -1, 1), material(-1, -1, -1, 0), material(2)});
	model.materials.at(0).emissive_factor = rove::vec<3>(1.0f, 0.5f, 0.0f);
	model.materials.at(2).emissive_factor = rove::vec<3>(0.2f, 0.2f, 0.2f);          // a glow of a colour, no texture
	edge::drop_emissive(model);
	for (auto const &m : model.materials)
	{
		BOOST_TEST(m.emissive == -1);
		BOOST_TEST(m.emissive_factor.x == 0.0f);
		BOOST_TEST(m.emissive_factor.y == 0.0f);
		BOOST_TEST(m.emissive_factor.z == 0.0f);
	}
	// texture 0 stays (the diffuse of the first), 1 was only emissive, 2 is the diffuse of the third
	BOOST_REQUIRE_EQUAL(model.textures.size(), 2u);
	BOOST_TEST(model.textures.at(0).width == 4);
	BOOST_TEST(model.textures.at(1).width == 6);
	BOOST_TEST(model.materials.at(0).diffuse_textures.at(0) == 0);
	BOOST_TEST(model.materials.at(2).diffuse_textures.at(0) == 1);
}

BOOST_AUTO_TEST_SUITE_END()

// bake_textures() with TextureCodec::ASTC, for phones (which lack BC): the same chains, each level ASTC of the block its use takes:
// colour 6x6 (8x8 for a texture of 128 or less on a side, where nobody sees the detail), numbers (metallic-roughness) 8x8, normal
// maps and blend weights (masks) 4x4.

namespace
{

PixelFormat const ASTC_4x4 = PixelFormat::ASTC_4x4, ASTC_6x6 = PixelFormat::ASTC_6x6, ASTC_8x8 = PixelFormat::ASTC_8x8;

int block_of(PixelFormat format)
{
	return format == ASTC_4x4 ? 4 : format == ASTC_6x6 ? 6 : format == ASTC_8x8 ? 8 : 0;
}

edge::Image<bgra8> astc_level_of(SerializedTexture const &t, int level)
{
	int const block = block_of(t.pixel_format);
	BOOST_REQUIRE(block > 0);
	size_t const offset = level == 0 ? 0 : edge::Texture::levels_size(t.width, t.height, t.pixel_format, level);
	int const w = std::max(1, t.width >> level), h = std::max(1, t.height >> level);
	size_t const size = edge::Texture::level_size(t.width, t.height, t.pixel_format, level);
	BOOST_REQUIRE_LE(offset + size, t.pixels.size());
	return edge::decode_astc(t.pixels.data() + offset, size, w, h, block, t.srgb);
}

SerializedModel baked_astc(std::vector<SerializedTexture> textures, std::vector<edge::SerializedMaterial> materials)
{
	SerializedModel model = model_of(std::move(textures), std::move(materials));
	edge::bake_textures(model, edge::TextureCodec::ASTC);
	return model;
}

}

BOOST_AUTO_TEST_SUITE(model_textures_astc)

BOOST_AUTO_TEST_CASE(each_use_has_its_block_and_the_whole_chain_in_the_size_a_texture_of_it_has)
{
	edge::SerializedMaterial m = material(0, 1, 2, 3);
	m.diffuse_textures.push_back(4);
	m.texture_blending_weights = 5;
	SerializedModel const model = baked_astc({flat(256, 200, bgra8(10, 20, 30, 255), true), flat(64, 64, bgra8(255, 128, 128, 255)),
		flat(300, 20, bgra8(0, 90, 200, 255)), flat(128, 128, bgra8(1, 2, 3, 255), true), flat(129, 7, bgra8(4, 5, 6, 255), true),
		flat(32, 32, bgra8(255, 0, 0, 255))}, {m});
	auto const &b = model.materials.at(0);
	auto const format = [&](std::int64_t slot) { return model.textures.at(size_t(slot)).pixel_format; };
	BOOST_TEST(format(b.diffuse_textures.at(0)) == ASTC_6x6);       // colour, 256 on its longer side
	BOOST_TEST(format(b.normal_map) == ASTC_4x4);
	BOOST_TEST(format(b.metallic_roughness) == ASTC_8x8);
	BOOST_TEST(format(b.emissive) == ASTC_8x8);                     // colour of 128 on a side
	BOOST_TEST(format(b.diffuse_textures.at(1)) == ASTC_6x6);       // colour of 129 on its longer side
	BOOST_TEST(format(b.texture_blending_weights) == ASTC_4x4);
	for (SerializedTexture const &t : model.textures)
	{
		BOOST_TEST(t.provided_mip_levels == levels_of(t.width, t.height), t.filename);
		BOOST_TEST(t.pixels.size() == edge::Texture::levels_size(t.width, t.height, t.pixel_format, t.provided_mip_levels), t.filename);
		BOOST_TEST(!t.mipmaps);
	}
	BOOST_TEST(model.textures.at(size_t(b.diffuse_textures.at(0))).srgb);
	BOOST_TEST(!model.textures.at(size_t(b.normal_map)).srgb);
}

BOOST_AUTO_TEST_CASE(level_zero_is_the_picture_and_the_last_level_its_average_each_averaged_its_way)
{
	auto const ramp = [](int x, int) { return bgra8(std::uint8_t(x * 30), 100, std::uint8_t(x * 30), 255); };
	SerializedModel const model = baked_astc({texture_of(8, 8, false, ramp), checker(8, true), checker(8, false)}, {material(0, -1, 2), material(1)});
	SerializedTexture const &colour = model.textures.at(size_t(model.materials.at(0).diffuse_textures.at(0)));
	auto const first = astc_level_of(colour, 0);
	for (int x = 0; x < 8; ++x)
		BOOST_TEST(close(first(x, 3), ramp(x, 3), 6), "texel " << x);
	BOOST_TEST(close(astc_level_of(colour, 3)(0, 0), bgra8(105, 100, 105, 255), 4));
	// a checker of sRGB is averaged in light, one of numbers as numbers
	BOOST_TEST(close(astc_level_of(model.textures.at(size_t(model.materials.at(1).diffuse_textures.at(0))), 1)(0, 0), bgra8(188, 188, 188, 255), 6));
	BOOST_TEST(close(astc_level_of(model.textures.at(size_t(model.materials.at(0).metallic_roughness)), 1)(0, 0), bgra8(128, 128, 128, 255), 6));
}

BOOST_AUTO_TEST_CASE(an_astc_texture_is_kept_and_a_BC_one_refused_since_a_phone_cannot_sample_it)
{
	SerializedTexture dds;
	dds.filename = "from a dds";
	dds.width = 8;
	dds.height = 8;
	dds.pixel_format = ASTC_4x4;
	dds.provided_mip_levels = 4;
	dds.pixels.assign(edge::Texture::levels_size(8, 8, ASTC_4x4, 4), 0x5A);
	SerializedModel const kept = baked_astc({dds, checker(8, true)}, {material(0), material(1)});
	BOOST_CHECK(kept.textures.at(size_t(kept.materials.at(0).diffuse_textures.at(0))).pixels == dds.pixels);

	dds.pixel_format = BC7;
	dds.pixels.assign(edge::Texture::levels_size(8, 8, BC7, 4), 0x5A);
	SerializedModel model = model_of({checker(8, true), dds}, {material(0), material(1)});
	SerializedModel const before = model;
	try
	{
		edge::bake_textures(model, edge::TextureCodec::ASTC);
		BOOST_FAIL("a BC7 texture was let into an ASTC model");
	}
	catch (std::runtime_error const &e)
	{
		BOOST_CHECK_NE(std::string(e.what()).find("from a dds"), std::string::npos);
	}
	BOOST_CHECK(model.textures.at(0).pixels == before.textures.at(0).pixels);
	BOOST_CHECK_EQUAL(model.textures.at(0).pixel_format, before.textures.at(0).pixel_format);
}

BOOST_AUTO_TEST_SUITE_END()
