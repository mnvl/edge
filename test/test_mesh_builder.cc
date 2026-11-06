// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include <cmath>
#include <cstring>
#include <stdexcept>
#include <boost/test/unit_test.hpp>

#include "camera.h"
#include "model_builder.h"
#include "null_renderer.h"
#include "scheduler.h"
#include "test_fixture.h"

namespace
{

std::shared_ptr<edge::ModelBuilder> make_model()
{
	edge::RendererPtr renderer(new edge::NullRenderer(320, 200));
	edge::SchedulerPtr scheduler(new edge::Scheduler(renderer));
	return std::make_shared<edge::ModelBuilder>(renderer, scheduler);
}

std::string make_asset_path(std::string const &asset_name)
{
	return edge::TestFixture::graphic_sources_path() + "test/data/" + asset_name;
}

edge::MeshBuilderPtr make_triangle_mesh(edge::ModelBuilder &model)
{
	edge::MeshBuilderPtr mesh = model.add_mesh_builder();

	std::vector<edge::MeshBuilder::vertex_type> verts(3);
	std::memset(verts.data(), 0, sizeof(verts[0]) * verts.size());
	verts[0].position.set(0.0f, 0.0f, 0.0f);
	verts[0].normal.set(0.0f, 0.0f, 1.0f);
	verts[1].position.set(1.0f, 0.0f, 0.0f);
	verts[1].normal.set(0.0f, 0.0f, 1.0f);
	verts[2].position.set(0.0f, 1.0f, 0.0f);
	verts[2].normal.set(0.0f, 0.0f, 1.0f);

	std::vector<edge::MeshBuilder::index_type> indices = {0, 1, 2};
	mesh->set_geometry(verts, indices);
	mesh->add_triangles(edge::MaterialPtr(), 0, 1);
	return mesh;
}

}

BOOST_AUTO_TEST_SUITE(mesh_builder)

BOOST_AUTO_TEST_CASE(set_geometry_sets_vertex_and_index_buffers)
{
	auto model = make_model();
	edge::MeshBuilderPtr mesh = make_triangle_mesh(*model);

	BOOST_CHECK_EQUAL(mesh->vertex_buffer().size(), 3u);
	BOOST_CHECK_EQUAL(mesh->index_buffer().size(), 3u);
	BOOST_CHECK_EQUAL(mesh->triangles().size(), 1u);
}

BOOST_AUTO_TEST_CASE(set_vertices_generic)
{
	auto model = make_model();
	edge::MeshBuilderPtr mesh = model->add_mesh_builder();

	edge::GenericVertex verts[3];
	std::memset(verts, 0, sizeof(verts));
	verts[0].position.set(10.0f, 20.0f, 30.0f);
	verts[1].position.set(11.0f, 21.0f, 31.0f);
	verts[2].position.set(12.0f, 22.0f, 32.0f);
	verts[0].normal.set(0.0f, 0.0f, 1.0f);
	verts[1].normal.set(0.0f, 0.0f, 1.0f);
	verts[2].normal.set(0.0f, 0.0f, 1.0f);

	mesh->set_vertices(verts,
		edge::GenericVertex::FORMAT_POSITION | edge::GenericVertex::FORMAT_NORMAL, 3);

	BOOST_CHECK_EQUAL(mesh->vertex_buffer().size(), 3u);
	BOOST_CHECK_SMALL(std::abs(mesh->vertex_buffer()[0].position.x - 10.0f), 1e-6f);
	BOOST_CHECK_SMALL(std::abs(mesh->vertex_buffer()[0].normal.z - 1.0f), 1e-6f);
}

BOOST_AUTO_TEST_CASE(set_indices_generic)
{
	auto model = make_model();
	edge::MeshBuilderPtr mesh = model->add_mesh_builder();

	edge::GenericVertex verts[3];
	std::memset(verts, 0, sizeof(verts));
	mesh->set_vertices(verts, edge::GenericVertex::FORMAT_POSITION, 3);

	unsigned indices[] = {2, 1, 0};
	mesh->set_indices(indices, 3);

	BOOST_CHECK_EQUAL(mesh->index_buffer().size(), 3u);
	BOOST_CHECK_EQUAL(static_cast<unsigned>(mesh->index_buffer()[0]), 2u);
	BOOST_CHECK_EQUAL(static_cast<unsigned>(mesh->index_buffer()[1]), 1u);
	BOOST_CHECK_EQUAL(static_cast<unsigned>(mesh->index_buffer()[2]), 0u);
}

BOOST_AUTO_TEST_CASE(morph_targets_applied_to_position)
{
	auto model = make_model();
	edge::MeshBuilderPtr mesh = model->add_mesh_builder();

	std::vector<edge::MeshBuilder::vertex_type> verts(2);
	std::memset(verts.data(), 0, sizeof(verts[0]) * verts.size());
	verts[0].position.set(0.0f, 0.0f, 0.0f);
	verts[1].position.set(1.0f, 0.0f, 0.0f);
	std::vector<edge::MeshBuilder::index_type> indices = {0, 1, 0};
	mesh->set_geometry(verts, indices);

	edge::GenericVertex delta;
	std::memset(&delta, 0, sizeof(delta));
	delta.position.set(0.0f, 5.0f, 0.0f);

	std::vector<std::vector<edge::GenericVertex>> targets;
	targets.push_back({delta, delta});
	mesh->set_morph_targets(targets, edge::GenericVertex::FORMAT_POSITION);
	BOOST_CHECK_EQUAL(mesh->morph_targets_count(), 1u);

	mesh->set_morph_weights({0.5f});
	BOOST_CHECK_SMALL(std::abs(mesh->vertex_buffer()[0].position.y - 2.5f), 1e-4f);
	BOOST_CHECK_SMALL(std::abs(mesh->vertex_buffer()[1].position.y - 2.5f), 1e-4f);
}

BOOST_AUTO_TEST_CASE(a_mesh_builder_holds_no_skin)
{
	// it is saved as a static mesh: joints and weights are refused, not dropped
	auto model = make_model();
	edge::MeshBuilderPtr mesh = model->add_mesh_builder();
	std::vector<edge::GenericVertex> vertices(3);
	std::memset(vertices.data(), 0, sizeof(vertices[0]) * vertices.size());
	for (auto format : {edge::GenericVertex::FORMAT_JOINT_INDICES, edge::GenericVertex::FORMAT_JOINT_WEIGHTS})
		BOOST_CHECK_THROW(mesh->set_vertices(vertices.data(), edge::GenericVertex::FORMAT_POSITION | format, vertices.size()), std::invalid_argument);
}

BOOST_AUTO_TEST_CASE(a_model_builder_does_not_draw)
{
	auto model = make_model();
	make_triangle_mesh(*model);
	rove::matrix<4,4> identity;
	identity.identity();
	BOOST_CHECK_THROW(model->draw(std::make_shared<edge::Camera>(), identity), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(add_triangles_out_of_range_throws)
{
	auto model = make_model();
	edge::MeshBuilderPtr mesh = model->add_mesh_builder();

	std::vector<edge::MeshBuilder::vertex_type> verts(3);
	std::memset(verts.data(), 0, sizeof(verts[0]) * verts.size());
	std::vector<edge::MeshBuilder::index_type> indices = {0, 1, 2};
	mesh->set_geometry(verts, indices);

	// Out of bounds: offset > indices size
	BOOST_CHECK_THROW(mesh->add_triangles(edge::MaterialPtr(), 100, 1), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(shallow_copy)
{
	auto model = make_model();
	edge::MeshBuilderPtr mesh = make_triangle_mesh(*model);
	mesh->set_id("original_id");
	mesh->set_name("original_name");

	edge::MeshPtr copy = mesh->shallow_copy();
	BOOST_REQUIRE(copy);
	BOOST_CHECK_EQUAL(copy->id(), "original_id");
	BOOST_CHECK_EQUAL(copy->name(), "original_name");
	BOOST_CHECK_EQUAL(copy->triangles().size(), mesh->triangles().size());
}

BOOST_AUTO_TEST_SUITE_END()
