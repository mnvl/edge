// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

// Contract of the pipelines a renderer keeps for the shader programs drawn with: a program draws with its own shaders, whatever programs were
// made and freed before it.  A game that makes its programs anew for each level (one scene a round, say) frees the old ones, and the
// allocator hands their addresses to the new ones: a pipeline remembered by the address of the program it was made for would then run the
// shaders of a program that is gone.

#include <boost/test/unit_test.hpp>
#include "test_fixture.h"
#include <cmath>
#include <cstring>
#include <new>
#include <vector>

#include "camera.h"
#include "draw_call.h"
#include "material.h"
#include "pixel.h"
#include "render_target.h"
#include "shader_program.h"
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

/// A quad of colour (0.8, 0.2, 0.4) over a whole target, drawn with `program`: the pixel in the middle.
Rgba drawn_with(VulkanRendererPtr const &renderer, ShaderProgramPtr const &program)
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
		v[k].color.set(0.8f, 0.2f, 0.4f, 1.0f);
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
	material->set_shader_program(program);
	DrawCallPtr draw_call = renderer->create_draw_call();
	draw_call->set_vertex_buffer(vb);
	draw_call->set_index_buffer(ib);
	draw_call->set_material(material);
	draw_call->set_camera(camera);
	draw_call->set_triangles_count(2);
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
	return middle;
}

bool near(float a, float b)
{
	return std::fabs(a - b) <= 1e-3f;
}

}

BOOST_FIXTURE_TEST_SUITE(vulkan_pipeline_cache, VulkanFixture)

BOOST_AUTO_TEST_CASE(a_program_made_where_a_freed_one_stood_draws_with_its_own_shaders)
{
	BOOST_REQUIRE_MESSAGE(renderer, "Vulkan renderer unavailable: " << error);
	// both programs in the same storage, one after the other: the second has the freed one's address whatever the allocator does
	alignas(ShaderProgram) unsigned char storage[sizeof(ShaderProgram)];
	auto const in_storage = [&](std::string const &fragment) {
		return ShaderProgramPtr(new (storage) ShaderProgram(edge::test_shader("test_vertex_color.vert"), fragment), [](ShaderProgram *p) { p->~ShaderProgram(); });
	};
	{
		ShaderProgramPtr const plain = in_storage(edge::test_shader("test_vertex_color.frag"));
		Rgba const p = drawn_with(renderer, plain);
		BOOST_REQUIRE_MESSAGE(near(p.r, 0.8f) && near(p.g, 0.2f) && near(p.b, 0.4f), "the plain program drew " << p.r << " " << p.g << " " << p.b);
		BOOST_REQUIRE_EQUAL(plain.use_count(), 1);         // nothing kept it: it is freed below
	}
	ShaderProgramPtr const inverse = in_storage(edge::test_shader("test_inverse_color.frag"));
	BOOST_REQUIRE(static_cast<void const *>(inverse.get()) == static_cast<void const *>(storage));
	Rgba const p = drawn_with(renderer, inverse);
	BOOST_CHECK_MESSAGE(near(p.r, 0.2f) && near(p.g, 0.8f) && near(p.b, 0.6f),
		"the inverse program drew " << p.r << " " << p.g << " " << p.b << " (the plain one's pipeline: 0.8 0.2 0.4)");
}

BOOST_AUTO_TEST_SUITE_END()
