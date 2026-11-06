// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

// Contracts for what a material's own shaders can read besides the material's
// diffuse and normal textures:
//
// - the frame constants (binding 9): bytes the application sets between
//   frames, the same for every draw of the frame;
// - the global textures (bindings 12..19): textures the application sets
//   between frames (a shadow map, a reflection), sampled clamped;
// - the material's PBR parameters: metallic-roughness texture (binding 10),
//   emissive texture (binding 11), and the factors in the push constants;
// - sRGB textures (sampled as linear values) and mipmapped textures (a
//   texture seen small is filtered, not aliased);
// - edge.glsl, the header that names all of the above for a shader: each of
//   its bindings, push-constant fields and flags reaches what the C++ API
//   puts there (test_contract_*.glsl read them through it).
//
// Every check renders through the renderer and reads the pixels back.

#include "test_fixture.h"
#include <boost/test/unit_test.hpp>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#include "camera.h"
#include "draw_call.h"
#include "image.h"
#include "material.h"
#include "pixel.h"
#include "render_target.h"
#include "shader_program.h"
#include "test_backends.h"
#include "test_vulkan_fixture.h"
#include "texture.h"
#include "vertex.h"
#include "vertex_traits.h"
#include "shader_interface.h"

using namespace edge;

namespace
{

int const TARGET_SIZE = 64;

struct Rgba
{
	float r, g, b, a;
};

// A quad over the middle of an orthographic view; `extent` is its half size
// in clip units (1 covers the whole target), UVs 0..1 across it.
struct Quad
{
	VertexBufferPtr vb;
	IndexBufferPtr ib;
	CameraPtr camera;
	RenderTargetPtr target;

	Quad(RendererPtr const &renderer, float extent = 1.0f, int size = TARGET_SIZE, float uv_offset = 0.0f)
	{
		vb = renderer->create_vertex_buffer(VertexTraits::of<VERTEX_PT>(), sizeof(VERTEX_PT) * 4);
		auto vb_mapping = vb->write(Contents::DISCARD);
		VERTEX_PT *v = vb_mapping.as<VERTEX_PT>().data();
		BOOST_REQUIRE(v);
		v[0].position = rove::vec<3>(-extent, -extent, 0.0f);
		v[1].position = rove::vec<3>( extent, -extent, 0.0f);
		v[2].position = rove::vec<3>( extent,  extent, 0.0f);
		v[3].position = rove::vec<3>(-extent,  extent, 0.0f);
		float const u0 = uv_offset, u1 = 1.0f + uv_offset;
		v[0].texcoords = rove::vec<2>(u0, u0);
		v[1].texcoords = rove::vec<2>(u1, u0);
		v[2].texcoords = rove::vec<2>(u1, u1);
		v[3].texcoords = rove::vec<2>(u0, u1);
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
		target = renderer->create_render_target(size, size, PixelFormat::R32G32B32A32F);
	}

	// Every pixel of the target after drawing the quad with `material`
	// over a clear of (-1, -1, -1, -1).
	std::vector<Rgba> render(RendererPtr const &renderer, MaterialPtr const &material,
		rove::matrix<4, 4> const *world = nullptr)
	{
		DrawCallPtr draw_call = renderer->create_draw_call();
		if (world)
			draw_call->set_world_matrix(*world);
		draw_call->set_vertex_buffer(vb);
		draw_call->set_index_buffer(ib);
		draw_call->set_material(material);
		draw_call->set_camera(camera);
		draw_call->set_triangles_count(2);
		draw_call->set_always_visible(true);
		draw_call->commit();

		RenderPassClearState clear;
		clear.clear_color_r = clear.clear_color_g = clear.clear_color_b = clear.clear_color_a = -1.0f;
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
		std::vector<Rgba> out(pixels, pixels + target->width() * target->height());
		color_mapping.unmap();
		return out;
	}

	Rgba centre(RendererPtr const &renderer, MaterialPtr const &material,
		rove::matrix<4, 4> const *world = nullptr)
	{
		std::vector<Rgba> px = render(renderer, material, world);
		int const s = target->width();
		return px[(s / 2) * s + s / 2];
	}
};

MaterialPtr material_with(RendererPtr const &renderer, std::string const &fragment)
{
	MaterialPtr material = renderer->create_material();
	material->set_cull_mode(Material::CullMode::NONE);
	material->set_shader_program(renderer->create_shader_program(edge::test_shader("test_frame.vert"), fragment));
	return material;
}

TexturePtr solid(RendererPtr const &renderer, uint32_t bgra, TextureUsage const &usage = TextureUsage())
{
	TexturePtr t = renderer->create_texture(4, 4, PixelFormat::A8R8G8B8, usage);
	auto t_mapping = t->write(Contents::DISCARD);
	uint32_t *p = t_mapping.as<uint32_t>().data();
	BOOST_REQUIRE(p);
	for (int i = 0; i < 16; ++i)
		p[i] = bgra;
	t_mapping.unmap();
	return t;
}

bool near(float a, float b, float tolerance = 0.01f)
{
	return std::fabs(a - b) <= tolerance;
}

// edge.glsl: test_contract_frag.glsl returns one thing the header names,
// chosen by extra parameter 0's x.
enum Probe
{
	PROBE_DIFFUSE_0 = 0,          // .. 3
	PROBE_BLENDING_WEIGHTS = 4,
	PROBE_NORMAL_MAP = 5,
	PROBE_METALLIC_ROUGHNESS = 6,
	PROBE_EMISSIVE = 7,
	PROBE_GLOBAL_0 = 8,           // .. 15
	PROBE_BASE_COLOR_FACTOR = 16,
	PROBE_MATERIAL_PBR = 17,
	PROBE_EMISSIVE_FACTOR = 18,
	PROBE_CAMERA_EYE = 19,
	PROBE_EXTRA_0_YZW_AND_PADDING = 20,
	PROBE_EXTRA_1 = 21,
	PROBE_EXTRA_2 = 22,
	PROBE_EXTRA_LAST = 23,
	PROBE_MATERIAL_FLAGS = 24,
	PROBE_UBER_FLAG_VALUES = 25,
	PROBE_MATERIAL_FLAG_VALUES = 26,
	PROBE_COUNTS = 27,
	PROBE_FRAME_CONSTANTS = 28,
	PROBE_WORLD_ORIGIN = 29,
	PROBE_FRAME_CONSTANTS_SECOND = 30,
	PROBE_INSTANCES_AND_PARAMETERS = 31,
	PROBE_NEW_BINDINGS = 32,
	PROBE_PREVIOUS_WORLD_ORIGIN = 33,
	PROBE_PREVIOUS_CLIP_ORIGIN = 34,
	PROBE_PREVIOUS_BINDINGS = 35,
	PROBE_OCCLUSION = 36,
	PROBE_PREVIOUS_UNKNOWN = 37,
};

MaterialPtr contract_material(RendererPtr const &renderer)
{
	MaterialPtr material = renderer->create_material();
	material->set_cull_mode(Material::CullMode::NONE);
	material->set_shader_program(renderer->create_shader_program(edge::test_shader("test_contract.vert"), edge::test_shader("test_contract.frag")));
	return material;
}

Rgba probe(Quad &quad, RendererPtr const &renderer, MaterialPtr const &material, Probe p,
	rove::vec<3> const &extra_0_yzw = rove::vec<3>(0, 0, 0), rove::matrix<4, 4> const *world = nullptr)
{
	material->set_extra_parameter(0, rove::vec<4>(float(p), extra_0_yzw.x, extra_0_yzw.y, extra_0_yzw.z));
	return quad.centre(renderer, material, world);
}

bool near(Rgba const &got, Rgba const &want)
{
	return near(got.r, want.r) && near(got.g, want.g) && near(got.b, want.b) && near(got.a, want.a);
}

std::ostream &operator<<(std::ostream &out, Rgba const &c)
{
	return out << "(" << c.r << ", " << c.g << ", " << c.b << ", " << c.a << ")";
}

// A colour no other slot has, for texture slot k (0..15): every channel differs.
uint32_t slot_color(unsigned k)
{
	return 0xFF000000u | (16u * k + 8u) << 16 | (248u - 16u * k) << 8 | (k * 53u + 100u) % 256u;
}

Rgba slot_rgba(unsigned k)
{
	uint32_t const c = slot_color(k);
	return {float(c >> 16 & 255u) / 255.0f, float(c >> 8 & 255u) / 255.0f, float(c & 255u) / 255.0f, 1.0f};
}

}

BOOST_AUTO_TEST_SUITE(frame_resources)

BOOST_AUTO_TEST_CASE_TEMPLATE(frame_constants_reach_every_draw_and_change_between_frames, F, edge::Backends)
{
	F fixture;
	auto const &renderer = fixture.renderer;
	BOOST_REQUIRE_MESSAGE(renderer, "renderer unavailable: " << fixture.error);
	Quad quad(renderer);
	MaterialPtr material = material_with(renderer, edge::test_shader("test_frame_constants.frag"));

	// The shader returns the first vec4 of the block plus the second's x.
	float constants[8] = {0.25f, 0.5f, 0.75f, 1.0f, 0.125f, 0, 0, 0};
	renderer->set_frame_constants(constants, sizeof(constants));
	Rgba a = quad.centre(renderer, material);
	BOOST_CHECK(near(a.r, 0.375f) && near(a.g, 0.5f) && near(a.b, 0.75f) && near(a.a, 1.0f));

	float other[8] = {0.0f, 0.1f, 0.2f, 0.3f, 0.0f, 0, 0, 0};
	renderer->set_frame_constants(other, sizeof(other));
	Rgba b = quad.centre(renderer, material);
	BOOST_CHECK(near(b.r, 0.0f) && near(b.g, 0.1f) && near(b.b, 0.2f) && near(b.a, 0.3f));
}

// 16 KB of frame constants, what Vulkan, OpenGL ES 3.0 and WebGL 2 all promise a uniform block (it was 4 KB, which a game's lights,
// cascades and fog can fill to the byte): the last vec4 of a block that size reaches the shader.
BOOST_AUTO_TEST_CASE_TEMPLATE(a_frame_constants_block_of_16_kb_reaches_the_shader_to_its_last_byte, F, edge::Backends)
{
	F fixture;
	auto const &renderer = fixture.renderer;
	BOOST_REQUIRE_MESSAGE(renderer, "renderer unavailable: " << fixture.error);
	BOOST_REQUIRE_EQUAL(Renderer::FRAME_CONSTANTS_SIZE, 16384u);
	Quad quad(renderer);
	MaterialPtr material = material_with(renderer, edge::test_shader("test_frame_constants_last.frag"));
	std::vector<float> constants(Renderer::FRAME_CONSTANTS_SIZE / sizeof(float), 0.9f);
	float *const last = constants.data() + constants.size() - 4;
	last[0] = 0.1f;
	last[1] = 0.2f;
	last[2] = 0.3f;
	last[3] = 0.4f;
	renderer->set_frame_constants(constants.data(), constants.size() * sizeof(float));
	Rgba const got = quad.centre(renderer, material);
	BOOST_CHECK(near(got.r, 0.1f) && near(got.g, 0.2f) && near(got.b, 0.3f) && near(got.a, 0.4f));
}

BOOST_AUTO_TEST_CASE_TEMPLATE(frame_constants_are_bounded_and_set_only_between_frames, F, edge::Backends)
{
	F fixture;
	auto const &renderer = fixture.renderer;
	BOOST_REQUIRE_MESSAGE(renderer, "renderer unavailable: " << fixture.error);
	std::vector<uint8_t> too_big(Renderer::FRAME_CONSTANTS_SIZE + 4);
	BOOST_CHECK_THROW(renderer->set_frame_constants(too_big.data(), too_big.size()), std::invalid_argument);
	float c[4] = {0, 0, 0, 0};
	renderer->begin_frame();
	BOOST_CHECK_THROW(renderer->set_frame_constants(c, sizeof(c)), std::logic_error);
	renderer->finish_frame();
}

BOOST_AUTO_TEST_CASE_TEMPLATE(each_global_texture_slot_reads_its_own_texture, F, edge::Backends)
{
	F fixture;
	auto const &renderer = fixture.renderer;
	BOOST_REQUIRE_MESSAGE(renderer, "renderer unavailable: " << fixture.error);
	Quad quad(renderer);
	MaterialPtr material = material_with(renderer, edge::test_shader("test_global_texture.frag"));
	BOOST_REQUIRE_EQUAL(Renderer::GLOBAL_TEXTURES_COUNT, 8u);
	uint32_t const colors[8] = {0xFFFF0000u, 0xFF00FF00u, 0xFF0000FFu, 0xFF808080u,
		0xFFFFFF00u, 0xFF00FFFFu, 0xFFFF00FFu, 0xFF404040u};
	for (unsigned slot = 0; slot < Renderer::GLOBAL_TEXTURES_COUNT; ++slot)
		renderer->set_global_texture(slot, solid(renderer, colors[slot]));
	Rgba const want[8] = {{1, 0, 0, 1}, {0, 1, 0, 1}, {0, 0, 1, 1}, {0.502f, 0.502f, 0.502f, 1},
		{1, 1, 0, 1}, {0, 1, 1, 1}, {1, 0, 1, 1}, {0.251f, 0.251f, 0.251f, 1}};
	for (unsigned slot = 0; slot < Renderer::GLOBAL_TEXTURES_COUNT; ++slot)
	{
		material->set_extra_parameter(0, rove::vec<4>(float(slot), 0, 0, 0));
		Rgba got = quad.centre(renderer, material);
		BOOST_CHECK_MESSAGE(near(got.r, want[slot].r) && near(got.g, want[slot].g) && near(got.b, want[slot].b),
			"slot " << slot << ": " << got.r << " " << got.g << " " << got.b);
	}
	// A slot set to another texture between frames is read anew.
	renderer->set_global_texture(0, solid(renderer, 0xFFFFFFFFu));
	material->set_extra_parameter(0, rove::vec<4>(0, 0, 0, 0));
	Rgba white = quad.centre(renderer, material);
	BOOST_CHECK(near(white.r, 1) && near(white.g, 1) && near(white.b, 1));
	BOOST_CHECK_THROW(renderer->set_global_texture(Renderer::GLOBAL_TEXTURES_COUNT, TexturePtr()), std::out_of_range);
	// a texture of another renderer is images on another device
	F other;
	BOOST_REQUIRE_MESSAGE(other.renderer, "renderer unavailable: " << other.error);
	BOOST_CHECK_THROW(renderer->set_global_texture(0, solid(other.renderer, 0xFFFFFFFFu)), std::invalid_argument);
}

BOOST_AUTO_TEST_CASE_TEMPLATE(a_render_target_feeds_a_later_pass_through_a_global_texture, F, edge::Backends)
{
	F fixture;
	auto const &renderer = fixture.renderer;
	BOOST_REQUIRE_MESSAGE(renderer, "renderer unavailable: " << fixture.error);
	Quad first(renderer);
	MaterialPtr writer = material_with(renderer, edge::test_shader("test_frame_constants.frag"));
	float constants[8] = {0.2f, 0.4f, 0.6f, 0.8f, 0, 0, 0, 0};
	renderer->set_frame_constants(constants, sizeof(constants));
	first.render(renderer, writer);
	renderer->set_global_texture(1, first.target->color_texture());

	Quad second(renderer);
	MaterialPtr reader = material_with(renderer, edge::test_shader("test_global_texture.frag"));
	reader->set_extra_parameter(0, rove::vec<4>(1, 0, 0, 0));
	Rgba got = second.centre(renderer, reader);
	BOOST_CHECK(near(got.r, 0.2f) && near(got.g, 0.4f) && near(got.b, 0.6f) && near(got.a, 0.8f));
}

BOOST_FIXTURE_TEST_CASE(srgb_textures_are_sampled_as_linear_values, VulkanFixture)
{
	BOOST_REQUIRE_MESSAGE(renderer, "Vulkan renderer unavailable: " << error);
	Quad quad(renderer);
	MaterialPtr material = material_with(renderer, edge::test_shader("test_sample.frag"));
	TextureUsage srgb;
	srgb.srgb = true;
	material->set_diffuse_texture(solid(renderer, 0xFF808080u, srgb));
	Rgba lin = quad.centre(renderer, material);
	BOOST_CHECK_MESSAGE(near(lin.r, 0.2158f, 0.005f), "sRGB 128 sampled as " << lin.r);
	BOOST_CHECK(near(lin.a, 1.0f));      // alpha is never converted

	material->set_diffuse_texture(solid(renderer, 0xFF808080u));
	Rgba raw = quad.centre(renderer, material);
	BOOST_CHECK_MESSAGE(near(raw.r, 0.502f, 0.005f), "UNORM 128 sampled as " << raw.r);
}

BOOST_FIXTURE_TEST_CASE(a_mipmapped_texture_seen_small_is_filtered_not_aliased, VulkanFixture)
{
	BOOST_REQUIRE_MESSAGE(renderer, "Vulkan renderer unavailable: " << error);
	// A 256 x 256 one-texel checker over an 8 x 8 pixel quad: 32 texels a pixel.
	int const n = 256;
	auto checker = [&](TextureUsage const &usage) {
		TexturePtr t = renderer->create_texture(n, n, PixelFormat::A8R8G8B8, usage);
		auto t_mapping = t->write(Contents::DISCARD);
		uint32_t *p = t_mapping.as<uint32_t>().data();
		BOOST_REQUIRE(p);
		for (int y = 0; y < n; ++y)
			for (int x = 0; x < n; ++x)
				p[y * n + x] = ((x + y) & 1) ? 0xFFFFFFFFu : 0xFF000000u;
		t_mapping.unmap();
		return t;
	};
	// Half a texel off, so that without mips each pixel samples one texel's
	// centre (pure black or white) rather than the edge between two.
	Quad quad(renderer, 8.0f / TARGET_SIZE, TARGET_SIZE, 0.5f / n);
	MaterialPtr material = material_with(renderer, edge::test_shader("test_sample.frag"));
	auto spread = [&](TexturePtr const &t) {
		material->set_diffuse_texture(t);
		std::vector<Rgba> px = quad.render(renderer, material);
		float lo = 2, hi = -2;
		for (Rgba const &p : px)
			if (p.a >= 0)    // covered by the quad
			{
				lo = std::min(lo, p.r);
				hi = std::max(hi, p.r);
			}
		return std::make_pair(lo, hi);
	};
	TextureUsage mipped;
	mipped.mipmaps = true;
	TexturePtr with_mips = checker(mipped);
	BOOST_CHECK_EQUAL(with_mips->mip_levels(), 9);
	auto filtered = spread(with_mips);
	BOOST_CHECK_MESSAGE(filtered.first > 0.35f && filtered.second < 0.65f,
		"mipmapped checker spans " << filtered.first << ".." << filtered.second);
	// The same texture without mips aliases: the test can tell them apart.
	TexturePtr without = checker(TextureUsage());
	BOOST_CHECK_EQUAL(without->mip_levels(), 1);
	auto aliased = spread(without);
	BOOST_CHECK_MESSAGE(aliased.first < 0.2f || aliased.second > 0.8f,
		"unmipmapped checker spans " << aliased.first << ".." << aliased.second);
}

// X8R8G8B8 is sampled as A8R8G8B8 is, its fourth byte as written, on every renderer: Vulkan's format for both is B8G8R8A8, and the GLES
// renderer's has no swizzle that would make it 1 (WebGL 2 has none).  What is X about it is that edge reads no meaning into the byte.
BOOST_AUTO_TEST_CASE_TEMPLATE(an_x8r8g8b8_texture_is_sampled_as_written_its_fourth_byte_too, F, edge::Backends)
{
	F fixture;
	auto const &renderer = fixture.renderer;
	BOOST_REQUIRE_MESSAGE(renderer, "renderer unavailable: " << fixture.error);
	for (uint8_t const x : {uint8_t(0), uint8_t(77), uint8_t(255)})
	{
		TexturePtr texture = renderer->create_texture(4, 4, PixelFormat::X8R8G8B8);
		auto texture_mapping = texture->write(Contents::DISCARD);
		uint8_t *p = texture_mapping.as<uint8_t>().data();
		BOOST_REQUIRE(p);
		for (int i = 0; i < 16; ++i)
		{
			p[4 * i + 0] = 51;              // B, G, R, X
			p[4 * i + 1] = 102;
			p[4 * i + 2] = 204;
			p[4 * i + 3] = x;
		}
		texture_mapping.unmap();
		Quad quad(renderer);
		MaterialPtr material = material_with(renderer, edge::test_shader("test_sample.frag"));
		material->set_diffuse_texture(texture);
		Rgba const c = quad.centre(renderer, material);
		BOOST_TEST(std::abs(c.r - 0.8f) < 1e-3f, "red " << c.r << " with X " << int(x));
		BOOST_TEST(std::abs(c.g - 0.4f) < 1e-3f, "green " << c.g << " with X " << int(x));
		BOOST_TEST(std::abs(c.b - 0.2f) < 1e-3f, "blue " << c.b << " with X " << int(x));
		BOOST_TEST(std::abs(c.a - x / 255.0f) < 1e-3f, "alpha " << c.a << " with X " << int(x));
	}
}

// Vulkan promises 128 bytes of push constants and no more; phones' GPUs have no more.  Every field is one of the 16-byte words the shader's block
// declares, the flags in the fourth float of the emissive colour's: the probes above read each of them back.
BOOST_AUTO_TEST_CASE(the_push_constants_fit_the_128_bytes_every_device_has)
{
	BOOST_CHECK_EQUAL(sizeof(shader::PushConstants), 128u);
	BOOST_CHECK_EQUAL(offsetof(shader::PushConstants, flags), 124u);
	BOOST_CHECK_EQUAL(offsetof(shader::PushConstants, emissive_factor), 112u);
}

BOOST_AUTO_TEST_CASE_TEMPLATE(material_pbr_parameters_and_textures_reach_the_shader, F, edge::Backends)
{
	F fixture;
	auto const &renderer = fixture.renderer;
	BOOST_REQUIRE_MESSAGE(renderer, "renderer unavailable: " << fixture.error);
	Quad quad(renderer);
	// The shader returns (metal-rough texture's g * roughness factor,
	// metal-rough texture's b * metallic factor, emissive texture's r *
	// emissive factor's r, alpha cutoff).
	MaterialPtr material = material_with(renderer, edge::test_shader("test_material_pbr.frag"));
	material->set_metallic_roughness_texture(solid(renderer, 0xFF00C080u));   // r 0, g 0.753, b 0.502
	material->set_emissive_texture(solid(renderer, 0xFF400000u));             // r 0.251
	material->set_roughness_factor(0.5f);
	material->set_metallic_factor(0.25f);
	material->set_emissive_factor(rove::vec<3>(2.0f, 0.0f, 0.0f));
	material->set_alpha_cutoff(0.375f);
	Rgba got = quad.centre(renderer, material);
	BOOST_CHECK_MESSAGE(near(got.r, 0.753f * 0.5f), "roughness " << got.r);
	BOOST_CHECK_MESSAGE(near(got.g, 0.502f * 0.25f), "metallic " << got.g);
	BOOST_CHECK_MESSAGE(near(got.b, 0.251f * 2.0f), "emissive " << got.b);
	BOOST_CHECK_MESSAGE(near(got.a, 0.375f), "alpha cutoff " << got.a);

	// Without the textures the factors alone come through (the flags say so).
	MaterialPtr bare = material_with(renderer, edge::test_shader("test_material_pbr.frag"));
	bare->set_roughness_factor(0.5f);
	bare->set_metallic_factor(0.25f);
	bare->set_emissive_factor(rove::vec<3>(2.0f, 0.0f, 0.0f));
	Rgba plain = quad.centre(renderer, bare);
	BOOST_CHECK(near(plain.r, 0.5f) && near(plain.g, 0.25f) && near(plain.b, 2.0f) && near(plain.a, 0.0f));
}

BOOST_AUTO_TEST_CASE_TEMPLATE(pbr_defaults_follow_gltf, F, edge::Backends)
{
	F fixture;
	auto const &renderer = fixture.renderer;
	BOOST_REQUIRE_MESSAGE(renderer, "renderer unavailable: " << fixture.error);
	MaterialPtr m = renderer->create_material();
	BOOST_CHECK_EQUAL(m->metallic_factor(), 1.0f);    // glTF: metallicFactor 1
	BOOST_CHECK_EQUAL(m->roughness_factor(), 1.0f);   // glTF: roughnessFactor 1
	BOOST_CHECK_EQUAL(m->emissive_factor().x, 0.0f);
	BOOST_CHECK_EQUAL(m->alpha_cutoff(), 0.0f);
	BOOST_CHECK(!m->metallic_roughness_texture());
	BOOST_CHECK(!m->emissive_texture());
	MaterialPtr c = m->clone();
	m->set_roughness_factor(0.3f);
	BOOST_CHECK_EQUAL(c->roughness_factor(), 1.0f);
}

BOOST_AUTO_TEST_CASE_TEMPLATE(a_viewport_confines_draws_to_its_rectangle, F, edge::Backends)
{
	F fixture;
	auto const &renderer = fixture.renderer;
	BOOST_REQUIRE_MESSAGE(renderer, "renderer unavailable: " << fixture.error);
	// a full-screen quad drawn twice: once into the top-left quarter in one
	// colour, once into the bottom-right quarter in another
	Quad quad(renderer);
	MaterialPtr material = material_with(renderer, edge::test_shader("test_frame_constants.frag"));
	DrawCallPtr call = renderer->create_draw_call();
	call->set_vertex_buffer(quad.vb);
	call->set_index_buffer(quad.ib);
	call->set_material(material);
	call->set_camera(quad.camera);
	call->set_triangles_count(2);
	call->set_always_visible(true);
	float red[8] = {1, 0, 0, 1, 0, 0, 0, 0};
	renderer->set_frame_constants(red, sizeof(red));
	RenderPassClearState clear;
	clear.clear_color_r = clear.clear_color_g = clear.clear_color_b = clear.clear_color_a = 0;
	int const s = TARGET_SIZE, h = s / 2;
	renderer->begin_frame();
	renderer->begin_pass(quad.target, clear);
	renderer->set_viewport(0, 0, h, h);
	call->commit();
	call->execute();
	material->set_extra_parameter(0, rove::vec<4>(0, 0, 0, 0));
	renderer->set_viewport(h, h, h, h);
	call->commit();
	call->execute();
	BOOST_CHECK_THROW(renderer->set_viewport(h, h, h + 1, h), std::out_of_range);
	renderer->end_pass();
	BOOST_CHECK_THROW(renderer->set_viewport(0, 0, 1, 1), std::logic_error);
	renderer->finish_frame();
	TexturePtr colour = quad.target->color_texture();
	colour->download();
	auto colour_mapping = colour->read();
	Rgba const *px = colour_mapping.as<Rgba const>().data();
	auto at = [&](int x, int y) { return px[y * s + x]; };
	BOOST_CHECK(near(at(h / 2, h / 2).r, 1) && near(at(h / 2, h / 2).a, 1));          // top left: drawn
	BOOST_CHECK(near(at(h + h / 2, h + h / 2).r, 1));                              // bottom right: drawn
	BOOST_CHECK(near(at(h + h / 2, h / 2).a, 0) && near(at(h / 2, h + h / 2).a, 0)); // the others: clear
	colour_mapping.unmap();
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(edge_glsl)

BOOST_AUTO_TEST_CASE_TEMPLATE(each_texture_binding_reads_the_texture_the_api_puts_there, F, edge::Backends)
{
	F fixture;
	auto const &renderer = fixture.renderer;
	BOOST_REQUIRE_MESSAGE(renderer, "renderer unavailable: " << fixture.error);
	Quad quad(renderer);
	MaterialPtr material = contract_material(renderer);
	for (unsigned i = 0; i < 4; ++i)
		material->set_diffuse_texture(solid(renderer, slot_color(i)), i);
	material->set_texture_blending_weights_texture(solid(renderer, slot_color(4)));
	material->set_normal_map_texture(solid(renderer, slot_color(5)));
	material->set_metallic_roughness_texture(solid(renderer, slot_color(6)));
	material->set_emissive_texture(solid(renderer, slot_color(7)));
	for (unsigned slot = 0; slot < Renderer::GLOBAL_TEXTURES_COUNT; ++slot)
		renderer->set_global_texture(slot, solid(renderer, slot_color(8 + slot)));

	char const *names[16] = {"diffuse 0", "diffuse 1", "diffuse 2", "diffuse 3", "blending weights",
		"normal map", "metallic-roughness", "emissive", "global 0", "global 1", "global 2", "global 3",
		"global 4", "global 5", "global 6", "global 7"};
	for (unsigned k = 0; k < 16; ++k)
	{
		Rgba got = probe(quad, renderer, material, Probe(PROBE_DIFFUSE_0 + k));
		BOOST_CHECK_MESSAGE(near(got, slot_rgba(k)), names[k] << ": got " << got << ", want " << slot_rgba(k));
	}
}

BOOST_AUTO_TEST_CASE_TEMPLATE(a_texture_the_material_lacks_is_white_and_leaves_the_others_as_they_are, F, edge::Backends)
{
	// (a fresh renderer: the first draw that finds a texture missing is the one that has to make the white one, in the middle of
	// binding the textures it does have)
	F fixture;
	auto const &renderer = fixture.renderer;
	BOOST_REQUIRE_MESSAGE(renderer, "renderer unavailable: " << fixture.error);
	Quad quad(renderer);
	MaterialPtr material = contract_material(renderer);
	material->set_diffuse_texture(solid(renderer, slot_color(0)));
	material->set_emissive_texture(solid(renderer, slot_color(7)));
	Rgba got = probe(quad, renderer, material, PROBE_DIFFUSE_0);
	BOOST_CHECK_MESSAGE(near(got, slot_rgba(0)), "diffuse 0: got " << got << ", want " << slot_rgba(0));
	got = probe(quad, renderer, material, PROBE_EMISSIVE);
	BOOST_CHECK_MESSAGE(near(got, slot_rgba(7)), "emissive: got " << got << ", want " << slot_rgba(7));
	for (Probe const missing : {Probe(PROBE_DIFFUSE_0 + 1), Probe(PROBE_DIFFUSE_0 + 3), PROBE_BLENDING_WEIGHTS, PROBE_NORMAL_MAP, PROBE_METALLIC_ROUGHNESS})
	{
		got = probe(quad, renderer, material, missing);
		BOOST_CHECK_MESSAGE(near(got, {1, 1, 1, 1}), "probe " << int(missing) << " (no texture): got " << got << ", want white");
	}
}

BOOST_AUTO_TEST_CASE_TEMPLATE(push_constant_fields_carry_the_material_and_the_camera, F, edge::Backends)
{
	F fixture;
	auto const &renderer = fixture.renderer;
	BOOST_REQUIRE_MESSAGE(renderer, "renderer unavailable: " << fixture.error);
	Quad quad(renderer);
	quad.camera->look_at(rove::vec<3>(0.25f, -0.5f, 1.5f), rove::vec<3>(0.25f, -0.5f, 0.0f),
		rove::vec<3>(0, 1, 0));
	MaterialPtr material = contract_material(renderer);
	material->set_base_color_factor(rove::vec<4>(0.1f, 0.2f, 0.3f, 0.4f));
	material->set_metallic_factor(0.35f);
	material->set_roughness_factor(0.65f);
	material->set_alpha_cutoff(0.45f);
	material->set_normal_scale(0.55f);
	material->set_emissive_factor(rove::vec<3>(1.5f, 2.5f, 3.5f));
	material->set_extra_parameter(1, rove::vec<4>(11, 12, 13, 14));
	material->set_extra_parameter(2, rove::vec<4>(21, 22, 23, 24));
	material->set_extra_parameter(Material::EXTRA_PARAMETERS_COUNT - 1, rove::vec<4>(31, 32, 33, 34));

	Rgba got = probe(quad, renderer, material, PROBE_BASE_COLOR_FACTOR);
	BOOST_CHECK_MESSAGE(near(got, {0.1f, 0.2f, 0.3f, 0.4f}), "base_color_factor " << got);
	got = probe(quad, renderer, material, PROBE_MATERIAL_PBR);
	BOOST_CHECK_MESSAGE(near(got, {0.35f, 0.65f, 0.45f, 0.55f}), "material_pbr (metallic, roughness, cutoff, normal scale) " << got);
	got = probe(quad, renderer, material, PROBE_EMISSIVE_FACTOR);
	BOOST_CHECK_MESSAGE(near(got, {1.5f, 2.5f, 3.5f, 0.0f}), "emissive_factor " << got);
	got = probe(quad, renderer, material, PROBE_CAMERA_EYE);
	BOOST_CHECK_MESSAGE(near(got, {0.25f, -0.5f, 1.5f, 1.0f}), "camera_eye " << got);
	got = probe(quad, renderer, material, PROBE_EXTRA_0_YZW_AND_PADDING, rove::vec<3>(2, 3, 4));
	BOOST_CHECK_MESSAGE(near(got, {2, 3, 4, 0}), "extra_parameters[0].yzw, padding 0: " << got);
	got = probe(quad, renderer, material, PROBE_EXTRA_1);
	BOOST_CHECK_MESSAGE(near(got, {11, 12, 13, 14}), "extra_parameters[1] " << got);
	got = probe(quad, renderer, material, PROBE_EXTRA_2);
	BOOST_CHECK_MESSAGE(near(got, {21, 22, 23, 24}), "extra_parameters[2] " << got);
	got = probe(quad, renderer, material, PROBE_EXTRA_LAST);
	BOOST_CHECK_MESSAGE(near(got, {31, 32, 33, 34}), "extra_parameters[EDGE_EXTRA_PARAMETERS - 1] " << got);
}

BOOST_AUTO_TEST_CASE_TEMPLATE(flags_are_the_bits_the_renderer_sets, F, edge::Backends)
{
	F fixture;
	auto const &renderer = fixture.renderer;
	BOOST_REQUIRE_MESSAGE(renderer, "renderer unavailable: " << fixture.error);
	Quad quad(renderer);

	// a material's textures set exactly its own bit, which the header's flag tests (each drawn by a call made afresh, whose previous frame
	// is not known: EDGE_FLAG_PREVIOUS_UNKNOWN too)
	struct
	{
		char const *name;
		void (*give)(MaterialPtr const &, TexturePtr const &);
		Rgba want;
	} const cases[] = {
		{"none", [](MaterialPtr const &, TexturePtr const &) {}, {0, 0, 0, float(shader::FLAG_PREVIOUS_UNKNOWN)}},
		{"normal map", [](MaterialPtr const &m, TexturePtr const &t) { m->set_normal_map_texture(t); },
			{1, 0, 0, float(shader::FLAG_NORMAL_MAP | shader::FLAG_PREVIOUS_UNKNOWN)}},
		{"metallic-roughness", [](MaterialPtr const &m, TexturePtr const &t) { m->set_metallic_roughness_texture(t); },
			{0, 1, 0, float(shader::FLAG_METALLIC_ROUGHNESS_MAP | shader::FLAG_PREVIOUS_UNKNOWN)}},
		{"emissive", [](MaterialPtr const &m, TexturePtr const &t) { m->set_emissive_texture(t); },
			{0, 0, 1, float(shader::FLAG_EMISSIVE_MAP | shader::FLAG_PREVIOUS_UNKNOWN)}},
	};
	for (auto const &c : cases)
	{
		MaterialPtr material = contract_material(renderer);
		c.give(material, solid(renderer, 0xFF808080u));
		Rgba got = probe(quad, renderer, material, PROBE_MATERIAL_FLAGS);
		BOOST_CHECK_MESSAGE(near(got, c.want), c.name << ": (normal, metal-rough, emissive, flags) " << got
			<< ", want " << c.want);
	}

	// the flags only the uber draw call sets, by value
	MaterialPtr material = contract_material(renderer);
	Rgba got = probe(quad, renderer, material, PROBE_UBER_FLAG_VALUES);
	BOOST_CHECK_MESSAGE(near(got, {float(shader::FLAG_TEXTURE_BLENDING_VERTEX), float(shader::FLAG_TEXTURE_BLENDING_TEXTURE),
		float(shader::FLAG_SKELETAL), 0}), "blending vertex, blending texture, skeletal " << got);
	got = probe(quad, renderer, material, PROBE_MATERIAL_FLAG_VALUES);
	BOOST_CHECK_MESSAGE(near(got, {float(shader::FLAG_NORMAL_MAP), float(shader::FLAG_METALLIC_ROUGHNESS_MAP),
		float(shader::FLAG_EMISSIVE_MAP), 0}), "normal map, metallic-roughness, emissive " << got);

	// occlusion: its flag, and its strength in the bits above the flags, which leave the flags as they are
	got = probe(quad, renderer, material, PROBE_OCCLUSION);
	BOOST_CHECK_MESSAGE(near(got, {0, float(shader::FLAG_OCCLUSION_MAP), 0, float(shader::FLAG_PREVIOUS_UNKNOWN)}), "no occlusion " << got);
	material->set_metallic_roughness_texture(solid(renderer, 0xFF808080u));
	material->set_occlusion(true);
	material->set_occlusion_strength(0.25f);
	got = probe(quad, renderer, material, PROBE_OCCLUSION);
	BOOST_CHECK_MESSAGE(near(got, {1, float(shader::FLAG_OCCLUSION_MAP), 0.25f,
		float(shader::FLAG_METALLIC_ROUGHNESS_MAP | shader::FLAG_OCCLUSION_MAP | shader::FLAG_PREVIOUS_UNKNOWN)}), "occlusion at a quarter " << got);
	material->set_occlusion_strength(1.0f);
	got = probe(quad, renderer, material, PROBE_OCCLUSION);
	BOOST_CHECK_MESSAGE(near(got, {1, float(shader::FLAG_OCCLUSION_MAP), 1,
		float(shader::FLAG_METALLIC_ROUGHNESS_MAP | shader::FLAG_OCCLUSION_MAP | shader::FLAG_PREVIOUS_UNKNOWN)}),
		"occlusion at full strength " << got);
}

BOOST_AUTO_TEST_CASE_TEMPLATE(counts_draw_constants_and_frame_constants, F, edge::Backends)
{
	F fixture;
	auto const &renderer = fixture.renderer;
	BOOST_REQUIRE_MESSAGE(renderer, "renderer unavailable: " << fixture.error);
	Quad quad(renderer);
	MaterialPtr material = contract_material(renderer);

	Rgba got = probe(quad, renderer, material, PROBE_COUNTS);
	BOOST_CHECK_MESSAGE(near(got, {float(Renderer::GLOBAL_TEXTURES_COUNT), float(Material::EXTRA_PARAMETERS_COUNT),
		float(Renderer::FRAME_CONSTANTS_SIZE), 0}), "global textures, extra parameters, frame constants size " << got);
	got = probe(quad, renderer, material, PROBE_INSTANCES_AND_PARAMETERS);
	BOOST_CHECK_MESSAGE(near(got, {float(Renderer::MAX_INSTANCES), float(Material::PARAMETERS_SIZE), float(shader::FLAG_INSTANCED), 0}),
		"max instances, material parameters size, instanced flag " << got);
	got = probe(quad, renderer, material, PROBE_NEW_BINDINGS);
	BOOST_CHECK_MESSAGE(near(got, {float(shader::BINDING_INSTANCES), float(shader::BINDING_MATERIAL_PARAMETERS), 0, 0}),
		"the instances' and the material parameters' bindings " << got);

	float constants[4] = {0.125f, 0.25f, 0.375f, 0.5f};
	renderer->set_frame_constants(constants, sizeof(constants));
	got = probe(quad, renderer, material, PROBE_FRAME_CONSTANTS);
	BOOST_CHECK_MESSAGE(near(got, {0.125f, 0.25f, 0.375f, 0.5f}), "frame constants " << got);
	// what a shorter block leaves is zero, not what a longer one before it had there
	float const longer[8] = {1, 1, 1, 1, 0.75f, 0.75f, 0.75f, 0.75f};
	renderer->set_frame_constants(longer, sizeof(longer));
	got = probe(quad, renderer, material, PROBE_FRAME_CONSTANTS_SECOND);
	BOOST_CHECK_MESSAGE(near(got, {0.75f, 0.75f, 0.75f, 0.75f}), "frame constants, second vec4 " << got);
	renderer->set_frame_constants(constants, sizeof(constants));
	got = probe(quad, renderer, material, PROBE_FRAME_CONSTANTS_SECOND);
	BOOST_CHECK_MESSAGE(near(got, {0, 0, 0, 0}), "frame constants past a shorter block " << got);

	// the world matrix moves the quad (world_view_projection) and reaches the shader (world_matrix)
	rove::matrix<4, 4> world;
	world.identity();
	world.ij[3][0] = 0.125f;
	world.ij[3][1] = -0.25f;
	world.ij[3][2] = 0.375f;
	got = probe(quad, renderer, material, PROBE_WORLD_ORIGIN, rove::vec<3>(0, 0, 0), &world);
	BOOST_CHECK_MESSAGE(near(got, {0.125f, -0.25f, 0.375f, 1.0f}), "world matrix origin " << got);
	// moved 1.5 to the right, the quad (half size 1) covers the right quarter of the target only
	world.ij[3][0] = 1.5f;
	world.ij[3][1] = world.ij[3][2] = 0.0f;
	material->set_extra_parameter(0, rove::vec<4>(float(PROBE_WORLD_ORIGIN), 0, 0, 0));
	std::vector<Rgba> px = quad.render(renderer, material, &world);
	int const s = quad.target->width();
	Rgba const centre = px[(s / 2) * s + s / 2], right = px[(s / 2) * s + 7 * s / 8];
	BOOST_CHECK_MESSAGE(near(centre.r, -1.0f), "the centre stays clear: " << centre);
	BOOST_CHECK_MESSAGE(near(right.r, 1.5f), "the right quarter is drawn: " << right);
}

BOOST_AUTO_TEST_CASE_TEMPLATE(the_previous_frames_draw_constants_and_bindings_are_the_ones_the_api_names, F, edge::Backends)
{
	F fixture;
	auto const &renderer = fixture.renderer;
	BOOST_REQUIRE_MESSAGE(renderer, "renderer unavailable: " << fixture.error);
	Quad quad(renderer);
	MaterialPtr material = contract_material(renderer);
	DrawCallPtr call = renderer->create_draw_call();
	call->set_vertex_buffer(quad.vb);
	call->set_index_buffer(quad.ib);
	call->set_material(material);
	call->set_camera(quad.camera);
	call->set_triangles_count(2);
	call->set_always_visible(true);
	// one call, a frame after another: dc.previous_* are what it was drawn with the frame before
	auto const centre = [&](Probe p, rove::matrix<4, 4> const &world) {
		material->set_extra_parameter(0, rove::vec<4>(float(p), 0, 0, 0));
		call->set_world_matrix(world);
		call->commit();
		RenderPassClearState clear;
		renderer->begin_frame();
		renderer->begin_pass(quad.target, clear);
		call->execute();
		renderer->end_pass();
		renderer->finish_frame();
		Image<rove::rgba<float>> const image = read_image<rove::rgba<float>>(*quad.target->color_texture());
		rove::rgba<float> const c = image.at(image.width() / 2, image.height() / 2);
		return Rgba{c.r, c.g, c.b, c.a};
	};
	rove::matrix<4, 4> a, b, c;
	a.translation(0.125f, -0.25f, 0.375f);
	b.translation(-0.0625f, 0.125f, 0.25f);
	c.translation(0.25f, 0.0625f, -0.125f);
	Rgba got = centre(PROBE_PREVIOUS_WORLD_ORIGIN, a);
	BOOST_CHECK_MESSAGE(near(got, {0.125f, -0.25f, 0.375f, 1}), "the first frame: the previous world is this one's: " << got);
	got = centre(PROBE_PREVIOUS_WORLD_ORIGIN, b);
	BOOST_CHECK_MESSAGE(near(got, {0.125f, -0.25f, 0.375f, 1}), "dc.previous_world_matrix: the frame before's " << got);
	got = centre(PROBE_PREVIOUS_CLIP_ORIGIN, c);
	rove::matrix<4, 4> const clip = quad.camera->projection_matrix() * quad.camera->view_matrix() * b;
	Rgba const want{clip.ij[3][0], clip.ij[3][1], clip.ij[3][2], clip.ij[3][3]};
	BOOST_CHECK_MESSAGE(near(got, want), "dc.previous_world_view_projection: the frame before's " << got << ", want " << want);
	got = centre(PROBE_PREVIOUS_BINDINGS, c);
	BOOST_CHECK_MESSAGE(near(got, {float(shader::BINDING_PREVIOUS_INSTANCES), float(shader::BINDING_PREVIOUS_JOINT_MATRICES), 0, 0}),
		"the previous instances' and joint matrices' bindings " << got);

	// the flag of a previous frame not known: set for a call seen the first time and through a cut camera, not for one seen before
	got = centre(PROBE_PREVIOUS_UNKNOWN, c);
	BOOST_CHECK_MESSAGE(near(got, {0, float(shader::FLAG_PREVIOUS_UNKNOWN), 0, 0}), "a call seen before " << got);
	quad.camera->cut();
	got = centre(PROBE_PREVIOUS_UNKNOWN, c);
	BOOST_CHECK_MESSAGE(near(got, {1, float(shader::FLAG_PREVIOUS_UNKNOWN), 0, 0}), "through a cut camera " << got);
	DrawCallPtr const fresh = renderer->create_draw_call();
	fresh->set_vertex_buffer(quad.vb);
	fresh->set_index_buffer(quad.ib);
	fresh->set_material(material);
	fresh->set_camera(quad.camera);
	fresh->set_triangles_count(2);
	fresh->set_always_visible(true);
	call = fresh;
	got = centre(PROBE_PREVIOUS_UNKNOWN, c);
	BOOST_CHECK_MESSAGE(near(got, {1, float(shader::FLAG_PREVIOUS_UNKNOWN), 0, 0}), "a call seen the first time " << got);
}

BOOST_AUTO_TEST_SUITE_END()
