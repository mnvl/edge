// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include <boost/test/unit_test.hpp>

#include "mesh.h"
#include "model_builder.h"
#include "null_renderer.h"
#include "scheduler.h"

namespace
{

std::shared_ptr<edge::ModelBuilder> make_model()
{
	edge::RendererPtr renderer(new edge::NullRenderer(320, 200));
	edge::SchedulerPtr scheduler(new edge::Scheduler(renderer));
	return std::make_shared<edge::ModelBuilder>(renderer, scheduler);
}

}

BOOST_AUTO_TEST_SUITE(mesh)

BOOST_AUTO_TEST_CASE(bind_shape_matrix_defaults_to_identity)
{
	auto model = make_model();
	edge::MeshBuilderPtr mesh = model->add_mesh_builder();

	rove::matrix<4,4> identity;
	identity.identity();

	for (std::size_t col = 0; col < 4; ++col)
	{
		for (std::size_t row = 0; row < 4; ++row)
		{
			BOOST_CHECK_SMALL(mesh->bind_shape_matrix().ij[col][row] - identity.ij[col][row], 1e-6f);
		}
	}
}

BOOST_AUTO_TEST_CASE(morph_targets_count_defaults_to_zero)
{
	auto model = make_model();
	edge::MeshBuilderPtr mesh = model->add_mesh_builder();
	BOOST_CHECK_EQUAL(mesh->morph_targets_count(), 0u);
}

BOOST_AUTO_TEST_CASE(set_morph_weights_no_op_without_targets)
{
	auto model = make_model();
	edge::MeshBuilderPtr mesh = model->add_mesh_builder();
	std::vector<float> weights = {1.0f, 0.5f};
	mesh->set_morph_weights(weights);
	BOOST_CHECK_EQUAL(mesh->morph_targets_count(), 0u);
}

BOOST_AUTO_TEST_CASE(set_morph_targets_no_op_without_vertices)
{
	auto model = make_model();
	edge::MeshBuilderPtr mesh = model->add_mesh_builder();

	edge::GenericVertex delta;
	std::memset(&delta, 0, sizeof(delta));
	delta.position.set(1.0f, 0.0f, 0.0f);

	std::vector<std::vector<edge::GenericVertex>> targets;
	targets.push_back({delta});
	mesh->set_morph_targets(targets, edge::GenericVertex::FORMAT_POSITION);
	BOOST_CHECK_EQUAL(mesh->morph_targets_count(), 0u);
}

BOOST_AUTO_TEST_CASE(triangles_list_initially_empty)
{
	auto model = make_model();
	edge::MeshBuilderPtr mesh = model->add_mesh_builder();
	BOOST_CHECK(mesh->triangles().empty());
}

BOOST_AUTO_TEST_CASE(serialization_type)
{
	auto model = make_model();
	edge::MeshBuilderPtr mesh = model->add_mesh_builder();
	std::string const type = mesh->serialization_type();
	BOOST_CHECK(!type.empty());
	BOOST_CHECK(type.find("static_mesh:") == 0);
}

BOOST_AUTO_TEST_CASE(id_and_name_settable)
{
	auto model = make_model();
	edge::MeshBuilderPtr mesh = model->add_mesh_builder("test_id", "test_name");
	BOOST_CHECK_EQUAL(mesh->id(), "test_id");
	BOOST_CHECK_EQUAL(mesh->name(), "test_name");
}

BOOST_AUTO_TEST_CASE(model_pointer)
{
	auto model = make_model();
	edge::MeshBuilderPtr mesh = model->add_mesh_builder();
	BOOST_CHECK(mesh->model() != nullptr);
	BOOST_CHECK_EQUAL(mesh->model(), model.get());
}

BOOST_AUTO_TEST_SUITE_END()
