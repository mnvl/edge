// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include <cmath>
#include <optional>
#include <stdexcept>
#include <boost/test/unit_test.hpp>

#include "material.h"
#include "null_renderer.h"
#include "serialized_model.h"
#include "shader_interface.h"
#include "vertex.h"
#include "vertex_traits.h"
#include "test_fixture.h"

namespace
{

edge::MaterialPtr make_material()
{
	edge::RendererPtr renderer(new edge::NullRenderer(320, 200));
	return renderer->create_material();
}

}

BOOST_AUTO_TEST_SUITE(material)

/// What a material is before it is set (a glTF material is made from it, field by field) is what SerializedMaterial is before it is filled.
BOOST_AUTO_TEST_CASE(a_new_material_saves_as_a_serialized_material_that_was_not_filled)
{
	edge::MaterialPtr const fresh = make_material();
	fresh->set_name("m");
	edge::TextureTable textures;
	edge::SerializedMaterial const saved = edge::export_material(fresh, textures);
	edge::SerializedMaterial const blank;
	BOOST_CHECK_EQUAL(saved.cull_mode, blank.cull_mode);
	BOOST_CHECK_EQUAL(saved.zwrite_enabled, blank.zwrite_enabled);
	BOOST_CHECK_EQUAL(saved.alpha_blend_enabled, blank.alpha_blend_enabled);
	BOOST_CHECK_EQUAL(saved.preserve_destination_alpha, blank.preserve_destination_alpha);
	BOOST_CHECK_EQUAL(saved.src_blend_mode, blank.src_blend_mode);
	BOOST_CHECK_EQUAL(saved.dest_blend_mode, blank.dest_blend_mode);
	for (std::size_t i = 0; i < edge::Material::EXTRA_PARAMETERS_COUNT; ++i)
		for (int c = 0; c < 4; ++c)
			BOOST_CHECK_EQUAL(saved.extra_parameters[i].i[c], blank.extra_parameters[i].i[c]);
	for (int c = 0; c < 4; ++c)
		BOOST_CHECK_EQUAL(saved.base_color_factor.i[c], blank.base_color_factor.i[c]);
	for (int c = 0; c < 3; ++c)
		BOOST_CHECK_EQUAL(saved.emissive_factor.i[c], blank.emissive_factor.i[c]);
	BOOST_CHECK_EQUAL(saved.metallic_factor, blank.metallic_factor);
	BOOST_CHECK_EQUAL(saved.roughness_factor, blank.roughness_factor);
	BOOST_CHECK_EQUAL(saved.alpha_cutoff, blank.alpha_cutoff);
	BOOST_CHECK_EQUAL(saved.normal_scale, blank.normal_scale);
	BOOST_CHECK(saved.diffuse_textures == blank.diffuse_textures);
	BOOST_CHECK_EQUAL(saved.texture_blending_weights, blank.texture_blending_weights);
	BOOST_CHECK_EQUAL(saved.normal_map, blank.normal_map);
	BOOST_CHECK_EQUAL(saved.metallic_roughness, blank.metallic_roughness);
	BOOST_CHECK_EQUAL(saved.emissive, blank.emissive);
}

BOOST_AUTO_TEST_CASE(default_cull_mode)
{
	auto mat = make_material();
	BOOST_CHECK_EQUAL(static_cast<int>(mat->cull_mode()),
		static_cast<int>(edge::Material::CullMode::NONE));
}

BOOST_AUTO_TEST_CASE(default_zwrite_enabled)
{
	auto mat = make_material();
	BOOST_CHECK(mat->zwrite_enabled());
}

BOOST_AUTO_TEST_CASE(default_alpha_blend_disabled)
{
	auto mat = make_material();
	BOOST_CHECK(!mat->alpha_blend_enabled());
}

BOOST_AUTO_TEST_CASE(destination_alpha_is_written_by_default)
{
	auto mat = make_material();
	BOOST_CHECK(!mat->preserve_destination_alpha());
}

BOOST_AUTO_TEST_CASE(preserve_destination_alpha_can_be_set_cleared_and_survives_a_clone)
{
	auto mat = make_material();
	mat->set_preserve_destination_alpha(true);
	BOOST_CHECK(mat->preserve_destination_alpha());
	BOOST_CHECK(mat->clone()->preserve_destination_alpha());
	mat->set_preserve_destination_alpha(false);
	BOOST_CHECK(!mat->preserve_destination_alpha());
	BOOST_CHECK(!mat->clone()->preserve_destination_alpha());
}

BOOST_AUTO_TEST_CASE(the_stencil_is_left_alone_by_default)
{
	auto mat = make_material();
	BOOST_CHECK(!mat->stencil_write());
}

BOOST_AUTO_TEST_CASE(a_stencil_value_can_be_set_cleared_and_survives_a_clone)
{
	auto mat = make_material();
	mat->set_stencil_write(0);          // a zero is a value written, not "none"
	BOOST_REQUIRE(mat->stencil_write());
	BOOST_CHECK_EQUAL(*mat->stencil_write(), 0);
	mat->set_stencil_write(255);
	BOOST_CHECK_EQUAL(*mat->stencil_write(), 255);
	BOOST_REQUIRE(mat->clone()->stencil_write());
	BOOST_CHECK_EQUAL(*mat->clone()->stencil_write(), 255);
	mat->set_stencil_write(std::nullopt);
	BOOST_CHECK(!mat->stencil_write());
	BOOST_CHECK(!mat->clone()->stencil_write());
}

BOOST_AUTO_TEST_CASE(default_blend_modes)
{
	auto mat = make_material();
	BOOST_CHECK_EQUAL(static_cast<int>(mat->src_blend_mode()),
		static_cast<int>(edge::Material::BlendMode::ONE));
	BOOST_CHECK_EQUAL(static_cast<int>(mat->dest_blend_mode()),
		static_cast<int>(edge::Material::BlendMode::ZERO));
}

BOOST_AUTO_TEST_CASE(set_cull_mode)
{
	auto mat = make_material();
	mat->set_cull_mode(edge::Material::CullMode::CW);
	BOOST_CHECK_EQUAL(static_cast<int>(mat->cull_mode()),
		static_cast<int>(edge::Material::CullMode::CW));

	mat->set_cull_mode(edge::Material::CullMode::CCW);
	BOOST_CHECK_EQUAL(static_cast<int>(mat->cull_mode()),
		static_cast<int>(edge::Material::CullMode::CCW));
}

BOOST_AUTO_TEST_CASE(set_blend_modes)
{
	auto mat = make_material();
	mat->set_alpha_blend_enabled(true);
	mat->set_src_blend_mode(edge::Material::BlendMode::SRC_ALPHA);
	mat->set_dest_blend_mode(edge::Material::BlendMode::INV_SRC_ALPHA);

	BOOST_CHECK(mat->alpha_blend_enabled());
	BOOST_CHECK_EQUAL(static_cast<int>(mat->src_blend_mode()),
		static_cast<int>(edge::Material::BlendMode::SRC_ALPHA));
	BOOST_CHECK_EQUAL(static_cast<int>(mat->dest_blend_mode()),
		static_cast<int>(edge::Material::BlendMode::INV_SRC_ALPHA));
}

BOOST_AUTO_TEST_CASE(set_name)
{
	auto mat = make_material();
	mat->set_name("test_material");
	BOOST_CHECK_EQUAL(mat->name(), "test_material");
}

BOOST_AUTO_TEST_CASE(no_diffuse_textures_by_default)
{
	auto mat = make_material();
	BOOST_CHECK_EQUAL(mat->diffuse_textures_count(), 0u);
}

BOOST_AUTO_TEST_CASE(no_normal_map_by_default)
{
	auto mat = make_material();
	BOOST_CHECK(!mat->normal_map_texture());
}

BOOST_AUTO_TEST_CASE(no_blending_weights_texture_by_default)
{
	auto mat = make_material();
	BOOST_CHECK(!mat->texture_blending_weights_texture());
}

BOOST_AUTO_TEST_CASE(extra_parameters_default_to_zero)
{
	auto mat = make_material();
	for (std::size_t i = 0; i < edge::Material::EXTRA_PARAMETERS_COUNT; ++i)
	{
		rove::vec<4> const &p = mat->extra_parameter(i);
		BOOST_CHECK_SMALL(std::abs(p.x), 1e-6f);
		BOOST_CHECK_SMALL(std::abs(p.y), 1e-6f);
		BOOST_CHECK_SMALL(std::abs(p.z), 1e-6f);
		BOOST_CHECK_SMALL(std::abs(p.w), 1e-6f);
	}
}

BOOST_AUTO_TEST_CASE(set_extra_parameter)
{
	auto mat = make_material();
	rove::vec<4> value(1.0f, 2.0f, 3.0f, 4.0f);
	mat->set_extra_parameter(0, value);

	rove::vec<4> const &result = mat->extra_parameter(0);
	BOOST_CHECK_SMALL(std::abs(result.x - 1.0f), 1e-6f);
	BOOST_CHECK_SMALL(std::abs(result.y - 2.0f), 1e-6f);
	BOOST_CHECK_SMALL(std::abs(result.z - 3.0f), 1e-6f);
	BOOST_CHECK_SMALL(std::abs(result.w - 4.0f), 1e-6f);
}

BOOST_AUTO_TEST_CASE(extra_parameter_out_of_range_throws)
{
	auto mat = make_material();
	BOOST_CHECK_THROW(mat->extra_parameter(edge::Material::EXTRA_PARAMETERS_COUNT),
		std::out_of_range);

	rove::vec<4> value(0.0f, 0.0f, 0.0f, 0.0f);
	BOOST_CHECK_THROW(mat->set_extra_parameter(edge::Material::EXTRA_PARAMETERS_COUNT, value),
		std::out_of_range);
}

BOOST_AUTO_TEST_CASE(set_zwrite)
{
	auto mat = make_material();
	mat->set_zwrite_enabled(false);
	BOOST_CHECK(!mat->zwrite_enabled());
	mat->set_zwrite_enabled(true);
	BOOST_CHECK(mat->zwrite_enabled());
}

BOOST_AUTO_TEST_CASE(set_diffuse_texture_by_ptr)
{
	auto mat = make_material();
	edge::TexturePtr tex = mat->renderer()->create_texture(16, 16, edge::PixelFormat::X8R8G8B8);
	mat->set_diffuse_texture(tex);
	BOOST_CHECK_EQUAL(mat->diffuse_textures_count(), 1u);
	BOOST_CHECK_EQUAL(mat->diffuse_texture(0), tex);
}

BOOST_AUTO_TEST_CASE(set_multiple_diffuse_textures)
{
	auto mat = make_material();
	edge::TexturePtr tex0 = mat->renderer()->create_texture(16, 16, edge::PixelFormat::X8R8G8B8);
	edge::TexturePtr tex1 = mat->renderer()->create_texture(32, 32, edge::PixelFormat::A8R8G8B8);
	mat->set_diffuse_texture(tex0);
	mat->set_diffuse_texture(tex1, 1);
	BOOST_CHECK_EQUAL(mat->diffuse_textures_count(), 2u);
	BOOST_CHECK_EQUAL(mat->diffuse_texture(0), tex0);
	BOOST_CHECK_EQUAL(mat->diffuse_texture(1), tex1);
}

BOOST_AUTO_TEST_CASE(set_normal_map_texture_by_ptr)
{
	auto mat = make_material();
	edge::TexturePtr tex = mat->renderer()->create_texture(16, 16, edge::PixelFormat::X8R8G8B8);
	mat->set_normal_map_texture(tex);
	BOOST_CHECK_EQUAL(mat->normal_map_texture(), tex);
}

BOOST_AUTO_TEST_CASE(default_base_color_factor_is_white)
{
	auto mat = make_material();
	rove::vec<4> const &bcf = mat->base_color_factor();
	BOOST_CHECK_SMALL(std::abs(bcf.x - 1.0f), 1e-6f);
	BOOST_CHECK_SMALL(std::abs(bcf.y - 1.0f), 1e-6f);
	BOOST_CHECK_SMALL(std::abs(bcf.z - 1.0f), 1e-6f);
	BOOST_CHECK_SMALL(std::abs(bcf.w - 1.0f), 1e-6f);
}

BOOST_AUTO_TEST_CASE(set_base_color_factor)
{
	auto mat = make_material();
	rove::vec<4> factor(0.5f, 0.3f, 0.1f, 0.8f);
	mat->set_base_color_factor(factor);
	rove::vec<4> const &bcf = mat->base_color_factor();
	BOOST_CHECK_SMALL(std::abs(bcf.x - 0.5f), 1e-6f);
	BOOST_CHECK_SMALL(std::abs(bcf.y - 0.3f), 1e-6f);
	BOOST_CHECK_SMALL(std::abs(bcf.z - 0.1f), 1e-6f);
	BOOST_CHECK_SMALL(std::abs(bcf.w - 0.8f), 1e-6f);
}

BOOST_AUTO_TEST_CASE(a_material_has_a_renderer)
{
	// its textures are the renderer's: there is no texture that is only a file name
	BOOST_CHECK_THROW(edge::Material{edge::RendererPtr{}}, std::invalid_argument);
}

BOOST_AUTO_TEST_CASE(a_blending_weights_texture_by_file_name_is_loaded_by_the_material_s_renderer)
{
	auto mat = make_material();
	mat->set_texture_blending_weights_texture(edge::TestFixture::graphic_sources_path() + "test/data/test_texture_blending_weights.png");
	BOOST_REQUIRE(mat->texture_blending_weights_texture());
	BOOST_CHECK_GT(mat->texture_blending_weights_texture()->width(), 0);
}

BOOST_AUTO_TEST_CASE(a_normal_map_is_unscaled_by_default_and_its_scale_reaches_the_shader)
{
	auto mat = make_material();
	BOOST_CHECK_EQUAL(mat->normal_scale(), 1.0f);
	mat->set_normal_scale(0.25f);
	BOOST_CHECK_EQUAL(mat->clone()->normal_scale(), 0.25f);
	edge::shader::PushConstants const pc = edge::shader::push_constants(*mat, *edge::VertexTraits::of<edge::VERTEX_PT>(), false, false,
		rove::vec<3>(0, 0, 0));
	BOOST_CHECK_EQUAL(pc.material_pbr[3], 0.25f);
}

BOOST_AUTO_TEST_SUITE_END()
