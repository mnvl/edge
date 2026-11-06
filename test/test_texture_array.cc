// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

// Contract of array textures (TextureUsage::array_layers, a sampler2DArray in the shader): every layer and every mip level of an array lands where
// it belongs (the files here give each layer and each level a colour of its own, so a layer-major and a mip-major order cannot be mistaken for each
// other); an array DDS loads as one, and an array assembled from single DDS files has each file as its layer, in order, or is refused, naming the file
// that does not agree with the first; a layer can be chosen per pixel; an array made by hand takes all its layers through lock() and makes the mips of
// each; and an array does not pass for one image (download, pixels).

#include <boost/test/unit_test.hpp>
#include "test_fixture.h"
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <span>
#include <string>
#include <vector>

#include "camera.h"
#include "draw_call.h"
#include "material.h"
#include "null_renderer.h"
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

int const TARGET = 64;

struct Rgba
{
	float r, g, b, a;
};

/// A colour of RGB565 for each layer and level, all of them different.
struct Colour565
{
	int r5, g6, b5;
	uint16_t packed() const { return uint16_t(r5 << 11 | g6 << 5 | b5); }
};

Colour565 colour(int layer, int level)
{
	return {4 + layer * 9, 8 + level * 20, 31 - layer * 5 - level * 3};
}

/// A DX10 DDS of `layers` layers (1: a plain 2D texture), each with `levels` levels, every level one colour: BC1 (DXGI 71, 72 sRGB) or BC3
/// (77, 78; the alpha block opaque).  Layer-major, as DDS has it: a layer's levels, largest first, then the next layer.
std::vector<char> dds(int width, int height, int levels, int layers, uint32_t dxgi, std::function<Colour565(int, int)> const &paint = colour)
{
	bool const bc3 = dxgi == 77 || dxgi == 78;
	std::vector<char> out(148, 0);
	auto put = [&](size_t at, uint32_t v) { std::memcpy(out.data() + at, &v, 4); };
	std::memcpy(out.data(), "DDS ", 4);
	put(4, 124);
	put(12, uint32_t(height));
	put(16, uint32_t(width));
	put(28, uint32_t(levels));
	put(76, 32);
	put(80, 4);                     // DDPF_FOURCC
	std::memcpy(out.data() + 84, "DX10", 4);
	put(128, dxgi);
	put(132, 3);                    // 2D
	put(140, uint32_t(layers));
	for (int layer = 0; layer < layers; ++layer)
		for (int level = 0; level < levels; ++level)
		{
			int const w = std::max(1, width >> level), h = std::max(1, height >> level);
			int const blocks = ((w + 3) / 4) * ((h + 3) / 4);
			uint16_t const c0 = paint(layer, level).packed(), c1 = 0;
			for (int b = 0; b < blocks; ++b)
			{
				if (bc3)
				{
					unsigned char const alpha[8] = {255, 255, 0, 0, 0, 0, 0, 0};
					out.insert(out.end(), alpha, alpha + 8);
				}
				char block[8] = {};
				std::memcpy(block, &c0, 2);
				std::memcpy(block + 2, &c1, 2);       // indices 0: every texel is colour 0
				out.insert(out.end(), block, block + 8);
			}
		}
	return out;
}

std::span<std::byte const> bytes(std::vector<char> const &v)
{
	return std::as_bytes(std::span<char const>(v));
}

/// The 64 x 64 picture of a quad over the whole target, the array sampled by test_sample_array_frag with `parameter` (layer, level, layers
/// across or 0).
std::vector<Rgba> draw(VulkanRendererPtr const &renderer, TexturePtr const &texture, rove::vec<4> const &parameter)
{
	VertexBufferPtr vb = renderer->create_vertex_buffer(VertexTraits::of<VERTEX_PT>(), sizeof(VERTEX_PT) * 4);
	auto vb_mapping = vb->write(Contents::DISCARD);
	VERTEX_PT *v = vb_mapping.as<VERTEX_PT>().data();
	float const xy[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}}, uv[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
	for (int i = 0; i < 4; ++i)
	{
		v[i].position = rove::vec<3>(xy[i][0], xy[i][1], 0);
		v[i].texcoords = rove::vec<2>(uv[i][0], uv[i][1]);
	}
	vb_mapping.unmap();
	IndexBufferPtr ib = renderer->create_index_buffer(IndexFormat::U16, sizeof(INDEX_U16) * 6);
	auto ib_mapping = ib->write(Contents::DISCARD);
	INDEX_U16 *idx = ib_mapping.as<INDEX_U16>().data();
	INDEX_U16 const q[6] = {0, 1, 2, 0, 2, 3};
	std::copy(q, q + 6, idx);
	ib_mapping.unmap();
	CameraPtr camera(new Camera());
	camera->set_orthographic_projection(2, 2, 0, 2);
	camera->look_at(rove::vec<3>(0, 0, 1), rove::vec<3>(0, 0, 0), rove::vec<3>(0, 1, 0));
	RenderTargetPtr target = renderer->create_render_target(TARGET, TARGET, PixelFormat::R32G32B32A32F);
	MaterialPtr material = renderer->create_material();
	material->set_cull_mode(Material::CullMode::NONE);
	material->set_shader_program(renderer->create_shader_program(edge::test_shader("test_frame.vert"), edge::test_shader("test_sample_array.frag")));
	material->set_diffuse_texture(texture);
	material->set_extra_parameter(0, parameter);
	DrawCallPtr call = renderer->create_draw_call();
	call->set_vertex_buffer(vb);
	call->set_index_buffer(ib);
	call->set_material(material);
	call->set_camera(camera);
	call->set_triangles_count(2);
	call->set_always_visible(true);
	call->commit();
	renderer->begin_frame();
	renderer->begin_pass(target, RenderPassClearState());
	call->execute();
	renderer->end_pass();
	renderer->finish_frame();
	TexturePtr picture = target->color_texture();
	picture->download();
	auto picture_mapping = picture->read();
	Rgba const *px = picture_mapping.as<Rgba const>().data();
	std::vector<Rgba> out(px, px + TARGET * TARGET);
	picture_mapping.unmap();
	return out;
}

/// The middle of the picture of `layer` at `level`.
Rgba sample(VulkanRendererPtr const &renderer, TexturePtr const &texture, int layer, int level)
{
	return draw(renderer, texture, rove::vec<4>(float(layer), float(level), 0, 0))[TARGET / 2 * TARGET + TARGET / 2];
}

bool near(Rgba c, Colour565 e, float tol = 0.03f)
{
	return std::fabs(c.r - e.r5 / 31.0f) < tol && std::fabs(c.g - e.g6 / 63.0f) < tol && std::fabs(c.b - e.b5 / 31.0f) < tol;
}

bool near(Rgba c, float r, float g, float b, float tol = 0.02f)
{
	return std::fabs(c.r - r) < tol && std::fabs(c.g - g) < tol && std::fabs(c.b - b) < tol;
}

std::string text(Rgba c)
{
	return std::to_string(c.r) + " " + std::to_string(c.g) + " " + std::to_string(c.b);
}

/// What `call` throws (std::runtime_error), or "" when it does not.
std::string refusal(std::function<void()> const &call)
{
	try
	{
		call();
	}
	catch (std::runtime_error const &e)
	{
		return e.what();
	}
	return "";
}

}

BOOST_FIXTURE_TEST_SUITE(texture_array, VulkanFixture)

BOOST_AUTO_TEST_CASE(every_layer_and_every_mip_level_of_an_array_dds_is_where_it_belongs)
{
	BOOST_REQUIRE_MESSAGE(renderer, "Vulkan renderer unavailable: " << error);
	std::vector<char> const file = dds(16, 16, 3, 3, 71);
	TexturePtr t = Texture::load_dds(renderer, file.data(), file.size());
	BOOST_CHECK_EQUAL(t->array_layers(), 3);
	BOOST_CHECK_EQUAL(t->mip_levels(), 3);
	BOOST_CHECK_EQUAL(t->width(), 16);
	BOOST_CHECK(!t->is_srgb());
	for (int layer = 0; layer < 3; ++layer)
		for (int level = 0; level < 3; ++level)
		{
			Rgba const c = sample(renderer, t, layer, level);
			BOOST_CHECK_MESSAGE(near(c, colour(layer, level)), "layer " << layer << " level " << level << ": " << text(c));
		}
}

BOOST_AUTO_TEST_CASE(a_dds_of_one_layer_is_still_a_plain_texture)
{
	BOOST_REQUIRE_MESSAGE(renderer, "Vulkan renderer unavailable: " << error);
	std::vector<char> const file = dds(8, 8, 2, 1, 72);
	TexturePtr t = Texture::load_dds(renderer, file.data(), file.size());
	BOOST_CHECK_EQUAL(t->array_layers(), 0);
	BOOST_CHECK_EQUAL(t->mip_levels(), 2);
	BOOST_CHECK(t->is_srgb());
}

BOOST_AUTO_TEST_CASE(an_array_assembled_from_single_dds_files_has_each_file_as_its_layer_in_order)
{
	BOOST_REQUIRE_MESSAGE(renderer, "Vulkan renderer unavailable: " << error);
	// file k paints itself as layer 2 - k would: the layers come out in the order of the files, not of anything inside them
	std::vector<std::vector<char>> files;
	for (int k = 0; k < 3; ++k)
		files.push_back(dds(16, 16, 3, 1, 71, [k](int, int level) { return colour(2 - k, level); }));
	TexturePtr t = Texture::load_dds_array(renderer, {bytes(files[0]), bytes(files[1]), bytes(files[2])});
	BOOST_CHECK_EQUAL(t->array_layers(), 3);
	BOOST_CHECK_EQUAL(t->mip_levels(), 3);
	BOOST_CHECK_EQUAL(t->pixel_format(), PixelFormat::BC1);
	BOOST_CHECK(!t->is_srgb());
	for (int layer = 0; layer < 3; ++layer)
		for (int level = 0; level < 3; ++level)
		{
			Rgba const c = sample(renderer, t, layer, level);
			BOOST_CHECK_MESSAGE(near(c, colour(2 - layer, level)), "layer " << layer << " level " << level << ": " << text(c));
		}
	// sRGB files make an sRGB array; linear ones asked to be sRGB too
	std::vector<char> const srgb_file = dds(16, 16, 3, 1, 72);
	BOOST_CHECK(Texture::load_dds_array(renderer, {bytes(srgb_file), bytes(srgb_file)})->is_srgb());
	TextureUsage srgb;
	srgb.srgb = true;
	BOOST_CHECK(Texture::load_dds_array(renderer, {bytes(files[0])}, srgb)->is_srgb());
}

BOOST_AUTO_TEST_CASE(a_layer_chosen_per_pixel_is_the_layer_sampled)
{
	BOOST_REQUIRE_MESSAGE(renderer, "Vulkan renderer unavailable: " << error);
	std::vector<char> const file = dds(8, 8, 1, 4, 71);
	TexturePtr t = Texture::load_dds(renderer, file.data(), file.size());
	std::vector<Rgba> const picture = draw(renderer, t, rove::vec<4>(0, 0, 4, 0));      // four layers across: a quarter each
	for (int quarter = 0; quarter < 4; ++quarter)
	{
		Rgba const c = picture[TARGET / 2 * TARGET + quarter * TARGET / 4 + TARGET / 8];
		BOOST_CHECK_MESSAGE(near(c, colour(quarter, 0)), "quarter " << quarter << ": " << text(c));
	}
}

BOOST_AUTO_TEST_CASE(an_array_of_one_file_is_an_array_of_one_layer)
{
	BOOST_REQUIRE_MESSAGE(renderer, "Vulkan renderer unavailable: " << error);
	std::vector<char> const file = dds(8, 8, 1, 1, 71);
	TexturePtr t = Texture::load_dds_array(renderer, {bytes(file)});
	BOOST_CHECK_EQUAL(t->array_layers(), 1);
	Rgba const c = sample(renderer, t, 0, 0);
	BOOST_CHECK_MESSAGE(near(c, colour(0, 0)), text(c));
}

BOOST_AUTO_TEST_CASE(files_that_do_not_agree_with_the_first_are_refused_naming_the_file)
{
	BOOST_REQUIRE_MESSAGE(renderer, "Vulkan renderer unavailable: " << error);
	std::vector<char> const good = dds(16, 16, 3, 1, 71);
	std::vector<std::pair<char const *, std::vector<char>>> const odd = {
		{"wider", dds(32, 16, 3, 1, 71)},
		{"taller", dds(16, 32, 3, 1, 71)},
		{"fewer levels", dds(16, 16, 2, 1, 71)},
		{"another format", dds(16, 16, 3, 1, 77)},
		{"sRGB", dds(16, 16, 3, 1, 72)},
		{"an array itself", dds(16, 16, 3, 2, 71)},
	};
	for (auto const &[what, file] : odd)
	{
		std::string const e = refusal([&] { Texture::load_dds_array(renderer, {bytes(good), bytes(good), bytes(file)}); });
		BOOST_CHECK_MESSAGE(!e.empty(), what << ": accepted");
		BOOST_CHECK_MESSAGE(e.find("file 2") != std::string::npos, what << ": the error does not name file 2: " << e);
	}
	BOOST_CHECK(!refusal([&] { Texture::load_dds_array(renderer, {}); }).empty());
	std::vector<char> truncated = good;
	truncated.resize(truncated.size() - 8);
	std::string const e = refusal([&] { Texture::load_dds_array(renderer, {bytes(good), bytes(truncated)}); });
	BOOST_CHECK_MESSAGE(e.find("file 1") != std::string::npos, "a short file: " << e);
	// the first file is where the others are measured from: an array as the first is refused too
	std::vector<char> const array = dds(16, 16, 3, 2, 71);
	BOOST_CHECK(refusal([&] { Texture::load_dds_array(renderer, {bytes(array), bytes(good)}); }).find("file 0") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(an_array_made_by_hand_takes_every_layer_through_lock_and_makes_the_mips_of_each)
{
	BOOST_REQUIRE_MESSAGE(renderer, "Vulkan renderer unavailable: " << error);
	TextureUsage usage;
	usage.array_layers = 3;
	usage.mipmaps = true;
	int const side = 8;
	TexturePtr t = renderer->create_texture(side, side, PixelFormat::A8R8G8B8, usage);
	BOOST_CHECK_EQUAL(t->array_layers(), 3);
	BOOST_CHECK_EQUAL(t->mip_levels(), 4);
	// layer k all one colour: BGRA bytes, red, green, blue
	uint32_t const texel[3] = {0xFFFF0000u, 0xFF00FF00u, 0xFF0000FFu};
	auto t_mapping = t->write(Contents::DISCARD);
	auto *p = t_mapping.as<uint32_t>().data();
	BOOST_REQUIRE(p);
	for (int layer = 0; layer < 3; ++layer)
		for (int i = 0; i < side * side; ++i)
			p[layer * side * side + i] = texel[layer];
	t_mapping.unmap();
	float const expect[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
	for (int layer = 0; layer < 3; ++layer)
		for (int level : {0, 1, 3})
		{
			Rgba const c = sample(renderer, t, layer, level);
			BOOST_CHECK_MESSAGE(near(c, expect[layer][0], expect[layer][1], expect[layer][2]), "layer " << layer << " level " << level << ": " << text(c));
		}
}

BOOST_AUTO_TEST_CASE(an_array_does_not_pass_for_one_image)
{
	BOOST_REQUIRE_MESSAGE(renderer, "Vulkan renderer unavailable: " << error);
	TextureUsage usage;
	usage.array_layers = 2;
	TexturePtr t = renderer->create_texture(4, 4, PixelFormat::A8R8G8B8, usage);
	BOOST_CHECK_THROW(t->download(), std::logic_error);
	BOOST_CHECK_THROW(t->pixels<rove::bgra<uint8_t>>(), std::logic_error);
	BOOST_CHECK_THROW(read_image<rove::bgra<uint8_t>>(*t), std::logic_error);
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_CASE(an_array_without_a_gpu_holds_every_layer_and_a_negative_count_is_refused)
{
	RendererPtr renderer(new NullRenderer(320, 200));
	std::vector<char> const file = dds(16, 16, 3, 4, 71);
	TexturePtr t = Texture::load_dds(renderer, file.data(), file.size());
	BOOST_CHECK_EQUAL(t->array_layers(), 4);
	// the CPU copy is the file's data, all four layers of it
	auto t_mapping = t->read();
	auto const *held = t_mapping.as<char const>().data();
	BOOST_REQUIRE(held);
	BOOST_CHECK(std::memcmp(held, file.data() + 148, file.size() - 148) == 0);
	t_mapping.unmap();
	TextureUsage bad;
	bad.array_layers = -1;
	BOOST_CHECK_THROW(renderer->create_texture(4, 4, PixelFormat::A8R8G8B8, bad), std::invalid_argument);
}
