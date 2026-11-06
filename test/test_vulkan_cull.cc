// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

// Contract of a material's face culling, rendered and read back.  glTF's front faces are the ones whose vertices run counter-clockwise as the
// camera sees them, and the glTF loader gives a single-sided material CullMode::CCW: such a material draws a triangle that runs counter-clockwise on
// the screen and culls one that runs clockwise.  CullMode::CW is the other way round; CullMode::NONE draws both, its front faces those of CullMode::CCW.

#include <boost/test/unit_test.hpp>
#include "test_fixture.h"
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

using namespace edge;

namespace
{

int const TARGET_SIZE = 16;

struct Rgba
{
	float r, g, b, a;
};

/// One triangle over the middle of the target in red, its vertices running counter-clockwise (or clockwise) as a camera on +Z looking at
/// the origin with +Y up sees them, drawn with a material of `cull_mode` into a target cleared to black: is the middle pixel red?
bool drawn(RendererPtr const &renderer, Material::CullMode cull_mode, bool counter_clockwise, char const *fragment = "test_vertex_color.frag")
{
	VertexBufferPtr vb = renderer->create_vertex_buffer(VertexTraits::of<VERTEX_PTBNTC>(), sizeof(VERTEX_PTBNTC) * 3);
	auto vb_mapping = vb->write(Contents::DISCARD);
	VERTEX_PTBNTC *v = vb_mapping.as<VERTEX_PTBNTC>().data();
	BOOST_REQUIRE(v);
	float const xs[3] = {-1, 1, 0}, ys[3] = {-1, -1, 1};      // counter-clockwise seen from +Z
	for (int k = 0; k < 3; ++k)
	{
		std::memset(&v[k], 0, sizeof(v[k]));
		v[k].position = rove::vec<3>(xs[k], ys[k], 0.0f);
		v[k].color.set(1.0f, 0.0f, 0.0f, 1.0f);
	}
	vb_mapping.unmap();
	IndexBufferPtr ib = renderer->create_index_buffer(IndexFormat::U16, sizeof(INDEX_U16) * 3);
	auto ib_mapping = ib->write(Contents::DISCARD);
	INDEX_U16 *idx = ib_mapping.as<INDEX_U16>().data();
	BOOST_REQUIRE(idx);
	idx[0] = 0;
	idx[1] = counter_clockwise ? 1 : 2;
	idx[2] = counter_clockwise ? 2 : 1;
	ib_mapping.unmap();
	CameraPtr camera(new Camera());
	camera->set_orthographic_projection(2.0f, 2.0f, 0.0f, 2.0f);
	camera->look_at(rove::vec<3>(0, 0, 1), rove::vec<3>(0, 0, 0), rove::vec<3>(0, 1, 0));
	RenderTargetPtr target = renderer->create_render_target(TARGET_SIZE, TARGET_SIZE, PixelFormat::R32G32B32A32F);

	MaterialPtr material = renderer->create_material();
	material->set_cull_mode(cull_mode);
	material->set_shader_program(renderer->create_shader_program(edge::test_shader("test_vertex_color.vert"), edge::test_shader(fragment)));

	DrawCallPtr draw_call = renderer->create_draw_call();
	draw_call->set_vertex_buffer(vb);
	draw_call->set_index_buffer(ib);
	draw_call->set_material(material);
	draw_call->set_camera(camera);
	draw_call->set_triangles_count(1);
	draw_call->set_always_visible(true);
	draw_call->commit();
	RenderPassClearState clear;
	clear.clear_color_r = clear.clear_color_g = clear.clear_color_b = clear.clear_color_a = 0.0f;
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
	return middle.r > 0.5f;
}

}

BOOST_AUTO_TEST_SUITE(cull)

BOOST_AUTO_TEST_CASE_TEMPLATE(a_single_sided_gltf_material_draws_what_runs_counter_clockwise_on_the_screen_and_culls_the_rest, F, edge::Backends)
{
	F fixture;
	auto const &renderer = fixture.renderer;
	BOOST_REQUIRE_MESSAGE(renderer, "renderer unavailable: " << fixture.error);
	BOOST_TEST(drawn(renderer, Material::CullMode::CCW, true));
	BOOST_TEST(!drawn(renderer, Material::CullMode::CCW, false));
}

BOOST_AUTO_TEST_CASE_TEMPLATE(cull_cw_is_the_other_way_round_and_cull_none_draws_both, F, edge::Backends)
{
	F fixture;
	auto const &renderer = fixture.renderer;
	BOOST_REQUIRE_MESSAGE(renderer, "renderer unavailable: " << fixture.error);
	BOOST_TEST(!drawn(renderer, Material::CullMode::CW, true));
	BOOST_TEST(drawn(renderer, Material::CullMode::CW, false));
	BOOST_TEST(drawn(renderer, Material::CullMode::NONE, true));
	BOOST_TEST(drawn(renderer, Material::CullMode::NONE, false));
}

BOOST_AUTO_TEST_CASE_TEMPLATE(a_front_face_is_one_that_runs_as_its_material_keeps_whether_or_not_it_culls, F, edge::Backends)
{
	// gl_FrontFacing as the culling has it: a shader lighting both sides of a surface turns its normal by it (on
	// OpenGL ES a material drawn both sides had it the other way round, the image being drawn upside down there)
	F fixture;
	auto const &renderer = fixture.renderer;
	BOOST_REQUIRE_MESSAGE(renderer, "renderer unavailable: " << fixture.error);
	BOOST_TEST(drawn(renderer, Material::CullMode::NONE, true, "test_front_facing.frag"));          // red: front
	BOOST_TEST(!drawn(renderer, Material::CullMode::NONE, false, "test_front_facing.frag"));        // green: back
	BOOST_TEST(drawn(renderer, Material::CullMode::CCW, true, "test_front_facing.frag"));
	BOOST_TEST(drawn(renderer, Material::CullMode::CW, false, "test_front_facing.frag"));           // (its front faces run clockwise)
	BOOST_TEST(drawn(renderer, Material::CullMode::NONE, true, "test_front_facing.frag"));          // (after a draw of the other winding)
}

BOOST_AUTO_TEST_SUITE_END()
