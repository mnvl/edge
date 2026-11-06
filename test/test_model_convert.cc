// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

// Contract of the glTF to .edge converter (edge_convert is a thin command line over convert_gltf_to_edge): a skinned glTF becomes
// an AnimatedModelPTBNJWT file and any other a StaticModelPTBNT one, made with the options it was given, and a conversion that
// fails leaves no output behind -- a half-written file would look up to date to a build that goes by file times.

#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include <boost/test/unit_test.hpp>

#include "animated_mesh.h"
#include "animated_model.h"
#include "gltf_loader.h"
#include "model_convert.h"
#include "null_renderer.h"
#include "pipeline.h"
#include "pixel.h"
#include "serialized_model.h"
#include "scheduler.h"
#include "static_model.h"
#include "test_fixture.h"
#include "texture.h"
#include "image.h"
#include "test_bc7.h"

namespace
{

std::string asset(std::string const &name)
{
	return edge::TestFixture::graphic_sources_path() + "test/data/" + name;
}

struct Fixture
{
	edge::RendererPtr renderer = [] { edge::RendererPtr r(new edge::NullRenderer(320, 200)); return r; }();
	edge::SchedulerPtr scheduler = std::make_shared<edge::Scheduler>(renderer);
	std::filesystem::path const dir = edge::unique_temp_directory("edge_model_convert_");


	~Fixture()
	{
		std::error_code ignored;
		std::filesystem::remove_all(dir, ignored);
	}

	std::string out(std::string const &name) const { return (dir / name).string(); }
};

}

BOOST_FIXTURE_TEST_SUITE(model_convert, Fixture)

BOOST_AUTO_TEST_CASE(a_glTF_with_a_skin_is_skinned_and_one_without_is_not)
{
	BOOST_CHECK(edge::gltf_contents(asset("test_figure.glb")).skin);
	BOOST_CHECK(!edge::gltf_contents(asset("test_gltf_loder_plane.glb")).skin);
}

BOOST_AUTO_TEST_CASE(asking_whether_a_missing_file_has_a_skin_is_an_error)
{
	BOOST_CHECK_THROW(edge::gltf_contents(asset("no_such.glb")).skin, edge::GltfLoader::load_error);
}

/// A .gltf in the fixture's directory naming @p buffer_uri and @p image_uris (an empty one: none), with nothing else in it.
std::filesystem::path write_gltf(std::filesystem::path const &dir, std::string const &buffer_uri, std::vector<std::string> const &image_uris)
{
	std::string images;
	for (std::string const &uri : image_uris)
		images += std::string(images.empty() ? "" : ",") + "{\"uri\":\"" + uri + "\"}";
	std::filesystem::path const gltf = dir / "model.gltf";
	std::ofstream(gltf) << "{\"asset\":{\"version\":\"2.0\"}"
		<< (buffer_uri.empty() ? "" : ",\"buffers\":[{\"byteLength\":4,\"uri\":\"" + buffer_uri + "\"}]")
		<< (images.empty() ? "" : ",\"images\":[" + images + "]") << "}";
	return gltf;
}

void touch(std::filesystem::path const &file)
{
	std::filesystem::create_directories(file.parent_path());
	std::ofstream(file) << "data";
}

BOOST_AUTO_TEST_CASE(a_glb_that_holds_everything_is_made_from_itself_alone)
{
	std::vector<std::filesystem::path> const files = edge::gltf_contents(asset("test_figure.glb")).files;
	BOOST_REQUIRE_EQUAL(files.size(), 1u);
	BOOST_CHECK_EQUAL(files[0], std::filesystem::absolute(asset("test_figure.glb")).lexically_normal());
}

BOOST_AUTO_TEST_CASE(a_gltf_is_made_from_itself_and_the_files_it_names_each_once_whatever_way_it_names_them)
{
	touch(dir / "mesh.bin");
	touch(dir / "a.png");
	touch(dir / "sub dir" / "b.png");
	std::filesystem::path const gltf = write_gltf(dir, "mesh.bin", {"a.png", "sub%20dir/b.png", "./a.png", "sub dir/../a.png",
		"data:image/png;base64,iVBORw0KGgo="});
	std::vector<std::filesystem::path> const files = edge::gltf_contents(gltf.string()).files;
	std::vector<std::filesystem::path> const expected = {gltf, dir / "mesh.bin", dir / "a.png", dir / "sub dir" / "b.png"};
	BOOST_CHECK_EQUAL_COLLECTIONS(files.begin(), files.end(), expected.begin(), expected.end());
}

BOOST_AUTO_TEST_CASE(a_gltf_naming_a_file_that_is_not_there_is_an_error_that_names_it)
{
	touch(dir / "mesh.bin");
	std::filesystem::path const gltf = write_gltf(dir, "mesh.bin", {"gone.png"});
	try
	{
		edge::gltf_contents(gltf.string());
		BOOST_FAIL("a missing image went unnoticed");
	}
	catch (edge::GltfLoader::load_error const &e)
	{
		BOOST_CHECK_NE(std::string(e.what()).find("gone.png"), std::string::npos);
	}
	BOOST_CHECK_THROW(edge::gltf_contents(write_gltf(dir, "nothing.bin", {}).string()), edge::GltfLoader::load_error);
}

BOOST_AUTO_TEST_CASE(the_files_of_a_test_model_with_an_external_texture_are_it_and_its_texture)
{
	std::vector<std::filesystem::path> const files = edge::gltf_contents(asset("test_uv_set_1.gltf")).files;
	std::vector<std::filesystem::path> const expected = {std::filesystem::absolute(asset("test_uv_set_1.gltf")).lexically_normal(),
		std::filesystem::absolute(asset("test_white.png")).lexically_normal()};
	BOOST_CHECK_EQUAL_COLLECTIONS(files.begin(), files.end(), expected.begin(), expected.end());
}

BOOST_AUTO_TEST_CASE(a_skinned_glTF_becomes_an_animated_model_file_with_its_clips_and_pose_box)
{
	edge::convert_gltf_to_edge(asset("test_figure.glb"), out("figure.edge"), edge::GltfOptions());
	auto model = std::make_shared<edge::AnimatedModelPTBNJWT>(renderer, scheduler);
	model->load_from_file(out("figure.edge"));
	BOOST_CHECK_EQUAL(model->animations().size(), 3u);
	BOOST_REQUIRE(!model->meshes().empty());
	BOOST_CHECK_GT(static_cast<edge::AnimatedMeshPTBNJWT const &>(*model->meshes()[0]).pose_bounds().hi.x, 0.0f);
}

BOOST_AUTO_TEST_CASE(a_glTF_without_a_skin_becomes_a_static_model_file)
{
	edge::convert_gltf_to_edge(asset("test_gltf_loder_plane.glb"), out("plane.edge"), edge::GltfOptions());
	auto model = std::make_shared<edge::StaticModelPTBNT>(renderer, scheduler);
	model->load_from_file(out("plane.edge"));
	BOOST_CHECK(!model->meshes().empty());
}

BOOST_AUTO_TEST_CASE(a_file_is_made_for_the_kind_of_model_its_glTF_is_so_the_other_kind_refuses_it)
{
	edge::convert_gltf_to_edge(asset("test_figure.glb"), out("figure.edge"), edge::GltfOptions());
	auto wrong = std::make_shared<edge::StaticModelPTBNT>(renderer, scheduler);
	BOOST_CHECK_THROW(wrong->load_from_file(out("figure.edge")), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(the_options_decide_how_the_textures_are_made)
{
	edge::GltfOptions game;
	game.compress_textures = false;
	game.srgb_color = true;
	game.mipmaps = true;
	game.keep_cpu_copy = false;         // the file keeps the texels all the same: they are what it is made of
	edge::convert_gltf_to_edge(asset("test_gltf_loader_pbr.glb"), out("pbr.edge"), game);
	auto model = std::make_shared<edge::StaticModelPTBNT>(renderer, scheduler);
	model->load_from_file(out("pbr.edge"));
	// colour is sRGB, data (normals, metallic-roughness) linear, and every one has its mip chain: each texture, not some
	size_t colour = 0, data = 0;
	auto check = [](edge::TexturePtr const &t, bool srgb, std::string const &what) {
		BOOST_CHECK_MESSAGE(t->is_srgb() == srgb, what << (srgb ? " is not sRGB" : " is sRGB"));
		BOOST_CHECK_MESSAGE(t->usage().mipmaps && t->mip_levels() > 1, what << " has no mip chain");
	};
	for (auto const &m : model->materials())
	{
		for (size_t i = 0; i < m->diffuse_textures_count(); ++i)
			if (auto const t = m->diffuse_texture(i))
				check(t, true, m->name() + " diffuse"), ++colour;
		if (auto const t = m->emissive_texture())
			check(t, true, m->name() + " emissive"), ++colour;
		if (auto const t = m->normal_map_texture())
			check(t, false, m->name() + " normal map"), ++data;
		if (auto const t = m->metallic_roughness_texture())
			check(t, false, m->name() + " metallic-roughness"), ++data;
	}
	BOOST_CHECK_GT(colour, 0u);
	BOOST_CHECK_GT(data, 0u);
}

BOOST_AUTO_TEST_CASE(the_textures_are_BC7_with_their_whole_mip_chain_unless_asked_not_to_compress)
{
	edge::GltfOptions game;
	game.srgb_color = true;
	edge::convert_gltf_to_edge(asset("test_gltf_loader_pbr.glb"), out("pbr.edge"), game);
	auto model = std::make_shared<edge::StaticModelPTBNT>(renderer, scheduler);
	model->load_from_file(out("pbr.edge"));
	size_t colour = 0, data = 0;
	auto check = [](edge::TexturePtr const &t, bool srgb, std::string const &what) {
		BOOST_CHECK_MESSAGE(t->pixel_format() == edge::PixelFormat::BC7, what << " is not BC7");
		int full = 1;
		for (int side = std::max(t->width(), t->height()); side > 1; side /= 2)
			++full;
		BOOST_CHECK_MESSAGE(t->mip_levels() == full && full > 1, what << " has " << t->mip_levels() << " mip levels of " << full);
		BOOST_CHECK_MESSAGE(t->is_srgb() == srgb, what << (srgb ? " is not sRGB" : " is sRGB"));
	};
	for (auto const &m : model->materials())
	{
		for (size_t i = 0; i < m->diffuse_textures_count(); ++i)
			if (auto const t = m->diffuse_texture(i))
				check(t, true, m->name() + " diffuse"), ++colour;
		if (auto const t = m->normal_map_texture())
			check(t, false, m->name() + " normal map"), ++data;
		if (auto const t = m->metallic_roughness_texture())
			check(t, false, m->name() + " metallic-roughness"), ++data;
	}
	BOOST_CHECK_GT(colour, 0u);
	BOOST_CHECK_GT(data, 0u);
}

BOOST_AUTO_TEST_CASE(a_see_through_texture_keeps_its_alpha_in_its_blocks_with_or_without_srgb_colour)
{
	// a triangle whose base colour is an 8 x 8 picture clear on its left half, opaque on its right (a net's mesh, a cut-out leaf)
	edge::Image<rove::bgra<std::uint8_t>> picture(8, 8);
	for (int y = 0; y < 8; ++y)
		for (int x = 0; x < 8; ++x)
			picture(x, y) = rove::bgra<std::uint8_t>(200, 180, 160, std::uint8_t(x < 4 ? 0 : 255));
	edge::save_image<rove::bgra<std::uint8_t>>(dir / "clear.png", picture);
	float const vertices[] = {0, 0, 0, 1, 0, 0, 0, 1, 0,     0, 0, 1, 0, 0, 1, 0, 0, 1,     0, 0, 1, 0, 0, 1};      // positions, normals, UVs
	std::ofstream(dir / "clear.bin", std::ios::binary).write(reinterpret_cast<char const *>(vertices), sizeof vertices);
	std::ofstream(dir / "clear.gltf") << R"({"asset": {"version": "2.0"}, "scene": 0, "scenes": [{"nodes": [0]}], "nodes": [{"mesh": 0}],
		"meshes": [{"primitives": [{"attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2}, "material": 0}]}],
		"materials": [{"name": "clear", "alphaMode": "BLEND", "pbrMetallicRoughness": {"baseColorTexture": {"index": 0}}}],
		"textures": [{"source": 0}], "images": [{"uri": "clear.png"}],
		"buffers": [{"uri": "clear.bin", "byteLength": 96}],
		"bufferViews": [{"buffer": 0, "byteOffset": 0, "byteLength": 36}, {"buffer": 0, "byteOffset": 36, "byteLength": 36},
			{"buffer": 0, "byteOffset": 72, "byteLength": 24}],
		"accessors": [{"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3", "min": [0, 0, 0], "max": [1, 1, 0]},
			{"bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC3"},
			{"bufferView": 2, "componentType": 5126, "count": 3, "type": "VEC2"}]})";
	for (bool srgb : {false, true})
	{
		BOOST_TEST_CONTEXT("srgb_color " << srgb)
		{
			edge::GltfOptions options;          // (as edge_convert's: BC7, made here)
			options.srgb_color = srgb;
			edge::convert_gltf_to_edge((dir / "clear.gltf").string(), out("clear.edge"), options);
			edge::SerializedModel const model = edge::Model::read_file(out("clear.edge"));
			BOOST_REQUIRE_EQUAL(model.materials.size(), 1u);
			BOOST_REQUIRE_EQUAL(model.materials[0].diffuse_textures.size(), 1u);
			edge::SerializedTexture const &t = model.textures.at(std::size_t(model.materials[0].diffuse_textures[0]));
			BOOST_REQUIRE(t.pixel_format == edge::PixelFormat::BC7);
			auto const level0 = edge::decode_bc7(t.pixels.data(), 4 * 16, 8, 8);          // (the first level: 2 x 2 blocks)
			for (int y = 0; y < 8; ++y)
				for (int x = 0; x < 8; ++x)
					BOOST_TEST(std::abs(int(level0(x, y).a) - (x < 4 ? 0 : 255)) <= 8, "texel " << x << "," << y << ": alpha " << int(level0(x, y).a));
		}
	}
}

BOOST_AUTO_TEST_CASE(with_astc_the_textures_are_astc_of_their_use_s_block_with_their_whole_mip_chain)
{
	edge::GltfOptions game;
	game.srgb_color = true;
	edge::TextureOptions phone;
	phone.codec = edge::TextureCodec::ASTC;
	edge::convert_gltf_to_edge(asset("test_gltf_loader_pbr.glb"), out("pbr_astc.edge"), game, edge::LodOptions(), 0, phone);
	auto model = std::make_shared<edge::StaticModelPTBNT>(renderer, scheduler);
	model->load_from_file(out("pbr_astc.edge"));
	auto check = [](edge::TexturePtr const &t, std::set<edge::PixelFormat> const &blocks, std::string const &what) {
		BOOST_CHECK_MESSAGE(blocks.count(t->pixel_format()), what << " is of pixel format " << t->pixel_format());
		int full = 1;
		for (int side = std::max(t->width(), t->height()); side > 1; side /= 2)
			++full;
		BOOST_CHECK_MESSAGE(t->mip_levels() == full, what << " has " << t->mip_levels() << " mip levels of " << full);
	};
	edge::PixelFormat const astc4 = edge::PixelFormat::ASTC_4x4, astc6 = edge::PixelFormat::ASTC_6x6, astc8 = edge::PixelFormat::ASTC_8x8;
	size_t seen = 0;
	for (auto const &m : model->materials())
	{
		for (size_t i = 0; i < m->diffuse_textures_count(); ++i)
			if (auto const t = m->diffuse_texture(i))
				check(t, {astc6, astc8}, m->name() + " diffuse"), ++seen;
		if (auto const t = m->normal_map_texture())
			check(t, {astc4}, m->name() + " normal map"), ++seen;
		if (auto const t = m->metallic_roughness_texture())
			check(t, {astc8}, m->name() + " metallic-roughness"), ++seen;
	}
	BOOST_CHECK_GE(seen, 3u);
	// and only with the textures compressed
	edge::GltfOptions raw;
	raw.compress_textures = false;
	BOOST_CHECK_THROW(edge::convert_gltf_to_edge(asset("test_gltf_loader_pbr.glb"), out("raw_astc.edge"), raw, edge::LodOptions(), 0, phone),
		std::invalid_argument);
	BOOST_CHECK(!std::filesystem::exists(out("raw_astc.edge")));
}

BOOST_AUTO_TEST_CASE(a_converted_file_says_the_pipeline_and_the_options_that_made_it)
{
	std::string const hash = edge::pipeline_hash();
	BOOST_TEST(hash.size() == 16u);
	BOOST_TEST(hash.find_first_not_of("0123456789abcdef") == std::string::npos, hash);

	edge::GltfOptions plain;
	edge::convert_gltf_to_edge(asset("test_gltf_loader_pbr.glb"), out("plain.edge"), plain);
	edge::SerializedModel const a = edge::Model::read_file(out("plain.edge"));
	BOOST_TEST(a.pipeline == hash);
	BOOST_TEST(!a.options.empty());

	// every option it was made with shows: another of any is another description
	edge::GltfOptions srgb = plain;
	srgb.srgb_color = true;
	edge::LodOptions lods;
	lods.ratios = {0.5f};
	edge::TextureOptions phone;
	phone.codec = edge::TextureCodec::ASTC;
	phone.atlas = edge::TextureOptions::Atlas::Grid;
	std::set<std::string> seen = {a.options};
	edge::convert_gltf_to_edge(asset("test_gltf_loader_pbr.glb"), out("srgb.edge"), srgb);
	edge::convert_gltf_to_edge(asset("test_gltf_loader_pbr.glb"), out("lods.edge"), plain, lods);
	edge::convert_gltf_to_edge(asset("test_gltf_loader_pbr.glb"), out("phone.edge"), plain, edge::LodOptions(), 0, phone);
	for (char const *name : {"srgb.edge", "lods.edge", "phone.edge"})
	{
		edge::SerializedModel const b = edge::Model::read_file(out(name));
		BOOST_TEST(b.pipeline == hash, name);
		BOOST_TEST(seen.insert(b.options).second, name << ": " << b.options);
	}
	BOOST_TEST_MESSAGE("options of the plain conversion: " << a.options);
	// the same options, the same words
	edge::convert_gltf_to_edge(asset("test_gltf_loader_pbr.glb"), out("again.edge"), plain);
	BOOST_TEST(edge::Model::read_file(out("again.edge")).options == a.options);
}

BOOST_AUTO_TEST_CASE(mipmaps_by_the_GPU_are_for_textures_that_are_not_compressed)
{
	edge::GltfOptions both;
	both.mipmaps = true;
	BOOST_CHECK_THROW(edge::convert_gltf_to_edge(asset("test_gltf_loader_pbr.glb"), out("both.edge"), both), std::invalid_argument);
	BOOST_CHECK(!std::filesystem::exists(out("both.edge")));
}

BOOST_AUTO_TEST_CASE(a_texture_the_glTF_brings_compressed_is_kept_as_it_is)
{
	edge::convert_gltf_to_edge(asset("test_gltf_loader_dds.glb"), out("dds.edge"), edge::GltfOptions());
	auto model = std::make_shared<edge::StaticModelPTBNT>(renderer, scheduler);
	model->load_from_file(out("dds.edge"));
	size_t found = 0;
	for (auto const &m : model->materials())
		if (auto const t = m->diffuse_texture(0))
		{
			BOOST_CHECK(edge::is_block_compressed(t->pixel_format()));
			BOOST_CHECK_GT(t->mip_levels(), 1);
			++found;
		}
	BOOST_CHECK_GT(found, 0u);
}

BOOST_AUTO_TEST_CASE(an_atlas_puts_the_textures_of_the_materials_in_one_set_and_leaves_what_tiles)
{
	edge::TextureOptions textures;
	textures.atlas = edge::TextureOptions::Atlas::Grid;
	textures.atlas_size = 1024;
	edge::convert_gltf_to_edge(asset("test_figure.glb"), out("atlas.edge"), edge::GltfOptions(), edge::LodOptions(), 0, textures);
	edge::convert_gltf_to_edge(asset("test_figure.glb"), out("plain.edge"), edge::GltfOptions());
	// (the textures, not their addresses: a model's textures go with it)
	auto distinct_diffuse = [&](std::string const &file) {
		auto model = std::make_shared<edge::AnimatedModelPTBNJWT>(renderer, scheduler);
		model->load_from_file(file);
		std::set<edge::TexturePtr> diffuse;
		for (auto const &m : model->materials())
			for (size_t i = 0; i < m->diffuse_textures_count(); ++i)
				if (auto const t = m->diffuse_texture(i))
				{
					diffuse.insert(t);
					BOOST_CHECK_MESSAGE(t->pixel_format() == edge::PixelFormat::BC7 && t->mip_levels() > 1, "a texture of " << file << " is not BC7 with mips");
				}
		return diffuse;
	};
	auto const plain = distinct_diffuse(out("plain.edge")), atlas = distinct_diffuse(out("atlas.edge"));
	BOOST_CHECK_GE(plain.size(), 8u);
	// the shoes tile their texture, and keep it; the other eight textured materials have the atlas
	BOOST_CHECK_EQUAL(atlas.size(), 2u);
	size_t big = 0;
	for (edge::TexturePtr const &t : atlas)
		big += t->width() == 1024 || t->width() == 512;
	BOOST_CHECK_GE(big, 1u);
}

BOOST_AUTO_TEST_CASE(an_xatlas_atlas_takes_every_textured_material_even_one_that_tiles)
{
	edge::TextureOptions textures;
	textures.atlas = edge::TextureOptions::Atlas::Xatlas;
	textures.atlas_size = 1024;
	edge::convert_gltf_to_edge(asset("test_figure.glb"), out("xatlas.edge"), edge::GltfOptions(), edge::LodOptions(), 0, textures);
	auto model = std::make_shared<edge::AnimatedModelPTBNJWT>(renderer, scheduler);
	model->load_from_file(out("xatlas.edge"));
	std::set<edge::Texture const *> diffuse, normal;
	for (auto const &m : model->materials())
	{
		if (m->diffuse_textures_count() > 0)
			if (auto const t = m->diffuse_texture(0))
				diffuse.insert(t.get());
		if (auto const t = m->normal_map_texture())
			normal.insert(t.get());
	}
	// the nine textured materials, the shoes that tile among them, have one colour texture and one normal map
	BOOST_CHECK_EQUAL(diffuse.size(), 1u);
	BOOST_CHECK_EQUAL(normal.size(), 1u);
	BOOST_CHECK_LE((*diffuse.begin())->width(), 1024);
	BOOST_CHECK(edge::is_block_compressed((*diffuse.begin())->pixel_format()));
}

BOOST_AUTO_TEST_CASE(without_emissive_nothing_glows)
{
	auto glowing = [&](edge::TextureOptions const &textures) {
		edge::convert_gltf_to_edge(asset("test_gltf_loader_pbr.glb"), out("glow.edge"), edge::GltfOptions(), edge::LodOptions(), 0, textures);
		auto model = std::make_shared<edge::StaticModelPTBNT>(renderer, scheduler);
		model->load_from_file(out("glow.edge"));
		size_t n = 0;
		for (auto const &m : model->materials())
			n += m->emissive_texture() || m->emissive_factor().x != 0 || m->emissive_factor().y != 0 || m->emissive_factor().z != 0;
		return n;
	};
	BOOST_CHECK_GT(glowing(edge::TextureOptions()), 0u);
	edge::TextureOptions dark;
	dark.emissive = false;
	BOOST_CHECK_EQUAL(glowing(dark), 0u);
}

/// The margin in the options is in the box the file keeps.
BOOST_AUTO_TEST_CASE(the_pose_margin_is_in_the_box_the_file_keeps)
{
	edge::GltfOptions margined;
	margined.pose_margin = 0.5f;
	edge::convert_gltf_to_edge(asset("test_figure.glb"), out("plain.edge"), edge::GltfOptions());
	edge::convert_gltf_to_edge(asset("test_figure.glb"), out("margined.edge"), margined);
	auto plain = std::make_shared<edge::AnimatedModelPTBNJWT>(renderer, scheduler);
	plain->load_from_file(out("plain.edge"));
	auto grown = std::make_shared<edge::AnimatedModelPTBNJWT>(renderer, scheduler);
	grown->load_from_file(out("margined.edge"));
	auto const &a = static_cast<edge::AnimatedMeshPTBNJWT const &>(*plain->meshes().at(0)).pose_bounds();
	auto const &b = static_cast<edge::AnimatedMeshPTBNJWT const &>(*grown->meshes().at(0)).pose_bounds();
	BOOST_CHECK_SMALL((b.hi.y - b.lo.y) - 2.0f * (a.hi.y - a.lo.y), 1e-4f);        // each side grew by half the size: twice the size
}

BOOST_AUTO_TEST_CASE(a_missing_glTF_is_an_error_and_leaves_no_file)
{
	BOOST_CHECK_THROW(edge::convert_gltf_to_edge(asset("no_such.glb"), out("x.edge"), edge::GltfOptions()), std::runtime_error);
	BOOST_CHECK(!std::filesystem::exists(out("x.edge")));
}

BOOST_AUTO_TEST_CASE(a_glTF_that_cannot_be_parsed_is_an_error_and_leaves_no_file)
{
	{
		std::ofstream(out("bad.glb")) << "this is not a glTF";
	}
	BOOST_CHECK_THROW(edge::convert_gltf_to_edge(out("bad.glb"), out("bad.edge"), edge::GltfOptions()), std::runtime_error);
	BOOST_CHECK(!std::filesystem::exists(out("bad.edge")));
}

BOOST_AUTO_TEST_CASE(an_output_that_cannot_be_written_is_an_error)
{
	BOOST_CHECK_THROW(edge::convert_gltf_to_edge(asset("test_figure.glb"), out("no_such_dir/figure.edge"), edge::GltfOptions()), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(a_failed_conversion_leaves_the_file_that_was_there_as_it_was)
{
	edge::convert_gltf_to_edge(asset("test_gltf_loder_plane.glb"), out("m.edge"), edge::GltfOptions());
	auto const size = std::filesystem::file_size(out("m.edge"));
	BOOST_CHECK_THROW(edge::convert_gltf_to_edge(asset("no_such.glb"), out("m.edge"), edge::GltfOptions()), std::runtime_error);
	BOOST_CHECK_EQUAL(std::filesystem::file_size(out("m.edge")), size);
}

BOOST_AUTO_TEST_CASE(converting_again_gives_the_same_bytes)
{
	edge::convert_gltf_to_edge(asset("test_figure.glb"), out("a.edge"), edge::GltfOptions());
	edge::convert_gltf_to_edge(asset("test_figure.glb"), out("b.edge"), edge::GltfOptions());
	std::ifstream a(out("a.edge"), std::ios::binary), b(out("b.edge"), std::ios::binary);
	std::string const x((std::istreambuf_iterator<char>(a)), std::istreambuf_iterator<char>());
	std::string const y((std::istreambuf_iterator<char>(b)), std::istreambuf_iterator<char>());
	BOOST_CHECK(x == y);
}

BOOST_AUTO_TEST_SUITE_END()
