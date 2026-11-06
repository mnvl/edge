// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

// Contract of a material's blending, rendered and read back: the colour channels blend as the material's blend modes say, and so does
// the alpha channel -- unless the material keeps the destination's alpha, which is then left exactly as the target had it.  An unknown
// blend mode is refused.

#include <stdexcept>
#include <boost/test/unit_test.hpp>
#include "test_fixture.h"
#include <cmath>
#include <cstring>

#include "camera.h"
#include "draw_call.h"
#include "material.h"
#include "pixel.h"
#include "render_target.h"
#include "test_backends.h"
#include "test_vulkan_fixture.h"
#include "texture.h"
#include "vertex.h"
#include "vertex_traits.h"
#include "vulkan_pipeline_builder.h"

using namespace edge;

namespace
{

int const TARGET_SIZE = 16;

struct Rgba
{
	float r, g, b, a;
};

/// A quad over the whole target of colour (1, 0.5, 0) at alpha 0.5, blended source alpha over one minus source alpha into a target
/// cleared to black at alpha 0.25: the pixel in the middle.
Rgba blended(RendererPtr const &renderer, bool preserve_destination_alpha)
{
	VertexBufferPtr vb = renderer->create_vertex_buffer(VertexTraits::of<VERTEX_PTBNTC>(), sizeof(VERTEX_PTBNTC) * 4);
	auto vb_mapping = vb->write(Contents::DISCARD);
	VERTEX_PTBNTC *v = vb_mapping.as<VERTEX_PTBNTC>().data();
	BOOST_REQUIRE(v);
	float const xs[4] = {-1, 1, 1, -1}, ys[4] = {-1, -1, 1, 1};
	for (int k = 0; k < 4; ++k)
	{
		std::memset(&v[k], 0, sizeof(v[k]));
		v[k].position = rove::vec<3>(xs[k], ys[k], 0.0f);
		v[k].color.set(1.0f, 0.5f, 0.0f, 0.5f);
	}
	vb_mapping.unmap();
	IndexBufferPtr ib = renderer->create_index_buffer(IndexFormat::U16, sizeof(INDEX_U16) * 6);
	auto ib_mapping = ib->write(Contents::DISCARD);
	INDEX_U16 *idx = ib_mapping.as<INDEX_U16>().data();
	BOOST_REQUIRE(idx);
	idx[0] = 0; idx[1] = 1; idx[2] = 2; idx[3] = 0; idx[4] = 2; idx[5] = 3;
	ib_mapping.unmap();
	CameraPtr camera(new Camera());
	camera->set_orthographic_projection(2.0f, 2.0f, 0.0f, 2.0f);
	camera->look_at(rove::vec<3>(0, 0, 1), rove::vec<3>(0, 0, 0), rove::vec<3>(0, 1, 0));
	RenderTargetPtr target = renderer->create_render_target(TARGET_SIZE, TARGET_SIZE, PixelFormat::R32G32B32A32F);

	MaterialPtr material = renderer->create_material();
	material->set_cull_mode(Material::CullMode::NONE);
	material->set_shader_program(renderer->create_shader_program(edge::test_shader("test_vertex_color.vert"), edge::test_shader("test_vertex_color.frag")));
	material->set_alpha_blend_enabled(true);
	material->set_src_blend_mode(Material::BlendMode::SRC_ALPHA);
	material->set_dest_blend_mode(Material::BlendMode::INV_SRC_ALPHA);
	material->set_preserve_destination_alpha(preserve_destination_alpha);

	DrawCallPtr draw_call = renderer->create_draw_call();
	draw_call->set_vertex_buffer(vb);
	draw_call->set_index_buffer(ib);
	draw_call->set_material(material);
	draw_call->set_camera(camera);
	draw_call->set_triangles_count(2);
	draw_call->set_always_visible(true);
	draw_call->commit();
	RenderPassClearState clear;
	clear.clear_color_r = clear.clear_color_g = clear.clear_color_b = 0.0f;
	clear.clear_color_a = 0.25f;
	renderer->begin_frame();
	renderer->begin_pass(target, clear);
	draw_call->execute();
	renderer->end_pass();
	renderer->finish_frame();

	TexturePtr color = target->color_texture();
	color->download();
	auto color_mapping = color->read();
	Rgba const *pixels = color_mapping.as<Rgba const>().data();
	BOOST_REQUIRE(pixels);
	Rgba const middle = pixels[(TARGET_SIZE / 2) * TARGET_SIZE + TARGET_SIZE / 2];
	color_mapping.unmap();
	return middle;
}

bool near(float a, float b)
{
	return std::fabs(a - b) <= 1e-3f;
}

}

BOOST_AUTO_TEST_SUITE(blend)

BOOST_AUTO_TEST_CASE_TEMPLATE(without_preserve_the_alpha_blends_like_the_colour, F, edge::Backends)
{
	F fixture;
	auto const &renderer = fixture.renderer;
	BOOST_REQUIRE_MESSAGE(renderer, "renderer unavailable: " << fixture.error);
	Rgba const p = blended(renderer, false);
	BOOST_CHECK_MESSAGE(near(p.r, 0.5f) && near(p.g, 0.25f) && near(p.b, 0.0f), "colour " << p.r << " " << p.g << " " << p.b);
	BOOST_CHECK_MESSAGE(near(p.a, 0.5f * 0.5f + 0.25f * 0.5f), "alpha " << p.a);
}

BOOST_AUTO_TEST_CASE_TEMPLATE(with_preserve_the_alpha_is_the_targets_and_the_colour_blends_as_before, F, edge::Backends)
{
	F fixture;
	auto const &renderer = fixture.renderer;
	BOOST_REQUIRE_MESSAGE(renderer, "renderer unavailable: " << fixture.error);
	Rgba const p = blended(renderer, true);
	BOOST_CHECK_MESSAGE(near(p.r, 0.5f) && near(p.g, 0.25f) && near(p.b, 0.0f), "colour " << p.r << " " << p.g << " " << p.b);
	BOOST_CHECK_MESSAGE(near(p.a, 0.25f), "alpha " << p.a);
}

BOOST_FIXTURE_TEST_CASE(every_blend_mode_resolves_and_an_unknown_one_is_refused, VulkanFixture)
{
	for (Material::BlendMode mode : {Material::BlendMode::ZERO, Material::BlendMode::ONE, Material::BlendMode::SRC_ALPHA, Material::BlendMode::INV_SRC_ALPHA,
			Material::BlendMode::DEST_ALPHA, Material::BlendMode::INV_DEST_ALPHA})
		BOOST_CHECK_NO_THROW(blend_factors(mode, Material::BlendMode::ONE, false));
	BOOST_CHECK_THROW(blend_factors(Material::BlendMode(99), Material::BlendMode::ONE, false), std::runtime_error);
	BOOST_CHECK_THROW(blend_factors(Material::BlendMode::ONE, Material::BlendMode(-1), true), std::runtime_error);
}

BOOST_AUTO_TEST_SUITE_END()
