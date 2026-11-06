// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include <cmath>
#include <boost/test/unit_test.hpp>

#include "animated_mesh.h"
#include "animated_model.h"
#include "mesh.h"
#include "null_renderer.h"
#include "scheduler.h"
#include "test_fixture.h"

namespace
{

std::shared_ptr<edge::AnimatedModelPTBNJWT> make_model()
{
	edge::RendererPtr renderer(new edge::NullRenderer(320, 200));
	edge::SchedulerPtr scheduler(new edge::Scheduler(renderer));
	return std::make_shared<edge::AnimatedModelPTBNJWT>(renderer, scheduler);
}

std::string make_asset_path(std::string const &asset_name)
{
	return edge::TestFixture::graphic_sources_path() + "test/data/" + asset_name;
}

std::shared_ptr<edge::AnimatedModelPTBNJWT> load_model(std::string const &asset_name)
{
	auto model = make_model();
	model->load_from_gltf(make_asset_path(asset_name));
	return model;
}

float matrix_max_delta(rove::matrix<4,4> const &a, rove::matrix<4,4> const &b)
{
	float max_delta = 0.0f;
	for (std::size_t col = 0; col < 4; ++col)
		for (std::size_t row = 0; row < 4; ++row)
			max_delta = std::max(max_delta, std::abs(a.ij[col][row] - b.ij[col][row]));
	return max_delta;
}

}

BOOST_AUTO_TEST_SUITE(animated_mesh)

BOOST_AUTO_TEST_CASE(joint_model_has_skin_data)
{
	auto model = load_model("test_gltf_loder_joint.glb");
	BOOST_REQUIRE(!model->meshes().empty());

	edge::AnimatedMeshPTBNJWT const *mesh = dynamic_cast<edge::AnimatedMeshPTBNJWT const *>(model->meshes()[0].get());
	BOOST_REQUIRE(mesh);

	// Animated mesh should have joint index transform and inverted bind matrices
	BOOST_CHECK(!mesh->joint_index_transform().empty());
	BOOST_CHECK(!mesh->inverted_bind_matrices().empty());
}

BOOST_AUTO_TEST_CASE(bind_shape_matrix_is_identity_for_joint_model)
{
	auto model = load_model("test_gltf_loder_joint.glb");
	BOOST_REQUIRE(!model->meshes().empty());

	edge::AnimatedMeshPTBNJWT const *mesh = dynamic_cast<edge::AnimatedMeshPTBNJWT const *>(model->meshes()[0].get());
	BOOST_REQUIRE(mesh);

	rove::matrix<4,4> identity;
	identity.identity();

	BOOST_CHECK_SMALL(matrix_max_delta(mesh->bind_shape_matrix(), identity), 1e-4f);
}

BOOST_AUTO_TEST_CASE(vertex_weights_normalized)
{
	auto model = load_model("test_gltf_loder_joint.glb");
	BOOST_REQUIRE(!model->meshes().empty());

	edge::AnimatedMeshPTBNJWT const *mesh = dynamic_cast<edge::AnimatedMeshPTBNJWT const *>(model->meshes()[0].get());
	BOOST_REQUIRE(mesh);

	for (auto const &v : mesh->vertices())
	{
		float weight_sum = 0.0f;
		for (std::size_t j = 0; j < 8; ++j)
		{
			weight_sum += v.weights.i[j];
		}
		BOOST_CHECK_SMALL(std::abs(weight_sum - 1.0f), 1e-4f);
	}
}

BOOST_AUTO_TEST_CASE(skinned_vertices_change_positions)
{
	auto model = load_model("test_gltf_loder_animation.glb");
	BOOST_REQUIRE(!model->meshes().empty());
	BOOST_REQUIRE(!model->animations().empty());

	edge::AnimatedMeshPTBNJWT const *mesh = dynamic_cast<edge::AnimatedMeshPTBNJWT const *>(model->meshes()[0].get());
	BOOST_REQUIRE(mesh);

	// Set to start
	model->pose(-1000.0f);
	std::vector<edge::VERTEX_PTBNJWT> const verts_start = mesh->skinned_vertices(model->joints());

	// Set to end
	model->pose(1000.0f);
	std::vector<edge::VERTEX_PTBNJWT> const verts_end = mesh->skinned_vertices(model->joints());

	BOOST_REQUIRE_EQUAL(verts_start.size(), verts_end.size());

	bool has_motion = false;
	for (std::size_t i = 0; i < verts_start.size(); ++i)
	{
		float dx = verts_start[i].position.x - verts_end[i].position.x;
		float dy = verts_start[i].position.y - verts_end[i].position.y;
		float dz = verts_start[i].position.z - verts_end[i].position.z;
		if (std::abs(dx) + std::abs(dy) + std::abs(dz) > 1e-4f)
		{
			has_motion = true;
			break;
		}
	}
	BOOST_CHECK(has_motion);
}

BOOST_AUTO_TEST_CASE(joint_index_transform_valid_indices)
{
	auto model = load_model("test_gltf_loder_animation.glb");
	BOOST_REQUIRE(!model->meshes().empty());

	edge::AnimatedMeshPTBNJWT const *mesh = dynamic_cast<edge::AnimatedMeshPTBNJWT const *>(model->meshes()[0].get());
	BOOST_REQUIRE(mesh);

	for (unsigned long i : mesh->joint_index_transform())
	{
		BOOST_CHECK_LT(i, model->joints().size());
	}
}

BOOST_AUTO_TEST_CASE(joint_count_within_shader_limit)
{
	auto model = load_model("test_gltf_loder_joint.glb");
	BOOST_REQUIRE(!model->meshes().empty());

	edge::AnimatedMeshPTBNJWT const *mesh = dynamic_cast<edge::AnimatedMeshPTBNJWT const *>(model->meshes()[0].get());
	BOOST_REQUIRE(mesh);

	BOOST_CHECK_LE(mesh->joint_index_transform().size(), edge::MAX_JOINT_MATRICES);
}

BOOST_AUTO_TEST_CASE(animation_changes_joint_matrices)
{
	auto model = load_model("test_gltf_loder_animation.glb");
	BOOST_REQUIRE(!model->animations().empty());

	model->pose(-1000.0f);
	std::vector<rove::matrix<4,4>> matrices_start;
	for (auto const &j : model->joints())
		matrices_start.push_back(j->matrix);

	model->pose(1000.0f);

	bool has_motion = false;
	for (std::size_t i = 0; i < model->joints().size(); ++i)
	{
		if (matrix_max_delta(matrices_start[i], model->joints()[i]->matrix) > 1e-4f)
		{
			has_motion = true;
			break;
		}
	}
	BOOST_CHECK(has_motion);
}

BOOST_AUTO_TEST_SUITE_END()
