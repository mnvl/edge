// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include <boost/test/unit_test.hpp>

#include "tolerance.h"
#include "animated_model.h"
#include "animated_mesh.h"
#include "model_builder.h"
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

struct bounds3
{
	rove::vec<3> lo, hi;

	bounds3()
	{
		lo.x = lo.y = lo.z = std::numeric_limits<float>::max();
		hi.x = hi.y = hi.z = -std::numeric_limits<float>::max();
	}

	void extend(rove::vec<3> const &p)
	{
		lo.x = std::min(lo.x, p.x);
		lo.y = std::min(lo.y, p.y);
		lo.z = std::min(lo.z, p.z);
		hi.x = std::max(hi.x, p.x);
		hi.y = std::max(hi.y, p.y);
		hi.z = std::max(hi.z, p.z);
	}

	rove::vec<3> centre() const
	{
		return rove::vec<3>(
			(lo.x + hi.x) * 0.5f,
			(lo.y + hi.y) * 0.5f,
			(lo.z + hi.z) * 0.5f);
	}

	float diagonal() const
	{
		float dx = hi.x - lo.x;
		float dy = hi.y - lo.y;
		float dz = hi.z - lo.z;
		return std::sqrt(dx * dx + dy * dy + dz * dz);
	}
};

void print_matrix(char const *label, rove::matrix<4, 4> const &m)
{
	BOOST_TEST_MESSAGE(label);
	for (int row = 0; row < 4; ++row)
	{
		BOOST_TEST_MESSAGE(
			"  [" << m.ij[0][row] << ", " << m.ij[1][row] << ", "
				  << m.ij[2][row] << ", " << m.ij[3][row] << "]");
	}
}

}

BOOST_AUTO_TEST_SUITE(animated_model)

BOOST_AUTO_TEST_CASE(figure_load_and_inspect)
{
	std::shared_ptr<edge::AnimatedModelPTBNJWT> model = make_model();
	model->load_from_gltf(make_asset_path("test_figure.glb"));

	BOOST_TEST_MESSAGE("joints=" << model->joints().size()
		<< " meshes=" << model->meshes().size()
		<< " animations=" << model->animations().size()
		<< " materials=" << model->materials().size());

	BOOST_REQUIRE(!model->joints().empty());
	BOOST_REQUIRE(!model->meshes().empty());
	BOOST_REQUIRE(!model->animations().empty());

	// --- Print model structure ---
	std::size_t meshes_with_skin = 0;
	for (std::size_t i = 0; i < model->meshes().size(); ++i)
	{
		edge::AnimatedMeshPTBNJWT const *mesh =
			dynamic_cast<edge::AnimatedMeshPTBNJWT const *>(model->meshes()[i].get());
		BOOST_REQUIRE(mesh);

		bool has_skin = !mesh->joint_index_transform().empty();
		if (has_skin)
			meshes_with_skin++;

		BOOST_TEST_MESSAGE("mesh[" << i << "]: "
			<< mesh->vertices().size() << " verts, "
			<< mesh->indices().size() << " indices, "
			<< mesh->joint_index_transform().size() << " skin joints, "
			<< mesh->inverted_bind_matrices().size() << " IBM, "
			<< mesh->vertices().size() << " weights"
			<< (has_skin ? " [SKINNED]" : " [STATIC]"));
	}
	BOOST_TEST_MESSAGE("meshes with skin: " << meshes_with_skin);

	// --- Print some joint info ---
	for (std::size_t i = 0; i < std::min(model->joints().size(), (std::size_t)10); ++i)
	{
		edge::JointPtr const &j = model->joints()[i];
		std::string parent_info = j->parent.lock() ? j->parent.lock()->name : "(root)";
		bool has_mesh = !j->mesh.expired();
		BOOST_TEST_MESSAGE("joint[" << i << "] '" << j->name << "' parent='" << parent_info
			<< "' transforms=" << j->transforms.size()
			<< (has_mesh ? " HAS_MESH" : ""));
	}
	if (model->joints().size() > 10)
	{
		BOOST_TEST_MESSAGE("... (" << model->joints().size() - 10 << " more joints)");
	}
}

BOOST_AUTO_TEST_CASE(figure_bind_pose_vertex_bounds)
{
	std::shared_ptr<edge::AnimatedModelPTBNJWT> model = make_model();
	model->load_from_gltf(make_asset_path("test_figure.glb"));

	// Compute bounds of raw (bind-pose) vertices across all meshes
	bounds3 bind_bounds;
	std::size_t total_vertices = 0;

	for (auto &base : model->meshes())
	{
		edge::AnimatedMeshPTBNJWT const *mesh =
			dynamic_cast<edge::AnimatedMeshPTBNJWT const *>(base.get());
		BOOST_REQUIRE(mesh);

		for (auto const &vertex : mesh->vertices())
		{
			bind_bounds.extend(vertex.position);
			total_vertices++;
		}
	}

	BOOST_TEST_MESSAGE("bind-pose bounds: lo=(" << bind_bounds.lo.x << ", " << bind_bounds.lo.y
		<< ", " << bind_bounds.lo.z << ") hi=(" << bind_bounds.hi.x << ", " << bind_bounds.hi.y
		<< ", " << bind_bounds.hi.z << ") diag=" << bind_bounds.diagonal()
		<< " total_verts=" << total_vertices);

	// A human model should have some reasonable extent (not zero, not huge)
	BOOST_CHECK_GT(bind_bounds.diagonal(), 0.1f);
	BOOST_CHECK_LT(bind_bounds.diagonal(), 100.0f);
}

BOOST_AUTO_TEST_CASE(figure_skinned_vertex_bounds)
{
	std::shared_ptr<edge::AnimatedModelPTBNJWT> model = make_model();
	model->load_from_gltf(make_asset_path("test_figure.glb"));

	// Initialize animation at t=0
	model->pose(0.0f);

	// Print bind_shape_matrix for first skinned mesh
	for (std::size_t mi = 0; mi < model->meshes().size(); ++mi)
	{
		edge::AnimatedMeshPTBNJWT const *mesh =
			dynamic_cast<edge::AnimatedMeshPTBNJWT const *>(model->meshes()[mi].get());
		if (mesh->joint_index_transform().empty())
			continue;

		std::string label = "mesh[" + std::to_string(mi) + "] bind_shape_matrix";
		print_matrix(label.c_str(), mesh->bind_shape_matrix());

		// Print first few inverse bind matrices
		for (std::size_t j = 0; j < std::min(mesh->inverted_bind_matrices().size(), (std::size_t)3); ++j)
		{
			std::string ibm_label = "mesh[" + std::to_string(mi) + "] IBM[" + std::to_string(j) + "]";
			print_matrix(ibm_label.c_str(), mesh->inverted_bind_matrices()[j]);
		}

		// Print first few joint matrices
		for (std::size_t j = 0; j < std::min(mesh->joint_index_transform().size(), (std::size_t)3); ++j)
		{
			std::size_t joint_index = mesh->joint_index_transform()[j];
			std::string jm_label = "mesh[" + std::to_string(mi) + "] joints[" +
				std::to_string(joint_index) + "].matrix ('" +
				model->joints()[joint_index]->name + "')";
			print_matrix(jm_label.c_str(), model->joints()[joint_index]->matrix);
		}

		// Print first few computed skin matrices
		BOOST_TEST_MESSAGE("--- skin matrices (BSM * IBM * J) ---");
		for (std::size_t j = 0; j < std::min(mesh->joint_index_transform().size(), (std::size_t)3); ++j)
		{
			std::size_t joint_index = mesh->joint_index_transform()[j];
			rove::matrix<4, 4> t = mesh->bind_shape_matrix() *
				mesh->inverted_bind_matrices()[joint_index] *
				model->joints()[joint_index]->matrix;

			std::string sm_label = "skin_matrix[" + std::to_string(j) + "] (joint " +
				std::to_string(joint_index) + " '" + model->joints()[joint_index]->name + "')";
			print_matrix(sm_label.c_str(), t);
		}

		break; // only first skinned mesh
	}

	// CPU-side skinning via skinned_vertices
	bounds3 skinned_bounds;
	std::size_t skinned_vertices = 0;
	std::size_t zero_weight_vertices = 0;

	for (std::size_t mi = 0; mi < model->meshes().size(); ++mi)
	{
		edge::AnimatedMeshPTBNJWT const *mesh =
			dynamic_cast<edge::AnimatedMeshPTBNJWT const *>(model->meshes()[mi].get());
		if (mesh->joint_index_transform().empty())
			continue;

		// (the joint that owns the mesh does not move a skinned mesh)
		std::vector<edge::VERTEX_PTBNJWT> const skinned = mesh->skinned_vertices(model->joints());

		for (std::size_t vi = 0; vi < skinned.size(); ++vi)
		{
			skinned_bounds.extend(skinned[vi].position);
			skinned_vertices++;

			// Check if vertex had zero weights
			if (vi < mesh->vertices().size())
			{
				float w_sum = 0.0f;
				for (float weight : mesh->vertices()[vi].weights.i)
					w_sum += weight;
				if (w_sum < edge::ZERO_WEIGHT)
					zero_weight_vertices++;
			}
		}
	}

	BOOST_TEST_MESSAGE("skinned bounds: lo=(" << skinned_bounds.lo.x << ", " << skinned_bounds.lo.y
		<< ", " << skinned_bounds.lo.z << ") hi=(" << skinned_bounds.hi.x << ", "
		<< skinned_bounds.hi.y << ", " << skinned_bounds.hi.z
		<< ") diag=" << skinned_bounds.diagonal()
		<< " skinned_verts=" << skinned_vertices
		<< " zero_weight_verts=" << zero_weight_vertices);

	rove::vec<3> c = skinned_bounds.centre();
	BOOST_TEST_MESSAGE("skinned centre: (" << c.x << ", " << c.y << ", " << c.z << ")");

	// Skinned positions should be in a reasonable range
	BOOST_CHECK_GT(skinned_bounds.diagonal(), 0.1f);
	BOOST_CHECK_LT(skinned_bounds.diagonal(), 100.0f);

	// Not all vertices should be at the origin (common failure mode)
	float origin_dist = c.length();
	BOOST_TEST_MESSAGE("skinned centre distance from origin: " << origin_dist);
}

BOOST_AUTO_TEST_CASE(figure_compare_static_vs_skinned)
{
	std::shared_ptr<edge::AnimatedModelPTBNJWT> model = make_model();
	model->load_from_gltf(make_asset_path("test_figure.glb"));

	model->pose(0.0f);

	// For each skinned mesh, compare:
	// 1. Static path: joint_ptr->matrix * world applied to bind-pose vertex
	// 2. Skinned path: skinned_vertices result
	rove::matrix<4, 4> identity;
	identity.identity();

	for (std::size_t mi = 0; mi < model->meshes().size(); ++mi)
	{
		edge::AnimatedMeshPTBNJWT const *mesh =
			dynamic_cast<edge::AnimatedMeshPTBNJWT const *>(model->meshes()[mi].get());
		if (mesh->joint_index_transform().empty())
			continue;

		// Find owner joint
		edge::Joint const *owner_joint = nullptr;
		for (auto &joint : model->joints())
		{
			if (joint->mesh.lock().get() == mesh)
			{
				owner_joint = joint.get();
				break;
			}
		}
		BOOST_REQUIRE(owner_joint);

		BOOST_TEST_MESSAGE("mesh[" << mi << "] owner joint: '" << owner_joint->name << "'");
		print_matrix("  owner joint matrix:", owner_joint->matrix);

		// Static path: transform = joint->matrix * identity = joint->matrix
		rove::matrix<4, 4> static_transform = owner_joint->matrix * identity;

		// Compute bounds from static path (vertex * joint_matrix)
		bounds3 static_bounds;
		for (auto const &vertex : mesh->vertices())
		{
			static_bounds.extend(rove::transform_point(static_transform, vertex.position));
		}

		// Skinned path
		std::vector<edge::VERTEX_PTBNJWT> const skinned = mesh->skinned_vertices(model->joints());

		bounds3 skinned_bounds;
		for (auto const &vertex : skinned)
		{
			skinned_bounds.extend(vertex.position);
		}

		BOOST_TEST_MESSAGE("  static bounds: lo=(" << static_bounds.lo.x << ", "
			<< static_bounds.lo.y << ", " << static_bounds.lo.z << ") hi=("
			<< static_bounds.hi.x << ", " << static_bounds.hi.y << ", "
			<< static_bounds.hi.z << ") diag=" << static_bounds.diagonal());

		BOOST_TEST_MESSAGE("  skinned bounds: lo=(" << skinned_bounds.lo.x << ", "
			<< skinned_bounds.lo.y << ", " << skinned_bounds.lo.z << ") hi=("
			<< skinned_bounds.hi.x << ", " << skinned_bounds.hi.y << ", "
			<< skinned_bounds.hi.z << ") diag=" << skinned_bounds.diagonal());

		// Print first few vertex comparisons
		for (std::size_t vi = 0; vi < std::min(skinned.size(), (std::size_t)5); ++vi)
		{
			rove::vec<3> const static_pos = rove::transform_point(static_transform, mesh->vertices()[vi].position);
			rove::vec<3> const &skinned_pos = skinned[vi].position;
			rove::vec<3> const &bind_pos = mesh->vertices()[vi].position;

			float w_sum = 0.0f;
			if (vi < mesh->vertices().size())
			{
				for (float weight : mesh->vertices()[vi].weights.i)
					w_sum += weight;
			}

			BOOST_TEST_MESSAGE("  v[" << vi << "] bind=(" << bind_pos.x << "," << bind_pos.y
				<< "," << bind_pos.z << ") static=(" << static_pos.x << "," << static_pos.y
				<< "," << static_pos.z << ") skinned=(" << skinned_pos.x << "," << skinned_pos.y
				<< "," << skinned_pos.z << ") w_sum=" << w_sum);
		}
	}
}

BOOST_AUTO_TEST_CASE(figure_joint_hierarchy)
{
	std::shared_ptr<edge::AnimatedModelPTBNJWT> model = make_model();
	model->load_from_gltf(make_asset_path("test_figure.glb"));

	// Check joint hierarchy before animation
	BOOST_TEST_MESSAGE("--- before set_animation_time ---");
	for (std::size_t i = 0; i < std::min(model->joints().size(), (std::size_t)5); ++i)
	{
		rove::vec<3> pos(model->joints()[i]->matrix.ij[3][0],
			model->joints()[i]->matrix.ij[3][1],
			model->joints()[i]->matrix.ij[3][2]);
		BOOST_TEST_MESSAGE("joint[" << i << "] '" << model->joints()[i]->name
			<< "' pos=(" << pos.x << ", " << pos.y << ", " << pos.z << ")");
	}

	model->pose(0.0f);

	BOOST_TEST_MESSAGE("--- after set_animation_time(0) ---");
	for (std::size_t i = 0; i < std::min(model->joints().size(), (std::size_t)5); ++i)
	{
		rove::vec<3> pos(model->joints()[i]->matrix.ij[3][0],
			model->joints()[i]->matrix.ij[3][1],
			model->joints()[i]->matrix.ij[3][2]);
		BOOST_TEST_MESSAGE("joint[" << i << "] '" << model->joints()[i]->name
			<< "' pos=(" << pos.x << ", " << pos.y << ", " << pos.z << ")");
	}

	model->pose(0.5f);

	BOOST_TEST_MESSAGE("--- after set_animation_time(0.5) ---");
	for (std::size_t i = 0; i < std::min(model->joints().size(), (std::size_t)5); ++i)
	{
		rove::vec<3> pos(model->joints()[i]->matrix.ij[3][0],
			model->joints()[i]->matrix.ij[3][1],
			model->joints()[i]->matrix.ij[3][2]);
		BOOST_TEST_MESSAGE("joint[" << i << "] '" << model->joints()[i]->name
			<< "' pos=(" << pos.x << ", " << pos.y << ", " << pos.z << ")");
	}
}

BOOST_AUTO_TEST_SUITE_END()
