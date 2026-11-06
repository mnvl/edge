// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

// Contract of DDS textures: a block-compressed file loads with the format and
// every mip level it holds (each level lands where it belongs: the test
// assets give each level its own colour), sRGB when the file says so, BC5
// keeps its two channels, a short file is refused, and the glTF loader
// prefers MSFT_texture_dds images.  Assets: test/make_test_dds.py.

#include <boost/test/unit_test.hpp>
#include <chrono>
#include <cstring>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

#include "camera.h"
#include "draw_call.h"
#include "material.h"
#include "model_builder.h"
#include "null_renderer.h"
#include "pixel.h"
#include "render_target.h"
#include "scheduler.h"
#include "shader_program.h"
#include "test_fixture.h"
#include "test_backends.h"
#include "test_vulkan_fixture.h"
#include "texture.h"
#include "vertex.h"
#include "vertex_traits.h"

using namespace edge;

namespace
{

bool supports_astc(VulkanRendererPtr const &renderer)
{
	VkFormatProperties properties;
	vkGetPhysicalDeviceFormatProperties(renderer->device_context()->physical_device(), VK_FORMAT_ASTC_6x6_SRGB_BLOCK, &properties);
	return (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) != 0;
}

#ifdef EDGE_USE_GLES
bool supports_astc(GLESRendererPtr const &renderer)
{
	return renderer->context()->has_extension("GL_KHR_texture_compression_astc_ldr");
}
#endif

#ifdef EDGE_USE_METAL
bool supports_astc(MetalRendererPtr const &renderer)
{
	return renderer->context()->supports_astc();
}
#endif

std::vector<char> read(std::string const &name)
{
	std::ifstream in(TestFixture::graphic_sources_path() + "test/data/" + name, std::ios::binary);
	BOOST_REQUIRE_MESSAGE(in, "no " << name);
	return std::vector<char>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

struct Rgba
{
	float r, g, b, a;
};

// The centre pixel of a 64 x 64 target after drawing a quad `pixels` wide
// with the texture.
Rgba sample(RendererPtr const &renderer, TexturePtr const &texture, float pixels)
{
	float const e = pixels / 64.0f;
	VertexBufferPtr vb = renderer->create_vertex_buffer(VertexTraits::of<VERTEX_PT>(), sizeof(VERTEX_PT) * 4);
	auto vb_mapping = vb->write(Contents::DISCARD);
	VERTEX_PT *v = vb_mapping.as<VERTEX_PT>().data();
	float const xy[4][2] = {{-e, -e}, {e, -e}, {e, e}, {-e, e}}, uv[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
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
	RenderTargetPtr target = renderer->create_render_target(64, 64, PixelFormat::R32G32B32A32F);
	MaterialPtr material = renderer->create_material();
	material->set_cull_mode(Material::CullMode::NONE);
	material->set_shader_program(renderer->create_shader_program(edge::test_shader("test_frame.vert"), edge::test_shader("test_sample.frag")));
	material->set_diffuse_texture(texture);
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
	TexturePtr colour = target->color_texture();
	colour->download();
	auto colour_mapping = colour->read();
	Rgba const *px = colour_mapping.as<Rgba const>().data();
	Rgba const c = px[32 * 64 + 32];
	colour_mapping.unmap();
	return c;
}

bool near(Rgba c, float r, float g, float b, float tol = 0.03f)
{
	return std::fabs(c.r - r) < tol && std::fabs(c.g - g) < tol && std::fabs(c.b - b) < tol;
}

}

BOOST_AUTO_TEST_SUITE(dds)

BOOST_AUTO_TEST_CASE_TEMPLATE(every_mip_level_of_a_bc7_file_is_where_it_belongs, F, edge::Backends)
{
	F fixture;
	auto const &renderer = fixture.renderer;
	BOOST_REQUIRE_MESSAGE(renderer, "renderer unavailable: " << fixture.error);
	std::vector<char> const file = read("test_dds_bc7_mips.dds");
	BOOST_REQUIRE(Texture::is_dds(file.data(), file.size()));
	TexturePtr t = Texture::load_dds(renderer, file.data(), file.size());
	BOOST_CHECK_EQUAL(t->pixel_format(), PixelFormat::BC7);
	BOOST_CHECK_EQUAL(t->mip_levels(), 4);
	BOOST_CHECK(t->is_srgb());                       // DXGI 99: BC7 sRGB
	// 8 texels over 32, 4 and 2 pixels: levels 0 (magnified), 1 and 2
	Rgba const big = sample(renderer, t, 32), mid = sample(renderer, t, 4), small = sample(renderer, t, 2);
	BOOST_CHECK_MESSAGE(near(big, 1, 0, 0), "level 0: " << big.r << " " << big.g << " " << big.b);
	BOOST_CHECK_MESSAGE(near(mid, 0, 1, 0), "level 1: " << mid.r << " " << mid.g << " " << mid.b);
	BOOST_CHECK_MESSAGE(near(small, 0, 0, 1), "level 2: " << small.r << " " << small.g << " " << small.b);
}

BOOST_AUTO_TEST_CASE_TEMPLATE(bc5_keeps_two_linear_channels_even_when_srgb_is_asked, F, edge::Backends)
{
	F fixture;
	auto const &renderer = fixture.renderer;
	BOOST_REQUIRE_MESSAGE(renderer, "renderer unavailable: " << fixture.error);
	std::vector<char> const file = read("test_dds_bc5.dds");
	TextureUsage srgb;
	srgb.srgb = true;
	TexturePtr t = Texture::load_dds(renderer, file.data(), file.size(), srgb);
	BOOST_CHECK_EQUAL(t->pixel_format(), PixelFormat::BC5);
	BOOST_CHECK(!t->is_srgb());
	Rgba const c = sample(renderer, t, 64);
	BOOST_CHECK_MESSAGE(std::fabs(c.r - 200 / 255.0f) < 0.02f && std::fabs(c.g - 50 / 255.0f) < 0.02f,
		"BC5 sampled as " << c.r << " " << c.g);
}

// ASTC (phones lack BC).  Sizes and the file's header need no GPU.
BOOST_AUTO_TEST_CASE(astc_blocks_are_sixteen_bytes_of_four_six_or_eight_texels)
{
	BOOST_CHECK_EQUAL(pixel_data_size(8, 8, PixelFormat::ASTC_4x4), 4u * 16);
	BOOST_CHECK_EQUAL(pixel_data_size(12, 12, PixelFormat::ASTC_6x6), 4u * 16);
	BOOST_CHECK_EQUAL(pixel_data_size(13, 7, PixelFormat::ASTC_6x6), 3u * 2 * 16);   // partial blocks round up
	BOOST_CHECK_EQUAL(pixel_data_size(16, 16, PixelFormat::ASTC_8x8), 4u * 16);
	BOOST_CHECK_EQUAL(pixel_data_size(1, 1, PixelFormat::ASTC_8x8), 16u);
	BOOST_CHECK_EQUAL(pixel_data_size(5, 5, PixelFormat::BC7), 4u * 16);              // BC unchanged
	BOOST_CHECK_EQUAL(pixel_data_size(5, 5, PixelFormat::BC1), 4u * 8);
	// all the levels: 12x12 over four 6x6 blocks, then 6x6, 3x3 and 1x1 one block each
	BOOST_CHECK_EQUAL(Texture::levels_size(12, 12, PixelFormat::ASTC_6x6, 4), (4u + 1 + 1 + 1) * 16);
	BOOST_CHECK(is_block_compressed(PixelFormat::ASTC_6x6));
	BOOST_CHECK(!is_block_compressed(PixelFormat::A8R8G8B8));
}

BOOST_AUTO_TEST_CASE(an_astc_dds_loads_with_its_block_size_levels_and_srgb)
{
	RendererPtr null(new NullRenderer(64, 64));
	struct { char const *file; PixelFormat format; int side; } const cases[] = {
		{"test_dds_astc4x4_mips.dds", PixelFormat::ASTC_4x4, 8},
		{"test_dds_astc6x6_mips.dds", PixelFormat::ASTC_6x6, 12},
		{"test_dds_astc8x8_mips.dds", PixelFormat::ASTC_8x8, 16},
	};
	for (auto const &c : cases)
	{
		std::vector<char> const file = read(c.file);
		TexturePtr t = Texture::load_dds(null, file.data(), file.size());
		BOOST_CHECK_EQUAL(t->pixel_format(), c.format);
		BOOST_CHECK_EQUAL(t->width(), c.side);
		BOOST_CHECK_EQUAL(t->mip_levels(), 4);
		BOOST_CHECK_MESSAGE(t->is_srgb(), c.file << " is an sRGB DXGI format");
		// level 0 of the file's first block is what lies at the start of the texture's data
		auto t_mapping = t->read();
		BOOST_REQUIRE(t_mapping.data());
		auto t_mapping2 = t->read();
		BOOST_CHECK(std::memcmp(t_mapping2.data(), file.data() + 148, 16) == 0);
		t_mapping2.unmap();
		BOOST_CHECK_THROW(Texture::load_dds(null, file.data(), file.size() - 1), std::runtime_error);
	}
}

BOOST_AUTO_TEST_CASE(an_astc_dds_of_a_block_size_edge_lacks_is_refused)
{
	RendererPtr null(new NullRenderer(64, 64));
	std::vector<char> file = read("test_dds_astc6x6_mips.dds");
	uint32_t const astc_5x5 = 141;
	std::memcpy(file.data() + 128, &astc_5x5, 4);
	BOOST_CHECK_THROW(Texture::load_dds(null, file.data(), file.size()), std::runtime_error);
}

// A GPU either samples the ASTC levels as the file holds them or the texture refuses to exist: never a texture that is wrong
BOOST_AUTO_TEST_CASE_TEMPLATE(an_astc_texture_is_sampled_per_level_or_refused_by_a_gpu_without_astc, F, edge::Backends)
{
	F fixture;
	auto const &renderer = fixture.renderer;
	BOOST_REQUIRE_MESSAGE(renderer, "renderer unavailable: " << fixture.error);
	std::vector<char> const file = read("test_dds_astc6x6_mips.dds");
	if (!supports_astc(fixture.renderer))
	{
		try
		{
			Texture::load_dds(renderer, file.data(), file.size());
			BOOST_FAIL("a GPU without ASTC made an ASTC texture");
		}
		catch (std::runtime_error const &e)
		{
			BOOST_CHECK_MESSAGE(std::string(e.what()).find("ASTC") != std::string::npos, "the error does not say ASTC: " << e.what());
		}
		return;
	}
	TexturePtr t = Texture::load_dds(renderer, file.data(), file.size());
	Rgba const big = sample(renderer, t, 32), mid = sample(renderer, t, 6), small = sample(renderer, t, 3);
	BOOST_CHECK_MESSAGE(near(big, 1, 0, 0), "level 0: " << big.r << " " << big.g << " " << big.b);
	BOOST_CHECK_MESSAGE(near(mid, 0, 1, 0), "level 1: " << mid.r << " " << mid.g << " " << mid.b);
	BOOST_CHECK_MESSAGE(near(small, 0, 0, 1), "level 2: " << small.r << " " << small.g << " " << small.b);
}

BOOST_AUTO_TEST_CASE_TEMPLATE(a_short_or_foreign_file_is_refused, F, edge::Backends)
{
	F fixture;
	auto const &renderer = fixture.renderer;
	BOOST_REQUIRE_MESSAGE(renderer, "renderer unavailable: " << fixture.error);
	std::vector<char> const file = read("test_dds_truncated.dds");
	BOOST_CHECK_THROW(Texture::load_dds(renderer, file.data(), file.size()), std::runtime_error);
	std::vector<char> const png = read("test_white.png");
	BOOST_CHECK(!Texture::is_dds(png.data(), png.size()));
	BOOST_CHECK_THROW(Texture::load_dds(renderer, png.data(), png.size()), std::runtime_error);
}

/// A cube map is six faces, which no texture here holds: refused as what it is, whichever header says so, not for a byte count.
BOOST_AUTO_TEST_CASE_TEMPLATE(a_cube_map_is_refused_as_one, F, edge::Backends)
{
	F fixture;
	auto const &renderer = fixture.renderer;
	BOOST_REQUIRE_MESSAGE(renderer, "renderer unavailable: " << fixture.error);
	auto header = [](size_t size) {
		std::vector<char> dds(size);
		std::memcpy(dds.data(), "DDS ", 4);
		auto put = [&](size_t at, uint32_t v) { std::memcpy(dds.data() + at, &v, 4); };
		put(4, 124);
		put(12, 4);
		put(16, 4);
		put(28, 1);
		put(76, 32);
		put(80, 4);                  // DDPF_FOURCC
		return dds;
	};
	auto refused_as_a_cube = [&](std::vector<char> const &dds) {
		try
		{
			Texture::load_dds(renderer, dds.data(), dds.size());
			BOOST_ERROR("a cube map was loaded");
		}
		catch (std::runtime_error const &e)
		{
			BOOST_CHECK_MESSAGE(std::string(e.what()).find("cube") != std::string::npos, e.what());
		}
	};
	std::vector<char> dx10 = header(148 + 6 * 16);             // six 4x4 BC7 faces
	std::memcpy(dx10.data() + 84, "DX10", 4);
	uint32_t const dx10_fields[] = {98, 3, 0x4, 1};             // BC7, 2D, RESOURCE_MISC_TEXTURECUBE, one cube
	std::memcpy(dx10.data() + 128, dx10_fields, sizeof(dx10_fields));
	refused_as_a_cube(dx10);
	std::vector<char> legacy = header(128 + 6 * 8);             // six 4x4 DXT1 faces
	std::memcpy(legacy.data() + 84, "DXT1", 4);
	uint32_t const caps2 = 0x200 | 0xFC00;                      // DDSCAPS2_CUBEMAP and its six faces
	std::memcpy(legacy.data() + 112, &caps2, 4);
	refused_as_a_cube(legacy);
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_CASE(the_gltf_loader_prefers_msft_texture_dds)
{
	RendererPtr renderer(new NullRenderer(320, 200));
	SchedulerPtr scheduler(new Scheduler(renderer));
	auto model = std::make_shared<ModelBuilder>(renderer, scheduler);
	GltfOptions o;
	o.compress_textures = false;
	o.srgb_color = true;
	o.mipmaps = true;
	model->load_from_gltf(TestFixture::graphic_sources_path() + "test/data/test_gltf_loader_dds.glb", o);
	BOOST_REQUIRE_EQUAL(model->materials().size(), 1u);
	TexturePtr t = model->materials().front()->diffuse_texture();
	BOOST_REQUIRE(t);
	BOOST_CHECK_EQUAL(t->pixel_format(), PixelFormat::BC7);
	BOOST_CHECK_EQUAL(t->mip_levels(), 4);       // the file's, not made
	BOOST_CHECK(t->is_srgb());
}

// A 4K texture is 22 MB with its mips and a level has several: reading the file a byte at a time cost over a second
// each (20 MB/s) where the upload takes tens of milliseconds.  8192 x 8192 BC7 is 64 MB.  A timing, so not in the default run
// (it fails under load): the target edge_performance_tests runs it.
BOOST_AUTO_TEST_SUITE(dds_speed)

BOOST_AUTO_TEST_CASE(a_large_external_dds_is_read_at_disk_speed)
{
	std::filesystem::path const dir = edge::unique_temp_directory("edge_test_large_dds_");
	int const side = 8192;
	std::vector<char> dds(148 + size_t(side) * side);
	auto put = [&](size_t at, uint32_t v) { std::memcpy(dds.data() + at, &v, 4); };
	std::memcpy(dds.data(), "DDS ", 4);
	put(4, 124);
	put(12, side);
	put(16, side);
	put(28, 1);
	put(76, 32);
	put(80, 4);                  // DDPF_FOURCC
	std::memcpy(dds.data() + 84, "DX10", 4);
	put(128, 98);                // BC7
	put(132, 3);                 // 2D
	put(140, 1);
	{
		std::ofstream out(dir / "big.dds", std::ios::binary);
		out.write(dds.data(), std::streamsize(dds.size()));
		BOOST_REQUIRE(out);
	}
	{
		// one triangle (the buffer is inline) with a material whose colour is big.dds
		std::ofstream out(dir / "big.gltf");
		out << R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],"nodes":[{"mesh":0}],
			"meshes":[{"primitives":[{"attributes":{"POSITION":0,"TEXCOORD_0":1},"material":0}]}],
			"materials":[{"pbrMetallicRoughness":{"baseColorTexture":{"index":0}}}],
			"textures":[{"source":0}],"images":[{"uri":"big.dds"}],
			"buffers":[{"byteLength":60,"uri":"data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"}],
			"bufferViews":[{"buffer":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":24}],
			"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},
				{"bufferView":1,"componentType":5126,"count":3,"type":"VEC2"}]})";
		BOOST_REQUIRE(out);
	}
	RendererPtr renderer(new NullRenderer(320, 200));
	SchedulerPtr scheduler(new Scheduler(renderer));
	auto model = std::make_shared<ModelBuilder>(renderer, scheduler);
	GltfOptions o;
	o.compress_textures = false;
	o.mipmaps = false;
	auto const start = std::chrono::steady_clock::now();
	model->load_from_gltf((dir / "big.gltf").string(), o);
	double const ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
	std::filesystem::remove_all(dir);
	BOOST_REQUIRE_EQUAL(model->materials().size(), 1u);
	TexturePtr t = model->materials().front()->diffuse_texture();
	BOOST_REQUIRE(t);
	BOOST_CHECK_EQUAL(t->width(), side);
	BOOST_CHECK_EQUAL(t->pixel_format(), PixelFormat::BC7);
	BOOST_CHECK_MESSAGE(ms < 1000, "64 MB DDS took " << ms << " ms");
}

BOOST_AUTO_TEST_SUITE_END()
