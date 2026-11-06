// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

// Contract of the vertex colors of a glTF (COLOR_0): a model of the vertex type that has them (StaticModelPTBNTC) gets them as the
// file writes them -- floats as they are (a value above 1 is an emission strength), normalised integers as glTF normalises them, a VEC3
// with an alpha of 1 -- for every vertex, in the order of the vertices of the mesh; a file that lacks them, or lacks them on some
// primitives of a mesh, is refused rather than given a color it does not have; a model of a type without a color leaves them out; the
// .edge format keeps them exactly; and the converter picks the type by what the file has (a skinned file's colors are left out unless it
// is asked to keep them, in AnimatedModelPTBNJWTC, the skinned type with a color).  The files are written by test/make_test_vertex_colors_glb.py and test/make_test_skinned_groups_glb.py.

#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <boost/test/unit_test.hpp>

#include "gltf_loader.h"
#include "model_cache.h"
#include "animated_model.h"
#if defined(EDGE_TEST_ASSET_TOOLS)
#include "model_convert.h"
#endif
#include "null_renderer.h"
#include "scheduler.h"
#include "static_model.h"
#include "test_fixture.h"

namespace
{

using Colors = std::vector<std::array<float, 4>>;

std::string asset(std::string const &name)
{
	return edge::TestFixture::graphic_sources_path() + "test/data/" + name;
}

struct Fixture
{
	edge::RendererPtr renderer = [] { edge::RendererPtr r(new edge::NullRenderer(320, 200)); return r; }();
	edge::SchedulerPtr scheduler = std::make_shared<edge::Scheduler>(renderer);
	std::filesystem::path const dir = std::filesystem::temp_directory_path() / "edge_vertex_colors";

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

	std::shared_ptr<edge::StaticModelPTBNTC> load(std::string const &name)
	{
		auto model = std::make_shared<edge::StaticModelPTBNTC>(renderer, scheduler);
		model->load_from_gltf(asset(name));
		return model;
	}
};

edge::StaticMeshPTBNTC const &only_mesh(edge::Model const &model)
{
	BOOST_REQUIRE_EQUAL(model.meshes().size(), 1u);
	return static_cast<edge::StaticMeshPTBNTC const &>(*model.meshes()[0]);
}

Colors colors_of(edge::Model const &model)
{
	Colors out;
	for (edge::VERTEX_PTBNTC const &v : only_mesh(model).vertices())
		out.push_back({v.color.i[0], v.color.i[1], v.color.i[2], v.color.i[3]});
	return out;
}

void check_colors(Colors const &got, Colors const &expected)
{
	BOOST_REQUIRE_EQUAL(got.size(), expected.size());
	for (size_t v = 0; v < got.size(); ++v)
		for (size_t c = 0; c < 4; ++c)
			BOOST_CHECK_MESSAGE(std::abs(got[v][c] - expected[v][c]) < 1e-5f,
				"vertex " << v << " channel " << c << ": " << got[v][c] << ", expected " << expected[v][c]);
}

std::string message_of(std::function<void()> const &action)
{
	try
	{
		action();
	}
	catch (std::runtime_error const &e)
	{
		return e.what();
	}
	return "";
}

}

BOOST_FIXTURE_TEST_SUITE(vertex_colors, Fixture)

BOOST_AUTO_TEST_CASE(float_colors_arrive_vertex_by_vertex_and_an_alpha_above_one_is_kept)
{
	check_colors(colors_of(*load("test_vertex_colors.glb")), {{1, 0, 0, 0.5f}, {0, 1, 0, 2.5f}, {0, 0, 1, 4}});
}

BOOST_AUTO_TEST_CASE(a_vec3_color_has_an_alpha_of_one)
{
	check_colors(colors_of(*load("test_vertex_colors_vec3.glb")), {{0.25f, 0.5f, 0.75f, 1}, {1, 0, 0, 1}, {0, 1, 0.5f, 1}});
}

BOOST_AUTO_TEST_CASE(normalised_bytes_are_divided_by_255)
{
	check_colors(colors_of(*load("test_vertex_colors_ubyte.glb")), {{1, 0, 0.2f, 0.4f}, {0, 1, 0, 1}, {0.4f, 0.2f, 1, 0}});
}

BOOST_AUTO_TEST_CASE(normalised_shorts_are_divided_by_65535)
{
	check_colors(colors_of(*load("test_vertex_colors_ushort.glb")), {{1, 0, 0.2f, 0.4f}, {0, 1, 0, 1}, {0.4f, 0.2f, 1, 0}});
}

/// A mesh of two primitives is one mesh: the colors run on through both, in the order of the vertices.
BOOST_AUTO_TEST_CASE(the_primitives_of_a_mesh_give_one_run_of_colors)
{
	check_colors(colors_of(*load("test_vertex_colors_two.glb")),
		{{1, 0, 0, 0.5f}, {1, 0, 0, 0.5f}, {1, 0, 0, 0.5f}, {0, 0, 1, 2}, {0, 0, 1, 2}, {0, 0, 1, 2}});
}

BOOST_AUTO_TEST_CASE(colors_on_some_primitives_of_a_mesh_and_not_on_others_are_refused_by_name)
{
	std::string const message = message_of([&] { load("test_vertex_colors_mixed.glb"); });
	BOOST_CHECK_MESSAGE(message.find("COLOR_0") != std::string::npos, "'" << message << "'");
}

/// A color that is not there is not made up (white, say): a mesh whose type has a color needs the file's.
BOOST_AUTO_TEST_CASE(a_file_without_colors_is_refused_by_a_model_that_needs_them)
{
	std::string const message = message_of([&] { load("test_vertex_colors_none.glb"); });
	BOOST_CHECK_MESSAGE(message.find("COLOR_0") != std::string::npos, "'" << message << "'");
}

BOOST_AUTO_TEST_CASE(a_model_of_a_type_without_a_color_loads_the_file_and_leaves_the_colors_out)
{
	auto plain = std::make_shared<edge::StaticModelPTBNT>(renderer, scheduler);
	plain->load_from_gltf(asset("test_vertex_colors.glb"));
	auto with = load("test_vertex_colors.glb");
	BOOST_REQUIRE_EQUAL(plain->meshes().size(), 1u);
	auto const &a = static_cast<edge::StaticMeshPTBNT const &>(*plain->meshes()[0]).vertices();
	auto const &b = only_mesh(*with).vertices();
	BOOST_REQUIRE_EQUAL(a.size(), b.size());
	for (size_t v = 0; v < a.size(); ++v)
	{
		for (int k = 0; k < 3; ++k)
		{
			BOOST_CHECK_EQUAL(a[v].position.i[k], b[v].position.i[k]);
			BOOST_CHECK_EQUAL(a[v].normal.i[k], b[v].normal.i[k]);
			BOOST_CHECK_EQUAL(a[v].tangent.i[k], b[v].tangent.i[k]);
		}
		BOOST_CHECK_EQUAL(a[v].texcoords.i[1], b[v].texcoords.i[1]);
	}
}

BOOST_AUTO_TEST_CASE(the_edge_file_keeps_the_colors_exactly_and_the_rest_of_the_vertex)
{
	auto original = load("test_vertex_colors.glb");
	original->save_to_file(out("a.edge"));
	auto loaded = std::make_shared<edge::StaticModelPTBNTC>(renderer, scheduler);
	loaded->load_from_file(out("a.edge"));
	check_colors(colors_of(*loaded), {{1, 0, 0, 0.5f}, {0, 1, 0, 2.5f}, {0, 0, 1, 4}});
	auto const &a = only_mesh(*original).vertices(), &b = only_mesh(*loaded).vertices();
	BOOST_REQUIRE_EQUAL(a.size(), b.size());
	for (size_t v = 0; v < a.size(); ++v)
		BOOST_CHECK(std::memcmp(&a[v], &b[v], sizeof(a[v])) == 0);
}

BOOST_AUTO_TEST_CASE(a_file_of_one_vertex_type_does_not_load_as_the_other)
{
	load("test_vertex_colors.glb")->save_to_file(out("colored.edge"));
	auto plain = std::make_shared<edge::StaticModelPTBNT>(renderer, scheduler);
	plain->load_from_gltf(asset("test_vertex_colors_none.glb"));
	plain->save_to_file(out("plain.edge"));

	BOOST_CHECK_THROW(std::make_shared<edge::StaticModelPTBNT>(renderer, scheduler)->load_from_file(out("colored.edge")), std::runtime_error);
	BOOST_CHECK_THROW(std::make_shared<edge::StaticModelPTBNTC>(renderer, scheduler)->load_from_file(out("plain.edge")), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(an_instance_from_the_cache_has_the_colors_too)
{
	edge::ModelCache cache(renderer);
	auto first = cache.load<edge::StaticModelPTBNTC>(asset("test_vertex_colors.glb"), scheduler);
	auto second = cache.load<edge::StaticModelPTBNTC>(asset("test_vertex_colors.glb"), scheduler);
	check_colors(colors_of(*first), {{1, 0, 0, 0.5f}, {0, 1, 0, 2.5f}, {0, 0, 1, 4}});
	check_colors(colors_of(*second), {{1, 0, 0, 0.5f}, {0, 1, 0, 2.5f}, {0, 0, 1, 4}});
}

BOOST_AUTO_TEST_CASE(asking_whether_a_glTF_has_vertex_colors)
{
	BOOST_CHECK(edge::gltf_contents(asset("test_vertex_colors.glb")).vertex_colors);
	BOOST_CHECK(edge::gltf_contents(asset("test_vertex_colors_ubyte.glb")).vertex_colors);
	BOOST_CHECK(!edge::gltf_contents(asset("test_vertex_colors_none.glb")).vertex_colors);
	BOOST_CHECK(!edge::gltf_contents(asset("test_gltf_loder_plane.glb")).vertex_colors);
	BOOST_CHECK_THROW(edge::gltf_contents(asset("no_such.glb")).vertex_colors, edge::GltfLoader::load_error);
}

/// One primitive with them is enough: a mesh with them on some primitives is refused by the load, so the converter must not turn
/// it into a type that leaves them out.
BOOST_AUTO_TEST_CASE(a_glTF_with_colors_on_any_primitive_has_vertex_colors)
{
	BOOST_CHECK(edge::gltf_contents(asset("test_vertex_colors_mixed.glb")).vertex_colors);
}

// (edge_asset's converter: EDGE_ASSET_TOOLS)
#if defined(EDGE_TEST_ASSET_TOOLS)
BOOST_AUTO_TEST_CASE(a_glTF_with_colors_becomes_a_colored_static_model_file)
{
	edge::convert_gltf_to_edge(asset("test_vertex_colors.glb"), out("c.edge"), edge::GltfOptions());
	auto model = std::make_shared<edge::StaticModelPTBNTC>(renderer, scheduler);
	model->load_from_file(out("c.edge"));
	check_colors(colors_of(*model), {{1, 0, 0, 0.5f}, {0, 1, 0, 2.5f}, {0, 0, 1, 4}});
	BOOST_CHECK_THROW(std::make_shared<edge::StaticModelPTBNT>(renderer, scheduler)->load_from_file(out("c.edge")), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(a_glTF_without_colors_still_becomes_a_plain_static_model_file)
{
	edge::convert_gltf_to_edge(asset("test_vertex_colors_none.glb"), out("p.edge"), edge::GltfOptions());
	auto model = std::make_shared<edge::StaticModelPTBNT>(renderer, scheduler);
	model->load_from_file(out("p.edge"));
	BOOST_CHECK_EQUAL(model->meshes().size(), 1u);
	BOOST_CHECK_THROW(std::make_shared<edge::StaticModelPTBNTC>(renderer, scheduler)->load_from_file(out("p.edge")), std::runtime_error);
}

/// A skinned file's colors are left out unless asked for: a character with a color layer it does not use (as exporters often write them;
/// test_figure.glb, the converter's skinned example, has two) converts to the skinned type every game loads, with a warning.
BOOST_AUTO_TEST_CASE(a_skinned_glTF_with_colors_still_becomes_an_animated_model_file_without_them)
{
	BOOST_CHECK(edge::gltf_contents(asset("test_skinned_colors.glb")).skin);
	BOOST_CHECK(edge::gltf_contents(asset("test_skinned_colors.glb")).vertex_colors);
	edge::convert_gltf_to_edge(asset("test_skinned_colors.glb"), out("s.edge"), edge::GltfOptions());
	auto model = std::make_shared<edge::AnimatedModelPTBNJWT>(renderer, scheduler);
	model->load_from_file(out("s.edge"));
	BOOST_CHECK_EQUAL(model->meshes().size(), 1u);
}

BOOST_AUTO_TEST_CASE(a_skinned_model_of_the_coloured_type_has_the_colors_of_the_glTF)
{
	auto model = std::make_shared<edge::AnimatedModelPTBNJWTC>(renderer, scheduler);
	model->load_from_gltf(asset("test_skinned_colors.glb"));
	BOOST_REQUIRE_EQUAL(model->meshes().size(), 1u);
	auto const &mesh = dynamic_cast<edge::AnimatedMeshPTBNJWTC const &>(*model->meshes()[0]);
	BOOST_REQUIRE(!mesh.vertices().empty());
	for (edge::VERTEX_PTBNJWTC const &v : mesh.vertices())
	{
		BOOST_CHECK_EQUAL(v.color.i[0], 1.0f);
		BOOST_CHECK_EQUAL(v.color.i[1], 0.0f);
		BOOST_CHECK_EQUAL(v.color.i[2], 0.0f);
		BOOST_CHECK_EQUAL(v.color.i[3], 1.0f);
		BOOST_CHECK_GT(v.weights.i[0] + v.weights.i[1], 0.0f);          // and the skin
	}
	// a skinned file without colors is refused by it, as by the static coloured type
	BOOST_CHECK_THROW(std::make_shared<edge::AnimatedModelPTBNJWTC>(renderer, scheduler)->load_from_gltf(asset("test_skinned_groups.glb")),
		std::runtime_error);
}

BOOST_AUTO_TEST_CASE(asked_to_the_converter_keeps_a_skinned_glTF_s_colors_in_the_coloured_skinned_type)
{
	edge::ConvertOptions keep;
	keep.skinned_colors = true;
	edge::convert_gltf_to_edge(asset("test_skinned_colors.glb"), out("c.edge"), edge::GltfOptions(), edge::LodOptions(), 0, edge::TextureOptions(),
		keep);
	auto model = std::make_shared<edge::AnimatedModelPTBNJWTC>(renderer, scheduler);
	model->load_from_file(out("c.edge"));
	auto const &mesh = dynamic_cast<edge::AnimatedMeshPTBNJWTC const &>(*model->meshes().at(0));
	for (edge::VERTEX_PTBNJWTC const &v : mesh.vertices())
		BOOST_CHECK_EQUAL(v.color.i[0], 1.0f);
	BOOST_CHECK_THROW(std::make_shared<edge::AnimatedModelPTBNJWT>(renderer, scheduler)->load_from_file(out("c.edge")), std::runtime_error);
	// a skinned file without colors stays the plain skinned type however asked
	edge::convert_gltf_to_edge(asset("test_skinned_groups.glb"), out("g.edge"), edge::GltfOptions(), edge::LodOptions(), 0, edge::TextureOptions(),
		keep);
	std::make_shared<edge::AnimatedModelPTBNJWT>(renderer, scheduler)->load_from_file(out("g.edge"));
}

#endif

BOOST_AUTO_TEST_SUITE_END()
