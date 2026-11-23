// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include <cmath>
#include <cstring>
#include <boost/test/unit_test.hpp>

#include "model_builder.h"
#include "null_renderer.h"
#include "scheduler.h"
#include "test_fixture.h"

namespace
{

std::shared_ptr<edge::ModelBuilder> make_model()
{
	std::shared_ptr<edge::Renderer> renderer(new edge::NullRenderer(320, 200));
	std::shared_ptr<edge::Scheduler> scheduler(new edge::Scheduler(renderer));
	return std::make_shared<edge::ModelBuilder>(renderer);
}

std::string make_asset_path(std::string const &asset_name)
{
	return edge::TestFixture::graphic_sources_path() + "test/data/" + asset_name;
}

}

BOOST_AUTO_TEST_SUITE(static_mesh)

BOOST_AUTO_TEST_CASE(set_vertices_and_indices)
{
	auto model = make_model();
	std::shared_ptr<edge::MeshBuilder> mesh = model->add_mesh_builder();

	std::vector<edge::MeshBuilder::vertex_type> verts(3);
	std::memset(verts.data(), 0, sizeof(verts[0]) * verts.size());
	verts[0].position.set(0.0f, 0.0f, 0.0f);
	verts[1].position.set(1.0f, 0.0f, 0.0f);
	verts[2].position.set(0.0f, 1.0f, 0.0f);

	std::vector<edge::MeshBuilder::index_type> indices = {0, 1, 2};
	mesh->set_geometry(verts, indices);

	BOOST_CHECK_EQUAL(mesh->vertex_buffer().size(), 3u);
	BOOST_CHECK_EQUAL(mesh->index_buffer().size(), 3u);
}

BOOST_AUTO_TEST_CASE(append_geometry)
{
	auto model = make_model();
	std::shared_ptr<edge::MeshBuilder> mesh = model->add_mesh_builder();

	std::vector<edge::MeshBuilder::vertex_type> verts(3);
	std::memset(verts.data(), 0, sizeof(verts[0]) * verts.size());
	verts[0].position.set(0.0f, 0.0f, 0.0f);
	verts[1].position.set(1.0f, 0.0f, 0.0f);
	verts[2].position.set(0.0f, 1.0f, 0.0f);

	std::vector<edge::MeshBuilder::index_type> indices = {0, 1, 2};
	mesh->append_geometry(verts, indices);

	BOOST_CHECK_EQUAL(mesh->vertex_buffer().size(), 3u);
	BOOST_CHECK_EQUAL(mesh->index_buffer().size(), 3u);
	BOOST_CHECK_EQUAL(mesh->triangles().size(), 1u);

	// Append more
	mesh->append_geometry(verts, indices);
	BOOST_CHECK_EQUAL(mesh->vertex_buffer().size(), 6u);
	BOOST_CHECK_EQUAL(mesh->index_buffer().size(), 6u);
	BOOST_CHECK_EQUAL(mesh->triangles().size(), 2u);
}

BOOST_AUTO_TEST_CASE(clear_geometry)
{
	auto model = make_model();
	std::shared_ptr<edge::MeshBuilder> mesh = model->add_mesh_builder();

	std::vector<edge::MeshBuilder::vertex_type> verts(3);
	std::memset(verts.data(), 0, sizeof(verts[0]) * verts.size());
	std::vector<edge::MeshBuilder::index_type> indices = {0, 1, 2};
	mesh->set_geometry(verts, indices);
	mesh->add_triangles(std::shared_ptr<edge::Material>(), 0, 1);

	mesh->clear_geometry();
	BOOST_CHECK(mesh->vertex_buffer().empty());
	BOOST_CHECK(mesh->index_buffer().empty());
	BOOST_CHECK(mesh->triangles().empty());
}

BOOST_AUTO_TEST_CASE(add_triangles)
{
	auto model = make_model();
	std::shared_ptr<edge::MeshBuilder> mesh = model->add_mesh_builder();

	std::vector<edge::MeshBuilder::vertex_type> verts(6);
	std::memset(verts.data(), 0, sizeof(verts[0]) * verts.size());
	verts[0].position.set(0.0f, 0.0f, 0.0f);
	verts[1].position.set(1.0f, 0.0f, 0.0f);
	verts[2].position.set(0.0f, 1.0f, 0.0f);
	verts[3].position.set(2.0f, 0.0f, 0.0f);
	verts[4].position.set(3.0f, 0.0f, 0.0f);
	verts[5].position.set(2.0f, 1.0f, 0.0f);

	std::vector<edge::MeshBuilder::index_type> indices = {0, 1, 2, 3, 4, 5};
	mesh->set_geometry(verts, indices);

	std::shared_ptr<edge::Material> mat = model->renderer()->create_material();
	mesh->add_triangles(mat, 0, 1);
	mesh->add_triangles(mat, 3, 1);

	BOOST_CHECK_EQUAL(mesh->triangles().size(), 2u);
	BOOST_CHECK_EQUAL(mesh->triangles()[0].triangles_count, 1u);
	BOOST_CHECK_EQUAL(mesh->triangles()[0].first_index_offset, 0u);
	BOOST_CHECK_EQUAL(mesh->triangles()[1].first_index_offset, 3u);
}

BOOST_AUTO_TEST_CASE(serialization_type_contains_format)
{
	auto model = make_model();
	std::shared_ptr<edge::MeshBuilder> mesh = model->add_mesh_builder();
	std::string type = mesh->serialization_type();
	// Should contain the vertex format number
	BOOST_CHECK(type.find("static_mesh:") == 0);
	BOOST_CHECK(type.size() > std::string("static_mesh:").size());
}

BOOST_AUTO_TEST_CASE(loaded_model_mesh_has_bounds)
{
	auto model = make_model();
	model->load_from_gltf(make_asset_path("test_gltf_loder_plane.glb"));

	// The loaded model should have valid vertex data
	BOOST_REQUIRE(!model->meshes().empty());
	edge::MeshBuilder const *mesh = dynamic_cast<edge::MeshBuilder const *>(model->meshes()[0].get());
	BOOST_REQUIRE(mesh);
	BOOST_CHECK(!mesh->vertex_buffer().empty());
}

BOOST_AUTO_TEST_SUITE_END()
