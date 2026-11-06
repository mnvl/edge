// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

// edge_material.glsl and edge_brdf.glsl, the includes a game's fragment shaders light a material with: what they make of what edge
// binds, on every backend.

#include <cmath>
#include <cstring>

#include <boost/test/unit_test.hpp>

#include "test_fixture.h"
#include "camera.h"
#include "draw_call.h"
#include "material.h"
#include "pixel.h"
#include "render_target.h"
#include "shader_program.h"
#include "test_backends.h"
#include "texture.h"
#include "vertex.h"
#include "vertex_traits.h"

using namespace edge;

namespace
{

struct Rgba
{
	float r, g, b, a;
};

bool near(Rgba const &x, Rgba const &y, float tolerance = 2e-3f)
{
	return std::abs(x.r - y.r) < tolerance && std::abs(x.g - y.g) < tolerance && std::abs(x.b - y.b) < tolerance &&
		std::abs(x.a - y.a) < tolerance;
}

std::ostream &operator<<(std::ostream &s, Rgba const &c)
{
	return s << "(" << c.r << ", " << c.g << ", " << c.b << ", " << c.a << ")";
}

TexturePtr texture_of(RendererPtr const &renderer, unsigned char r, unsigned char g, unsigned char b, unsigned char a)
{
	TexturePtr texture = renderer->create_texture(4, 4, PixelFormat::A8R8G8B8);
	auto texture_mapping = texture->write(Contents::DISCARD);
	unsigned char *p = texture_mapping.as<unsigned char>().data();
	for (int i = 0; i < 16; ++i)
	{
		p[4 * i + 0] = b;           // (the texels are B, G, R, A)
		p[4 * i + 1] = g;
		p[4 * i + 2] = r;
		p[4 * i + 3] = a;
	}
	texture_mapping.unmap();
	return texture;
}

/// The middle of a quad facing +z (tangent +x, binormal +y) drawn with test_surface.frag's probe @p probe.
Rgba drawn(RendererPtr const &renderer, MaterialPtr const &material, int probe)
{
	VertexBufferPtr vb = renderer->create_vertex_buffer(VertexTraits::of<VERTEX_PTBNT>(), sizeof(VERTEX_PTBNT) * 4);
	auto vb_mapping = vb->write(Contents::DISCARD);
	auto *v = vb_mapping.as<VERTEX_PTBNT>().data();
	float const xs[4] = {-1, 1, 1, -1}, ys[4] = {-1, -1, 1, 1};
	for (int k = 0; k < 4; ++k)
	{
		v[k].position = rove::vec<3>(xs[k], ys[k], 0);
		v[k].normal = rove::vec<3>(0, 0, 1);
		v[k].tangent = rove::vec<3>(1, 0, 0);
		v[k].binormal = rove::vec<3>(0, 1, 0);
		v[k].texcoords = rove::vec<2>(0.5f, 0.5f);
	}
	vb_mapping.unmap();
	IndexBufferPtr ib = renderer->create_index_buffer(IndexFormat::U16, sizeof(INDEX_U16) * 6);
	auto ib_mapping = ib->write(Contents::DISCARD);
	auto *i = ib_mapping.as<INDEX_U16>().data();
	INDEX_U16 const order[6] = {0, 1, 2, 0, 2, 3};
	std::memcpy(i, order, sizeof(order));
	ib_mapping.unmap();
	CameraPtr camera(new Camera());
	camera->set_orthographic_projection(2.0f, 2.0f, 0.0f, 2.0f);
	camera->look_at(rove::vec<3>(0, 0, 1), rove::vec<3>(0, 0, 0), rove::vec<3>(0, 1, 0));
	material->set_cull_mode(Material::CullMode::NONE);
	material->set_shader_program(renderer->create_shader_program("", test_shader("test_surface.frag")));
	material->set_extra_parameter(0, rove::vec<4>(float(probe), 0, 0, 0));
	DrawCallPtr draw = renderer->create_draw_call();
	draw->set_vertex_buffer(vb);
	draw->set_index_buffer(ib);
	draw->set_material(material);
	draw->set_camera(camera);
	draw->set_triangles_count(2);
	draw->set_always_visible(true);
	draw->commit();
	RenderTargetPtr target = renderer->create_render_target(8, 8, PixelFormat::R32G32B32A32F);
	RenderPassClearState clear;
	clear.clear_color_r = clear.clear_color_g = clear.clear_color_b = clear.clear_color_a = -1.0f;
	renderer->begin_frame();
	renderer->begin_pass(target, clear);
	draw->execute();
	renderer->end_pass();
	renderer->finish_frame();
	TexturePtr color = target->color_texture();
	color->download();
	auto color_mapping = color->read();
	Rgba const middle = color_mapping.as<Rgba const>().data()[4 * 8 + 4];
	color_mapping.unmap();
	return middle;
}

}

BOOST_AUTO_TEST_SUITE(shader_includes)

BOOST_AUTO_TEST_CASE_TEMPLATE(a_surface_is_the_material_s_factors_times_its_textures, F, Backends)
{
	F fixture;
	auto const &renderer = fixture.renderer;
	BOOST_REQUIRE_MESSAGE(renderer, "renderer unavailable: " << fixture.error);
	MaterialPtr material = renderer->create_material();
	material->set_diffuse_texture(texture_of(renderer, 255, 255, 255, 255));
	material->set_base_color_factor(rove::vec<4>(0.25f, 0.5f, 0.75f, 0.5f));
	Rgba got = drawn(renderer, material, 0);
	BOOST_CHECK_MESSAGE(near(got, {0.25f, 0.5f, 0.75f, 0.5f}), "base colour and alpha " << got);

	// glTF: roughness in g, metallic in b
	material->set_metallic_factor(0.5f);
	material->set_roughness_factor(0.8f);
	material->set_alpha_cutoff(0.3f);
	got = drawn(renderer, material, 1);
	BOOST_CHECK_MESSAGE(near(got, {0.5f, 0.8f, 0.3f, 0}), "metallic, roughness and cutoff without a texture " << got);
	material->set_metallic_roughness_texture(texture_of(renderer, 0, 51, 255, 255));
	got = drawn(renderer, material, 1);
	BOOST_CHECK_MESSAGE(near(got, {0.5f, 0.8f * 0.2f, 0.3f, 0}), "and with one " << got);

	material->set_emissive_factor(rove::vec<3>(1, 2, 3));
	got = drawn(renderer, material, 2);
	BOOST_CHECK_MESSAGE(near(got, {1, 2, 3, 0}), "emissive without a texture " << got);
	material->set_emissive_texture(texture_of(renderer, 255, 0, 255, 255));
	got = drawn(renderer, material, 2);
	BOOST_CHECK_MESSAGE(near(got, {1, 0, 3, 0}), "and with one " << got);
}

BOOST_AUTO_TEST_CASE_TEMPLATE(the_normal_is_the_map_s_two_channels_scaled_in_the_surface_s_frame, F, Backends)
{
	F fixture;
	auto const &renderer = fixture.renderer;
	BOOST_REQUIRE_MESSAGE(renderer, "renderer unavailable: " << fixture.error);
	MaterialPtr material = renderer->create_material();
	Rgba got = drawn(renderer, material, 3);
	BOOST_CHECK_MESSAGE(near(got, {0, 0, 1, 0}), "the geometry's normal without a map " << got);
	// x 1, y 0 (128: 0.004), whatever b holds (a BC5 map has none): tilted toward the tangent, +x
	material->set_normal_map_texture(texture_of(renderer, 255, 128, 0, 255));
	material->set_normal_scale(0.5f);
	got = drawn(renderer, material, 3);
	BOOST_CHECK_MESSAGE(near(got, {0.5f, 0.002f, 0.866f, 0}, 5e-3f), "x and y halved, z rebuilt " << got);
	material->set_normal_scale(0.0f);
	got = drawn(renderer, material, 3);
	BOOST_CHECK_MESSAGE(near(got, {0, 0, 1, 0}), "a scale of 0: flat " << got);
}

BOOST_AUTO_TEST_CASE_TEMPLATE(the_brdf_and_srgb_are_glTF_s, F, Backends)
{
	F fixture;
	auto const &renderer = fixture.renderer;
	BOOST_REQUIRE_MESSAGE(renderer, "renderer unavailable: " << fixture.error);
	MaterialPtr material = renderer->create_material();
	// white, dielectric, rough 1, lit and seen square on: diffuse (1 - F) / pi plus specular D V F, F = 0.04, D = 1 / pi, V = 1/4
	float const expected = (0.96f + 0.25f * 0.04f) / 3.14159265f;
	Rgba got = drawn(renderer, material, 4);
	BOOST_CHECK_MESSAGE(near(got, {expected, expected, expected, 0}), "BRDF " << got << ", want " << expected);
	got = drawn(renderer, material, 5);
	BOOST_CHECK_MESSAGE(near(got, {0, 0.5f, 1, 0}, 3e-3f), "sRGB of 0, 0.214, 1 " << got);
}

BOOST_AUTO_TEST_SUITE_END()
