// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

// SECURITY.md says it: a model file may come from anyone.  What is wrong with one is an error when it is loaded, never a read out of
// bounds when it is drawn, a hang, or a crash.  (On the null backend: what these check is the loader, and the bytes reach no driver.)

#include <algorithm>
#include <climits>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#include <boost/test/unit_test.hpp>

#include "animated_mesh.h"
#include "animated_model.h"
#include "camera.h"
#include "gltf_loader.h"
#include "model.h"
#include "null_renderer.h"
#include "scheduler.h"
#include "serialized_model.h"
#include "static_model.h"
#include "test_fixture.h"
#include "texture.h"

namespace
{

using Animated = edge::AnimatedModelPTBNJWT;
using Static = edge::StaticModelPTBNT;

std::string asset(std::string const &name)
{
	return edge::TestFixture::graphic_sources_path() + "test/data/" + name;
}

struct Files
{
	edge::RendererPtr renderer = [] { edge::RendererPtr r(new edge::NullRenderer(64, 64)); return r; }();
	edge::SchedulerPtr scheduler = std::make_shared<edge::Scheduler>(renderer);
	std::filesystem::path const dir = std::filesystem::temp_directory_path() / ("edge_untrusted_" + std::to_string(reinterpret_cast<std::uintptr_t>(this)));
	std::filesystem::path const file = dir / "model.edge";

	Files() { std::filesystem::create_directories(dir); }
	~Files()
	{
		std::error_code ignored;
		std::filesystem::remove_all(dir, ignored);
	}

	template <class M>
	std::shared_ptr<M> from_gltf(std::string const &name)
	{
		auto model = std::make_shared<M>(renderer, scheduler);
		model->load_from_gltf(asset(name), edge::GltfOptions());
		return model;
	}

	/// A load of the serialized model, then a frame that draws it: an exception from either is what a damaged file may do.
	template <class M>
	bool loads_and_draws(edge::SerializedModel const &serialized)
	{
		auto model = std::make_shared<M>(renderer, scheduler);
		model->load(serialized, false);
		draw(*model);
		return true;
	}

	void draw(edge::Model &model)
	{
		edge::CameraPtr const camera = std::make_shared<edge::Camera>();
		rove::matrix<4, 4> world;
		world.identity();
		model.scheduler()->clear();
		model.draw(camera, world);
		edge::flush_in_a_pass(model.scheduler());
	}

	/// What a load of the serialized model throws: a std::runtime_error that names @p expected.
	template <class M>
	void refused(edge::SerializedModel const &serialized, std::string const &expected)
	{
		auto model = std::make_shared<M>(renderer, scheduler);
		try
		{
			model->load(serialized, false);
			BOOST_ERROR("the model was loaded; expected an error about " << expected);
		}
		catch (std::runtime_error const &e)
		{
			BOOST_CHECK_MESSAGE(std::string(e.what()).find(expected) != std::string::npos, e.what());
		}
	}
};

edge::SerializedMesh &first_skinned(edge::SerializedModel &model)
{
	for (edge::SerializedMesh &mesh : model.meshes)
		if (!mesh.joint_index_transform.empty())
			return mesh;
	throw std::logic_error("the test model has no skinned mesh");
}

std::vector<char> bytes_of(std::string const &path)
{
	std::ifstream in(path, std::ios::binary);
	return std::vector<char>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void write(std::filesystem::path const &path, std::vector<char> const &data)
{
	std::ofstream out(path, std::ios::binary | std::ios::trunc);
	out.write(data.data(), std::streamsize(data.size()));
}

}

BOOST_AUTO_TEST_SUITE(untrusted_files)

// --- skin ------------------------------------------------------------------------------------------------------------------------

BOOST_FIXTURE_TEST_CASE(a_skin_joint_that_is_not_there_is_refused, Files)
{
	edge::SerializedModel model = from_gltf<Animated>("test_figure.glb")->serialize();
	first_skinned(model).joint_index_transform[0] = 1000000;
	refused<Animated>(model, "joint");
}

BOOST_FIXTURE_TEST_CASE(a_skin_with_no_bind_matrices_is_refused, Files)
{
	edge::SerializedModel model = from_gltf<Animated>("test_figure.glb")->serialize();
	first_skinned(model).inverted_bind_matrices.clear();
	refused<Animated>(model, "bind");
}

BOOST_FIXTURE_TEST_CASE(a_vertex_that_names_a_skin_joint_that_is_not_there_is_refused, Files)
{
	edge::SerializedModel model = from_gltf<Animated>("test_figure.glb")->serialize();
	edge::SerializedMesh &mesh = first_skinned(model);
	mesh.vertices.at(0).joints.i[0] = 1000000;
	mesh.vertices.at(0).weights.i[0] = 1;
	refused<Animated>(model, "joint");
}

// --- geometry --------------------------------------------------------------------------------------------------------------------

BOOST_FIXTURE_TEST_CASE(a_mesh_with_triangles_and_no_indices_or_no_vertices_is_refused, Files)
{
	edge::SerializedModel const valid = from_gltf<Static>("cone.glb")->serialize();
	edge::SerializedModel no_indices = valid;
	no_indices.meshes.at(0).indices.clear();
	refused<Static>(no_indices, "indices");
	edge::SerializedModel no_vertices = valid;
	no_vertices.meshes.at(0).vertices.clear();
	refused<Static>(no_vertices, "vert");
}

BOOST_FIXTURE_TEST_CASE(triangles_past_the_indices_are_refused, Files)
{
	edge::SerializedModel const valid = from_gltf<Static>("cone.glb")->serialize();
	edge::SerializedModel too_many = valid;
	too_many.meshes.at(0).triangles.at(0).triangles_count = 1000000000;
	refused<Static>(too_many, "indices");
	edge::SerializedModel too_far = valid;
	too_far.meshes.at(0).triangles.at(0).first_index_offset = 1000000000;
	refused<Static>(too_far, "indices");
	// (past 2^64 in the sum, the check itself would wrap)
	edge::SerializedModel wraps = valid;
	wraps.meshes.at(0).triangles.at(0).first_index_offset = std::numeric_limits<std::uint64_t>::max() - 2;
	wraps.meshes.at(0).triangles.at(0).triangles_count = 1;
	refused<Static>(wraps, "indices");
	edge::SerializedModel wraps_by_count = valid;
	wraps_by_count.meshes.at(0).triangles.at(0).triangles_count = std::numeric_limits<std::uint64_t>::max() / 3 + 1;
	refused<Static>(wraps_by_count, "indices");
}

BOOST_FIXTURE_TEST_CASE(an_index_past_the_vertices_is_refused, Files)
{
	edge::SerializedModel model = from_gltf<Static>("cone.glb")->serialize();
	model.meshes.at(0).indices.at(0) = 0x7fffffff;
	refused<Static>(model, "index");
}

BOOST_FIXTURE_TEST_CASE(a_vertex_offset_past_the_vertices_is_refused, Files)
{
	edge::SerializedModel model = from_gltf<Static>("cone.glb")->serialize();
	model.meshes.at(0).triangles.at(0).minimal_vertex_index = 1000000000;
	refused<Static>(model, "vert");
}

BOOST_FIXTURE_TEST_CASE(what_is_valid_still_loads_and_draws, Files)
{
	BOOST_TEST(loads_and_draws<Static>(from_gltf<Static>("cone.glb")->serialize()));
	BOOST_TEST(loads_and_draws<Animated>(from_gltf<Animated>("test_figure.glb")->serialize()));
}

// A NaN is a number nobody can place: an octree over it ends, whether the bounds are refused or the tree is built
BOOST_FIXTURE_TEST_CASE(an_octree_of_nan_positions_ends, Files)
{
	edge::SerializedModel model = from_gltf<Static>("cone.glb")->serialize();
	float const nan = std::numeric_limits<float>::quiet_NaN();
	for (edge::GenericVertex &vertex : model.meshes.at(0).vertices)
		vertex.position.set(nan, nan, nan);
	refused<Static>(model, "not finite");
}

BOOST_FIXTURE_TEST_CASE(a_material_of_a_mode_that_is_not_one_is_refused, Files)
{
	edge::SerializedModel const valid = from_gltf<Static>("cone.glb")->serialize();
	BOOST_REQUIRE(!valid.materials.empty());
	edge::SerializedModel blend = valid;
	blend.materials.at(0).src_blend_mode = 99;
	refused<Static>(blend, "blend");
	edge::SerializedModel cull = valid;
	cull.materials.at(0).cull_mode = -3;
	refused<Static>(cull, "cull");
}

BOOST_AUTO_TEST_CASE(a_byte_that_is_neither_true_nor_false_is_refused)
{
	char const two = 2, one = 1;
	bool value = false;
	edge::BoundedInputArchive bad(&two, 1);
	BOOST_CHECK_THROW(bad(value), cereal::Exception);
	edge::BoundedInputArchive good(&one, 1);
	good(value);
	BOOST_TEST(value);
}

// --- textures --------------------------------------------------------------------------------------------------------------------

BOOST_FIXTURE_TEST_CASE(a_texture_of_more_mip_levels_than_its_size_has_is_refused_at_once, Files)
{
	edge::SerializedTexture texture;
	texture.filename = "t";
	texture.width = 4;
	texture.height = 4;
	texture.pixel_format = edge::PixelFormat::A8R8G8B8;
	texture.provided_mip_levels = INT_MAX;
	BOOST_CHECK_THROW(edge::import_texture(texture, renderer, false), std::runtime_error);
	texture.provided_mip_levels = 33;
	BOOST_CHECK_THROW(edge::import_texture(texture, renderer, false), std::runtime_error);
	texture.provided_mip_levels = -1;
	BOOST_CHECK_THROW(edge::import_texture(texture, renderer, false), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(a_level_past_the_width_of_an_int_is_one_texel)
{
	BOOST_TEST(edge::Texture::level_size(4, 4, edge::PixelFormat::A8R8G8B8, 40) == 4u);
}

namespace
{

/// A DX10 DDS of BC1 with these numbers in its header, and no data
std::vector<char> dds_of(uint32_t width, uint32_t height, uint32_t mips, uint32_t layers)
{
	std::vector<char> file(148);
	auto const put = [&](size_t at, uint32_t value) { std::memcpy(file.data() + at, &value, 4); };
	std::memcpy(file.data(), "DDS ", 4);
	put(4, 124);
	put(12, height);
	put(16, width);
	put(28, mips);
	put(76, 32);
	put(80, 4);
	std::memcpy(file.data() + 84, "DX10", 4);
	put(128, 71);
	put(132, 3);
	put(140, layers);
	return file;
}

}

BOOST_FIXTURE_TEST_CASE(a_dds_of_a_layer_count_that_wraps_is_refused, Files)
{
	std::vector<char> const file = dds_of(4, 4, 1, 0xff000000u);
	BOOST_CHECK_THROW(edge::Texture::load_dds(renderer, file.data(), file.size(), edge::TextureUsage()), std::runtime_error);
	std::vector<char> const near_int_max = dds_of(4, 4, 1, 0x7fffffffu);
	BOOST_CHECK_THROW(edge::Texture::load_dds(renderer, near_int_max.data(), near_int_max.size(), edge::TextureUsage()), std::runtime_error);
}

BOOST_FIXTURE_TEST_CASE(a_dds_of_more_mip_levels_than_an_int_is_wide_is_refused, Files)
{
	std::vector<char> const file = dds_of(4, 4, 33, 1);
	BOOST_CHECK_THROW(edge::Texture::load_dds(renderer, file.data(), file.size(), edge::TextureUsage()), std::runtime_error);
}

BOOST_FIXTURE_TEST_CASE(a_dds_of_a_size_that_overflows_is_refused, Files)
{
	std::vector<char> const file = dds_of(0x7fffffffu, 0x7fffffffu, 1, 1);
	BOOST_CHECK_THROW(edge::Texture::load_dds(renderer, file.data(), file.size(), edge::TextureUsage()), std::runtime_error);
}

BOOST_FIXTURE_TEST_CASE(a_texture_of_no_size_or_of_a_negative_size_is_refused, Files)
{
	BOOST_CHECK_THROW(renderer->create_texture(0, 0, edge::PixelFormat::A8R8G8B8, edge::TextureUsage()), std::invalid_argument);
	BOOST_CHECK_THROW(renderer->create_texture(-1, 4, edge::PixelFormat::A8R8G8B8, edge::TextureUsage()), std::invalid_argument);
	BOOST_CHECK_THROW(renderer->create_texture(4, -1, edge::PixelFormat::A8R8G8B8, edge::TextureUsage()), std::invalid_argument);
	BOOST_CHECK_THROW(renderer->create_texture(100000, 100000, edge::PixelFormat::A8R8G8B8, edge::TextureUsage()), std::invalid_argument);
}

// --- save_to_file ----------------------------------------------------------------------------------------------------------------

BOOST_FIXTURE_TEST_CASE(save_to_file_refuses_a_format_it_cannot_write, Files)
{
	edge::TexturePtr const small = renderer->create_texture(64, 64, edge::PixelFormat::R8UI, edge::TextureUsage());
	BOOST_CHECK_THROW(edge::Texture::save_to_file(small, (dir / "a.png").string()), std::invalid_argument);
	BOOST_TEST(!std::filesystem::exists(dir / "a.png"));
	edge::TexturePtr const wide = renderer->create_texture(4, 4, edge::PixelFormat::R32G32B32A32F, edge::TextureUsage());
	BOOST_CHECK_THROW(edge::Texture::save_to_file(wide, (dir / "b.png").string()), std::invalid_argument);
}

BOOST_FIXTURE_TEST_CASE(save_to_file_writes_what_its_extension_says, Files)
{
	edge::TextureUsage usage;
	usage.keep_cpu_copy = true;
	edge::TexturePtr const texture = renderer->create_texture(4, 4, edge::PixelFormat::A8R8G8B8, usage);
	auto const header = [&](std::string const &name) {
		edge::Texture::save_to_file(texture, (dir / name).string());
		return bytes_of((dir / name).string());
	};
	BOOST_TEST(std::string(header("a.png").data() + 1, 3) == "PNG");
	BOOST_TEST(std::string(header("a.PNG").data() + 1, 3) == "PNG");
	BOOST_TEST(std::string(header("a.bmp").data(), 2) == "BM");
	BOOST_TEST(static_cast<unsigned char>(header("a.jpg")[0]) == 0xFFu);
	BOOST_TEST(static_cast<unsigned char>(header("a.jpeg")[1]) == 0xD8u);
	BOOST_TEST(header("a.tga").size() > 18u);
	// what it does not know is an error, not a BMP under another name
	BOOST_CHECK_THROW(edge::Texture::save_to_file(texture, (dir / "a.xyz").string()), std::invalid_argument);
	BOOST_CHECK_THROW(edge::Texture::save_to_file(texture, (dir / "noextension").string()), std::invalid_argument);
}

// --- glTF ------------------------------------------------------------------------------------------------------------------------

BOOST_FIXTURE_TEST_CASE(a_bare_file_name_loads, Files)
{
	std::filesystem::path const previous = std::filesystem::current_path();
	std::filesystem::copy_file(asset("cone.glb"), dir / "bare.glb");
	std::filesystem::current_path(dir);
	struct Back { std::filesystem::path to; ~Back() { std::filesystem::current_path(to); } } const back{previous};
	auto model = std::make_shared<Static>(renderer, scheduler);
	model->load_from_gltf("bare.glb", edge::GltfOptions());
	BOOST_TEST(model->meshes().size() >= 1u);
}

// Any byte of the file may be anything.  (It has failed when this crashes; run under AddressSanitizer it also catches a read past
// the buffers that happens not to crash.)
BOOST_FIXTURE_TEST_CASE(a_glb_of_changed_bytes_is_loaded_or_refused, Files)
{
	// (the figure takes a good part of a second a load: fewer rounds of it)
	struct Round { char const *name; int rounds; };
	for (Round const round_of : {Round{"cone.glb", 150}, Round{"test_attributes.glb", 150}, Round{"test_figure.glb", 6}}) {
		std::string const name = round_of.name;
		std::vector<char> const valid = bytes_of(asset(name));
		BOOST_REQUIRE(!valid.empty());
		std::mt19937 rng(11);
		std::uniform_int_distribution<size_t> where(0, valid.size() - 1);
		std::uniform_int_distribution<int> what(0, 255);
		std::filesystem::path const mutated = dir / "mutated.glb";
		for (int round = 0; round < round_of.rounds; ++round) {
			std::vector<char> data = valid;
			for (int change = 0; change < 1 + round % 8; ++change)
				data[where(rng)] = char(what(rng));
			write(mutated, data);
			try {
				auto model = std::make_shared<Animated>(renderer, scheduler);
				model->load_from_gltf(mutated.string(), edge::GltfOptions());
				draw(*model);
			} catch (std::exception const &) {
				// an error is what it may do
			}
		}
	}
}

BOOST_FIXTURE_TEST_CASE(an_edge_file_of_changed_bytes_is_loaded_or_refused, Files)
{
	from_gltf<Animated>("cone.glb")->save_to_file(file.string());
	std::vector<char> const valid = bytes_of(file.string());
	std::mt19937 rng(13);
	std::uniform_int_distribution<size_t> where(0, valid.size() - 1);
	std::uniform_int_distribution<int> what(0, 255);
	for (int round = 0; round < 300; ++round) {
		std::vector<char> data = valid;
		for (int change = 0; change < 1 + round % 8; ++change)
			data[where(rng)] = char(what(rng));
		write(file, data);
		try {
			auto model = std::make_shared<Animated>(renderer, scheduler);
			model->load_from_file(file.string());
			draw(*model);
		} catch (std::exception const &) {
			// an error is what it may do
		}
	}
}

BOOST_AUTO_TEST_SUITE_END()
