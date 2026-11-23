// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

// Contract of a buffer that shares its bytes with whoever made them (VertexBuffer::share(), IndexBuffer::share()): what a mesh does
// with its vertices and indices, which the CPU then holds once.  What is read is what was shared; a write is the buffer's own and
// leaves the shared bytes as they were; the bytes live for as long as the buffer reads them.  A backend that keeps a copy of its
// own (OpenGL ES, Metal) differs in the memory alone.

#include <algorithm>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <boost/test/unit_test.hpp>

#include "animated_mesh.h"
#include "animated_model.h"
#include "generic_vertex.h"
#include "index_buffer.h"
#include "mesh_builder.h"
#include "model_cache.h"
#include "renderer.h"
#include "static_mesh.h"
#include "static_model.h"
#include "test_backends.h"
#include "test_fixture.h"
#include "vertex.h"
#include "vertex_buffer.h"
#include "vertex_traits.h"

using namespace edge;

namespace
{

std::string asset(std::string const &name)
{
	return TestFixture::graphic_sources_path() + "test/data/" + name;
}

std::shared_ptr<std::vector<VERTEX_PT>> some_vertices(std::size_t count, float from)
{
	auto vertices = std::make_shared<std::vector<VERTEX_PT>>(count);
	for (std::size_t i = 0; i < count; ++i)
	{
		(*vertices)[i].position = rove::vec<3>(from + float(i), 2, 3);
		(*vertices)[i].texcoords = rove::vec<2>(float(i), from);
	}
	return vertices;
}

std::shared_ptr<std::vector<std::uint32_t>> some_indices(std::vector<std::uint32_t> values)
{
	return std::make_shared<std::vector<std::uint32_t>>(std::move(values));
}

template <class T>
std::shared_ptr<std::byte const> bytes_of(std::shared_ptr<std::vector<T>> const &vector)
{
	return std::shared_ptr<std::byte const>(vector, reinterpret_cast<std::byte const *>(vector->data()));
}

template <class B>
std::vector<std::byte> contents(B const &buffer)
{
	auto const mapping = buffer.read();
	return std::vector<std::byte>(mapping.bytes().begin(), mapping.bytes().end());
}

template <class T>
std::vector<std::byte> contents_of(std::vector<T> const &vector)
{
	auto const *begin = reinterpret_cast<std::byte const *>(vector.data());
	return std::vector<std::byte>(begin, begin + vector.size() * sizeof(T));
}

/// Whether a renderer's buffers hold what is shared with them themselves, not a copy.
template <class Fixture>
bool shares(Fixture const &)
{
#ifdef EDGE_USE_VULKAN
	if (std::is_same_v<Fixture, VulkanFixture>)
		return true;
#endif
	return std::is_same_v<Fixture, NullFixture>;
}

}

BOOST_AUTO_TEST_SUITE(shared_buffers)

BOOST_AUTO_TEST_CASE_TEMPLATE(a_buffer_reads_what_is_shared_with_it, Fixture, AllBackends)
{
	Fixture fixture;
	auto const vertices = some_vertices(5, 10);
	std::shared_ptr<VertexBuffer> const buffer = fixture.renderer->create_vertex_buffer(VertexTraits::of<VERTEX_PT>(), 5 * sizeof(VERTEX_PT));
	buffer->share(bytes_of(vertices), 5 * sizeof(VERTEX_PT));
	BOOST_TEST(buffer->size() == 5 * sizeof(VERTEX_PT));
	BOOST_CHECK(contents(*buffer) == contents_of(*vertices));
	if (shares(fixture))
		BOOST_CHECK(std::as_const(*buffer).read().data() == bytes_of(vertices).get());

	auto const indices = some_indices({0, 1, 2, 2, 3, 4});
	std::shared_ptr<IndexBuffer> const index_buffer = fixture.renderer->create_index_buffer(IndexFormat::U32, 24);
	index_buffer->share(bytes_of(indices), 24);
	BOOST_CHECK(contents(*index_buffer) == contents_of(*indices));
	if (shares(fixture))
		BOOST_CHECK(std::as_const(*index_buffer).read().data() == bytes_of(indices).get());
}

BOOST_AUTO_TEST_CASE_TEMPLATE(a_buffer_keeps_what_it_shares_and_lets_go_of_it, Fixture, AllBackends)
{
	Fixture fixture;
	auto vertices = some_vertices(4, 1);
	std::vector<std::byte> const expected = contents_of(*vertices);
	std::weak_ptr<std::vector<VERTEX_PT>> const watched = vertices;
	std::shared_ptr<VertexBuffer> buffer = fixture.renderer->create_vertex_buffer(VertexTraits::of<VERTEX_PT>(), 4 * sizeof(VERTEX_PT));
	buffer->share(bytes_of(vertices), 4 * sizeof(VERTEX_PT));
	vertices.reset();
	BOOST_CHECK(contents(*buffer) == expected);       // whoever shared it is gone
	BOOST_CHECK(watched.expired() != shares(fixture));

	auto const others = some_vertices(4, 7);           // shared again: the first are let go
	buffer->share(bytes_of(others), 4 * sizeof(VERTEX_PT));
	BOOST_CHECK(watched.expired());
	BOOST_CHECK(contents(*buffer) == contents_of(*others));
	buffer.reset();
	BOOST_TEST(others.use_count() == 1);
}

BOOST_AUTO_TEST_CASE_TEMPLATE(a_write_is_the_buffers_own, Fixture, AllBackends)
{
	Fixture fixture;
	for (Contents how : {Contents::KEEP, Contents::DISCARD})
	{
		auto const vertices = some_vertices(3, 20);
		std::vector<std::byte> const before = contents_of(*vertices);
		std::shared_ptr<VertexBuffer> const buffer = fixture.renderer->create_vertex_buffer(VertexTraits::of<VERTEX_PT>(), 3 * sizeof(VERTEX_PT));
		buffer->share(bytes_of(vertices), 3 * sizeof(VERTEX_PT));
		{
			auto mapping = buffer->write(how);
			std::span<VERTEX_PT> const written = mapping.template as<VERTEX_PT>();
			BOOST_REQUIRE(written.size() == 3u);
			if (how == Contents::DISCARD)
				std::copy(vertices->begin(), vertices->end(), written.begin());
			written[1].position.x = -5;
		}
		BOOST_CHECK(contents_of(*vertices) == before);   // the shared bytes are as they were
		std::vector<VERTEX_PT> expected = *vertices;
		expected[1].position.x = -5;
		BOOST_CHECK(contents(*buffer) == contents_of(expected));  // and the rest of the buffer's are what was shared
		BOOST_TEST(vertices.use_count() == 1);            // no longer shared
		{
			auto mapping = buffer->write();               // its own now: written in place
			mapping.template as<VERTEX_PT>()[0].position.x = 9;
		}
		expected[0].position.x = 9;
		BOOST_CHECK(contents(*buffer) == contents_of(expected));
	}
}

BOOST_AUTO_TEST_CASE_TEMPLATE(the_highest_index_is_of_the_bytes_there_are_now, Fixture, AllBackends)
{
	Fixture fixture;
	std::shared_ptr<IndexBuffer> const buffer = fixture.renderer->create_index_buffer(IndexFormat::U32, 24);
	buffer->share(bytes_of(some_indices({0, 1, 2, 9, 3, 4})), 24);
	BOOST_TEST(buffer->highest_index(0, 6) == 9u);
	BOOST_TEST(buffer->highest_index(0, 3) == 2u);
	buffer->share(bytes_of(some_indices({7, 1, 2, 0, 3, 4})), 24);
	BOOST_TEST(buffer->highest_index(0, 6) == 7u);       // not what it was of the bytes before
	BOOST_TEST(buffer->highest_index(3, 3) == 4u);
	buffer->write().template as<std::uint32_t>()[5] = 30;
	BOOST_TEST(buffer->highest_index(3, 3) == 30u);
}

BOOST_AUTO_TEST_CASE_TEMPLATE(bytes_that_are_not_the_buffers_are_refused_and_leave_it_as_it_was, Fixture, AllBackends)
{
	Fixture fixture;
	auto const vertices = some_vertices(4, 1);
	std::shared_ptr<VertexBuffer> const buffer = fixture.renderer->create_vertex_buffer(VertexTraits::of<VERTEX_PT>(), 4 * sizeof(VERTEX_PT));
	buffer->share(bytes_of(vertices), 4 * sizeof(VERTEX_PT));
	std::vector<std::byte> const before = contents(*buffer);
	auto const others = some_vertices(5, 50);
	BOOST_CHECK_THROW(buffer->share(bytes_of(others), 5 * sizeof(VERTEX_PT)), std::invalid_argument);
	BOOST_CHECK_THROW(buffer->share(bytes_of(others), 3 * sizeof(VERTEX_PT)), std::invalid_argument);
	BOOST_CHECK_THROW(buffer->share(nullptr, 4 * sizeof(VERTEX_PT)), std::invalid_argument);
	BOOST_CHECK(contents(*buffer) == before);
	BOOST_TEST(others.use_count() == 1);

	std::shared_ptr<IndexBuffer> const index_buffer = fixture.renderer->create_index_buffer(IndexFormat::U16, 12);
	BOOST_CHECK_THROW(index_buffer->share(bytes_of(some_indices({0, 1, 2, 3})), 16), std::invalid_argument);
	BOOST_CHECK_THROW(index_buffer->share(nullptr, 12), std::invalid_argument);
}

BOOST_AUTO_TEST_CASE_TEMPLATE(a_buffer_of_no_bytes_shares_none, Fixture, AllBackends)
{
	Fixture fixture;
	std::shared_ptr<IndexBuffer> const buffer = fixture.renderer->create_index_buffer(IndexFormat::U32, 0);
	buffer->share(nullptr, 0);
	buffer->share(bytes_of(some_indices({})), 0);
	BOOST_TEST(std::as_const(*buffer).read().size() == 0u);
}

// -- what meshes do with it --------------------------------------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE_TEMPLATE(a_meshs_vertices_and_indices_are_on_the_cpu_once, Fixture, AllBackends)
{
	Fixture fixture;
	ModelCache cache(fixture.renderer);
	auto const a = cache.load<AnimatedModelPTBNJWT>(asset("test_figure.glb"));
	auto const b = cache.load<AnimatedModelPTBNJWT>(asset("test_figure.glb"));
	auto const c = cache.load<StaticModelPTBNT>(asset("cylinder_30_degrees.glb"));
	std::size_t meshes = 0;
	auto const check = [&](auto const &mesh) {
		++meshes;
		BOOST_REQUIRE(!mesh.vertices().empty());
		BOOST_REQUIRE(!mesh.indices().empty());
		BOOST_CHECK(contents(*mesh.vertex_buffer()) == contents_of(mesh.vertices()));
		BOOST_CHECK(contents(*mesh.index_buffer()) == contents_of(mesh.indices()));
		if (shares(fixture))
		{
			BOOST_CHECK(std::as_const(*mesh.vertex_buffer()).read().data() == reinterpret_cast<std::byte const *>(mesh.vertices().data()));
			BOOST_CHECK(std::as_const(*mesh.index_buffer()).read().data() == reinterpret_cast<std::byte const *>(mesh.indices().data()));
		}
	};
	for (auto const &model : {a, b})
		for (auto const &mesh : model->meshes())
			check(dynamic_cast<AnimatedMeshPTBNJWT const &>(*mesh));
	for (auto const &mesh : c->meshes())
		check(dynamic_cast<BasicStaticMesh<VERTEX_PTBNT> const &>(*mesh));
	BOOST_TEST(meshes >= 3u);
}

BOOST_AUTO_TEST_CASE(a_morphed_mesh_s_buffer_is_its_own_and_its_vertices_stay)
{
	auto const renderer = std::make_shared<NullRenderer>(64, 64);
	auto const model = std::make_shared<StaticModelPTBNT>(renderer);
	model->load_from_gltf(asset("cylinder_30_degrees.glb"));
	auto &mesh = dynamic_cast<BasicStaticMesh<VERTEX_PTBNT> &>(*model->meshes().at(0));
	std::vector<std::byte> const base = contents_of(mesh.vertices());
	std::vector<std::vector<GenericVertex>> targets(1, std::vector<GenericVertex>(mesh.vertices().size()));
	for (GenericVertex &delta : targets[0])
		delta.position = rove::vec<3>(0, 0, 4);
	mesh.set_morph_targets(targets, GenericVertex::FORMAT_POSITION);
	mesh.set_morph_weights({0.5f});
	BOOST_CHECK(contents_of(mesh.vertices()) == base);
	auto const mapping = std::as_const(*mesh.vertex_buffer()).read();
	std::span<VERTEX_PTBNT const> const drawn = mapping.as<VERTEX_PTBNT const>();
	for (std::size_t i = 0; i < drawn.size(); ++i)
		BOOST_REQUIRE_CLOSE(drawn[i].position.z + 100.0f, mesh.vertices()[i].position.z + 102.0f, 1e-4);
}

BOOST_AUTO_TEST_CASE(vertices_set_again_are_drawn_and_a_component_set_alone_leaves_the_others)
{
	auto const renderer = std::make_shared<NullRenderer>(64, 64);
	auto const model = std::make_shared<StaticModelPTBNT>(renderer);
	model->load_from_gltf(asset("cylinder_30_degrees.glb"));
	auto &mesh = dynamic_cast<BasicStaticMesh<VERTEX_PTBNT> &>(*model->meshes().at(0));
	std::vector<VERTEX_PTBNT> expected = mesh.vertices();
	std::vector<GenericVertex> moved(expected.size());
	for (std::size_t i = 0; i < moved.size(); ++i)
	{
		moved[i].position = rove::vec<3>(float(i), 1, 2);
		expected[i].position = moved[i].position;
	}
	mesh.set_vertices(moved.data(), GenericVertex::FORMAT_POSITION, moved.size());
	BOOST_CHECK(contents_of(mesh.vertices()) == contents_of(expected));
	BOOST_CHECK(contents(*mesh.vertex_buffer()) == contents_of(expected));
}

BOOST_AUTO_TEST_SUITE_END()
