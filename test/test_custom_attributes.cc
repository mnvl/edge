// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

// Contract of the application-defined vertex attributes of a glTF (names that start with an underscore, which glTF leaves to the
// application): the loader keeps every one of them with the mesh, as floats, whatever they mean; a primitive that lacks one the
// others have, or has it with another width, is a file that is wrong; and the .edge format keeps them.  The files are written by
// test/make_test_attributes_glb.py and test/make_test_skinned_groups_glb.py.

#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <boost/test/unit_test.hpp>

#include "animated_model.h"
#include "model_cache.h"
#include "null_renderer.h"
#include "scheduler.h"
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
	std::filesystem::path const file = std::filesystem::temp_directory_path() / "edge_custom_attributes.edge";

	~Fixture()
	{
		std::error_code ignored;
		std::filesystem::remove(file, ignored);
	}

	std::shared_ptr<edge::StaticModelPTBNT> load(std::string const &name)
	{
		auto model = std::make_shared<edge::StaticModelPTBNT>(renderer, scheduler);
		model->load_from_gltf(asset(name));
		return model;
	}
};

edge::Mesh const &only_mesh(edge::Model const &model)
{
	BOOST_REQUIRE_EQUAL(model.meshes().size(), 1u);
	return *model.meshes()[0];
}

}

BOOST_FIXTURE_TEST_SUITE(custom_attributes, Fixture)

BOOST_AUTO_TEST_CASE(every_attribute_with_an_underscore_name_is_kept_and_no_other)
{
	auto model = load("test_attributes.glb");
	auto const &attributes = only_mesh(*model).custom_attributes();
	std::vector<std::string> names;
	for (auto const &entry : attributes)
		names.push_back(entry.first);
	BOOST_TEST(names == (std::vector<std::string>{"_BYTES", "_INTS", "_SCALAR", "_VEC3"}), boost::test_tools::per_element());
}

BOOST_AUTO_TEST_CASE(a_scalar_and_a_vector_are_read_as_floats_vertex_after_vertex)
{
	auto model = load("test_attributes.glb");
	auto const &attributes = only_mesh(*model).custom_attributes();
	BOOST_CHECK_EQUAL(attributes.at("_SCALAR").components, 1u);
	BOOST_TEST(attributes.at("_SCALAR").values == (std::vector<float>{10, 20, 30}), boost::test_tools::per_element());
	BOOST_CHECK_EQUAL(attributes.at("_VEC3").components, 3u);
	BOOST_TEST(attributes.at("_VEC3").values == (std::vector<float>{1, 2, 3, 4, 5, 6, 7, 8, 9}), boost::test_tools::per_element());
	BOOST_CHECK_EQUAL(attributes.at("_VEC3").count(), 3u);
}

BOOST_AUTO_TEST_CASE(integers_are_read_as_their_value_and_normalised_integers_as_glTF_normalises_them)
{
	auto model = load("test_attributes.glb");
	auto const &attributes = only_mesh(*model).custom_attributes();
	BOOST_TEST(attributes.at("_INTS").values == (std::vector<float>{7, 8, 9}), boost::test_tools::per_element());
	BOOST_CHECK_EQUAL(attributes.at("_BYTES").components, 2u);
	std::vector<float> const &bytes = attributes.at("_BYTES").values;
	BOOST_REQUIRE_EQUAL(bytes.size(), 6u);
	float const expected[6] = {0.0f, 1.0f, 0.2f, 0.4f, 1.0f, 0.0f};
	for (size_t i = 0; i < 6; ++i)
		BOOST_CHECK_CLOSE(bytes[i] + 1.0f, expected[i] + 1.0f, 1e-4);
}

BOOST_AUTO_TEST_CASE(a_skinned_meshs_custom_attribute_is_kept_with_it)
{
	auto model = std::make_shared<edge::AnimatedModelPTBNJWT>(renderer, scheduler);
	model->load_from_gltf(asset("test_skinned_groups.glb"));
	auto const &attributes = only_mesh(*model).custom_attributes();
	BOOST_REQUIRE_EQUAL(attributes.size(), 1u);
	BOOST_TEST(attributes.at("_GROUP").values == (std::vector<float>{2, 2, 2, 2, 0, 0, 0, 1, 1, 1}), boost::test_tools::per_element());
}

BOOST_AUTO_TEST_CASE(a_file_without_any_has_none)
{
	auto model = std::make_shared<edge::AnimatedModelPTBNJWT>(renderer, scheduler);
	model->load_from_gltf(asset("test_figure.glb"));
	for (auto const &mesh : model->meshes())
		BOOST_CHECK(mesh->custom_attributes().empty());
}

/// A mesh of two primitives is one mesh: the attribute runs on through both, in the order of the vertices.
BOOST_AUTO_TEST_CASE(the_primitives_of_a_mesh_give_one_run_of_values)
{
	auto model = load("test_attributes_two.glb");
	BOOST_TEST(only_mesh(*model).custom_attributes().at("_FOO").values == (std::vector<float>{1, 2, 3, 4, 5, 6}), boost::test_tools::per_element());
}

BOOST_AUTO_TEST_CASE(an_attribute_on_some_primitives_of_a_mesh_and_not_on_others_is_refused_by_name)
{
	auto model = std::make_shared<edge::StaticModelPTBNT>(renderer, scheduler);
	try
	{
		model->load_from_gltf(asset("test_attributes_mixed.glb"));
		BOOST_FAIL("loaded");
	}
	catch (std::runtime_error const &e)
	{
		BOOST_CHECK_MESSAGE(std::string(e.what()).find("_FOO") != std::string::npos, e.what());
	}
}

BOOST_AUTO_TEST_CASE(an_attribute_that_changes_width_between_primitives_is_refused_by_name)
{
	auto model = std::make_shared<edge::StaticModelPTBNT>(renderer, scheduler);
	try
	{
		model->load_from_gltf(asset("test_attributes_widths.glb"));
		BOOST_FAIL("loaded");
	}
	catch (std::runtime_error const &e)
	{
		BOOST_CHECK_MESSAGE(std::string(e.what()).find("_FOO") != std::string::npos, e.what());
	}
}

BOOST_AUTO_TEST_CASE(a_matrix_is_not_a_custom_attribute)
{
	auto model = std::make_shared<edge::StaticModelPTBNT>(renderer, scheduler);
	try
	{
		model->load_from_gltf(asset("test_attributes_matrix.glb"));
		BOOST_FAIL("loaded");
	}
	catch (std::runtime_error const &e)
	{
		BOOST_CHECK_MESSAGE(std::string(e.what()).find("_MAT") != std::string::npos, e.what());
	}
}

BOOST_AUTO_TEST_CASE(the_edge_file_keeps_them_exactly)
{
	auto original = load("test_attributes.glb");
	original->save_to_file(file.string());
	auto loaded = std::make_shared<edge::StaticModelPTBNT>(renderer, scheduler);
	loaded->load_from_file(file.string());
	auto const &a = only_mesh(*original).custom_attributes(), &b = only_mesh(*loaded).custom_attributes();
	BOOST_REQUIRE_EQUAL(a.size(), b.size());
	for (auto const &entry : a)
	{
		BOOST_REQUIRE_MESSAGE(b.count(entry.first), entry.first);
		BOOST_CHECK_EQUAL(entry.second.components, b.at(entry.first).components);
		BOOST_TEST(entry.second.values == b.at(entry.first).values, boost::test_tools::per_element());
	}
}

BOOST_AUTO_TEST_CASE(an_instance_from_the_cache_has_them_too)
{
	edge::ModelCache cache(renderer);
	auto instance = cache.load<edge::StaticModelPTBNT>(asset("test_attributes.glb"), scheduler);
	BOOST_CHECK_EQUAL(only_mesh(*instance).custom_attributes().size(), 4u);
}

BOOST_AUTO_TEST_SUITE_END()
