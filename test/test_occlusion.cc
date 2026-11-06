// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

// Occlusion maps: glTF's occlusionTexture packed into the red of the metallic-roughness texture (glTF's ORM: occlusion red, roughness
// green, metallic blue), as the 16 texture units OpenGL ES promises are spent; a material says whether it has occlusion, and how strong,
// and edge_material.glsl's surface makes it.  What cannot be packed (another set of UVs, a DDS) is refused, naming the file: never left
// out but where asked (GltfOptions::occlusion).  A .edge file keeps it from format 11; a file of format 10 loads, without occlusion.
// The assets are made by data/make_test_occlusion.py; test_format10_pbr.edge by edge_convert of format 10 (edge 1e9c88a) of
// test_gltf_loader_pbr.glb.

#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <boost/test/unit_test.hpp>

#include "camera.h"
#include "draw_call.h"
#include "gltf_loader.h"
#include "material.h"
#if defined(EDGE_TEST_ASSET_TOOLS)
#include "model_convert.h"
#endif
#include "null_renderer.h"
#include "pixel.h"
#include "render_target.h"
#include "scheduler.h"
#include "serialized_model.h"
#include "shader_interface.h"
#include "static_model.h"
#include "test_backends.h"
#include "test_fixture.h"
#include "texture.h"
#include "vertex.h"
#include "vertex_traits.h"

using namespace edge;

namespace
{

std::string asset(std::string const &name)
{
	return TestFixture::graphic_sources_path() + "test/data/" + name;
}

/// A file of its own in a directory of the test's own
std::string out(std::string const &name)
{
	static std::filesystem::path const dir = [] {
		std::string pattern = (std::filesystem::temp_directory_path() / "edge_occlusion_XXXXXX").string();
		if (!mkdtemp(pattern.data()))
			throw std::runtime_error("no directory for the test's files");
		return std::filesystem::path(pattern);
	}();
	return (dir / name).string();
}

GltfOptions uncompressed()
{
	GltfOptions options;
	options.compress_textures = false;
	return options;
}

std::shared_ptr<StaticModelPTBNT> loaded(RendererPtr const &renderer, std::string const &file, GltfOptions const &options = uncompressed())
{
	auto model = std::make_shared<StaticModelPTBNT>(renderer, std::make_shared<Scheduler>(renderer));
	model->load_from_gltf(asset(file), options);
	return model;
}

using Bgra = rove::bgra<unsigned char>;

/// The texels of a texture as (r, g, b, a), rows top first
std::vector<std::vector<rove::vec<4, int>>> texels(TexturePtr const &texture)
{
	ImageView<Bgra const> const view = std::as_const(*texture).pixels<Bgra>();
	std::vector<std::vector<rove::vec<4, int>>> rows(view.height());
	for (int y = 0; y < view.height(); ++y)
		for (int x = 0; x < view.width(); ++x)
		{
			Bgra const &t = view.at(x, y);
			rows[y].push_back(rove::vec<4, int>(t.r, t.g, t.b, t.a));
		}
	return rows;
}

// make_test_occlusion.py's images, (r, g, b): ORM and MR 2 x 2, OCCLUSION 4 x 4 (red 16 * (4 y + x), green 7, blue 9)
int const ORM[2][2][3] = {{{64, 128, 192}, {32, 96, 160}}, {{16, 80, 144}, {8, 72, 136}}};
int const MR[2][2][3] = {{{0, 0, 255}, {0, 200, 0}}, {{0, 100, 40}, {0, 40, 100}}};

/// What the GPU samples of a 2 x 2 texture that repeats at the centre of texel (x, y) of a 4 x 4 one, channel c: bilinear, each
/// coordinate (x + 0.5) / 4 * 2 - 0.5 between the two texels about it, the first's weight 0.75 or 0.25 by hand: x 0 and 1 are
/// 0.75 of texel 0 and 0.25 of texel 1 (x 0 reaches across the edge to texel 1, which repeats), x 2 and 3 the other way round.
double sampled(int const (&image)[2][2][3], int x, int y, int c)
{
	double const w[4][2] = {{0.75, 0.25}, {0.75, 0.25}, {0.25, 0.75}, {0.25, 0.75}};
	double v = 0;
	for (int j = 0; j < 2; ++j)
		for (int i = 0; i < 2; ++i)
			v += w[y][j] * w[x][i] * image[j][i][c];
	return v;
}

MaterialPtr material_named(Model const &model, std::string const &name)
{
	for (MaterialPtr const &m : model.materials())
		if (m->name() == name)
			return m;
	throw std::runtime_error("no material " + name);
}

void check_refused(std::string const &file, std::string const &what)
{
	auto renderer = std::make_shared<NullRenderer>(4, 4);
	try
	{
		loaded(renderer, file);
		BOOST_ERROR(file << ": loaded, where its occlusion cannot be packed (" << what << ")");
	}
	catch (GltfLoader::load_error const &e)
	{
		std::string const message = e.what();
		BOOST_CHECK_MESSAGE(message.find(file) != std::string::npos && message.find("occlusion") != std::string::npos,
			"the error names the file and the occlusion: " << message);
	}
	// asked to leave occlusion out, it loads without
	GltfOptions without = uncompressed();
	without.occlusion = false;
	auto const model = loaded(renderer, file, without);
	for (MaterialPtr const &m : model->materials())
		BOOST_CHECK_MESSAGE(!m->occlusion(), file << " without occlusion: " << m->name() << " has it");
}

}

BOOST_AUTO_TEST_SUITE(occlusion)

BOOST_AUTO_TEST_CASE(a_material_has_no_occlusion_until_told_and_a_strength_from_0_to_1)
{
	auto renderer = std::make_shared<NullRenderer>(4, 4);
	MaterialPtr const material = renderer->create_material();
	BOOST_CHECK(!material->occlusion());
	BOOST_CHECK_EQUAL(material->occlusion_strength(), 1.0f);     // glTF's
	material->set_occlusion(true);
	material->set_occlusion_strength(0.375f);
	for (float bad : {-0.001f, 1.001f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()})
		BOOST_CHECK_THROW(material->set_occlusion_strength(bad), std::invalid_argument);
	BOOST_CHECK_EQUAL(material->occlusion_strength(), 0.375f);
	MaterialPtr const clone = material->clone();
	BOOST_CHECK(clone->occlusion());
	BOOST_CHECK_EQUAL(clone->occlusion_strength(), 0.375f);
}

BOOST_AUTO_TEST_CASE(an_orm_image_is_the_metallic_roughness_texture_as_it_is)
{
	auto renderer = std::make_shared<NullRenderer>(4, 4);
	auto const model = loaded(renderer, "test_occlusion.gltf");
	MaterialPtr const orm = material_named(*model, "orm");
	BOOST_CHECK(orm->occlusion());
	BOOST_CHECK_EQUAL(orm->occlusion_strength(), 0.5f);
	TexturePtr const texture = orm->metallic_roughness_texture();
	BOOST_REQUIRE(texture);
	auto const got = texels(texture);
	BOOST_REQUIRE_EQUAL(got.size(), 2u);
	for (int y = 0; y < 2; ++y)
		for (int x = 0; x < 2; ++x)
			BOOST_CHECK_MESSAGE(got[y][x].x == ORM[y][x][0] && got[y][x].y == ORM[y][x][1] && got[y][x].z == ORM[y][x][2],
				"texel " << x << ", " << y << ": " << got[y][x].x << " " << got[y][x].y << " " << got[y][x].z);
}

BOOST_AUTO_TEST_CASE(occlusion_of_another_image_is_packed_into_red_the_smaller_image_sampled_as_the_gpu_would)
{
	auto renderer = std::make_shared<NullRenderer>(4, 4);
	auto const model = loaded(renderer, "test_occlusion.gltf");
	MaterialPtr const separate = material_named(*model, "separate");
	BOOST_CHECK(separate->occlusion());
	BOOST_CHECK_EQUAL(separate->occlusion_strength(), 1.0f);
	TexturePtr const texture = separate->metallic_roughness_texture();
	BOOST_REQUIRE(texture);
	BOOST_REQUIRE_EQUAL(texture->width(), 4);
	BOOST_REQUIRE_EQUAL(texture->height(), 4);
	auto const got = texels(texture);
	for (int y = 0; y < 4; ++y)
		for (int x = 0; x < 4; ++x)
		{
			BOOST_CHECK_MESSAGE(got[y][x].x == 16 * (4 * y + x), "occlusion at " << x << ", " << y << ": " << got[y][x].x);
			for (int c = 1; c < 3; ++c)
			{
				double const want = sampled(MR, x, y, c);
				BOOST_CHECK_MESSAGE(std::abs(got[y][x][c] - want) <= 0.5 + 1e-9,
					(c == 1 ? "roughness" : "metallic") << " at " << x << ", " << y << ": " << got[y][x][c] << ", want " << want);
			}
		}

	// the same two images: the same texture; the metallic-roughness image alone stays as it was
	BOOST_CHECK(material_named(*model, "separate_again")->metallic_roughness_texture() == texture);
	BOOST_CHECK(material_named(*model, "mr_of_orm")->metallic_roughness_texture() ==
		material_named(*model, "orm")->metallic_roughness_texture());
	MaterialPtr const plain = material_named(*model, "plain");
	BOOST_CHECK(!plain->occlusion());
	auto const unpacked = texels(plain->metallic_roughness_texture());
	BOOST_REQUIRE_EQUAL(unpacked.size(), 2u);
	BOOST_CHECK(unpacked[0][1].y == 200 && unpacked[1][0].z == 40);
}

BOOST_AUTO_TEST_CASE(a_smaller_occlusion_image_is_sampled_as_the_gpu_would_to_the_metallic_roughness_images_size)
{
	auto renderer = std::make_shared<NullRenderer>(4, 4);
	auto const model = loaded(renderer, "test_occlusion.gltf");
	MaterialPtr const small = material_named(*model, "small_occlusion");
	BOOST_CHECK(small->occlusion());
	TexturePtr const texture = small->metallic_roughness_texture();
	BOOST_REQUIRE(texture);
	BOOST_REQUIRE_EQUAL(texture->width(), 4);
	BOOST_REQUIRE_EQUAL(texture->height(), 4);
	auto const got = texels(texture);
	for (int y = 0; y < 4; ++y)
		for (int x = 0; x < 4; ++x)
		{
			double const want = sampled(ORM, x, y, 0);
			BOOST_CHECK_MESSAGE(std::abs(got[y][x].x - want) <= 0.5 + 1e-9, "occlusion at " << x << ", " << y << ": " << got[y][x].x
				<< ", want " << want);
			BOOST_CHECK_MESSAGE(got[y][x].y == 7 && got[y][x].z == 9, "roughness and metallic at " << x << ", " << y << ": "
				<< got[y][x].y << " " << got[y][x].z);
		}
}

BOOST_AUTO_TEST_CASE(occlusion_without_a_metallic_roughness_texture_leaves_its_factors_alone)
{
	auto renderer = std::make_shared<NullRenderer>(4, 4);
	auto const model = loaded(renderer, "test_occlusion.gltf");
	MaterialPtr const only = material_named(*model, "occlusion_only");
	BOOST_CHECK(only->occlusion());
	BOOST_CHECK_EQUAL(only->occlusion_strength(), 0.25f);
	TexturePtr const texture = only->metallic_roughness_texture();
	BOOST_REQUIRE(texture);
	auto const got = texels(texture);
	BOOST_REQUIRE_EQUAL(got.size(), 4u);
	for (int y = 0; y < 4; ++y)
		for (int x = 0; x < 4; ++x)
			BOOST_CHECK_MESSAGE(got[y][x].x == 16 * (4 * y + x) && got[y][x].y == 255 && got[y][x].z == 255,
				"texel " << x << ", " << y << ": " << got[y][x].x << " " << got[y][x].y << " " << got[y][x].z);
}

BOOST_AUTO_TEST_CASE(packed_occlusion_is_compressed_as_the_loader_compresses_textures)
{
	auto renderer = std::make_shared<NullRenderer>(4, 4);
	auto const model = loaded(renderer, "test_occlusion.gltf", GltfOptions());     // compress_textures: BC1/BC3 at load
	TexturePtr const texture = material_named(*model, "separate")->metallic_roughness_texture();
	BOOST_REQUIRE(texture);
	BOOST_CHECK(texture->pixel_format() == PixelFormat::BC1 || texture->pixel_format() == PixelFormat::BC3);
	BOOST_CHECK(material_named(*model, "separate")->occlusion());
}

BOOST_AUTO_TEST_CASE(occlusion_that_cannot_be_packed_is_refused_naming_the_file_unless_left_out)
{
	check_refused("test_occlusion_uv1.gltf", "it reads other UVs");
	check_refused("test_occlusion_dds.gltf", "a DDS beside a picture");
	check_refused("test_occlusion_strength.gltf", "a strength of 1.5");
}

BOOST_AUTO_TEST_CASE(a_gltf_without_occlusion_loads_as_it_did)
{
	auto renderer = std::make_shared<NullRenderer>(4, 4);
	auto const model = loaded(renderer, "test_gltf_loader_pbr.glb");
	MaterialPtr const pbr = material_named(*model, "pbr");
	BOOST_CHECK(!pbr->occlusion());
	auto const got = texels(pbr->metallic_roughness_texture());
	BOOST_CHECK(got[0][0].x == 0 && got[0][0].y == 192 && got[0][0].z == 128);
}

BOOST_AUTO_TEST_CASE(a_format_10_file_loads_without_occlusion)
{
	SerializedModel const serialized = Model::read_file(asset("test_format10_pbr.edge"));
	BOOST_CHECK_EQUAL(serialized.version, 10u);
	BOOST_REQUIRE_EQUAL(serialized.materials.size(), 2u);
	auto renderer = std::make_shared<NullRenderer>(4, 4);
	auto const model = std::make_shared<StaticModelPTBNT>(renderer, std::make_shared<Scheduler>(renderer));
	model->load_from_file(asset("test_format10_pbr.edge"));
	MaterialPtr const pbr = material_named(*model, "pbr");
	BOOST_CHECK(!pbr->occlusion());
	BOOST_CHECK_EQUAL(pbr->occlusion_strength(), 1.0f);
	BOOST_CHECK(pbr->metallic_roughness_texture());
	BOOST_CHECK_EQUAL(pbr->metallic_factor(), 0.25f);
	BOOST_CHECK_EQUAL(pbr->roughness_factor(), 0.5f);
	BOOST_CHECK_EQUAL(pbr->alpha_cutoff(), 0.3f);
	BOOST_CHECK(pbr->normal_map_texture() && pbr->emissive_texture() && pbr->diffuse_textures_count() == 1);
	BOOST_CHECK(!material_named(*model, "plain")->occlusion());
	// saved again, it is of this format, and the same
	model->save_to_file(out("again.edge"));
	SerializedModel const again = Model::read_file(out("again.edge"));
	BOOST_CHECK_EQUAL(again.version, MODEL_BINARY_VERSION);
	BOOST_CHECK(MODEL_BINARY_VERSION > 10u);
	BOOST_CHECK(!again.materials[0].occlusion && !again.materials[1].occlusion);
}

BOOST_AUTO_TEST_CASE(a_file_of_a_format_not_read_is_refused)
{
	// format 10 as 9, and as the format after this one
	std::ifstream in(asset("test_format10_pbr.edge"), std::ios::binary);
	std::vector<char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	for (std::uint32_t version : {9u, MODEL_BINARY_VERSION + 1})
	{
		std::memcpy(bytes.data() + 4, &version, 4);
		std::string const file = out("format_" + std::to_string(version) + ".edge");
		std::ofstream(file, std::ios::binary).write(bytes.data(), std::streamsize(bytes.size()));
		try
		{
			Model::read_file(file);
			BOOST_ERROR("a file of format " << version << " read");
		}
		catch (std::runtime_error const &e)
		{
			BOOST_CHECK_MESSAGE(std::string(e.what()).find(std::to_string(version)) != std::string::npos, e.what());
		}
	}
}

// (edge_asset's converter: EDGE_ASSET_TOOLS)
#if defined(EDGE_TEST_ASSET_TOOLS)
BOOST_AUTO_TEST_CASE(a_file_whose_occlusion_strength_is_not_0_to_1_is_damaged)
{
	convert_gltf_to_edge(asset("test_occlusion.gltf"), out("strength.edge"), uncompressed());
	SerializedModel serialized = Model::read_file(out("strength.edge"));
	serialized.materials.at(0).occlusion_strength = 2.0f;
	Model::write_file(serialized, out("strength.edge"));
	auto renderer = std::make_shared<NullRenderer>(4, 4);
	auto const model = std::make_shared<StaticModelPTBNT>(renderer, std::make_shared<Scheduler>(renderer));
	try
	{
		model->load_from_file(out("strength.edge"));
		BOOST_ERROR("a strength of 2 loaded");
	}
	catch (std::invalid_argument const &e)
	{
		BOOST_ERROR("a damaged file is std::runtime_error, not std::invalid_argument: " << e.what());
	}
	catch (std::runtime_error const &e)
	{
		BOOST_CHECK_MESSAGE(std::string(e.what()).find("occlusion strength") != std::string::npos, e.what());
	}
}

BOOST_AUTO_TEST_CASE(a_file_whose_material_has_occlusion_and_no_texture_to_hold_it_is_damaged)
{
	convert_gltf_to_edge(asset("test_occlusion.gltf"), out("no_texture.edge"), uncompressed());
	SerializedModel serialized = Model::read_file(out("no_texture.edge"));
	SerializedMaterial &orm = serialized.materials.at(0);
	BOOST_REQUIRE(orm.occlusion);
	orm.metallic_roughness = -1;
	Model::write_file(serialized, out("no_texture.edge"));
	auto renderer = std::make_shared<NullRenderer>(4, 4);
	auto const model = std::make_shared<StaticModelPTBNT>(renderer, std::make_shared<Scheduler>(renderer));
	try
	{
		model->load_from_file(out("no_texture.edge"));
		BOOST_ERROR("a material with occlusion and no metallic-roughness texture loaded");
	}
	catch (std::runtime_error const &e)
	{
		std::string const message = e.what();
		BOOST_CHECK_MESSAGE(message.find("no_texture.edge") != std::string::npos && message.find("occlusion") != std::string::npos,
			"the error names the file and the occlusion: " << message);
	}
}

BOOST_AUTO_TEST_CASE(a_converted_file_keeps_occlusion_and_its_strength)
{
	for (bool compress : {false, true})
	{
		GltfOptions options;
		options.compress_textures = compress;
		std::string const file = out(compress ? "bc7.edge" : "plain.edge");
		convert_gltf_to_edge(asset("test_occlusion.gltf"), file, options);
		auto renderer = std::make_shared<NullRenderer>(4, 4);
		auto const model = std::make_shared<StaticModelPTBNT>(renderer, std::make_shared<Scheduler>(renderer));
		model->load_from_file(file);
		BOOST_CHECK(material_named(*model, "orm")->occlusion());
		BOOST_CHECK_EQUAL(material_named(*model, "orm")->occlusion_strength(), 0.5f);
		BOOST_CHECK_EQUAL(material_named(*model, "occlusion_only")->occlusion_strength(), 0.25f);
		BOOST_CHECK(!material_named(*model, "plain")->occlusion());
		TexturePtr const packed = material_named(*model, "separate")->metallic_roughness_texture();
		BOOST_REQUIRE(packed);
		BOOST_CHECK_EQUAL(packed->width(), 4);
		if (!compress)
		{
			auto const got = texels(packed);
			BOOST_CHECK(got[3][2].x == 16 * 14 && got[0][0].x == 0);
		}
		else
		{
			BOOST_CHECK_EQUAL(packed->pixel_format(), PixelFormat::BC7);
		}
	}
	// asked to leave occlusion out
	GltfOptions without;
	without.occlusion = false;
	convert_gltf_to_edge(asset("test_occlusion.gltf"), out("without.edge"), without);
	SerializedModel const serialized = Model::read_file(out("without.edge"));
	for (SerializedMaterial const &m : serialized.materials)
		BOOST_CHECK_MESSAGE(!m.occlusion, m.name << " has occlusion");
	BOOST_CHECK(serialized.options.find("occlusion=0") != std::string::npos);
}
#endif

// ---- what a shader reads


namespace
{

struct Rgba
{
	float r, g, b, a;
};

std::ostream &operator<<(std::ostream &s, Rgba const &c)
{
	return s << "(" << c.r << ", " << c.g << ", " << c.b << ", " << c.a << ")";
}

bool near(Rgba const &x, Rgba const &y, float tolerance = 2e-3f)
{
	return std::abs(x.r - y.r) < tolerance && std::abs(x.g - y.g) < tolerance && std::abs(x.b - y.b) < tolerance &&
		std::abs(x.a - y.a) < tolerance;
}

TexturePtr texture_of(RendererPtr const &renderer, unsigned char r, unsigned char g, unsigned char b)
{
	TexturePtr texture = renderer->create_texture(4, 4, PixelFormat::A8R8G8B8);
	auto texture_mapping = texture->write(Contents::DISCARD);
	unsigned char *p = texture_mapping.as<unsigned char>().data();
	for (int i = 0; i < 16; ++i)
	{
		p[4 * i + 0] = b;
		p[4 * i + 1] = g;
		p[4 * i + 2] = r;
		p[4 * i + 3] = 255;
	}
	texture_mapping.unmap();
	return texture;
}

/// The middle of a quad drawn with test_surface.frag's occlusion probe: (occlusion, roughness, metallic, strength as the flags say)
Rgba surface(RendererPtr const &renderer, MaterialPtr const &material)
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
	material->set_extra_parameter(0, rove::vec<4>(6, 0, 0, 0));
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
	try
	{
		draw->execute();
	}
	catch (...)
	{
		renderer->end_pass();
		renderer->finish_frame();
		throw;
	}
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

BOOST_AUTO_TEST_CASE_TEMPLATE(a_surface_takes_its_occlusion_from_red_by_its_strength, F, Backends)
{
	F fixture;
	auto const &renderer = fixture.renderer;
	BOOST_REQUIRE_MESSAGE(renderer, "renderer unavailable: " << fixture.error);
	MaterialPtr const material = renderer->create_material();
	material->set_roughness_factor(0.5f);
	material->set_metallic_factor(1.0f);
	material->set_metallic_roughness_texture(texture_of(renderer, 51, 102, 204));     // 0.2, 0.4, 0.8
	// red is no occlusion of a material that has none
	Rgba got = surface(renderer, material);
	BOOST_CHECK_MESSAGE(near(got, {1, 0.2f, 0.8f, 0}), "no occlusion: " << got);
	material->set_occlusion(true);
	got = surface(renderer, material);
	BOOST_CHECK_MESSAGE(near(got, {0.2f, 0.2f, 0.8f, 1}), "occlusion at strength 1: red " << got);
	material->set_occlusion_strength(0.25f);
	got = surface(renderer, material);
	BOOST_CHECK_MESSAGE(near(got, {0.8f, 0.2f, 0.8f, 0.25f}), "at a quarter: 1 + 0.25 (0.2 - 1) " << got);
	material->set_occlusion_strength(0.0f);
	got = surface(renderer, material);
	BOOST_CHECK_MESSAGE(near(got, {1, 0.2f, 0.8f, 0}), "at strength 0: none " << got);
}

BOOST_AUTO_TEST_CASE_TEMPLATE(occlusion_without_a_metallic_roughness_texture_to_hold_it_is_refused_at_draw, F, AllBackends)
{
	F fixture;
	auto const &renderer = fixture.renderer;
	BOOST_REQUIRE_MESSAGE(renderer, "renderer unavailable: " << fixture.error);
	MaterialPtr const material = renderer->create_material();
	material->set_occlusion(true);
	BOOST_CHECK_THROW(surface(renderer, material), std::logic_error);
}

BOOST_AUTO_TEST_SUITE_END()
