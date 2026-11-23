// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

// Contract of a mesh's geometry on the CPU (BasicStaticMesh::vertices(), indices()): it is there once, the copy its buffers keep, and
// so what is collided with, skinned and saved is what is drawn.  Instances read one copy; geometry given to one of them is its own.

#include <algorithm>
#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <boost/test/unit_test.hpp>

#include <rove/matrix.h>

#include "animated_mesh.h"
#include "animated_model.h"
#include "generic_vertex.h"
#include "index_buffer.h"
#include "model_cache.h"
#include "renderer.h"
#include "static_mesh.h"
#include "static_model.h"
#include "test_backends.h"
#include "test_fixture.h"
#include "vertex.h"
#include "vertex_buffer.h"

using namespace edge;

namespace
{

using Mesh_ = BasicStaticMesh<VERTEX_PTBNT>;

std::string asset(std::string const &name)
{
	return TestFixture::graphic_sources_path() + "test/data/" + name;
}

template <class B>
std::vector<std::byte> contents(B const &buffer)
{
	auto const mapping = buffer.read();
	return std::vector<std::byte>(mapping.bytes().begin(), mapping.bytes().end());
}

template <class R>
std::vector<std::byte> bytes_of(R const &range)
{
	auto const bytes = std::as_bytes(std::span(range.data(), range.size()));
	return std::vector<std::byte>(bytes.begin(), bytes.end());
}

template <class R>
auto copy_of(R const &range)
{
	return std::vector<std::remove_cvref_t<decltype(range[0])>>(range.begin(), range.end());
}

Mesh_ &mesh_of(std::shared_ptr<StaticModelPTBNT> const &model)
{
	return dynamic_cast<Mesh_ &>(*model->meshes().at(0));
}

std::vector<GenericVertex> moved_to(std::size_t count, float x)
{
	std::vector<GenericVertex> vertices(count);
	for (std::size_t i = 0; i < count; ++i)
		vertices[i].position = rove::vec<3>(x, float(i), 2);
	return vertices;
}

rove::matrix<4,4> identity()
{
	rove::matrix<4,4> m;
	m.identity();
	return m;
}

struct NullCache
{
	std::shared_ptr<NullRenderer> renderer = std::make_shared<NullRenderer>(64, 64);
	ModelCache cache{renderer};

	std::shared_ptr<StaticModelPTBNT> load() { return cache.load<StaticModelPTBNT>(asset("cylinder_30_degrees.glb")); }
};

}

BOOST_AUTO_TEST_SUITE(mesh_geometry)

BOOST_AUTO_TEST_CASE_TEMPLATE(a_meshs_geometry_is_on_the_cpu_once, Fixture, AllBackends)
{
	Fixture fixture;
	ModelCache cache(fixture.renderer);
	auto const a = cache.template load<AnimatedModelPTBNJWT>(asset("test_figure.glb"));
	auto const b = cache.template load<AnimatedModelPTBNJWT>(asset("test_figure.glb"));
	auto const c = cache.template load<StaticModelPTBNT>(asset("cylinder_30_degrees.glb"));
	std::size_t meshes = 0;
	auto const check = [&](auto const &mesh) {
		++meshes;
		BOOST_REQUIRE(!mesh.vertices().empty());
		BOOST_REQUIRE(!mesh.indices().empty());
		BOOST_CHECK(std::as_const(*mesh.vertex_buffer()).read().data() == reinterpret_cast<std::byte const *>(mesh.vertices().data()));
		BOOST_CHECK(std::as_const(*mesh.index_buffer()).read().data() == reinterpret_cast<std::byte const *>(mesh.indices().data()));
		BOOST_CHECK(contents(*mesh.vertex_buffer()) == bytes_of(mesh.vertices()));
		BOOST_CHECK(contents(*mesh.index_buffer()) == bytes_of(mesh.indices()));
	};
	for (auto const &model : {a, b})
		for (auto const &mesh : model->meshes())
			check(dynamic_cast<AnimatedMeshPTBNJWT const &>(*mesh));
	for (auto const &mesh : c->meshes())
		check(dynamic_cast<Mesh_ const &>(*mesh));
	BOOST_TEST(meshes >= 3u);
}

BOOST_AUTO_TEST_CASE(a_mesh_with_no_geometry_has_none)
{
	NullCache f;
	auto const model = f.load();
	Mesh_ const mesh(model.get());
	BOOST_TEST(mesh.vertices().empty());
	BOOST_TEST(mesh.indices().empty());
}

BOOST_FIXTURE_TEST_CASE(vertices_given_to_an_instance_are_its_own, NullCache)
{
	auto const a = load(), b = load();
	Mesh_ &ma = mesh_of(a), &mb = mesh_of(b);
	BOOST_REQUIRE(ma.vertices().data() == mb.vertices().data());
	std::vector<VERTEX_PTBNT> const original = copy_of(ma.vertices());
	auto given = moved_to(original.size(), 70);
	mb.set_vertices(given.data(), GenericVertex::FORMAT_POSITION, given.size());

	BOOST_CHECK(bytes_of(ma.vertices()) == bytes_of(original));              // what the others collide with ...
	BOOST_CHECK(contents(*ma.vertex_buffer()) == bytes_of(original));        // ... and draw
	BOOST_CHECK(ma.vertex_buffer() != mb.vertex_buffer());
	std::vector<VERTEX_PTBNT> expected = original;                           // the components not given stay
	for (std::size_t i = 0; i < expected.size(); ++i)
		expected[i].position = given[i].position;
	BOOST_CHECK(bytes_of(mb.vertices()) == bytes_of(expected));
	BOOST_CHECK(contents(*mb.vertex_buffer()) == bytes_of(expected));
	BOOST_TEST(b->build_octree(identity()).bounds().lo.x >= 70.0f);          // (under the model's node, which scales it)
	BOOST_TEST(a->build_octree(identity()).bounds().lo.x < 10.0f);
	// another instance has the prototype's
	BOOST_CHECK(mesh_of(load()).vertices().data() == ma.vertices().data());
}

BOOST_FIXTURE_TEST_CASE(vertices_set_again_are_written_in_the_buffer_there_is, NullCache)
{
	auto const model = std::make_shared<StaticModelPTBNT>(renderer);
	model->load_from_gltf(asset("cylinder_30_degrees.glb"));
	Mesh_ &mesh = mesh_of(model);
	std::shared_ptr<VertexBuffer> const buffer = mesh.vertex_buffer();
	auto given = moved_to(mesh.vertices().size(), 3);
	mesh.set_vertices(given.data(), GenericVertex::FORMAT_POSITION, given.size());
	BOOST_CHECK(mesh.vertex_buffer() == buffer);       // nothing else has it: whoever holds it sees the new ones
	BOOST_TEST(mesh.vertices()[1].position.x == 3.0f);
}

BOOST_FIXTURE_TEST_CASE(more_or_fewer_vertices_are_as_many_as_are_given, NullCache)
{
	auto const model = std::make_shared<StaticModelPTBNT>(renderer);
	model->load_from_gltf(asset("cylinder_30_degrees.glb"));
	Mesh_ &mesh = mesh_of(model);
	std::size_t const had = mesh.vertices().size();
	for (std::size_t count : {had + 50, had - 7, std::size_t(1)})
	{
		auto given = moved_to(count, float(count));
		mesh.set_vertices(given.data(), VERTEX_PTBNT::FORMAT, count);
		BOOST_TEST(mesh.vertices().size() == count);
		BOOST_TEST(mesh.vertex_buffer()->size() == count * sizeof(VERTEX_PTBNT));
		BOOST_TEST(mesh.vertices().back().position.y == float(count - 1));
		BOOST_CHECK(contents(*mesh.vertex_buffer()) == bytes_of(mesh.vertices()));
	}
}

BOOST_AUTO_TEST_SUITE_END()
