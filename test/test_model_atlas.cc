// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

// Contract of atlas_materials(): the textures of the materials become one set, a material's triangles sample its own cell of it and
// see what they saw before (the same colour at the same place on the surface, the right way up and round), cells never overlap or
// bleed into a neighbour, and what cannot be packed is left alone or refused with the model as it was.

#include <boost/test/unit_test.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include <rove/vec.h>

#include "image.h"
#include "model_atlas.h"
#include "pixel.h"
#include "serialized_model.h"

namespace
{

using bgra8 = rove::bgra<std::uint8_t>;
using edge::PixelFormat;
using edge::SerializedModel;
using edge::SerializedTexture;

bgra8 const RED(0, 0, 255, 255), GREEN(0, 255, 0, 255), BLUE(255, 0, 0, 255), YELLOW(0, 255, 255, 255), WHITE(255, 255, 255, 255),
	GREY(90, 90, 90, 255), CYAN(255, 255, 0, 255), MAGENTA(255, 0, 255, 255);

/// A texture of four colours, one in each quarter: (top left, top right, bottom left, bottom right).
SerializedTexture quarters(int width, int height, bgra8 tl, bgra8 tr, bgra8 bl, bgra8 br, bool srgb = false, PixelFormat format = PixelFormat::A8R8G8B8)
{
	SerializedTexture t;
	t.filename = "quarters" + std::to_string(width) + "x" + std::to_string(height);
	t.width = width;
	t.height = height;
	t.pixel_format = format;
	t.srgb = srgb;
	t.pixels.resize(size_t(width) * height * 4);
	for (int y = 0; y < height; ++y)
		for (int x = 0; x < width; ++x)
		{
			bgra8 const c = y < height / 2 ? (x < width / 2 ? tl : tr) : (x < width / 2 ? bl : br);
			std::memcpy(&t.pixels[(size_t(y) * width + x) * 4], &c, 4);
		}
	return t;
}

SerializedTexture flat(int size, bgra8 c, bool srgb = false)
{
	return quarters(size, size, c, c, c, c, srgb);
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

/// The places on a surface the tests look at: the centres of its quarters and its four corners.
rove::vec<2> const PLACES[8] = {{0.25f, 0.25f}, {0.75f, 0.25f}, {0.25f, 0.75f}, {0.75f, 0.75f}, {0.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 1.0f}, {1.0f, 1.0f}};

/// A mesh with a group of four triangles for each material of @p materials, over 8 vertices of its own at PLACES (so ranges of vertices
/// never overlap).
edge::SerializedMesh mesh_of(std::vector<std::int64_t> const &materials)
{
	edge::SerializedMesh mesh;
	mesh.serialization_type = "static_mesh:test";
	for (long material : materials)
	{
		edge::SerializedTriangles group;
		group.material_index = material;
		group.minimal_vertex_index = mesh.vertices.size();
		group.vertices_count = 8;
		group.first_index_offset = mesh.indices.size();
		group.triangles_count = 4;
		for (auto p : PLACES)
		{
			edge::GenericVertex v{};
			v.texcoords = p;
			mesh.vertices.push_back(v);
		}
		unsigned const base = unsigned(group.minimal_vertex_index);
		for (unsigned i : {0u, 1u, 2u, 1u, 2u, 3u, 4u, 5u, 6u, 5u, 6u, 7u})
			mesh.indices.push_back(base + i);
		mesh.triangles.push_back(group);
	}
	return mesh;
}

SerializedModel model_of(std::vector<SerializedTexture> textures, std::vector<edge::SerializedMaterial> materials, std::vector<edge::SerializedMesh> meshes)
{
	SerializedModel model;
	model.textures = std::move(textures);
	model.materials = std::move(materials);
	model.meshes = std::move(meshes);
	return model;
}

/// A model of one mesh with a group for each material, in order.
SerializedModel model_one_mesh(std::vector<SerializedTexture> textures, std::vector<edge::SerializedMaterial> materials)
{
	std::vector<std::int64_t> order;
	for (size_t i = 0; i < materials.size(); ++i)
		order.push_back(std::int64_t(i));
	return model_of(std::move(textures), std::move(materials), {mesh_of(order)});
}

/// The texel of the texture at texture coordinates @p uv, the nearest one.
bgra8 sample(SerializedTexture const &t, rove::vec<2> uv)
{
	BOOST_REQUIRE_EQUAL(t.pixel_format, PixelFormat::A8R8G8B8);
	int const x = std::min(t.width - 1, int(std::floor(uv.x * float(t.width)))), y = std::min(t.height - 1, int(std::floor(uv.y * float(t.height))));
	bgra8 c;
	std::memcpy(&c, &t.pixels[(size_t(y) * t.width + x) * 4], 4);
	return c;
}

bool same(bgra8 a, bgra8 b)
{
	return std::memcmp(&a, &b, 4) == 0;
}

/// The texture coordinates of vertex @p p of group @p group of the model's first mesh.
rove::vec<2> uv_of(SerializedModel const &model, size_t group, int p)
{
	return model.meshes.at(0).vertices.at(model.meshes.at(0).triangles.at(group).minimal_vertex_index + size_t(p)).texcoords;
}

/// What a material's texture of a role shows at the @p p-th place of its triangles, after packing.
bgra8 seen(SerializedModel const &model, size_t material, std::int64_t edge::SerializedMaterial::*role, int p)
{
	return sample(model.textures.at(size_t(model.materials.at(material).*role)), uv_of(model, material, p));
}

bgra8 seen_diffuse(SerializedModel const &model, size_t material, int p)
{
	return sample(model.textures.at(size_t(model.materials.at(material).diffuse_textures.at(0))), uv_of(model, material, p));
}

}

BOOST_AUTO_TEST_SUITE(model_atlas)

BOOST_AUTO_TEST_CASE(materials_see_their_own_colours_in_the_right_places_of_one_texture)
{
	SerializedModel model = model_one_mesh({quarters(16, 16, RED, GREEN, BLUE, YELLOW), quarters(32, 32, WHITE, GREY, CYAN, MAGENTA)}, {material(0), material(1)});
	BOOST_TEST(edge::atlas_materials(model, {}) == 2u);
	BOOST_REQUIRE_EQUAL(model.textures.size(), 1u);
	BOOST_TEST(model.materials.at(0).diffuse_textures.at(0) == model.materials.at(1).diffuse_textures.at(0));
	// the quarters of the texture: top left, top right, bottom left, bottom right, and the corners which have the same colours
	bgra8 const first[4] = {RED, GREEN, BLUE, YELLOW}, second[4] = {WHITE, GREY, CYAN, MAGENTA};
	for (int p = 0; p < 4; ++p)
	{
		BOOST_TEST(same(seen_diffuse(model, 0, p), first[p]), "material 0 place " << p);
		BOOST_TEST(same(seen_diffuse(model, 1, p), second[p]), "material 1 place " << p);
	}
}

BOOST_AUTO_TEST_CASE(the_corners_of_a_surface_are_the_colours_of_its_corners_and_not_the_gutter)
{
	SerializedModel model = model_one_mesh({quarters(16, 16, RED, GREEN, BLUE, YELLOW), quarters(16, 16, WHITE, GREY, CYAN, MAGENTA)}, {material(0), material(1)});
	edge::atlas_materials(model, {});
	bgra8 const first[4] = {RED, GREEN, BLUE, YELLOW}, second[4] = {WHITE, GREY, CYAN, MAGENTA};
	for (int c = 0; c < 4; ++c)
	{
		BOOST_TEST(same(seen_diffuse(model, 0, 4 + c), first[c]), "material 0 corner " << c);
		BOOST_TEST(same(seen_diffuse(model, 1, 4 + c), second[c]), "material 1 corner " << c);
	}
}

BOOST_AUTO_TEST_CASE(cells_are_apart_a_cell_of_a_material_holds_nothing_of_the_other)
{
	// the first is red/green/blue/yellow all round, the second white: a cell that bled would show the first's colours in the second
	SerializedModel model = model_one_mesh({quarters(32, 32, RED, GREEN, BLUE, YELLOW), flat(32, WHITE)}, {material(0), material(1)});
	edge::atlas_materials(model, {});
	auto const &atlas = model.textures.at(0);
	// every texel under any of the second's triangles' places and a few texels round them is white
	rove::vec<2> const lo = uv_of(model, 1, 4), hi = uv_of(model, 1, 7);
	int const x0 = int(lo.x * float(atlas.width)), x1 = int(hi.x * float(atlas.width)), y0 = int(lo.y * float(atlas.height)), y1 = int(hi.y * float(atlas.height));
	for (int y = y0; y < y1; ++y)
		for (int x = x0; x < x1; ++x)
			BOOST_REQUIRE_MESSAGE(same(sample(atlas, {(float(x) + 0.5f) / float(atlas.width), (float(y) + 0.5f) / float(atlas.height)}), WHITE),
				"texel " << x << "," << y);
}

BOOST_AUTO_TEST_CASE(a_cell_is_surrounded_by_its_own_edge_texels)
{
	SerializedModel model = model_one_mesh({quarters(32, 32, RED, GREEN, BLUE, YELLOW), flat(32, WHITE)}, {material(0), material(1)});
	edge::atlas_materials(model, {});
	auto const &atlas = model.textures.at(0);
	// one texel beyond each corner of the first's surface is the colour of that corner
	rove::vec<2> const lo = uv_of(model, 0, 4), hi = uv_of(model, 0, 7);
	float const tx = 1.0f / float(atlas.width), ty = 1.0f / float(atlas.height);
	BOOST_TEST(same(sample(atlas, {lo.x - tx, lo.y - ty}), RED));
	BOOST_TEST(same(sample(atlas, {hi.x + tx * 0.5f, lo.y - ty}), GREEN));
	BOOST_TEST(same(sample(atlas, {lo.x - tx, hi.y + ty * 0.5f}), BLUE));
	BOOST_TEST(same(sample(atlas, {hi.x + tx * 0.5f, hi.y + ty * 0.5f}), YELLOW));
}

BOOST_AUTO_TEST_CASE(a_texture_that_is_not_square_is_stretched_to_its_cell_with_its_quarters_in_place)
{
	SerializedModel model = model_one_mesh({quarters(64, 16, RED, GREEN, BLUE, YELLOW), flat(16, WHITE)}, {material(0), material(1)});
	edge::atlas_materials(model, {});
	bgra8 const first[4] = {RED, GREEN, BLUE, YELLOW};
	for (int p = 0; p < 4; ++p)
		BOOST_TEST(same(seen_diffuse(model, 0, p), first[p]), "place " << p);
}

BOOST_AUTO_TEST_CASE(each_role_has_one_texture_and_a_material_without_the_role_has_none_in_it)
{
	// 0: colour and normal map; 1: colour and metallic-roughness; 2: emissive only
	bgra8 const UP(255, 128, 128, 255);       // a normal map is a direction, which comes out of the atlas as a unit vector: this one does as it is
	SerializedModel model = model_one_mesh({flat(16, RED), flat(16, GREEN), flat(16, UP), flat(16, YELLOW), flat(16, WHITE), flat(16, CYAN)},
		{material(0, 2), material(1, -1, 3), material(-1, -1, -1, 4), material(5)});
	BOOST_TEST(edge::atlas_materials(model, {}) == 4u);
	// colour, normal, metallic-roughness, emissive
	BOOST_TEST(model.textures.size() == 4u);
	auto const &m = model.materials;
	BOOST_TEST(m.at(0).diffuse_textures.at(0) == m.at(1).diffuse_textures.at(0));
	BOOST_TEST(m.at(0).diffuse_textures.at(0) == m.at(3).diffuse_textures.at(0));
	BOOST_TEST(m.at(0).normal_map >= 0);
	BOOST_TEST(m.at(1).normal_map == -1);
	BOOST_TEST(m.at(2).normal_map == -1);
	BOOST_TEST(m.at(1).metallic_roughness >= 0);
	BOOST_TEST(m.at(0).metallic_roughness == -1);
	BOOST_TEST(m.at(2).emissive >= 0);
	BOOST_TEST(m.at(0).emissive == -1);
	BOOST_TEST(m.at(2).diffuse_textures.empty());
	std::set<std::int64_t> distinct = {m.at(0).diffuse_textures.at(0), m.at(0).normal_map, m.at(1).metallic_roughness, m.at(2).emissive};
	BOOST_TEST(distinct.size() == 4u);
	// each shows what its material's texture of the role had
	BOOST_TEST(same(seen(model, 0, &edge::SerializedMaterial::normal_map, 0), UP));
	BOOST_TEST(same(seen(model, 1, &edge::SerializedMaterial::metallic_roughness, 0), YELLOW));
	BOOST_TEST(same(seen(model, 2, &edge::SerializedMaterial::emissive, 0), WHITE));
	BOOST_TEST(same(seen_diffuse(model, 3, 0), CYAN));
}

BOOST_AUTO_TEST_CASE(materials_with_the_same_textures_share_a_cell_and_the_atlas_is_smaller_for_it)
{
	SerializedModel two = model_one_mesh({quarters(64, 64, RED, GREEN, BLUE, YELLOW)}, {material(0), material(0)});
	// the other material is alone in having its texture: nothing to pack with, but the pair above is one cell and so one material to pack
	BOOST_TEST(edge::atlas_materials(two, {}) == 0u);
	SerializedModel three = model_one_mesh({quarters(64, 64, RED, GREEN, BLUE, YELLOW), flat(64, WHITE)}, {material(0), material(0), material(1)});
	BOOST_TEST(edge::atlas_materials(three, {}) == 3u);
	BOOST_TEST(uv_of(three, 0, 0).x == uv_of(three, 1, 0).x);
	BOOST_TEST(uv_of(three, 0, 0).y == uv_of(three, 1, 0).y);
	// two cells of 64 are 128 x 64
	BOOST_TEST(three.textures.at(0).width == 128);
	BOOST_TEST(three.textures.at(0).height == 64);
}

BOOST_AUTO_TEST_CASE(the_atlas_is_a_power_of_two_square_no_larger_than_the_size_asked_and_no_cell_overlaps)
{
	// 50 materials of 64 x 64: 204800 texels, in 256 x 256 only if the cells are made smaller
	std::vector<SerializedTexture> textures;
	std::vector<edge::SerializedMaterial> materials;
	for (int i = 0; i < 50; ++i)
	{
		textures.push_back(flat(64, bgra8(std::uint8_t(i * 5), std::uint8_t(255 - i * 5), std::uint8_t(i), 255)));
		materials.push_back(material(i));
	}
	SerializedModel model = model_one_mesh(std::move(textures), std::move(materials));
	edge::AtlasOptions options;
	options.size = 256;
	BOOST_TEST(edge::atlas_materials(model, options) == 50u);
	auto const &atlas = model.textures.at(0);
	BOOST_TEST(atlas.width <= 256);
	BOOST_TEST((atlas.width == atlas.height || atlas.width == 2 * atlas.height));
	BOOST_TEST(((atlas.width & (atlas.width - 1)) == 0));
	// every material sees its own colour at the middle of its surface, and the rectangles of the surfaces are apart
	std::vector<std::pair<rove::vec<2>, rove::vec<2>>> rects;
	for (size_t i = 0; i < 50; ++i)
	{
		bgra8 const want(std::uint8_t(i * 5), std::uint8_t(255 - i * 5), std::uint8_t(i), 255);
		BOOST_REQUIRE_MESSAGE(same(seen_diffuse(model, i, 3), want), "material " << i);
		rects.push_back({uv_of(model, i, 4), uv_of(model, i, 7)});
	}
	for (size_t i = 0; i < rects.size(); ++i)
		for (size_t j = i + 1; j < rects.size(); ++j)
			BOOST_REQUIRE_MESSAGE(rects[i].second.x <= rects[j].first.x || rects[j].second.x <= rects[i].first.x
				|| rects[i].second.y <= rects[j].first.y || rects[j].second.y <= rects[i].first.y, "materials " << i << " and " << j << " overlap");
	for (auto const &r : rects)
		BOOST_REQUIRE(r.first.x >= 0 && r.first.y >= 0 && r.second.x <= 1 && r.second.y <= 1);
}

BOOST_AUTO_TEST_CASE(a_cell_is_not_larger_than_its_texture_so_a_few_small_textures_make_a_small_atlas)
{
	SerializedModel model = model_one_mesh({flat(16, RED), flat(16, GREEN)}, {material(0), material(1)});
	edge::atlas_materials(model, {});
	BOOST_TEST(model.textures.at(0).width == 32);
	BOOST_TEST(model.textures.at(0).height == 16);
}

BOOST_AUTO_TEST_CASE(a_rectangle_of_cells_has_every_cell_in_the_right_place)
{
	// three cells of 16 fill 32 x 32 but for one: a square; two fill a half; five need 64 x 32
	for (size_t count : {2u, 3u, 5u})
	{
		std::vector<SerializedTexture> textures;
		std::vector<edge::SerializedMaterial> materials;
		for (size_t i = 0; i < count; ++i)
		{
			textures.push_back(flat(16, bgra8(std::uint8_t(i * 40 + 10), 0, 0, 255)));
			materials.push_back(material(std::int64_t(i)));
		}
		SerializedModel model = model_one_mesh(std::move(textures), std::move(materials));
		edge::atlas_materials(model, {});
		auto const &atlas = model.textures.at(0);
		BOOST_TEST(atlas.width * atlas.height >= int(count) * 16 * 16);
		BOOST_TEST(atlas.width * atlas.height < 4 * int(count) * 16 * 16);
		for (size_t i = 0; i < count; ++i)
			for (int p = 0; p < 8; ++p)
				BOOST_REQUIRE_MESSAGE(same(seen_diffuse(model, i, p), bgra8(std::uint8_t(i * 40 + 10), 0, 0, 255)), count << " cells, material " << i << " place " << p);
	}
}

BOOST_AUTO_TEST_CASE(too_many_materials_for_the_size_is_an_error_and_the_model_is_as_it_was)
{
	std::vector<SerializedTexture> textures;
	std::vector<edge::SerializedMaterial> materials;
	for (int i = 0; i < 10; ++i)
	{
		textures.push_back(flat(8, bgra8(std::uint8_t(i), 0, 0, 255)));
		materials.push_back(material(i));
	}
	SerializedModel model = model_one_mesh(std::move(textures), std::move(materials));
	SerializedModel const before = model;
	edge::AtlasOptions options;
	options.size = 8;           // 4 cells of 4 x 4 at the most
	BOOST_CHECK_THROW(edge::atlas_materials(model, options), std::runtime_error);
	BOOST_CHECK(model.textures.size() == before.textures.size() && model.meshes.at(0).vertices.size() == before.meshes.at(0).vertices.size());
	BOOST_TEST(uv_of(model, 3, 0).x == uv_of(before, 3, 0).x);
}

BOOST_AUTO_TEST_CASE(a_material_whose_texture_tiles_is_left_with_its_own_texture_and_coordinates)
{
	SerializedModel model = model_one_mesh({flat(16, RED), flat(16, GREEN), flat(16, BLUE)}, {material(0), material(1), material(2)});
	model.meshes.at(0).vertices.at(8 + 7).texcoords = {3.0f, 1.0f};         // the second material's
	BOOST_TEST(edge::atlas_materials(model, {}) == 2u);
	auto const &m = model.materials;
	BOOST_TEST(m.at(0).diffuse_textures.at(0) == m.at(2).diffuse_textures.at(0));
	BOOST_TEST(m.at(1).diffuse_textures.at(0) != m.at(0).diffuse_textures.at(0));
	BOOST_TEST(same(sample(model.textures.at(size_t(m.at(1).diffuse_textures.at(0))), {0.5f, 0.5f}), GREEN));
	BOOST_TEST(uv_of(model, 1, 7).x == 3.0f);
	BOOST_TEST(uv_of(model, 1, 0).x == 0.25f);
	BOOST_TEST(model.textures.size() == 2u);
}

BOOST_AUTO_TEST_CASE(coordinates_that_slightly_overshoot_are_not_tiling)
{
	SerializedModel model = model_one_mesh({flat(16, RED), flat(16, GREEN)}, {material(0), material(1)});
	model.meshes.at(0).vertices.at(7).texcoords = {1.0004f, -0.0004f};
	BOOST_TEST(edge::atlas_materials(model, {}) == 2u);
}

BOOST_AUTO_TEST_CASE(a_material_is_left_when_it_has_a_blend_a_compressed_texture_or_many_diffuse_textures)
{
	SerializedTexture dds = flat(16, RED);
	dds.pixel_format = PixelFormat::BC7;
	dds.pixels.assign(edge::pixel_data_size(16, 16, PixelFormat::BC7), 0);
	edge::SerializedMaterial blend = material(2);
	blend.diffuse_textures.push_back(3);
	blend.texture_blending_weights = 4;
	edge::SerializedMaterial many = material(5);
	many.diffuse_textures.push_back(6);
	SerializedModel model = model_one_mesh({dds, flat(16, GREEN), flat(16, BLUE), flat(16, BLUE), flat(16, WHITE), flat(16, CYAN), flat(16, MAGENTA), flat(16, GREY), flat(16, YELLOW)},
		{material(0), material(1), blend, many, material(7), material(8)});
	BOOST_TEST(edge::atlas_materials(model, {}) == 3u);          // the ones of green, grey and yellow
	auto const &m = model.materials;
	BOOST_TEST(m.at(2).diffuse_textures.size() == 2u);
	BOOST_TEST(m.at(2).texture_blending_weights >= 0);
	BOOST_TEST(m.at(3).diffuse_textures.size() == 2u);
	BOOST_TEST(model.textures.at(size_t(m.at(0).diffuse_textures.at(0))).pixel_format == PixelFormat::BC7);
}

BOOST_AUTO_TEST_CASE(fewer_than_two_materials_to_pack_is_nothing_to_do)
{
	SerializedModel model = model_one_mesh({flat(16, RED), flat(16, GREEN)}, {material(0), material(-1), material(-1)});
	SerializedModel const before = model;
	BOOST_TEST(edge::atlas_materials(model, {}) == 0u);
	BOOST_CHECK(model.textures.size() == 2u && model.materials.at(0).diffuse_textures.at(0) == 0);
	BOOST_TEST(uv_of(model, 0, 0).x == uv_of(before, 0, 0).x);
}

BOOST_AUTO_TEST_CASE(a_vertex_that_two_packed_materials_share_is_refused_and_the_model_left_as_it_was)
{
	SerializedModel model = model_one_mesh({flat(16, RED), flat(16, GREEN)}, {material(0), material(1)});
	auto &mesh = model.meshes.at(0);
	mesh.indices.at(12) = 0;         // the second group's first triangle uses the first group's vertex
	SerializedModel const before = model;
	BOOST_CHECK_THROW(edge::atlas_materials(model, {}), std::runtime_error);
	BOOST_CHECK(model.textures.size() == 2u);
	BOOST_TEST(uv_of(model, 0, 0).x == uv_of(before, 0, 0).x);
	BOOST_TEST(uv_of(model, 1, 0).x == uv_of(before, 1, 0).x);
}

BOOST_AUTO_TEST_CASE(the_same_mesh_may_be_used_by_a_material_in_many_groups_and_meshes)
{
	// the first material in two meshes, the second in one
	std::vector<SerializedTexture> textures = {quarters(16, 16, RED, GREEN, BLUE, YELLOW), flat(16, WHITE)};
	SerializedModel model = model_of(textures, {material(0), material(1)}, {mesh_of({0, 1}), mesh_of({0})});
	BOOST_TEST(edge::atlas_materials(model, {}) == 2u);
	auto const &a = model.meshes.at(0).vertices, &b = model.meshes.at(1).vertices;
	// the same place of the same material has the same coordinates in both meshes
	BOOST_TEST(a.at(0).texcoords.x == b.at(0).texcoords.x);
	BOOST_TEST(a.at(0).texcoords.y == b.at(0).texcoords.y);
	BOOST_TEST(a.at(0).texcoords.x != a.at(8).texcoords.x);
}

BOOST_AUTO_TEST_CASE(a_mesh_with_levels_of_detail_or_morph_targets_that_move_coordinates_is_refused)
{
	SerializedModel lod = model_one_mesh({flat(16, RED), flat(16, GREEN)}, {material(0), material(1)});
	lod.meshes.at(0).lods.push_back({});
	BOOST_CHECK_THROW(edge::atlas_materials(lod, {}), std::runtime_error);

	SerializedModel morph = model_one_mesh({flat(16, RED), flat(16, GREEN)}, {material(0), material(1)});
	morph.meshes.at(0).morph_target_deltas.push_back(std::vector<edge::GenericVertex>(16, edge::GenericVertex{}));
	morph.meshes.at(0).morph_target_deltas.at(0).at(3).texcoords = {0.1f, 0.0f};
	BOOST_CHECK_THROW(edge::atlas_materials(morph, {}), std::runtime_error);

	// a morph target that moves positions only is fine
	SerializedModel fine = model_one_mesh({flat(16, RED), flat(16, GREEN)}, {material(0), material(1)});
	fine.meshes.at(0).morph_target_deltas.push_back(std::vector<edge::GenericVertex>(16, edge::GenericVertex{}));
	fine.meshes.at(0).morph_target_deltas.at(0).at(3).position = {1.0f, 0.0f, 0.0f};
	BOOST_TEST(edge::atlas_materials(fine, {}) == 2u);
}

BOOST_AUTO_TEST_CASE(colour_textures_are_all_sRGB_or_none_and_the_atlas_is_what_they_are)
{
	SerializedModel srgb = model_one_mesh({flat(16, RED, true), flat(16, GREEN, true)}, {material(0), material(1)});
	edge::atlas_materials(srgb, {});
	BOOST_TEST(srgb.textures.at(0).srgb);
	SerializedModel linear = model_one_mesh({flat(16, RED), flat(16, GREEN)}, {material(0), material(1)});
	edge::atlas_materials(linear, {});
	BOOST_TEST(!linear.textures.at(0).srgb);
	SerializedModel mixed = model_one_mesh({flat(16, RED, true), flat(16, GREEN)}, {material(0), material(1)});
	BOOST_CHECK_THROW(edge::atlas_materials(mixed, {}), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(a_picture_made_smaller_is_averaged_in_the_way_of_its_role)
{
	// 48 x 48 of black and white texels by turns, and three more like it, in an atlas of 64: the cells are 32 (the 64 of 48 halved), a content of
	// 24, half the picture, so that each texel of it is two of the picture's, one black and one white
	auto checker = [](bool srgb) {
		SerializedTexture t;
		t.filename = "checker";
		t.width = t.height = 48;
		t.pixel_format = PixelFormat::A8R8G8B8;
		t.srgb = srgb;
		t.pixels.resize(48 * 48 * 4);
		for (int y = 0; y < 48; ++y)
			for (int x = 0; x < 48; ++x)
			{
				bgra8 const c = (x + y) & 1 ? WHITE : bgra8(0, 0, 0, 255);
				std::memcpy(&t.pixels[(size_t(y) * 48 + x) * 4], &c, 4);
			}
		return t;
	};
	edge::AtlasOptions options;
	options.size = 64;
	// colour as sRGB: the grey of half the light; the same picture as numbers (the metallic-roughness): half the number
	SerializedModel model = model_one_mesh({checker(true), checker(false), flat(48, RED, true), flat(48, GREEN, true)},
		{material(0, -1, 1), material(2), material(3)});
	BOOST_TEST(edge::atlas_materials(model, options) == 3u);
	BOOST_TEST(model.textures.at(size_t(model.materials.at(0).diffuse_textures.at(0))).width == 64);
	BOOST_TEST(std::abs(int(seen_diffuse(model, 0, 3).r) - 188) <= 2);
	BOOST_TEST(std::abs(int(seen(model, 0, &edge::SerializedMaterial::metallic_roughness, 3).r) - 128) <= 2);
}

BOOST_AUTO_TEST_CASE(a_texture_the_materials_dropped_is_gone_from_the_table_and_the_others_keep_their_places)
{
	SerializedModel model = model_one_mesh({flat(16, RED), flat(16, GREEN), flat(16, BLUE)}, {material(0), material(1), material(2)});
	model.meshes.at(0).vertices.at(8 + 7).texcoords = {3.0f, 1.0f};        // material 1 tiles: keeps texture 1
	edge::atlas_materials(model, {});
	BOOST_TEST(model.textures.size() == 2u);
	for (auto const &m : model.materials)
		BOOST_REQUIRE(m.diffuse_textures.at(0) >= 0 && size_t(m.diffuse_textures.at(0)) < model.textures.size());
}

BOOST_AUTO_TEST_CASE(packing_twice_gives_the_same_bytes)
{
	auto make = [] { return model_one_mesh({quarters(32, 32, RED, GREEN, BLUE, YELLOW), flat(16, WHITE), flat(64, GREY)}, {material(0, 1), material(1), material(2)}); };
	SerializedModel a = make(), b = make();
	edge::atlas_materials(a, {});
	edge::atlas_materials(b, {});
	BOOST_REQUIRE_EQUAL(a.textures.size(), b.textures.size());
	for (size_t i = 0; i < a.textures.size(); ++i)
		BOOST_CHECK(a.textures[i].pixels == b.textures[i].pixels);
	BOOST_CHECK(a.meshes.at(0).vertices.at(9).texcoords.x == b.meshes.at(0).vertices.at(9).texcoords.x);
}

BOOST_AUTO_TEST_CASE(a_model_that_is_not_as_it_says_is_refused_and_left_as_it_was)
{
	// a material that names a texture that is not there
	SerializedModel dangling = model_one_mesh({flat(16, RED)}, {material(0), material(4)});
	BOOST_CHECK_THROW(edge::atlas_materials(dangling, {}), std::runtime_error);
	// a texture of a pixel format of other than 8 bits
	SerializedTexture floats = flat(16, RED);
	floats.pixel_format = PixelFormat::R32F;
	SerializedModel odd = model_one_mesh({floats, flat(16, GREEN)}, {material(0), material(1)});
	BOOST_CHECK_THROW(edge::atlas_materials(odd, {}), std::runtime_error);
	// texels fewer than the size says
	SerializedTexture shorter = flat(16, RED);
	shorter.pixels.pop_back();
	SerializedModel cut = model_one_mesh({shorter, flat(16, GREEN)}, {material(0), material(1)});
	BOOST_CHECK_THROW(edge::atlas_materials(cut, {}), std::runtime_error);
	// a group whose material is not there
	SerializedModel lost = model_one_mesh({flat(16, RED), flat(16, GREEN)}, {material(0), material(1)});
	lost.meshes.at(0).triangles.at(0).material_index = 5;
	BOOST_CHECK_THROW(edge::atlas_materials(lost, {}), std::runtime_error);
}

BOOST_AUTO_TEST_SUITE_END()

namespace
{

/// A surface of two triangles from (x, 0, 0) to (x + side, 0, side) facing up, its texture coordinates 0..@p repeat across it.
struct Quad
{
	std::int64_t material;
	float x = 0;
	float side = 1;
	float repeat = 1;
};

edge::SerializedMesh quads_mesh(std::vector<Quad> const &quads)
{
	edge::SerializedMesh mesh;
	mesh.serialization_type = "static_mesh:test";
	for (Quad const &q : quads)
	{
		edge::SerializedTriangles group;
		group.material_index = q.material;
		group.minimal_vertex_index = mesh.vertices.size();
		group.vertices_count = 4;
		group.first_index_offset = mesh.indices.size();
		group.triangles_count = 2;
		for (int c = 0; c < 4; ++c)
		{
			edge::GenericVertex v{};
			float const u = float(c & 1), w = float(c >> 1);
			v.position = {q.x + u * q.side, 0.0f, w * q.side};
			v.normal = {0.0f, 1.0f, 0.0f};
			v.texcoords = {u * q.repeat, w * q.repeat};
			mesh.vertices.push_back(v);
		}
		unsigned const base = unsigned(group.minimal_vertex_index);
		for (unsigned i : {0u, 1u, 2u, 1u, 3u, 2u})
			mesh.indices.push_back(base + i);
		mesh.triangles.push_back(group);
	}
	return mesh;
}

SerializedModel model_of_quads(std::vector<SerializedTexture> textures, std::vector<edge::SerializedMaterial> materials, std::vector<Quad> const &quads)
{
	return model_of(std::move(textures), std::move(materials), {quads_mesh(quads)});
}

edge::AtlasOptions xatlas_options(int size = 1024)
{
	edge::AtlasOptions options;
	options.mode = edge::AtlasOptions::Mode::Xatlas;
	options.size = size;
	return options;
}

/// The surface point of the face @p face of mesh @p m at barycentric coordinates (a, b, 1 - a - b) of the model as it was and as it is now,
/// and what its material's diffuse texture shows there: nearest texel, textures repeating.
struct Seen
{
	bgra8 colour;
	rove::vec<2> uv;
};

Seen seen_at(SerializedModel const &model, size_t m, size_t face, float a, float b)
{
	auto const &mesh = model.meshes.at(m);
	unsigned const i0 = mesh.indices.at(3 * face), i1 = mesh.indices.at(3 * face + 1), i2 = mesh.indices.at(3 * face + 2);
	float const c = 1.0f - a - b;
	rove::vec<2> const uv(a * mesh.vertices.at(i0).texcoords.x + b * mesh.vertices.at(i1).texcoords.x + c * mesh.vertices.at(i2).texcoords.x,
		a * mesh.vertices.at(i0).texcoords.y + b * mesh.vertices.at(i1).texcoords.y + c * mesh.vertices.at(i2).texcoords.y);
	size_t group = 0;
	for (size_t g = 0; g < mesh.triangles.size(); ++g)
		if (3 * face >= mesh.triangles[g].first_index_offset && 3 * face < mesh.triangles[g].first_index_offset + 3 * mesh.triangles[g].triangles_count)
			group = g;
	auto const &material = model.materials.at(size_t(mesh.triangles.at(group).material_index));
	auto const &texture = model.textures.at(size_t(material.diffuse_textures.at(0)));
	float const u = uv.x - std::floor(uv.x), v = uv.y - std::floor(uv.y);
	return {sample(texture, {u, v}), uv};
}

/// Whether the point is further than @p margin (of a quarter of the texture, repeating) from where the quarters of the picture change colour.
bool clear_of_seams(rove::vec<2> uv, float margin)
{
	for (float t : {uv.x, uv.y})
	{
		float const f = t - std::floor(t);
		for (float edge : {0.0f, 0.5f, 1.0f})
			if (std::abs(f - edge) < margin)
				return false;
	}
	return true;
}

/// Every point of every face of the model that is clear of seams shows the same colour after the atlas as before: what the surface looks
/// like does not change.  Returns how many points were looked at.
size_t same_appearance(SerializedModel const &before, SerializedModel const &after, float margin = 0.08f, int tolerance = 40)
{
	size_t looked = 0;
	BOOST_REQUIRE_EQUAL(before.meshes.size(), after.meshes.size());
	for (size_t m = 0; m < before.meshes.size(); ++m)
	{
		BOOST_REQUIRE_EQUAL(before.meshes[m].indices.size(), after.meshes[m].indices.size());
		for (size_t face = 0; face < before.meshes[m].indices.size() / 3; ++face)
		{
			size_t group = 0;
			for (size_t g = 0; g < before.meshes[m].triangles.size(); ++g)
				if (3 * face >= before.meshes[m].triangles[g].first_index_offset)
					group = g;
			auto const &material = before.materials.at(size_t(before.meshes[m].triangles.at(group).material_index));
			if (material.diffuse_textures.empty())
				continue;
			for (auto const &ab : {std::pair{0.1f, 0.1f}, {0.8f, 0.1f}, {0.1f, 0.8f}, {0.4f, 0.3f}, {0.3f, 0.4f}, {0.25f, 0.25f}, {0.6f, 0.2f}, {0.2f, 0.6f}})
			{
				Seen const old = seen_at(before, m, face, ab.first, ab.second);
				if (!clear_of_seams(old.uv, margin))
					continue;
				Seen const now = seen_at(after, m, face, ab.first, ab.second);
				++looked;
				bool close = true;
				for (int c = 0; c < 4; ++c)
					close = close && std::abs(int(old.colour.i[c]) - int(now.colour.i[c])) <= tolerance;
				BOOST_REQUIRE_MESSAGE(close, "mesh " << m << " face " << face << " at " << ab.first << "," << ab.second << ": was " << int(old.colour.b) << "," << int(old.colour.g) << ","
					<< int(old.colour.r) << " now " << int(now.colour.b) << "," << int(now.colour.g) << "," << int(now.colour.r) << " (uv " << old.uv.x << "," << old.uv.y << " -> " << now.uv.x << "," << now.uv.y << ")");
			}
		}
	}
	return looked;
}

/// Every corner of every face still has the position and normal it had, and the groups still say what they hold.
void same_surface(SerializedModel const &before, SerializedModel const &after)
{
	for (size_t m = 0; m < before.meshes.size(); ++m)
	{
		auto const &a = before.meshes[m], &b = after.meshes[m];
		BOOST_REQUIRE_EQUAL(a.indices.size(), b.indices.size());
		for (size_t i = 0; i < a.indices.size(); ++i)
		{
			auto const &va = a.vertices.at(a.indices[i]), &vb = b.vertices.at(b.indices[i]);
			BOOST_REQUIRE_MESSAGE(va.position.x == vb.position.x && va.position.y == vb.position.y && va.position.z == vb.position.z && va.normal.y == vb.normal.y,
				"mesh " << m << " index " << i << " has another position or normal");
		}
		BOOST_REQUIRE_EQUAL(a.triangles.size(), b.triangles.size());
		for (size_t g = 0; g < a.triangles.size(); ++g)
		{
			auto const &ga = a.triangles[g], &gb = b.triangles[g];
			BOOST_TEST(ga.material_index == gb.material_index);
			BOOST_TEST(ga.first_index_offset == gb.first_index_offset);
			BOOST_TEST(ga.triangles_count == gb.triangles_count);
			size_t lowest = b.vertices.size(), highest = 0;
			for (size_t i = gb.first_index_offset; i < gb.first_index_offset + 3 * gb.triangles_count; ++i)
			{
				lowest = std::min<size_t>(lowest, b.indices[i]);
				highest = std::max<size_t>(highest, b.indices[i]);
			}
			BOOST_TEST(gb.minimal_vertex_index == lowest);
			BOOST_TEST(gb.vertices_count == highest - lowest + 1);
		}
	}
}

/// The area the faces of @p material take in the atlas, in texels.
double atlas_area(SerializedModel const &model, std::int64_t material)
{
	double area = 0;
	for (auto const &mesh : model.meshes)
		for (auto const &g : mesh.triangles)
		{
			if (g.material_index != material)
				continue;
			auto const &texture = model.textures.at(size_t(model.materials.at(size_t(material)).diffuse_textures.at(0)));
			for (size_t t = 0; t < g.triangles_count; ++t)
			{
				auto const &p0 = mesh.vertices.at(mesh.indices.at(g.first_index_offset + 3 * t)).texcoords;
				auto const &p1 = mesh.vertices.at(mesh.indices.at(g.first_index_offset + 3 * t + 1)).texcoords;
				auto const &p2 = mesh.vertices.at(mesh.indices.at(g.first_index_offset + 3 * t + 2)).texcoords;
				area += 0.5 * std::abs(double((p1.x - p0.x) * (p2.y - p0.y) - (p2.x - p0.x) * (p1.y - p0.y))) * texture.width * texture.height;
			}
		}
	return area;
}

}

BOOST_AUTO_TEST_SUITE(model_atlas_xatlas)

BOOST_AUTO_TEST_CASE(surfaces_look_as_they_did_with_the_textures_of_all_materials_in_one_set)
{
	SerializedModel const before = model_of_quads({quarters(32, 32, RED, GREEN, BLUE, YELLOW), quarters(64, 64, WHITE, GREY, CYAN, MAGENTA), flat(16, RED)},
		{material(0), material(1), material(2)}, {{0, 0.0f}, {1, 2.0f}, {2, 4.0f}});
	SerializedModel model = before;
	BOOST_TEST(edge::atlas_materials(model, xatlas_options()) == 3u);
	BOOST_REQUIRE_EQUAL(model.textures.size(), 1u);
	BOOST_TEST(model.materials.at(0).diffuse_textures.at(0) == model.materials.at(1).diffuse_textures.at(0));
	BOOST_TEST(same_appearance(before, model) > 20u);
	same_surface(before, model);
}

BOOST_AUTO_TEST_CASE(a_flat_surface_keeps_its_vertices_and_the_new_coordinates_are_in_the_atlas)
{
	SerializedModel model = model_of_quads({quarters(32, 32, RED, GREEN, BLUE, YELLOW), quarters(32, 32, WHITE, GREY, CYAN, MAGENTA)}, {material(0), material(1)}, {{0, 0.0f}, {1, 2.0f}});
	edge::atlas_materials(model, xatlas_options());
	BOOST_TEST(model.meshes.at(0).vertices.size() == 8u);
	for (auto const &v : model.meshes.at(0).vertices)
	{
		BOOST_TEST(v.texcoords.x >= 0.0f);
		BOOST_TEST(v.texcoords.x <= 1.0f);
		BOOST_TEST(v.texcoords.y >= 0.0f);
		BOOST_TEST(v.texcoords.y <= 1.0f);
	}
}

BOOST_AUTO_TEST_CASE(a_texture_that_tiles_is_drawn_into_the_atlas_as_it_shows_on_the_surface_with_no_more_texels_than_it_has)
{
	SerializedModel const before = model_of_quads({quarters(32, 32, RED, GREEN, BLUE, YELLOW), quarters(32, 32, WHITE, GREY, CYAN, MAGENTA)}, {material(0), material(1)}, {{0, 0.0f, 1.0f, 2.0f}, {1, 3.0f}});
	SerializedModel model = before;
	BOOST_TEST(edge::atlas_materials(model, xatlas_options()) == 2u);
	// 2 x 2 repeats of 32 x 32 texels on the surface, in the atlas with the texels of one
	BOOST_TEST(atlas_area(model, 0) < 1.3 * 32 * 32);
	BOOST_TEST(atlas_area(model, 0) > 0.5 * 32 * 32);
	BOOST_TEST(same_appearance(before, model, 0.14f) > 4u);
}

BOOST_AUTO_TEST_CASE(a_material_has_the_density_of_its_texture_so_a_larger_texture_has_a_larger_share)
{
	// two surfaces of the same size, one with a texture of 16 times the texels
	SerializedModel model = model_of_quads({quarters(64, 64, RED, GREEN, BLUE, YELLOW), quarters(16, 16, WHITE, GREY, CYAN, MAGENTA)}, {material(0), material(1)}, {{0, 0.0f}, {1, 2.0f}});
	edge::atlas_materials(model, xatlas_options());
	double const ratio = atlas_area(model, 0) / atlas_area(model, 1);
	BOOST_TEST(ratio > 16.0 * 0.7);
	BOOST_TEST(ratio < 16.0 * 1.4);
}

BOOST_AUTO_TEST_CASE(the_density_holds_when_everything_is_made_smaller_to_fit)
{
	std::vector<SerializedTexture> textures = {quarters(256, 256, RED, GREEN, BLUE, YELLOW), quarters(64, 64, WHITE, GREY, CYAN, MAGENTA), quarters(256, 256, CYAN, RED, GREEN, WHITE)};
	SerializedModel const before = model_of_quads(textures, {material(0), material(1), material(2)}, {{0, 0.0f}, {1, 2.0f}, {2, 4.0f}});
	SerializedModel model = before;
	edge::atlas_materials(model, xatlas_options(128));
	BOOST_TEST(model.textures.at(0).width <= 128);
	BOOST_TEST(model.textures.at(0).height <= 128);
	double const ratio = atlas_area(model, 0) / atlas_area(model, 1);
	BOOST_TEST(ratio > 16.0 * 0.7);
	BOOST_TEST(ratio < 16.0 * 1.4);
	BOOST_TEST(same_appearance(before, model, 0.12f) > 8u);
}

BOOST_AUTO_TEST_CASE(the_atlas_is_no_larger_than_asked_and_no_larger_than_the_surfaces_need)
{
	SerializedModel model = model_of_quads({flat(16, RED), flat(16, GREEN)}, {material(0), material(1)}, {{0, 0.0f}, {1, 2.0f}});
	edge::atlas_materials(model, xatlas_options(2048));
	// two surfaces of 16 x 16 texels: far below 2048
	BOOST_TEST(model.textures.at(0).width <= 128);
	BOOST_TEST(model.textures.at(0).height <= 128);
	BOOST_TEST(model.textures.at(0).width % 4 == 0);
	BOOST_TEST(model.textures.at(0).height % 4 == 0);
}

BOOST_AUTO_TEST_CASE(each_role_has_its_atlas_drawn_with_the_same_places)
{
	// the normal map of the first material is a picture of its own with quarters: it is seen at the places the colour is
	bgra8 const UPR(255, 128, 200, 255), UPL(255, 128, 56, 255), UPD(255, 200, 128, 255), UPU(255, 56, 128, 255);
	SerializedModel model = model_of_quads({quarters(32, 32, RED, GREEN, BLUE, YELLOW), quarters(32, 32, UPR, UPL, UPD, UPU), flat(16, WHITE)}, {material(0, 1), material(2)},
		{{0, 0.0f}, {1, 2.0f}});
	SerializedModel const before = model;
	BOOST_TEST(edge::atlas_materials(model, xatlas_options()) == 2u);
	BOOST_TEST(model.textures.size() == 2u);
	BOOST_REQUIRE(model.materials.at(0).normal_map >= 0);
	BOOST_TEST(model.materials.at(1).normal_map == -1);
	auto const &mesh = model.meshes.at(0);
	// a point inside one quarter of the first surface: its colour and its normal are the quarter's, from pictures that differ
	unsigned const i0 = mesh.indices[0], i1 = mesh.indices[1], i2 = mesh.indices[2];
	float const a = 0.2f, b = 0.2f, c = 0.6f;       // uv (0.2, 0.6) on the surface: the bottom left quarter
	rove::vec<2> const uv(a * mesh.vertices[i0].texcoords.x + b * mesh.vertices[i1].texcoords.x + c * mesh.vertices[i2].texcoords.x,
		a * mesh.vertices[i0].texcoords.y + b * mesh.vertices[i1].texcoords.y + c * mesh.vertices[i2].texcoords.y);
	auto const &was = before.meshes[0];
	unsigned const o0 = was.indices[0], o1 = was.indices[1], o2 = was.indices[2];
	rove::vec<2> const old(a * was.vertices[o0].texcoords.x + b * was.vertices[o1].texcoords.x + c * was.vertices[o2].texcoords.x,
		a * was.vertices[o0].texcoords.y + b * was.vertices[o1].texcoords.y + c * was.vertices[o2].texcoords.y);
	bgra8 const colour = sample(model.textures.at(size_t(model.materials.at(0).diffuse_textures.at(0))), uv);
	bgra8 const normal = sample(model.textures.at(size_t(model.materials.at(0).normal_map)), uv);
	bgra8 const want_colour = sample(before.textures.at(0), old), want_normal = sample(before.textures.at(1), old);
	for (int k = 0; k < 3; ++k)
	{
		BOOST_TEST(std::abs(int(colour.i[k]) - int(want_colour.i[k])) <= 40);
		BOOST_TEST(std::abs(int(normal.i[k]) - int(want_normal.i[k])) <= 40);
	}
}

BOOST_AUTO_TEST_CASE(a_picture_drawn_smaller_is_filtered_not_picked_from)
{
	// black and white texels by turns, made much smaller: every texel of the atlas that shows it is the grey of the mean, none is black or white
	SerializedTexture checker;
	checker.filename = "checker";
	checker.width = checker.height = 128;
	checker.pixel_format = PixelFormat::A8R8G8B8;
	checker.pixels.resize(128 * 128 * 4);
	for (int y = 0; y < 128; ++y)
		for (int x = 0; x < 128; ++x)
		{
			bgra8 const c = (x + y) & 1 ? WHITE : bgra8(0, 0, 0, 255);
			std::memcpy(&checker.pixels[(size_t(y) * 128 + x) * 4], &c, 4);
		}
	SerializedModel model = model_of_quads({checker, flat(16, RED)}, {material(0), material(1)}, {{0, 0.0f}, {1, 2.0f}});
	edge::atlas_materials(model, xatlas_options(64));
	auto const &atlas = model.textures.at(0);
	auto const &mesh = model.meshes.at(0);
	BOOST_TEST(atlas.width <= 64);
	// the middle of each face of the first surface, and a few points round it
	size_t looked = 0;
	for (size_t face = 0; face < 2; ++face)
		for (auto const &ab : {std::pair{0.34f, 0.33f}, {0.2f, 0.5f}, {0.5f, 0.2f}, {0.3f, 0.3f}})
		{
			auto const &v0 = mesh.vertices.at(mesh.indices.at(3 * face)).texcoords, &v1 = mesh.vertices.at(mesh.indices.at(3 * face + 1)).texcoords,
				&v2 = mesh.vertices.at(mesh.indices.at(3 * face + 2)).texcoords;
			float const c = 1.0f - ab.first - ab.second;
			bgra8 const t = sample(atlas, {ab.first * v0.x + ab.second * v1.x + c * v2.x, ab.first * v0.y + ab.second * v1.y + c * v2.y});
			BOOST_REQUIRE_MESSAGE(t.r > 70 && t.r < 185, "face " << face << " has " << int(t.r) << ": picked, not averaged");
			++looked;
		}
	BOOST_TEST(looked == 8u);
}

BOOST_AUTO_TEST_CASE(the_edge_of_a_chart_is_drawn_out_past_it_so_filtering_and_mips_find_no_hole)
{
	SerializedModel model = model_of_quads({flat(32, RED), flat(32, GREEN)}, {material(0), material(1)}, {{0, 0.0f}, {1, 2.0f}});
	edge::atlas_materials(model, xatlas_options());
	auto const &atlas = model.textures.at(0);
	auto const &mesh = model.meshes.at(0);
	size_t looked = 0;
	for (size_t face = 0; face < mesh.indices.size() / 3; ++face)
	{
		rove::vec<2> p[3];
		for (int c = 0; c < 3; ++c)
		{
			p[c] = mesh.vertices.at(mesh.indices.at(3 * face + size_t(c))).texcoords;
			p[c] = {p[c].x * float(atlas.width), p[c].y * float(atlas.height)};
		}
		for (int e = 0; e < 3; ++e)
		{
			rove::vec<2> const a = p[e], b = p[(e + 1) % 3], other = p[(e + 2) % 3];
			rove::vec<2> const mid((a.x + b.x) / 2, (a.y + b.y) / 2);
			rove::vec<2> along(b.x - a.x, b.y - a.y);
			float const length = std::sqrt(along.x * along.x + along.y * along.y);
			rove::vec<2> out(-along.y / length, along.x / length);
			if (out.x * (other.x - mid.x) + out.y * (other.y - mid.y) > 0)
				out = {-out.x, -out.y};
			// a texel past the edge by one or two texels
			for (float distance : {1.5f, 2.5f})
			{
				float const x = mid.x + out.x * distance, y = mid.y + out.y * distance;
				if (x < 0 || y < 0 || x >= float(atlas.width) || y >= float(atlas.height))
					continue;
				++looked;
				BOOST_REQUIRE_MESSAGE(sample(atlas, {x / float(atlas.width), y / float(atlas.height)}).a == 255, "face " << face << " edge " << e << " at " << distance << " texels out is empty");
			}
		}
	}
	BOOST_TEST(looked > 6u);
}

BOOST_AUTO_TEST_CASE(vertices_that_carry_more_than_coordinates_keep_everything_they_carry)
{
	// a custom attribute and a morph target that are a function of position: every vertex of the new mesh has the value of its position
	SerializedModel model = model_of_quads({quarters(32, 32, RED, GREEN, BLUE, YELLOW), quarters(32, 32, WHITE, GREY, CYAN, MAGENTA)}, {material(0), material(1)}, {{0, 0.0f}, {1, 2.0f}});
	auto &mesh = model.meshes.at(0);
	edge::SerializedCustomAttribute attribute;
	attribute.name = "_tag";
	attribute.components = 2;
	std::vector<edge::GenericVertex> deltas;
	for (auto &v : mesh.vertices)
	{
		attribute.values.push_back(v.position.x * 3 + v.position.z);
		attribute.values.push_back(7.0f);
		edge::GenericVertex d{};
		d.position = {v.position.x * 2, 0.0f, v.position.z * 2};
		d.texcoords = {0.0f, 0.0f};
		deltas.push_back(d);
	}
	mesh.custom_attributes.push_back(attribute);
	mesh.morph_target_deltas.push_back(deltas);
	mesh.morph_weights.push_back(0.5f);
	edge::atlas_materials(model, xatlas_options());
	auto const &made = model.meshes.at(0);
	BOOST_REQUIRE_EQUAL(made.custom_attributes.at(0).values.size(), made.vertices.size() * 2);
	BOOST_REQUIRE_EQUAL(made.morph_target_deltas.at(0).size(), made.vertices.size());
	for (size_t v = 0; v < made.vertices.size(); ++v)
	{
		BOOST_REQUIRE_EQUAL(made.custom_attributes.at(0).values[2 * v], made.vertices[v].position.x * 3 + made.vertices[v].position.z);
		BOOST_REQUIRE_EQUAL(made.custom_attributes.at(0).values[2 * v + 1], 7.0f);
		BOOST_REQUIRE_EQUAL(made.morph_target_deltas.at(0)[v].position.x, made.vertices[v].position.x * 2);
	}
	BOOST_TEST(made.morph_weights.size() == 1u);
}

BOOST_AUTO_TEST_CASE(a_material_that_is_not_packed_and_the_meshes_that_have_none_are_left_as_they_are)
{
	// the third material has no texture: its surface keeps its vertices and coordinates; a second mesh has only that
	auto mesh0 = quads_mesh({{0, 0.0f}, {1, 2.0f}, {2, 4.0f}});
	auto mesh1 = quads_mesh({{2, 6.0f}});
	SerializedModel before = model_of({quarters(32, 32, RED, GREEN, BLUE, YELLOW), quarters(32, 32, WHITE, GREY, CYAN, MAGENTA)}, {material(0), material(1), material(-1)}, {mesh0, mesh1});
	SerializedModel model = before;
	BOOST_TEST(edge::atlas_materials(model, xatlas_options()) == 2u);
	auto const &a = model.meshes.at(0), &b = model.meshes.at(1);
	BOOST_TEST(b.vertices.size() == 4u);
	for (size_t i = 0; i < 4; ++i)
		BOOST_TEST(b.vertices.at(i).texcoords.x == before.meshes.at(1).vertices.at(i).texcoords.x);
	// the group of the third material in the first mesh has its own vertices, still where its indices say
	auto const &group = a.triangles.at(2);
	for (size_t i = group.first_index_offset; i < group.first_index_offset + 6; ++i)
	{
		BOOST_REQUIRE(a.indices[i] < a.vertices.size());
		BOOST_TEST(a.vertices[a.indices[i]].position.x >= 4.0f);
		BOOST_TEST(a.vertices[a.indices[i]].position.x <= 5.0f);
	}
	same_surface(before, model);
}

BOOST_AUTO_TEST_CASE(a_cube_comes_out_with_its_sides_looking_as_they_did)
{
	// six sides of different pictures, each a surface with its own vertices (hard edges): they stay six surfaces with their pictures
	SerializedTexture pictures[6] = {quarters(32, 32, RED, GREEN, BLUE, YELLOW), quarters(32, 32, WHITE, GREY, CYAN, MAGENTA), quarters(32, 32, GREEN, RED, YELLOW, BLUE),
		quarters(32, 32, GREY, WHITE, MAGENTA, CYAN), quarters(32, 32, BLUE, YELLOW, RED, GREEN), quarters(32, 32, CYAN, MAGENTA, WHITE, GREY)};
	edge::SerializedMesh mesh;
	mesh.serialization_type = "static_mesh:test";
	rove::vec<3> const normals[6] = {{0, 0, 1}, {0, 0, -1}, {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}};
	rove::vec<3> const us[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 0, -1}, {0, 0, 1}, {1, 0, 0}, {1, 0, 0}};
	rove::vec<3> const vs[6] = {{0, 1, 0}, {0, 1, 0}, {0, 1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}};
	for (int side = 0; side < 6; ++side)
	{
		edge::SerializedTriangles group;
		group.material_index = side;
		group.minimal_vertex_index = mesh.vertices.size();
		group.vertices_count = 4;
		group.first_index_offset = mesh.indices.size();
		group.triangles_count = 2;
		for (int c = 0; c < 4; ++c)
		{
			float const u = float(c & 1), w = float(c >> 1);
			edge::GenericVertex v{};
			v.position = {normals[side].x * 0.5f + (u - 0.5f) * us[side].x + (w - 0.5f) * vs[side].x, normals[side].y * 0.5f + (u - 0.5f) * us[side].y + (w - 0.5f) * vs[side].y,
				normals[side].z * 0.5f + (u - 0.5f) * us[side].z + (w - 0.5f) * vs[side].z};
			v.normal = normals[side];
			v.texcoords = {u, 1.0f - w};
			mesh.vertices.push_back(v);
		}
		unsigned const base = unsigned(group.minimal_vertex_index);
		for (unsigned i : {0u, 1u, 2u, 1u, 3u, 2u})
			mesh.indices.push_back(base + i);
		mesh.triangles.push_back(group);
	}
	std::vector<SerializedTexture> textures(pictures, pictures + 6);
	std::vector<edge::SerializedMaterial> materials;
	for (int i = 0; i < 6; ++i)
		materials.push_back(material(i));
	SerializedModel const before = model_of(textures, materials, {mesh});
	SerializedModel model = before;
	BOOST_TEST(edge::atlas_materials(model, xatlas_options(512)) == 6u);
	BOOST_TEST(model.meshes.at(0).vertices.size() == 24u);
	BOOST_TEST(same_appearance(before, model) > 40u);
	same_surface(before, model);
}

BOOST_AUTO_TEST_CASE(the_same_model_gives_the_same_atlas_and_coordinates)
{
	auto make = [] {
		return model_of_quads({quarters(32, 32, RED, GREEN, BLUE, YELLOW), quarters(64, 64, WHITE, GREY, CYAN, MAGENTA), flat(16, RED)}, {material(0), material(1), material(2)},
			{{0, 0.0f}, {1, 2.0f, 2.0f}, {2, 5.0f}});
	};
	SerializedModel a = make(), b = make();
	edge::atlas_materials(a, xatlas_options());
	edge::atlas_materials(b, xatlas_options());
	BOOST_REQUIRE_EQUAL(a.textures.size(), b.textures.size());
	BOOST_CHECK(a.textures.at(0).pixels == b.textures.at(0).pixels);
	BOOST_REQUIRE_EQUAL(a.meshes.at(0).vertices.size(), b.meshes.at(0).vertices.size());
	for (size_t i = 0; i < a.meshes.at(0).vertices.size(); ++i)
		BOOST_REQUIRE(a.meshes.at(0).vertices[i].texcoords.x == b.meshes.at(0).vertices[i].texcoords.x && a.meshes.at(0).vertices[i].texcoords.y == b.meshes.at(0).vertices[i].texcoords.y);
}

BOOST_AUTO_TEST_CASE(a_model_that_cannot_be_packed_is_refused_and_left_as_it_was)
{
	SerializedModel morph = model_of_quads({flat(16, RED), flat(16, GREEN)}, {material(0), material(1)}, {{0, 0.0f}, {1, 2.0f}});
	morph.meshes.at(0).morph_target_deltas.push_back(std::vector<edge::GenericVertex>(8, edge::GenericVertex{}));
	morph.meshes.at(0).morph_target_deltas.at(0).at(1).texcoords = {0.1f, 0.0f};
	SerializedModel const before = morph;
	BOOST_CHECK_THROW(edge::atlas_materials(morph, xatlas_options()), std::runtime_error);
	BOOST_CHECK(morph.textures.size() == 2u && morph.meshes.at(0).vertices.size() == before.meshes.at(0).vertices.size());
	BOOST_TEST(morph.meshes.at(0).vertices.at(1).texcoords.x == before.meshes.at(0).vertices.at(1).texcoords.x);

	SerializedModel lod = model_of_quads({flat(16, RED), flat(16, GREEN)}, {material(0), material(1)}, {{0, 0.0f}, {1, 2.0f}});
	lod.meshes.at(0).lods.push_back({});
	BOOST_CHECK_THROW(edge::atlas_materials(lod, xatlas_options()), std::runtime_error);

	// no room: too little for the surfaces even made as small as they can be
	std::vector<SerializedTexture> textures;
	std::vector<edge::SerializedMaterial> materials;
	std::vector<Quad> quads;
	for (int i = 0; i < 40; ++i)
	{
		textures.push_back(flat(16, bgra8(std::uint8_t(i), 0, 0, 255)));
		materials.push_back(material(i));
		quads.push_back({i, float(i) * 2});
	}
	SerializedModel crowded = model_of_quads(textures, materials, quads);
	SerializedModel const crowded_before = crowded;
	BOOST_CHECK_THROW(edge::atlas_materials(crowded, xatlas_options(8)), std::runtime_error);
	BOOST_CHECK(crowded.textures.size() == 40u && crowded.meshes.at(0).vertices.size() == crowded_before.meshes.at(0).vertices.size());
}

BOOST_AUTO_TEST_CASE(materials_with_the_same_textures_on_the_same_surface_are_one_set_and_a_single_material_has_nothing_to_pack)
{
	SerializedModel one = model_of_quads({quarters(32, 32, RED, GREEN, BLUE, YELLOW)}, {material(0), material(0)}, {{0, 0.0f}, {1, 2.0f}});
	BOOST_TEST(edge::atlas_materials(one, xatlas_options()) == 0u);
	BOOST_TEST(one.meshes.at(0).vertices.size() == 8u);
	BOOST_TEST(one.textures.size() == 1u);
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(model_atlas_keep)

BOOST_AUTO_TEST_CASE(a_material_named_to_be_kept_keeps_its_textures_and_coordinates_in_both_modes)
{
	for (auto mode : {edge::AtlasOptions::Mode::Grid, edge::AtlasOptions::Mode::Xatlas})
	{
		SerializedModel before = model_of_quads({quarters(32, 32, RED, GREEN, BLUE, YELLOW), quarters(32, 32, WHITE, GREY, CYAN, MAGENTA), flat(32, RED)},
			{material(0), material(1), material(2)}, {{0, 0.0f}, {1, 2.0f}, {2, 4.0f}});
		before.materials.at(0).name = "sail";
		before.materials.at(1).name = "hull";
		before.materials.at(2).name = "deck";
		SerializedModel model = before;
		edge::AtlasOptions options;
		options.mode = mode;
		options.keep = {"sail"};
		BOOST_TEST(edge::atlas_materials(model, options) == 2u);
		// the sail has its own texture, at the same index in the table it was at or a place on, and its vertices as they were
		auto const &sail = model.materials.at(0);
		BOOST_REQUIRE(sail.diffuse_textures.at(0) >= 0 && size_t(sail.diffuse_textures.at(0)) < model.textures.size());
		BOOST_TEST(model.textures.at(size_t(sail.diffuse_textures.at(0))).filename == before.textures.at(0).filename);
		BOOST_CHECK(model.textures.at(size_t(sail.diffuse_textures.at(0))).pixels == before.textures.at(0).pixels);
		BOOST_TEST(model.materials.at(1).diffuse_textures.at(0) == model.materials.at(2).diffuse_textures.at(0));
		auto const &a = model.meshes.at(0), &b = before.meshes.at(0);
		for (unsigned i = 0; i < 4; ++i)
		{
			unsigned const now = a.indices.at(i), was = b.indices.at(i);
			BOOST_TEST(a.vertices.at(now).texcoords.x == b.vertices.at(was).texcoords.x);
			BOOST_TEST(a.vertices.at(now).texcoords.y == b.vertices.at(was).texcoords.y);
		}
	}
}

BOOST_AUTO_TEST_CASE(a_name_that_no_material_has_is_an_error_so_a_renamed_material_is_noticed)
{
	SerializedModel model = model_of_quads({flat(16, RED), flat(16, GREEN)}, {material(0), material(1)}, {{0, 0.0f}, {1, 2.0f}});
	SerializedModel const before = model;
	edge::AtlasOptions options;
	options.keep = {"sail"};
	BOOST_CHECK_THROW(edge::atlas_materials(model, options), std::runtime_error);
	BOOST_CHECK(model.textures.size() == before.textures.size());
}

BOOST_AUTO_TEST_SUITE_END()
