// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

// Contract: the glTF loader reads metal/rough PBR, emission and alpha cutoff
// into the material, loads colour textures as sRGB and data textures as
// linear when asked (GltfOptions), mipmaps them when asked, and with
// A texture that cannot be loaded is a load_error, not a material without it.
// The assets are written by test/make_test_gltf_pbr.py.

#include <memory>
#include <string>

#include <boost/test/unit_test.hpp>

#include "gltf_loader.h"
#include "material.h"
#include "model_builder.h"
#include "model_cache.h"
#include "static_model.h"
#include "null_renderer.h"
#include "scheduler.h"
#include "test_fixture.h"
#include "test_backends.h"
#include "test_vulkan_fixture.h"
#include "texture.h"

namespace
{

std::string asset(std::string const &name)
{
	return edge::TestFixture::graphic_sources_path() + "test/data/" + name;
}

std::shared_ptr<edge::ModelBuilder> load(edge::RendererPtr const &renderer, std::string const &name,
	edge::GltfOptions const &options = edge::GltfOptions())
{
	edge::SchedulerPtr scheduler(new edge::Scheduler(renderer));
	auto model = std::make_shared<edge::ModelBuilder>(renderer, scheduler);
	model->load_from_gltf(asset(name), options);
	return model;
}

edge::RendererPtr null_renderer()
{
	edge::RendererPtr renderer(new edge::NullRenderer(320, 200));
	return renderer;
}

edge::MaterialPtr material(std::shared_ptr<edge::ModelBuilder> const &model, std::string const &name)
{
	for (auto const &m : model->materials())
		if (m->name() == name)
			return m;
	BOOST_FAIL("no material " << name);
	return edge::MaterialPtr();
}

edge::GltfOptions game_options()
{
	edge::GltfOptions o;
	o.compress_textures = false;
	o.srgb_color = true;
	o.mipmaps = true;
	o.keep_cpu_copy = false;
	return o;
}

}

BOOST_AUTO_TEST_SUITE(gltf_pbr)

BOOST_AUTO_TEST_CASE(pbr_factors_emission_and_cutoff_are_read)
{
	auto model = load(null_renderer(), "test_gltf_loader_pbr.glb");
	auto pbr = material(model, "pbr");
	BOOST_CHECK_CLOSE(pbr->metallic_factor(), 0.25f, 1e-4);
	BOOST_CHECK_CLOSE(pbr->roughness_factor(), 0.5f, 1e-4);
	// emissiveFactor (1, 0.5, 0.25) times KHR_materials_emissive_strength 4
	BOOST_CHECK_CLOSE(pbr->emissive_factor().x, 4.0f, 1e-4);
	BOOST_CHECK_CLOSE(pbr->emissive_factor().y, 2.0f, 1e-4);
	BOOST_CHECK_CLOSE(pbr->emissive_factor().z, 1.0f, 1e-4);
	BOOST_CHECK_CLOSE(pbr->alpha_cutoff(), 0.3f, 1e-4);
	BOOST_CHECK(pbr->metallic_roughness_texture());
	BOOST_CHECK(pbr->emissive_texture());
	BOOST_CHECK(pbr->normal_map_texture());

	auto plain = material(model, "plain");
	BOOST_CHECK_EQUAL(plain->metallic_factor(), 1.0f);
	BOOST_CHECK_EQUAL(plain->roughness_factor(), 1.0f);
	BOOST_CHECK_EQUAL(plain->emissive_factor().x, 0.0f);
	BOOST_CHECK_EQUAL(plain->alpha_cutoff(), 0.0f);   // OPAQUE: no cutoff
	BOOST_CHECK(!plain->metallic_roughness_texture());
	BOOST_CHECK(!plain->emissive_texture());
}

BOOST_AUTO_TEST_CASE(default_options_keep_textures_linear_and_unmipmapped)
{
	auto pbr = material(load(null_renderer(), "test_gltf_loader_pbr.glb"), "pbr");
	BOOST_CHECK(!pbr->diffuse_texture()->is_srgb());
	BOOST_CHECK_EQUAL(pbr->diffuse_texture()->mip_levels(), 1);
}

BOOST_AUTO_TEST_CASE(colour_is_srgb_and_data_is_linear_and_all_are_mipmapped)
{
	auto pbr = material(load(null_renderer(), "test_gltf_loader_pbr.glb", game_options()), "pbr");
	BOOST_CHECK(pbr->diffuse_texture()->is_srgb());
	BOOST_CHECK(pbr->emissive_texture()->is_srgb());
	BOOST_CHECK(!pbr->normal_map_texture()->is_srgb());
	BOOST_CHECK(!pbr->metallic_roughness_texture()->is_srgb());
	// one image as colour and as data: two textures, not one shared wrongly
	BOOST_CHECK(pbr->diffuse_texture() == pbr->emissive_texture());
	for (auto const &t : {pbr->diffuse_texture(), pbr->normal_map_texture(), pbr->metallic_roughness_texture()})
		BOOST_CHECK_EQUAL(t->mip_levels(), 3);     // 4 x 4: 4, 2, 1
}

BOOST_AUTO_TEST_CASE(mipmaps_need_uncompressed_textures)
{
	edge::GltfOptions o;
	o.mipmaps = true;    // compress_textures stays true
	BOOST_CHECK_THROW(load(null_renderer(), "test_gltf_loader_pbr.glb", o), std::invalid_argument);
}

BOOST_AUTO_TEST_CASE(a_texture_that_cannot_be_loaded_is_an_error)
{
	// not a material without it
	BOOST_CHECK_THROW(load(null_renderer(), "test_gltf_loader_bad_image.glb", game_options()), edge::GltfLoader::load_error);
	BOOST_CHECK_THROW(load(null_renderer(), "test_gltf_loader_bad_image.glb", edge::GltfOptions()), edge::GltfLoader::load_error);
}

BOOST_AUTO_TEST_CASE(a_model_cache_loads_with_its_options)
{
	edge::RendererPtr renderer = null_renderer();
	edge::SchedulerPtr scheduler(new edge::Scheduler(renderer));
	edge::ModelCache plain(renderer), game(renderer, game_options());
	auto find = [](auto const &model) {
		for (auto const &m : model->materials())
			if (m->name() == "pbr")
				return m;
		return edge::MaterialPtr();
	};
	auto a = find(plain.load<edge::StaticModelPTBNT>(asset("test_gltf_loader_pbr.glb"), scheduler));
	auto b = find(game.load<edge::StaticModelPTBNT>(asset("test_gltf_loader_pbr.glb"), scheduler));
	BOOST_REQUIRE(a && b);
	BOOST_CHECK(!a->diffuse_texture()->is_srgb());
	BOOST_CHECK(b->diffuse_texture()->is_srgb());
	BOOST_CHECK_EQUAL(b->diffuse_texture()->mip_levels(), 3);
}

BOOST_AUTO_TEST_CASE_TEMPLATE(loaded_textures_without_cpu_copy_cannot_be_locked, F, edge::Backends)
{
	F fixture;
	auto const &renderer = fixture.renderer;
	BOOST_REQUIRE_MESSAGE(renderer, "renderer unavailable: " << fixture.error);
	auto pbr = material(load(renderer, "test_gltf_loader_pbr.glb", game_options()), "pbr");
	edge::TexturePtr t = pbr->diffuse_texture();
	BOOST_CHECK_EQUAL(t->mip_levels(), 3);
	BOOST_CHECK_THROW((void)t->write(edge::Contents::DISCARD), std::logic_error);
	BOOST_CHECK_THROW(t->download(), std::logic_error);
}

BOOST_AUTO_TEST_SUITE_END()
