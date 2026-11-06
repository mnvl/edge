// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

// Contract of levels of detail in a model: edge_convert can give every mesh of the .edge file coarser versions of itself (add_lods()), the
// loaded model knows how many levels it has and how far each strays from the first, and draws any of them: Model::draw(camera, world, level)
// submits the triangles of that level, a level past the last being the last.  Level 0 is what the model always was: its collision octree and
// the file's other contents do not change with the levels.

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <cereal/archives/binary.hpp>
#include <memory>
#include <stdexcept>
#include <string>

#include <boost/test/unit_test.hpp>

#include "animated_mesh.h"
#include "animated_model.h"
#include "camera.h"
#include "mesh_lod.h"
#include "model_cache.h"
#include "model_convert.h"
#include "null_renderer.h"
#include "scheduler.h"
#include "serialized_model.h"
#include "static_model.h"
#include "test_fixture.h"

namespace
{

using Animated = edge::AnimatedModelPTBNJWT;

std::string asset(std::string const &name)
{
	return edge::TestFixture::graphic_sources_path() + "test/data/" + name;
}

struct Fixture
{
	edge::RendererPtr renderer = [] { edge::RendererPtr r(new edge::NullRenderer(320, 200)); return r; }();
	edge::SchedulerPtr scheduler = std::make_shared<edge::Scheduler>(renderer);
	std::filesystem::path const dir = edge::unique_temp_directory("edge_model_lod_");
	edge::CameraPtr camera = [] {
		edge::CameraPtr c(new edge::Camera());
		c->set_perspective_projection(1.0f, 4.0f / 3.0f, 0.05f, 50.0f);
		c->look_at(rove::vec<3>(0, 1, 3), rove::vec<3>(0, 1, 0), rove::vec<3>(0, 1, 0));
		return c;
	}();


	~Fixture()
	{
		std::error_code ignored;
		std::filesystem::remove_all(dir, ignored);
	}

	std::string out(std::string const &name) const { return (dir / name).string(); }

	template <class M>
	std::shared_ptr<M> loaded(std::string const &file)
	{
		auto model = std::make_shared<M>(renderer, scheduler);
		model->load_from_file(file);
		return model;
	}

	/// The triangles the model offers to the scheduler drawn at `lod` (culling not counted: every call is offered).
	uint64_t offered(edge::Model &model, unsigned lod)
	{
		rove::matrix<4,4> identity;
		identity.identity();
		scheduler->clear();
		uint64_t const before = scheduler->offered_triangles_count();
		model.draw(camera, identity, lod);
		edge::flush_in_a_pass(scheduler);
		return scheduler->offered_triangles_count() - before;
	}
};

edge::LodOptions two_levels()
{
	return {{0.5f, 0.2f}, 0};
}

/// test_figure.glb converted once (plain, and with two_levels()): the simplification add_lods() runs is the test suite's expensive part, so
/// every case that only needs to read these two models shares this single conversion instead of repeating it.
struct SharedModels
{
	std::filesystem::path const dir = edge::unique_temp_directory("edge_model_lod_shared_");
	std::string const plain = (dir / "plain.edge").string();
	std::string const lod = (dir / "lod.edge").string();

	SharedModels()
	{
		edge::convert_gltf_to_edge(asset("test_figure.glb"), plain, edge::GltfOptions());
		edge::convert_gltf_to_edge(asset("test_figure.glb"), lod, edge::GltfOptions(), two_levels());
	}

	~SharedModels()
	{
		std::error_code ignored;
		std::filesystem::remove_all(dir, ignored);
	}
};

SharedModels const &shared_models()
{
	static SharedModels const instance;
	return instance;
}

}

BOOST_FIXTURE_TEST_SUITE(model_lod, Fixture)

BOOST_AUTO_TEST_CASE(a_converted_model_has_the_levels_asked_for_and_a_plain_one_has_just_the_first)
{
	auto plain = loaded<Animated>(shared_models().plain);
	auto lod = loaded<Animated>(shared_models().lod);
	BOOST_CHECK_EQUAL(plain->lod_count(), 1u);
	BOOST_CHECK_EQUAL(lod->lod_count(), 3u);
	BOOST_CHECK_EQUAL(plain->lod_error(0), 0.0f);
	BOOST_CHECK_GT(lod->lod_error(1), 0.0f);
	BOOST_CHECK_GE(lod->lod_error(2), lod->lod_error(1));
}

BOOST_AUTO_TEST_CASE(drawing_a_level_submits_the_triangles_of_that_level)
{
	auto plain = loaded<Animated>(shared_models().plain);
	auto lod = loaded<Animated>(shared_models().lod);
	uint64_t const full = offered(*plain, 0);
	BOOST_CHECK_GT(full, 50000u);
	BOOST_CHECK_EQUAL(offered(*lod, 0), full);              // level 0 is what the model was
	uint64_t const l1 = offered(*lod, 1), l2 = offered(*lod, 2);
	BOOST_CHECK_LT(double(l1), 0.6 * double(full));
	BOOST_CHECK_GT(double(l1), 0.3 * double(full));
	BOOST_CHECK_LT(l2, l1);
	BOOST_CHECK_LT(double(l2), 0.3 * double(full));
	BOOST_CHECK_GT(l2, 0u);
}

BOOST_AUTO_TEST_CASE(a_level_past_the_last_draws_the_last_and_a_model_without_levels_draws_itself)
{
	auto plain = loaded<Animated>(shared_models().plain);
	auto lod = loaded<Animated>(shared_models().lod);
	BOOST_CHECK_EQUAL(offered(*lod, 7), offered(*lod, 2));
	BOOST_CHECK_EQUAL(offered(*plain, 3), offered(*plain, 0));
}

BOOST_AUTO_TEST_CASE(a_chooser_gives_every_mesh_its_own_level_and_sees_the_mesh_and_where_it_is_placed)
{
	auto lod = loaded<Animated>(shared_models().lod);
	rove::matrix<4,4> identity;
	identity.identity();
	size_t asked = 0;
	scheduler->clear();
	uint64_t const before = scheduler->offered_triangles_count();
	lod->draw(camera, identity, [&](edge::Mesh const &mesh, rove::obb<3> const &bounds) {
		++asked;
		BOOST_CHECK_EQUAL(mesh.lod_count(), 3u);
		BOOST_CHECK_GT(bounds.radius(), 0.0f);
		return 1u;
	});
	edge::flush_in_a_pass(scheduler);
	BOOST_CHECK_EQUAL(asked, lod->meshes().size());
	uint64_t const chosen = scheduler->offered_triangles_count() - before;
	BOOST_CHECK_EQUAL(chosen, offered(*lod, 1));
	// by mesh: the first at level 2, the rest at 0
	scheduler->clear();
	uint64_t const b2 = scheduler->offered_triangles_count();
	edge::Mesh const *first = lod->meshes()[0].get();
	lod->draw(camera, identity, [&](edge::Mesh const &mesh, rove::obb<3> const &) { return &mesh == first ? 2u : 0u; });
	edge::flush_in_a_pass(scheduler);
	uint64_t const mixed = scheduler->offered_triangles_count() - b2;
	BOOST_CHECK_LT(mixed, offered(*lod, 0));
	BOOST_CHECK_GT(mixed, offered(*lod, 2));
}

BOOST_AUTO_TEST_CASE(a_mesh_reports_the_error_of_each_of_its_levels_and_a_level_past_the_last_is_the_last)
{
	auto lod = loaded<Animated>(shared_models().lod);
	edge::Mesh const &mesh = *lod->meshes()[0];
	BOOST_CHECK_EQUAL(mesh.lod_error(0), 0.0f);
	BOOST_CHECK_GT(mesh.lod_error(1), 0.0f);
	BOOST_CHECK_GE(mesh.lod_error(2), mesh.lod_error(1));
	BOOST_CHECK_EQUAL(mesh.lod_error(9), mesh.lod_error(2));
}

BOOST_AUTO_TEST_CASE(the_levels_survive_saving_and_loading_again_and_an_instance_from_the_cache)
{
	auto lod = loaded<Animated>(shared_models().lod);
	lod->save_to_file(out("again.edge"));
	auto again = loaded<Animated>(out("again.edge"));
	BOOST_CHECK_EQUAL(again->lod_count(), lod->lod_count());
	for (unsigned k = 0; k < 3; ++k)
	{
		BOOST_CHECK_EQUAL(again->lod_error(k), lod->lod_error(k));
		BOOST_CHECK_EQUAL(offered(*again, k), offered(*lod, k));
	}
	edge::ModelCache cache(renderer);
	auto instance = cache.load<Animated>(shared_models().lod, scheduler);
	BOOST_CHECK_EQUAL(instance->lod_count(), 3u);
	for (unsigned k = 0; k < 3; ++k)
		BOOST_CHECK_EQUAL(offered(*instance, k), offered(*lod, k));
}

BOOST_AUTO_TEST_CASE(a_levels_materials_are_the_models_own_not_the_prototypes)
{
	edge::ModelCache cache(renderer);
	auto a = cache.load<Animated>(shared_models().lod, scheduler);
	auto b = cache.load<Animated>(shared_models().lod, scheduler);
	for (auto const &mesh : a->meshes())
		for (auto const &lod : mesh->lods())
			for (auto const &group : lod.triangles)
			{
				BOOST_REQUIRE(group.material_ptr);
				bool owned = false;
				for (auto const &material : a->materials())
					owned = owned || material == group.material_ptr;
				BOOST_CHECK(owned);
				for (auto const &material : b->materials())
					BOOST_CHECK(material != group.material_ptr);
			}
}

BOOST_AUTO_TEST_CASE(the_collision_octree_is_made_of_level_zero_only)
{
	// a static model of a welded grid of 8192 triangles, with and without levels
	edge::RendererPtr r = renderer;
	auto plane = std::make_shared<edge::StaticModelPTBNT>(r, scheduler);
	plane->load_from_gltf(asset("test_gltf_loder_plane.glb"));
	edge::SerializedModel model = plane->serialize();
	edge::SerializedMesh &mesh = model.meshes.at(0);
	int const n = 64;
	mesh.vertices.clear();
	mesh.indices.clear();
	for (int y = 0; y <= n; ++y)
		for (int x = 0; x <= n; ++x)
		{
			edge::GenericVertex v{};
			v.position = rove::vec<3>(float(x), float(y), 0.8f * std::sin(0.4f * float(x)) * std::cos(0.3f * float(y)));
			v.normal = rove::vec<3>(0, 0, 1);
			v.texcoords = rove::vec<2>(float(x) / n, float(y) / n);
			mesh.vertices.push_back(v);
		}
	for (int y = 0; y < n; ++y)
		for (int x = 0; x < n; ++x)
			for (int k : {0, 1, n + 2, 0, n + 2, n + 1})
				mesh.indices.push_back(unsigned(y * (n + 1) + x + k));
	mesh.triangles.resize(1);
	mesh.triangles[0].first_index_offset = 0;
	mesh.triangles[0].triangles_count = mesh.indices.size() / 3;
	mesh.triangles[0].minimal_vertex_index = 0;
	mesh.triangles[0].vertices_count = mesh.vertices.size();
	edge::Model::write_file(model, out("plain.edge"));
	edge::add_lods(mesh, two_levels());
	edge::Model::write_file(model, out("lod.edge"));
	auto plain = loaded<edge::StaticModelPTBNT>(out("plain.edge"));
	auto lod = loaded<edge::StaticModelPTBNT>(out("lod.edge"));
	BOOST_REQUIRE_EQUAL(lod->lod_count(), 3u);
	rove::matrix<4,4> identity;
	identity.identity();
	BOOST_CHECK_EQUAL(plain->build_octree(identity).size(), 8192u);
	BOOST_CHECK_EQUAL(lod->build_octree(identity).size(), 8192u);
	BOOST_CHECK_EQUAL(offered(*plain, 0), 8192u);
}

BOOST_AUTO_TEST_CASE(a_model_converted_with_tiles_is_many_meshes_with_the_same_triangles)
{
	edge::convert_gltf_to_edge(asset("cylinder_30_degrees.glb"), out("whole.edge"), edge::GltfOptions());
	edge::convert_gltf_to_edge(asset("cylinder_30_degrees.glb"), out("tiled.edge"), edge::GltfOptions(), edge::LodOptions(), 0.5f);
	auto whole = loaded<edge::StaticModelPTBNT>(out("whole.edge"));
	auto tiled = loaded<edge::StaticModelPTBNT>(out("tiled.edge"));
	BOOST_CHECK_EQUAL(whole->meshes().size(), 1u);
	BOOST_CHECK_GT(tiled->meshes().size(), 3u);
	BOOST_CHECK_EQUAL(offered(*tiled, 0), offered(*whole, 0));
	rove::matrix<4,4> identity;
	identity.identity();
	BOOST_CHECK_EQUAL(tiled->build_octree(identity).size(), whole->build_octree(identity).size());
	BOOST_CHECK_THROW(edge::convert_gltf_to_edge(asset("test_figure.glb"), out("bad.edge"), edge::GltfOptions(), edge::LodOptions(), 0.5f), std::invalid_argument);
	BOOST_CHECK(!std::filesystem::exists(out("bad.edge")));
}

BOOST_AUTO_TEST_CASE(a_level_that_runs_past_the_files_indices_is_refused_on_load)
{
	auto lod = loaded<Animated>(shared_models().lod);
	lod->save_to_file(out("ok.edge"));
	// damage: load, edit the structures, write back
	edge::SerializedModel model;
	{
		std::ifstream in(out("ok.edge"), std::ios::binary);
		cereal::BinaryInputArchive archive(in);
		archive(model);
	}
	model.meshes.at(0).lods.at(0).triangles.at(0).triangles_count += 100000;
	{
		std::ofstream o(out("bad.edge"), std::ios::binary | std::ios::trunc);
		cereal::BinaryOutputArchive archive(o);
		archive(model);
	}
	auto bad = std::make_shared<Animated>(renderer, scheduler);
	BOOST_CHECK_THROW(bad->load_from_file(out("bad.edge")), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(a_level_with_another_number_of_groups_than_the_meshs_is_refused_on_load)
{
	auto lod = loaded<Animated>(shared_models().lod);
	lod->save_to_file(out("ok.edge"));
	edge::SerializedModel model;
	{
		std::ifstream in(out("ok.edge"), std::ios::binary);
		cereal::BinaryInputArchive archive(in);
		archive(model);
	}
	model.meshes.at(0).lods.at(0).triangles.pop_back();
	{
		std::ofstream o(out("bad.edge"), std::ios::binary | std::ios::trunc);
		cereal::BinaryOutputArchive archive(o);
		archive(model);
	}
	auto bad = std::make_shared<Animated>(renderer, scheduler);
	BOOST_CHECK_THROW(bad->load_from_file(out("bad.edge")), std::runtime_error);
}

BOOST_AUTO_TEST_SUITE_END()
