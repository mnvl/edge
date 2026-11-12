// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <boost/test/unit_test.hpp>

#include "animated_mesh.h"
#include "animated_model.h"
#include "model_cache.h"
#include "null_renderer.h"
#include "scheduler.h"
#include "static_mesh.h"
#include "static_model.h"
#include "test_fixture.h"

namespace
{

std::string asset(std::string const &name)
{
	return edge::TestFixture::graphic_sources_path() + "test/data/" + name;
}

edge::RendererPtr make_renderer()
{
	edge::RendererPtr renderer(new edge::NullRenderer(320, 200));
	return renderer;
}

template <class V>
edge::BasicStaticMesh<V> const &static_mesh(edge::MeshPtr const &mesh)
{
	auto const *result = dynamic_cast<edge::BasicStaticMesh<V> const *>(mesh.get());
	BOOST_REQUIRE(result);
	return *result;
}

template <class V>
std::vector<char> vertex_bytes(edge::MeshPtr const &mesh)
{
	edge::VertexBufferPtr const &buffer = static_mesh<V>(mesh).vertex_buffer();
	auto buffer_mapping = buffer->read();
	char const *begin = buffer_mapping.as<char const>().data();
	std::vector<char> bytes(begin, begin + buffer->size());
	buffer_mapping.unmap();
	return bytes;
}

std::vector<rove::matrix<4,4>> joint_matrices(edge::Model const &model)
{
	std::vector<rove::matrix<4,4>> result;
	for (auto const &joint : model.joints())
	{
		result.push_back(joint->matrix);
	}
	return result;
}

bool same_matrices(std::vector<rove::matrix<4,4>> const &a, std::vector<rove::matrix<4,4>> const &b)
{
	return a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size() * sizeof(a[0])) == 0;
}

struct CacheFixture
{
	edge::RendererPtr renderer = make_renderer();
	edge::SchedulerPtr scheduler_a = std::make_shared<edge::Scheduler>(renderer);
	edge::SchedulerPtr scheduler_b = std::make_shared<edge::Scheduler>(renderer);
	edge::ModelCache cache{renderer};
};

}

BOOST_FIXTURE_TEST_SUITE(model_cache, CacheFixture)

BOOST_AUTO_TEST_CASE(instances_share_geometry_and_textures_but_not_materials)
{
	auto a = cache.load<edge::StaticModelPTBNT>(asset("test_gltf_loder_directions.glb"), scheduler_a);
	auto b = cache.load<edge::StaticModelPTBNT>(asset("test_gltf_loder_directions.glb"), scheduler_b);

	BOOST_REQUIRE(a != b);
	BOOST_CHECK_EQUAL(cache.prototypes_count(), 1u);
	BOOST_CHECK(a->scheduler() == scheduler_a);
	BOOST_CHECK(b->scheduler() == scheduler_b);
	BOOST_CHECK(a->renderer() == renderer);

	BOOST_REQUIRE(!a->meshes().empty());
	BOOST_REQUIRE_EQUAL(a->meshes().size(), b->meshes().size());
	for (std::size_t i = 0; i < a->meshes().size(); ++i)
	{
		auto const &ma = static_mesh<edge::VERTEX_PTBNT>(a->meshes()[i]);
		auto const &mb = static_mesh<edge::VERTEX_PTBNT>(b->meshes()[i]);
		BOOST_CHECK(a->meshes()[i] != b->meshes()[i]);
		BOOST_CHECK(ma.vertex_buffer() == mb.vertex_buffer());
		BOOST_CHECK(ma.index_buffer() == mb.index_buffer());
		BOOST_CHECK(a->meshes()[i]->model() == a.get());
		BOOST_CHECK(b->meshes()[i]->model() == b.get());
	}

	BOOST_REQUIRE(!a->materials().empty());
	BOOST_REQUIRE_EQUAL(a->materials().size(), b->materials().size());
	std::size_t textured = 0;
	for (std::size_t i = 0; i < a->materials().size(); ++i)
	{
		edge::MaterialPtr const &ma = a->materials()[i];
		edge::MaterialPtr const &mb = b->materials()[i];
		BOOST_CHECK(ma != mb);
		BOOST_CHECK_EQUAL(ma->name(), mb->name());
		BOOST_REQUIRE_EQUAL(ma->diffuse_textures_count(), mb->diffuse_textures_count());
		for (std::size_t t = 0; t < ma->diffuse_textures_count(); ++t)
		{
			BOOST_CHECK(ma->diffuse_texture(t) == mb->diffuse_texture(t));
			++textured;
		}
	}
	// The asset has a texture; without one the sharing check above is vacuous.
	BOOST_CHECK_GT(textured, 0u);
}

BOOST_AUTO_TEST_CASE(triangles_draw_with_the_instances_own_materials)
{
	auto a = cache.load<edge::StaticModelPTBNT>(asset("test_gltf_loder_materials.glb"), scheduler_a);
	auto b = cache.load<edge::StaticModelPTBNT>(asset("test_gltf_loder_materials.glb"), scheduler_a);

	std::size_t checked = 0;
	for (auto const &mesh : a->meshes())
	{
		for (auto const &triangles : mesh->triangles())
		{
			BOOST_REQUIRE(triangles.material_ptr);
			bool in_a = false;
			for (auto const &material : a->materials())
				in_a = in_a || material == triangles.material_ptr;
			for (auto const &material : b->materials())
				BOOST_CHECK(material != triangles.material_ptr);
			BOOST_CHECK(in_a);
			++checked;
		}
	}
	BOOST_CHECK_GT(checked, 1u);
}

BOOST_AUTO_TEST_CASE(material_state_set_on_one_instance_stays_there)
{
	auto a = cache.load<edge::StaticModelPTBNT>(asset("test_gltf_loder_materials.glb"), scheduler_a);
	auto b = cache.load<edge::StaticModelPTBNT>(asset("test_gltf_loder_materials.glb"), scheduler_a);
	rove::vec<4> const before = b->materials()[0]->extra_parameter(0);

	a->set_materials_extra_parameter(0, rove::vec<4>(7.0f, 0.0f, 0.0f, 0.0f));
	a->materials()[0]->set_cull_mode(edge::Material::CullMode::CW);

	BOOST_CHECK_EQUAL(a->materials()[0]->extra_parameter(0).x, 7.0f);
	BOOST_CHECK_EQUAL(b->materials()[0]->extra_parameter(0).x, before.x);
	BOOST_CHECK(b->materials()[0]->cull_mode() != edge::Material::CullMode::CW);

	// And a third instance starts from the file, not from what a did.
	auto c = cache.load<edge::StaticModelPTBNT>(asset("test_gltf_loder_materials.glb"), scheduler_a);
	BOOST_CHECK_EQUAL(c->materials()[0]->extra_parameter(0).x, before.x);
}

BOOST_AUTO_TEST_CASE(an_instance_matches_a_direct_load)
{
	auto direct = std::make_shared<edge::AnimatedModelPTBNJWT>(renderer, scheduler_a);
	direct->load_from_gltf(asset("test_figure.glb"));
	auto instance = cache.load<edge::AnimatedModelPTBNJWT>(asset("test_figure.glb"), scheduler_a);
	// A second instance, so the one compared is not the first copy made.
	instance = cache.load<edge::AnimatedModelPTBNJWT>(asset("test_figure.glb"), scheduler_a);

	BOOST_REQUIRE_EQUAL(instance->meshes().size(), direct->meshes().size());
	BOOST_REQUIRE_EQUAL(instance->joints().size(), direct->joints().size());
	BOOST_REQUIRE_EQUAL(instance->animations().size(), direct->animations().size());
	BOOST_REQUIRE_EQUAL(instance->materials().size(), direct->materials().size());
	for (std::size_t i = 0; i < direct->meshes().size(); ++i)
	{
		BOOST_CHECK(vertex_bytes<edge::VERTEX_PTBNJWT>(instance->meshes()[i]) ==
			vertex_bytes<edge::VERTEX_PTBNJWT>(direct->meshes()[i]));
	}

	for (float t : {0.0f, 0.37f, 1.2f})
	{
		direct->pose(t);
		instance->pose(t);
		BOOST_CHECK(same_matrices(joint_matrices(*instance), joint_matrices(*direct)));
	}

	rove::matrix<4,4> world;
	world.identity();
	rove::aabb<3> const a = instance->bounds(world);
	rove::aabb<3> const b = direct->bounds(world);
	BOOST_CHECK(std::memcmp(&a, &b, sizeof(a)) == 0);
	BOOST_CHECK_EQUAL(instance->build_octree(world).size(), direct->build_octree(world).size());
}

BOOST_AUTO_TEST_CASE(each_instance_poses_alone)
{
	auto a = cache.load<edge::AnimatedModelPTBNJWT>(asset("test_gltf_loder_animation.glb"), scheduler_a);
	auto b = cache.load<edge::AnimatedModelPTBNJWT>(asset("test_gltf_loder_animation.glb"), scheduler_a);
	BOOST_REQUIRE(!a->animations().empty());

	a->pose(-1000.0f);
	b->pose(-1000.0f);
	auto const b_start = joint_matrices(*b);

	// Through the Animation, not the model: pose rebuilds the joints of
	// the model the animation is bound to, which has to be a, not the
	// prototype and not b.
	a->animations()[0]->pose(1000.0f);
	BOOST_CHECK(!same_matrices(joint_matrices(*a), b_start));
	BOOST_CHECK(same_matrices(joint_matrices(*b), b_start));

	for (std::size_t i = 0; i < a->joints().size(); ++i)
	{
		BOOST_CHECK(a->joints()[i] != b->joints()[i]);
		if (auto parent = a->joints()[i]->parent.lock())
		{
			bool in_a = false;
			for (auto const &joint : a->joints())
				in_a = in_a || joint == parent;
			BOOST_CHECK(in_a);
		}
	}
}

BOOST_AUTO_TEST_CASE(morph_weights_move_only_their_own_instance)
{
	// Morph weights are written into the vertex buffer, so a mesh with
	// morph targets cannot share it.
	std::string const path = asset("test_gltf_loder_animation_weights_path.gltf");
	auto a = cache.load<edge::StaticModelPTBNT>(path, scheduler_a);
	auto b = cache.load<edge::StaticModelPTBNT>(path, scheduler_a);
	BOOST_REQUIRE_GT(a->meshes()[0]->morph_targets_count(), 0u);
	BOOST_CHECK(static_mesh<edge::VERTEX_PTBNT>(a->meshes()[0]).vertex_buffer() !=
		static_mesh<edge::VERTEX_PTBNT>(b->meshes()[0]).vertex_buffer());

	a->pose(0.0f);
	b->pose(0.0f);
	auto const b_before = vertex_bytes<edge::VERTEX_PTBNT>(b->meshes()[0]);
	auto const a_before = vertex_bytes<edge::VERTEX_PTBNT>(a->meshes()[0]);

	a->pose(1.0f);
	BOOST_CHECK(vertex_bytes<edge::VERTEX_PTBNT>(a->meshes()[0]) != a_before);
	BOOST_CHECK(vertex_bytes<edge::VERTEX_PTBNT>(b->meshes()[0]) == b_before);

	// And a fresh instance starts at the file's weights, whatever a did.
	auto direct = std::make_shared<edge::StaticModelPTBNT>(renderer, scheduler_a);
	direct->load_from_gltf(path);
	auto c = cache.load<edge::StaticModelPTBNT>(path, scheduler_a);
	BOOST_CHECK(vertex_bytes<edge::VERTEX_PTBNT>(c->meshes()[0]) ==
		vertex_bytes<edge::VERTEX_PTBNT>(direct->meshes()[0]));
}

BOOST_AUTO_TEST_CASE(model_type_is_part_of_the_key)
{
	auto as_static = cache.load<edge::StaticModelPTBNT>(asset("test_gltf_loder_animation.glb"), scheduler_a);
	auto as_animated = cache.load<edge::AnimatedModelPTBNJWT>(asset("test_gltf_loder_animation.glb"), scheduler_a);
	BOOST_CHECK_EQUAL(cache.prototypes_count(), 2u);
	BOOST_CHECK(dynamic_cast<edge::StaticMeshPTBNT const *>(as_static->meshes()[0].get()));
	BOOST_CHECK(dynamic_cast<edge::AnimatedMeshPTBNJWT const *>(as_animated->meshes()[0].get()));
}

BOOST_AUTO_TEST_CASE(edge_files_load_through_the_cache)
{
	std::string const path = "model_cache_test.edge";
	{
		auto source = std::make_shared<edge::StaticModelPTBNT>(renderer, scheduler_a);
		source->load_from_gltf(asset("test_gltf_loder_directions.glb"));
		source->save_to_file(path);
	}
	auto direct = std::make_shared<edge::StaticModelPTBNT>(renderer, scheduler_a);
	direct->load_from_file(path);
	auto instance = cache.load<edge::StaticModelPTBNT>(path, scheduler_a);
	std::remove(path.c_str());

	BOOST_REQUIRE_EQUAL(instance->meshes().size(), direct->meshes().size());
	BOOST_CHECK(vertex_bytes<edge::VERTEX_PTBNT>(instance->meshes()[0]) ==
		vertex_bytes<edge::VERTEX_PTBNT>(direct->meshes()[0]));
}

BOOST_AUTO_TEST_CASE(a_failed_load_caches_nothing)
{
	BOOST_CHECK_THROW(cache.load<edge::StaticModelPTBNT>(asset("no_such_model.glb"), scheduler_a), std::exception);
	BOOST_CHECK_EQUAL(cache.prototypes_count(), 0u);
	BOOST_CHECK_THROW(cache.load<edge::StaticModelPTBNT>(asset("no_such_model.glb"), scheduler_a), std::exception);

	BOOST_CHECK_THROW(cache.load<edge::StaticModelPTBNT>(
		asset("test_gltf_loder_animation_invalid_times.gltf"), scheduler_a), std::exception);
	BOOST_CHECK_EQUAL(cache.prototypes_count(), 0u);
}

BOOST_AUTO_TEST_CASE(a_scheduler_of_another_renderer_is_refused)
{
	edge::RendererPtr other = make_renderer();
	auto foreign = std::make_shared<edge::Scheduler>(other);
	BOOST_CHECK_THROW(cache.load<edge::StaticModelPTBNT>(asset("cone.glb"), foreign), std::invalid_argument);
	BOOST_CHECK_THROW(cache.load<edge::StaticModelPTBNT>(asset("cone.glb"), edge::SchedulerPtr()),
		std::invalid_argument);
}

BOOST_AUTO_TEST_CASE(instances_outlive_a_cleared_cache)
{
	auto a = cache.load<edge::StaticModelPTBNT>(asset("cone.glb"), scheduler_a);
	auto const bytes = vertex_bytes<edge::VERTEX_PTBNT>(a->meshes()[0]);
	cache.clear();
	BOOST_CHECK_EQUAL(cache.prototypes_count(), 0u);
	BOOST_CHECK(vertex_bytes<edge::VERTEX_PTBNT>(a->meshes()[0]) == bytes);

	// Cleared means read again: the new instance has buffers of its own.
	auto b = cache.load<edge::StaticModelPTBNT>(asset("cone.glb"), scheduler_a);
	BOOST_CHECK_EQUAL(cache.prototypes_count(), 1u);
	BOOST_CHECK(static_mesh<edge::VERTEX_PTBNT>(a->meshes()[0]).vertex_buffer() !=
		static_mesh<edge::VERTEX_PTBNT>(b->meshes()[0]).vertex_buffer());
}

// The cache holds its prototypes weakly and each instance holds its prototype: a model is read once for as long as anything draws
// it, and its GPU memory goes with its last instance.

BOOST_AUTO_TEST_CASE(a_model_stays_cached_while_an_instance_of_it_lives)
{
	auto a = cache.load<edge::StaticModelPTBNT>(asset("cone.glb"), scheduler_a);
	auto b = cache.load<edge::StaticModelPTBNT>(asset("cone.glb"), scheduler_b);
	auto const buffer = static_mesh<edge::VERTEX_PTBNT>(a->meshes()[0]).vertex_buffer();
	a.reset();
	auto c = cache.load<edge::StaticModelPTBNT>(asset("cone.glb"), scheduler_a);
	BOOST_CHECK(static_mesh<edge::VERTEX_PTBNT>(c->meshes()[0]).vertex_buffer() == buffer);
	BOOST_CHECK_EQUAL(cache.prototypes_count(), 1u);
}

BOOST_AUTO_TEST_CASE(a_model_no_instance_uses_is_released)
{
	auto a = cache.load<edge::StaticModelPTBNT>(asset("cone.glb"), scheduler_a);
	std::weak_ptr<edge::VertexBuffer> buffer = static_mesh<edge::VERTEX_PTBNT>(a->meshes()[0]).vertex_buffer();
	BOOST_REQUIRE(!buffer.expired());
	a.reset();
	BOOST_CHECK(buffer.expired());
	BOOST_CHECK_EQUAL(cache.prototypes_count(), 0u);
}

BOOST_AUTO_TEST_CASE(a_released_model_is_read_again)
{
	auto a = cache.load<edge::StaticModelPTBNT>(asset("cone.glb"), scheduler_a);
	auto const bytes = vertex_bytes<edge::VERTEX_PTBNT>(a->meshes()[0]);
	a.reset();
	auto b = cache.load<edge::StaticModelPTBNT>(asset("cone.glb"), scheduler_a);
	BOOST_CHECK(vertex_bytes<edge::VERTEX_PTBNT>(b->meshes()[0]) == bytes);
	BOOST_CHECK_EQUAL(cache.prototypes_count(), 1u);
}

BOOST_AUTO_TEST_CASE(sweep_drops_the_entries_of_released_models)
{
	auto a = cache.load<edge::StaticModelPTBNT>(asset("cone.glb"), scheduler_a);
	auto b = cache.load<edge::StaticModelPTBNT>(asset("test_gltf_loder_directions.glb"), scheduler_a);
	a.reset();
	BOOST_CHECK_EQUAL(cache.sweep(), 1u);
	BOOST_CHECK_EQUAL(cache.sweep(), 0u);
	BOOST_CHECK_EQUAL(cache.prototypes_count(), 1u);
}

BOOST_AUTO_TEST_CASE(a_model_loaded_on_its_own_holds_nothing)
{
	auto direct = std::make_shared<edge::StaticModelPTBNT>(renderer, scheduler_a);
	direct->load_from_gltf(asset("cone.glb"));
	BOOST_CHECK(!direct->prototype());
	auto instance = cache.load<edge::StaticModelPTBNT>(asset("cone.glb"), scheduler_a);
	BOOST_REQUIRE(instance->prototype());
	instance->clear();
	BOOST_CHECK(!instance->prototype());
}

BOOST_AUTO_TEST_SUITE_END()
