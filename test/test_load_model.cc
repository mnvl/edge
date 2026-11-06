// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

// load_model(): a model of the type its file is -- a .edge file's meshes name their type, a glTF's contents decide it as the converter
// does -- so that what opens any model (pyedge, the viewer) need not know it beforehand.

#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <typeinfo>

#include <boost/test/unit_test.hpp>

#include "animated_model.h"
#include "load_model.h"
#include "model_convert.h"
#include "null_renderer.h"
#include "scheduler.h"
#include "serialized_model.h"
#include "static_model.h"
#include "test_fixture.h"

namespace
{

std::string asset(std::string const &name)
{
	return edge::TestFixture::graphic_sources_path() + "test/data/" + name;
}

struct Fixture
{
	edge::RendererPtr renderer = [] { edge::RendererPtr r(new edge::NullRenderer(320, 200)); return r; }();
	edge::SchedulerPtr scheduler = std::make_shared<edge::Scheduler>(renderer);
	std::filesystem::path const dir = std::filesystem::temp_directory_path() / "edge_load_model";

	Fixture()
	{
		std::filesystem::remove_all(dir);
		std::filesystem::create_directories(dir);
	}

	~Fixture()
	{
		std::error_code ignored;
		std::filesystem::remove_all(dir, ignored);
	}

	std::string out(std::string const &name) const { return (dir / name).string(); }

	/// The file the converter makes of @p gltf (with the coloured skinned type where @p skinned_colors).
	std::string converted(std::string const &gltf, bool skinned_colors = false)
	{
		edge::ConvertOptions convert;
		convert.skinned_colors = skinned_colors;
		std::string const file = out(gltf + (skinned_colors ? ".c" : "") + ".edge");
		edge::convert_gltf_to_edge(asset(gltf), file, edge::GltfOptions(), edge::LodOptions(), 0, edge::TextureOptions(), convert);
		return file;
	}

	/// @p model saved: what it holds, to compare.
	std::string saved(edge::Model const &model, std::string const &name)
	{
		model.save_to_file(out(name));
		std::ifstream in(out(name), std::ios::binary);
		return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
	}
};

template <class M> void is_a(edge::ModelPtr const &model, std::string const &what)
{
	BOOST_REQUIRE_MESSAGE(model, what);
	BOOST_CHECK_MESSAGE(dynamic_cast<M const *>(model.get()) != nullptr, what << " is a " << typeid(*model).name());
}

}

BOOST_FIXTURE_TEST_SUITE(load_model, Fixture)

BOOST_AUTO_TEST_CASE(a_edge_file_makes_a_model_of_the_type_its_meshes_are)
{
	is_a<edge::StaticModelPTBNT>(edge::load_model(renderer, scheduler, converted("test_gltf_loder_plane.glb")), "a plain file");
	is_a<edge::StaticModelPTBNTC>(edge::load_model(renderer, scheduler, converted("test_vertex_colors.glb")), "a coloured file");
	is_a<edge::AnimatedModelPTBNJWT>(edge::load_model(renderer, scheduler, converted("test_skinned_groups.glb")), "a skinned file");
	is_a<edge::AnimatedModelPTBNJWTC>(edge::load_model(renderer, scheduler, converted("test_skinned_colors.glb", true)), "a coloured skinned file");
}

BOOST_AUTO_TEST_CASE(the_model_is_the_one_its_type_loads)
{
	std::string const file = converted("test_figure.glb");
	edge::ModelPtr const any = edge::load_model(renderer, scheduler, file);
	auto typed = std::make_shared<edge::AnimatedModelPTBNJWT>(renderer, scheduler);
	typed->load_from_file(file);
	BOOST_CHECK(saved(*any, "any.edge") == saved(*typed, "typed.edge"));
}

BOOST_AUTO_TEST_CASE(a_glTF_makes_the_model_the_converter_would_make_of_it)
{
	is_a<edge::StaticModelPTBNT>(edge::load_model(renderer, scheduler, asset("test_gltf_loder_plane.glb")), "a plain glTF");
	is_a<edge::StaticModelPTBNTC>(edge::load_model(renderer, scheduler, asset("test_vertex_colors.glb")), "a coloured glTF");
	is_a<edge::AnimatedModelPTBNJWT>(edge::load_model(renderer, scheduler, asset("test_skinned_groups.glb")), "a skinned glTF");
	is_a<edge::AnimatedModelPTBNJWT>(edge::load_model(renderer, scheduler, asset("test_skinned_colors.glb")), "a coloured skinned glTF");
	is_a<edge::StaticModelPTBNT>(edge::load_model(renderer, scheduler, asset("test_uv_set_1.gltf")), "a .gltf");
	// its options are the glTF's loading options
	edge::GltfOptions keep;
	keep.keep_cpu_copy = false;
	edge::ModelPtr const gpu_only = edge::load_model(renderer, scheduler, asset("test_gltf_loader_pbr.glb"), keep);
	BOOST_CHECK_THROW(gpu_only->save_to_file(out("gpu.edge")), std::runtime_error);      // no CPU copy of the textures to save
}

BOOST_AUTO_TEST_CASE(a_file_of_no_known_type_or_of_two_is_refused_naming_them)
{
	std::string const file = converted("test_gltf_loder_plane.glb");
	edge::SerializedModel model = edge::Model::read_file(file);
	// a static mesh of skinned vertices: a type the format holds and no model loads
	model.meshes.at(0).serialization_type = edge::BasicStaticMesh<edge::VERTEX_PTBNJWT>::class_serialization_type();
	edge::Model::write_file(model, out("unknown.edge"));
	try
	{
		edge::load_model(renderer, scheduler, out("unknown.edge"));
		BOOST_FAIL("a mesh type nothing loads was taken");
	}
	catch (std::runtime_error const &e)
	{
		BOOST_CHECK_MESSAGE(std::string(e.what()).find(edge::BasicStaticMesh<edge::VERTEX_PTBNJWT>::class_serialization_type()) != std::string::npos,
			e.what());
	}
	edge::SerializedModel two = edge::Model::read_file(converted("test_gltf_loder_plane.glb"));
	edge::SerializedModel const skinned = edge::Model::read_file(converted("test_skinned_groups.glb"));
	two.meshes.push_back(skinned.meshes.at(0));
	edge::Model::write_file(two, out("two.edge"));
	BOOST_CHECK_THROW(edge::load_model(renderer, scheduler, out("two.edge")), std::runtime_error);
	edge::SerializedModel none = edge::Model::read_file(file);
	none.meshes.clear();
	none.joints.clear();
	edge::Model::write_file(none, out("none.edge"));
	BOOST_CHECK_THROW(edge::load_model(renderer, scheduler, out("none.edge")), std::runtime_error);
	BOOST_CHECK_THROW(edge::load_model(renderer, scheduler, out("not_there.edge")), std::runtime_error);
	BOOST_CHECK_THROW(edge::load_model(renderer, scheduler, asset("test_white.png")), std::invalid_argument);
}

BOOST_AUTO_TEST_SUITE_END()
