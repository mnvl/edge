// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

// Contract: a material's own vertex shader may sample the material's textures
// (a sea displaced by a height texture, say).  The descriptor set layout has to
// make the samplers visible to the vertex stage for that; MoltenVK happens to
// sample them regardless, so on it these cases pass either way, but under the
// validation layers or a stricter driver a fragment-only layout fails them.

#include "test_fixture.h"
#include <boost/test/unit_test.hpp>
#include <cstdint>
#include <memory>

#include "camera.h"
#include "draw_call.h"
#include "material.h"
#include "pixel.h"
#include "render_target.h"
#include "shader_program.h"
#include "test_backends.h"
#include "test_vulkan_fixture.h"
#include "texture.h"
#include "vertex.h"
#include "vertex_traits.h"

using namespace edge;

namespace
{

int const TARGET_SIZE = 64;
uint32_t const WHITE = 0x00FFFFFFu;
uint32_t const BLUE = 0x000000FFu;

// The left half of the screen: x from -1 to 0.
struct LeftHalf
{
	VertexBufferPtr vb;
	IndexBufferPtr ib;
	CameraPtr camera;
	RenderTargetPtr target;

	explicit LeftHalf(RendererPtr const &renderer)
	{
		vb = renderer->create_vertex_buffer(VertexTraits::of<VERTEX_PT>(), sizeof(VERTEX_PT) * 4);
		auto vb_mapping = vb->write(Contents::DISCARD);
		VERTEX_PT *v = vb_mapping.as<VERTEX_PT>().data();
		BOOST_REQUIRE(v);
		v[0].position = rove::vec<3>(-1.0f, -1.0f, 0.0f);
		v[1].position = rove::vec<3>( 0.0f, -1.0f, 0.0f);
		v[2].position = rove::vec<3>( 0.0f,  1.0f, 0.0f);
		v[3].position = rove::vec<3>(-1.0f,  1.0f, 0.0f);
		vb_mapping.unmap();
		ib = renderer->create_index_buffer(IndexFormat::U16, sizeof(INDEX_U16) * 6);
		auto ib_mapping = ib->write(Contents::DISCARD);
		INDEX_U16 *idx = ib_mapping.as<INDEX_U16>().data();
		BOOST_REQUIRE(idx);
		idx[0] = 0; idx[1] = 1; idx[2] = 2; idx[3] = 0; idx[4] = 2; idx[5] = 3;
		ib_mapping.unmap();
		camera = std::make_shared<Camera>();
		camera->set_orthographic_projection(2.0f, 2.0f, 0.0f, 2.0f);
		camera->look_at(rove::vec<3>(0, 0, 1), rove::vec<3>(0, 0, 0), rove::vec<3>(0, 1, 0));
		target = renderer->create_render_target(TARGET_SIZE, TARGET_SIZE, PixelFormat::X8R8G8B8);
	}

	// (the colour a quarter of the way across, three quarters of the way across)
	std::pair<uint32_t, uint32_t> render(RendererPtr const &renderer, uint8_t red)
	{
		TexturePtr texture = renderer->create_texture(2, 2, PixelFormat::X8R8G8B8);
		auto texture_mapping = texture->write(Contents::DISCARD);
		unsigned *p = texture_mapping.as<unsigned>().data();
		BOOST_REQUIRE(p);
		p[0] = p[1] = p[2] = p[3] = 0xFF000000u | (uint32_t(red) << 16);
		texture_mapping.unmap();

		MaterialPtr material = renderer->create_material();
		material->set_diffuse_texture(texture);
		material->set_cull_mode(Material::CullMode::NONE);
		material->set_shader_program(renderer->create_shader_program(
			edge::test_shader("test_vertex_texture.vert"), edge::test_shader("test_vertex_texture.frag")));

		DrawCallPtr draw_call = renderer->create_draw_call();
		draw_call->set_vertex_buffer(vb);
		draw_call->set_index_buffer(ib);
		draw_call->set_material(material);
		draw_call->set_camera(camera);
		draw_call->set_triangles_count(2);
		draw_call->set_always_visible(true);
		draw_call->commit();

		RenderPassClearState clear;
		clear.clear_color = true;
		clear.clear_color_r = 0.0f;
		clear.clear_color_g = 0.0f;
		clear.clear_color_b = 1.0f;
		clear.clear_color_a = 1.0f;
		clear.clear_depth = true;
		clear.clear_depth_value = 1.0f;
		renderer->begin_frame();
		renderer->begin_pass(target, clear);
		draw_call->execute();
		renderer->end_pass();
		renderer->finish_frame();

		TexturePtr color = target->color_texture();
		color->download();
		auto color_mapping = color->read();
		uint32_t const *pixels = color_mapping.as<uint32_t const>().data();
		BOOST_REQUIRE(pixels);
		int const row = (TARGET_SIZE / 2) * TARGET_SIZE;
		std::pair<uint32_t, uint32_t> out(pixels[row + TARGET_SIZE / 4] & 0x00FFFFFFu,
			pixels[row + 3 * TARGET_SIZE / 4] & 0x00FFFFFFu);
		color_mapping.unmap();
		return out;
	}
};

}

BOOST_AUTO_TEST_SUITE(vertex_texture)

BOOST_AUTO_TEST_CASE_TEMPLATE(a_vertex_shader_reads_the_material_s_texture, F, edge::Backends)
{
	F fixture;
	auto const &renderer = fixture.renderer;
	BOOST_REQUIRE_MESSAGE(renderer, "renderer unavailable: " << fixture.error);
	LeftHalf quad(renderer);

	std::pair<uint32_t, uint32_t> still = quad.render(renderer, 0);
	BOOST_CHECK_EQUAL(still.first, WHITE);
	BOOST_CHECK_EQUAL(still.second, BLUE);

	std::pair<uint32_t, uint32_t> moved = quad.render(renderer, 255);     // red 1: one unit right
	BOOST_CHECK_EQUAL(moved.first, BLUE);
	BOOST_CHECK_EQUAL(moved.second, WHITE);
}

BOOST_AUTO_TEST_SUITE_END()
