// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

// A material's own uniform block (Material::parameters, binding EDGE_BINDING_MATERIAL_PARAMETERS): up to
// Material::PARAMETERS_SIZE bytes a shader of the material's reads, beyond the four extra parameters; each draw reads its own
// material's, and a material without one reads zeros.

#include <cstring>
#include <stdexcept>
#include <vector>

#include <boost/test/unit_test.hpp>

#include "camera.h"
#include "draw_call.h"
#include "image.h"
#include "index.h"
#include "material.h"
#include "pixel.h"
#include "renderer.h"
#include "shader_program.h"
#include "test_backends.h"
#include "test_fixture.h"
#include "texture.h"
#include "vertex.h"

using namespace edge;

namespace
{

int const SIZE = 16;

/// A quad over the left or right half of the target, drawn with `material`
DrawCallPtr half(RendererPtr const &renderer, MaterialPtr const &material, CameraPtr const &camera, bool left)
{
	VertexBufferPtr vb = renderer->create_vertex_buffer(VertexTraits::of<VERTEX_PT>(), sizeof(VERTEX_PT) * 4);
	auto vb_mapping = vb->write(Contents::DISCARD);
	VERTEX_PT *v = vb_mapping.as<VERTEX_PT>().data();
	float const x0 = left ? -1.0f : 0.0f, x1 = left ? 0.0f : 1.0f;
	v[0].position = rove::vec<3>(x0, -1, 0);
	v[1].position = rove::vec<3>(x1, -1, 0);
	v[2].position = rove::vec<3>(x1, 1, 0);
	v[3].position = rove::vec<3>(x0, 1, 0);
	for (int k = 0; k < 4; ++k)
		v[k].texcoords = rove::vec<2>(0, 0);
	vb_mapping.unmap();
	IndexBufferPtr ib = renderer->create_index_buffer(IndexFormat::U16, sizeof(INDEX_U16) * 6);
	auto ib_mapping = ib->write(Contents::DISCARD);
	INDEX_U16 *i = ib_mapping.as<INDEX_U16>().data();
	INDEX_U16 const order[6] = {0, 1, 2, 0, 2, 3};
	std::copy(order, order + 6, i);
	ib_mapping.unmap();
	DrawCallPtr dc = renderer->create_draw_call();
	dc->set_vertex_buffer(vb);
	dc->set_index_buffer(ib);
	dc->set_material(material);
	dc->set_camera(camera);
	dc->set_triangles_count(2);
	dc->set_always_visible(true);
	dc->commit();
	return dc;
}

CameraPtr view()
{
	CameraPtr camera = std::make_shared<Camera>();
	camera->set_orthographic_projection(2.0f, 2.0f, 0.0f, 2.0f);
	camera->look_at(rove::vec<3>(0, 0, 1), rove::vec<3>(0, 0, 0), rove::vec<3>(0, 1, 0));
	return camera;
}

MaterialPtr reading(RendererPtr const &renderer, int vec4_index)
{
	MaterialPtr m = renderer->create_material();
	m->set_cull_mode(Material::CullMode::NONE);
	m->set_shader_program(renderer->create_shader_program(test_shader("test_frame.vert"), test_shader("test_material_parameters.frag")));
	m->set_extra_parameter(0, rove::vec<4>(float(vec4_index), 0, 0, 0));
	return m;
}

std::vector<float> block_of(float first, size_t vec4s)
{
	std::vector<float> block(vec4s * 4);
	for (size_t k = 0; k < block.size(); ++k)
		block[k] = first + 0.001f * float(k);
	return block;
}

/// The left half's and the right half's pixel after drawing the two
std::pair<rove::rgba<float>, rove::rgba<float>> draw(RendererPtr const &renderer, DrawCallPtr const &left, DrawCallPtr const &right)
{
	RenderTargetPtr const target = renderer->create_render_target(SIZE, SIZE, PixelFormat::R32G32B32A32F);
	RenderPassClearState clear;
	clear.clear_color_r = clear.clear_color_g = clear.clear_color_b = clear.clear_color_a = -1;
	renderer->begin_frame();
	renderer->begin_pass(target, clear);
	left->execute();
	right->execute();
	renderer->end_pass();
	renderer->finish_frame();
	Image<rove::rgba<float>> const image = read_image<rove::rgba<float>>(*target->color_texture());
	return {image.at(SIZE / 4, SIZE / 2), image.at(3 * SIZE / 4, SIZE / 2)};
}

bool is(rove::rgba<float> const &p, float r, float g, float b, float a)
{
	auto near = [](float x, float y) { return std::abs(x - y) < 1e-5f; };
	return near(p.r, r) && near(p.g, g) && near(p.b, b) && near(p.a, a);
}

}

BOOST_AUTO_TEST_SUITE(material_parameters)

BOOST_AUTO_TEST_CASE_TEMPLATE(each_draw_reads_its_own_materials_block_to_its_last_vec4, F, Backends)
{
	F fixture;
	auto const &renderer = fixture.renderer;
	BOOST_REQUIRE_MESSAGE(renderer, "renderer unavailable: " << fixture.error);
	BOOST_REQUIRE_EQUAL(Material::PARAMETERS_SIZE, 1024u);
	size_t const vec4s = Material::PARAMETERS_SIZE / 16;
	CameraPtr const camera = view();
	MaterialPtr const a = reading(renderer, int(vec4s) - 1), b = reading(renderer, 5);
	std::vector<float> const block_a = block_of(1.0f, vec4s), block_b = block_of(2.0f, vec4s);
	a->set_parameters(block_a.data(), block_a.size() * sizeof(float));
	b->set_parameters(block_b.data(), block_b.size() * sizeof(float));
	auto const [left, right] = draw(renderer, half(renderer, a, camera, true), half(renderer, b, camera, false));
	float const *last = block_a.data() + 4 * (vec4s - 1), *fifth = block_b.data() + 4 * 5;
	BOOST_CHECK(is(left, last[0], last[1], last[2], last[3]));
	BOOST_CHECK(is(right, fifth[0], fifth[1], fifth[2], fifth[3]));
}

BOOST_AUTO_TEST_CASE_TEMPLATE(a_material_without_a_block_reads_zeros_and_a_shorter_one_zeros_after_it, F, Backends)
{
	F fixture;
	auto const &renderer = fixture.renderer;
	BOOST_REQUIRE_MESSAGE(renderer, "renderer unavailable: " << fixture.error);
	CameraPtr const camera = view();
	MaterialPtr const with = reading(renderer, 0), past = reading(renderer, 1), without = reading(renderer, 0);
	float const one[4] = {0.5f, 0.25f, 0.125f, 1.0f};
	with->set_parameters(one, sizeof(one));
	past->set_parameters(one, sizeof(one));
	// drawn after a material with a block: a draw of one without (or past its end) does not read what was left there
	auto const [first, second] = draw(renderer, half(renderer, with, camera, true), half(renderer, without, camera, false));
	BOOST_CHECK(is(first, 0.5f, 0.25f, 0.125f, 1.0f));
	BOOST_CHECK(is(second, 0, 0, 0, 0));
	auto const [again, after] = draw(renderer, half(renderer, with, camera, true), half(renderer, past, camera, false));
	BOOST_CHECK(is(after, 0, 0, 0, 0));
}

BOOST_AUTO_TEST_CASE_TEMPLATE(a_block_set_again_is_read_from_the_next_frame, F, Backends)
{
	F fixture;
	auto const &renderer = fixture.renderer;
	BOOST_REQUIRE_MESSAGE(renderer, "renderer unavailable: " << fixture.error);
	CameraPtr const camera = view();
	MaterialPtr const m = reading(renderer, 0);
	float const first[4] = {0.1f, 0.2f, 0.3f, 0.4f}, second[4] = {0.9f, 0.8f, 0.7f, 0.6f};
	m->set_parameters(first, sizeof(first));
	DrawCallPtr const left = half(renderer, m, camera, true), right = half(renderer, m, camera, false);
	BOOST_CHECK(is(draw(renderer, left, right).first, 0.1f, 0.2f, 0.3f, 0.4f));
	m->set_parameters(second, sizeof(second));
	BOOST_CHECK(is(draw(renderer, left, right).second, 0.9f, 0.8f, 0.7f, 0.6f));
	// a clone has the block, its own afterwards
	MaterialPtr const copy = m->clone();
	BOOST_REQUIRE_EQUAL(copy->parameters().size(), sizeof(second));
	m->set_parameters(first, sizeof(first));
	BOOST_CHECK(std::memcmp(copy->parameters().data(), second, sizeof(second)) == 0);
}

BOOST_AUTO_TEST_CASE_TEMPLATE(a_block_past_the_size_or_with_no_bytes_is_refused, F, AllBackends)
{
	F fixture;
	auto const &renderer = fixture.renderer;
	BOOST_REQUIRE_MESSAGE(renderer, "renderer unavailable: " << fixture.error);
	MaterialPtr const m = renderer->create_material();
	std::vector<unsigned char> const too_big(Material::PARAMETERS_SIZE + 1, 0);
	BOOST_CHECK_THROW(m->set_parameters(too_big.data(), too_big.size()), std::invalid_argument);
	BOOST_CHECK_THROW(m->set_parameters(nullptr, 16), std::invalid_argument);
	m->set_parameters(too_big.data(), Material::PARAMETERS_SIZE);
	BOOST_CHECK_EQUAL(m->parameters().size(), Material::PARAMETERS_SIZE);
	m->set_parameters(nullptr, 0);            // none again
	BOOST_CHECK(m->parameters().empty());
}

BOOST_AUTO_TEST_SUITE_END()
