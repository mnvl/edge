// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <set>
#include <string>

#include <boost/test/unit_test.hpp>

#include "animated_mesh.h"
#include "animated_model.h"
#include "gltf_loader.h"
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

/// The model of a skinned file: the one that holds its skin.
std::shared_ptr<edge::AnimatedModelPTBNJWT> make_skinned_model()
{
	edge::RendererPtr renderer(new edge::NullRenderer(320, 200));

	edge::SchedulerPtr scheduler(new edge::Scheduler(renderer));
	return std::make_shared<edge::AnimatedModelPTBNJWT>(renderer, scheduler);
}

std::string make_asset_path(std::string const &asset_name)
{
	return edge::TestFixture::graphic_sources_path() + "test/data/" + asset_name;
}

std::shared_ptr<edge::ModelBuilder> load_model_from_asset(std::string const &asset_name)
{
	std::shared_ptr<edge::ModelBuilder> model = make_model();
	model->load_from_gltf(make_asset_path(asset_name));
	return model;
}

std::shared_ptr<edge::AnimatedModelPTBNJWT> load_skinned_model_from_asset(std::string const &asset_name)
{
	std::shared_ptr<edge::AnimatedModelPTBNJWT> model = make_skinned_model();
	model->load_from_gltf(make_asset_path(asset_name));
	return model;
}

void expect_load_error_contains(edge::Model &model, std::string const &asset_name, std::string const &substring)
{
	try
	{
		model.load_from_gltf(make_asset_path(asset_name));
		BOOST_FAIL("expected edge::GltfLoader::load_error");
	}
	catch (edge::GltfLoader::load_error const &error)
	{
		std::string const message = error.what();
		BOOST_TEST_MESSAGE("load_error: " << message);
		BOOST_CHECK(message.find(substring) != std::string::npos);
	}
}

void expect_load_error_contains(std::string const &asset_name, std::string const &substring)
{
	expect_load_error_contains(*make_model(), asset_name, substring);
}

void expect_skinned_load_error_contains(std::string const &asset_name, std::string const &substring)
{
	expect_load_error_contains(*make_skinned_model(), asset_name, substring);
}

void check_axis_aligned_unit_normal(rove::vec<3> const &normal)
{
	float const nx = std::abs(normal.x);
	float const ny = std::abs(normal.y);
	float const nz = std::abs(normal.z);

	std::vector<float> components;
	components.push_back(nx);
	components.push_back(ny);
	components.push_back(nz);
	std::sort(components.begin(), components.end());

	BOOST_CHECK_GT(components[2], 0.999f);
	BOOST_CHECK_LT(components[1], 1e-3f);
	BOOST_CHECK_LT(components[0], 1e-3f);
}

float matrix_max_abs_delta(rove::matrix<4,4> const &lhs, rove::matrix<4,4> const &rhs)
{
	float max_delta = 0.0f;
	for (std::size_t col = 0; col < 4; ++col)
	{
		for (std::size_t row = 0; row < 4; ++row)
		{
			max_delta = std::max(max_delta, std::abs(lhs.ij[col][row] - rhs.ij[col][row]));
		}
	}
	return max_delta;
}

std::vector<edge::MeshBuilder::vertex_type> const &vertices_of(edge::MeshBuilder const &mesh)
{
	return mesh.vertex_buffer();
}

std::vector<edge::MeshBuilder::index_type> const &indices_of(edge::MeshBuilder const &mesh)
{
	return mesh.index_buffer();
}

std::vector<edge::VERTEX_PTBNJWT> const &vertices_of(edge::AnimatedMeshPTBNJWT const &mesh)
{
	return mesh.vertices();
}

std::vector<edge::AnimatedMeshPTBNJWT::index_type> const &indices_of(edge::AnimatedMeshPTBNJWT const &mesh)
{
	return mesh.indices();
}

/// @p model's joints and meshes hang together; every mesh is a @p LoadedMesh.
template<class LoadedMesh>
void check_loaded_model(edge::Model const &model)
{
	BOOST_REQUIRE(!model.meshes().empty());
	BOOST_REQUIRE(!model.joints().empty());

	bool has_root_joint = false;
	std::size_t joints_with_mesh = 0;

	for (std::size_t i = 0; i < model.joints().size(); ++i)
	{
		edge::JointPtr const &joint = model.joints()[i];
		BOOST_REQUIRE(joint);

		if (!joint->parent.lock())
		{
			has_root_joint = true;
		}

		for (std::size_t child_index = 0; child_index < joint->children.size(); ++child_index)
		{
			edge::JointPtr child = joint->children[child_index].lock();
			BOOST_REQUIRE(child);
			BOOST_CHECK_EQUAL(child->parent.lock().get(), joint.get());
		}

		if (edge::MeshPtr mesh = joint->mesh.lock())
		{
			joints_with_mesh++;
			BOOST_CHECK(std::find(model.meshes().begin(), model.meshes().end(), mesh) != model.meshes().end());
		}
	}

	BOOST_REQUIRE(has_root_joint);
	BOOST_REQUIRE_GT(joints_with_mesh, 0u);

	for (auto const &mesh : model.meshes())
	{
			BOOST_REQUIRE(mesh);
		BOOST_REQUIRE(!mesh->triangles().empty());

		LoadedMesh const *loader_mesh = dynamic_cast<LoadedMesh const *>(mesh.get());
		BOOST_REQUIRE(loader_mesh);
		BOOST_REQUIRE_GE(vertices_of(*loader_mesh).size(), 3u);
		BOOST_REQUIRE_GE(indices_of(*loader_mesh).size(), 3u);

		for (auto const &triangles : mesh->triangles())
		{
				BOOST_REQUIRE(triangles.material_ptr);
			BOOST_REQUIRE_GT(triangles.triangles_count, 0u);

			std::size_t const index_begin = triangles.first_index_offset;
			std::size_t const index_end = index_begin + triangles.triangles_count * 3;
			BOOST_REQUIRE_LE(index_end, indices_of(*loader_mesh).size());

			unsigned min_index = std::numeric_limits<unsigned>::max();
			unsigned max_index = 0;
			for (std::size_t index_offset = index_begin; index_offset < index_end; ++index_offset)
			{
				unsigned const index = indices_of(*loader_mesh)[index_offset];
				BOOST_REQUIRE_LT(index, vertices_of(*loader_mesh).size());
				min_index = std::min(min_index, index);
				max_index = std::max(max_index, index);
			}

			BOOST_CHECK_EQUAL(triangles.minimal_vertex_index, min_index);
			BOOST_CHECK_EQUAL(triangles.vertices_count, static_cast<std::size_t>(max_index - min_index + 1));
		}
	}
}

void check_common_loaded_model(edge::ModelBuilder const &model)
{
	check_loaded_model<edge::MeshBuilder>(model);
}

void check_common_loaded_model(edge::AnimatedModelPTBNJWT const &model)
{
	check_loaded_model<edge::AnimatedMeshPTBNJWT>(model);
}

}

BOOST_AUTO_TEST_SUITE(gltf_loader)

/// The .edge bytes of a model loaded from @p path, written to @p out.
std::string edge_bytes_of(std::string const &path, std::filesystem::path const &out)
{
	std::shared_ptr<edge::ModelBuilder> model = make_model();
	model->load_from_gltf(path);
	model->save_to_file(out.string());
	std::ifstream in(out, std::ios::binary);
	return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

BOOST_AUTO_TEST_CASE(a_buffer_in_a_file_of_its_own_is_the_same_model_as_one_in_the_gltf)
{
	std::filesystem::path const dir = std::filesystem::temp_directory_path() / "edge_gltf_external_buffer";
	std::filesystem::remove_all(dir);
	std::filesystem::create_directories(dir);
	std::string const outside = edge_bytes_of(make_asset_path("test_external_buffer.gltf"), dir / "outside.edge");
	std::string const inside = edge_bytes_of(make_asset_path("test_uv_set_1.gltf"), dir / "inside.edge");
	BOOST_CHECK(!outside.empty());
	BOOST_CHECK(outside == inside);
	std::filesystem::remove_all(dir);
}

BOOST_AUTO_TEST_CASE(a_buffer_file_that_is_not_there_is_an_error_that_names_it)
{
	std::filesystem::path const dir = std::filesystem::temp_directory_path() / "edge_gltf_missing_buffer";
	std::filesystem::remove_all(dir);
	std::filesystem::create_directories(dir);
	std::filesystem::copy_file(make_asset_path("test_external_buffer.gltf"), dir / "model.gltf");
	std::filesystem::copy_file(make_asset_path("test_white.png"), dir / "test_white.png");
	std::shared_ptr<edge::ModelBuilder> model = make_model();
	try
	{
		model->load_from_gltf((dir / "model.gltf").string());
		BOOST_FAIL("a missing buffer file went unnoticed");
	}
	catch (edge::GltfLoader::load_error const &e)
	{
		BOOST_CHECK_MESSAGE(std::string(e.what()).find("test_external_buffer.bin") != std::string::npos, e.what());
	}
	std::filesystem::remove_all(dir);
}

BOOST_AUTO_TEST_CASE(plane)
{
	std::shared_ptr<edge::ModelBuilder> model = load_model_from_asset("test_gltf_loder_plane.glb");
	check_common_loaded_model(*model);

	BOOST_REQUIRE_EQUAL(model->meshes().size(), 1u);
	BOOST_REQUIRE_EQUAL(model->joints().size(), 1u);
	BOOST_REQUIRE_EQUAL(model->animations().size(), 0u);
	BOOST_REQUIRE_EQUAL(model->materials().size(), 1u);
	BOOST_CHECK_EQUAL(model->materials()[0]->name(), "lambert1");

	edge::MeshBuilder const *mesh = dynamic_cast<edge::MeshBuilder const *>(model->meshes()[0].get());
	BOOST_REQUIRE(mesh);

	std::vector<edge::MeshBuilder::vertex_type> const &vertex_buffer = mesh->vertex_buffer();
	std::vector<edge::MeshBuilder::index_type> const &index_buffer = mesh->index_buffer();
	BOOST_REQUIRE_EQUAL(vertex_buffer.size(), 4u);
	BOOST_REQUIRE_EQUAL(index_buffer.size(), 6u);

	rove::vec<3> min_position = vertex_buffer[0].position;
	rove::vec<3> max_position = vertex_buffer[0].position;
	for (std::size_t i = 1; i < vertex_buffer.size(); ++i)
	{
		min_position.x = std::min(min_position.x, vertex_buffer[i].position.x);
		min_position.y = std::min(min_position.y, vertex_buffer[i].position.y);
		min_position.z = std::min(min_position.z, vertex_buffer[i].position.z);
		max_position.x = std::max(max_position.x, vertex_buffer[i].position.x);
		max_position.y = std::max(max_position.y, vertex_buffer[i].position.y);
		max_position.z = std::max(max_position.z, vertex_buffer[i].position.z);
	}

	std::vector<float> spans;
	spans.push_back(max_position.x - min_position.x);
	spans.push_back(max_position.y - min_position.y);
	spans.push_back(max_position.z - min_position.z);
	std::sort(spans.begin(), spans.end());
	BOOST_CHECK_SMALL(std::abs(spans[0]), 1e-6f);
	BOOST_CHECK_SMALL(std::abs(spans[1] - 1.0f), 1e-6f);
	BOOST_CHECK_SMALL(std::abs(spans[2] - 1.0f), 1e-6f);

	rove::vec<3> const normal0 = vertex_buffer[0].normal;
	BOOST_CHECK_SMALL(std::abs(normal0.length_sq() - 1.0f), 1e-4f);
	check_axis_aligned_unit_normal(normal0);
	for (auto const &vertex : vertex_buffer)
	{
		BOOST_CHECK_SMALL(std::abs(vertex.normal.length_sq() - 1.0f), 1e-4f);
		BOOST_CHECK_GT(std::abs(rove::dot(vertex.normal, normal0)), 0.999f);
	}

	std::set<unsigned> plane_indices;
	for (unsigned int i : index_buffer)
	{
		plane_indices.insert(i);
	}
	BOOST_CHECK_EQUAL(plane_indices.size(), 4u);
	BOOST_CHECK(plane_indices.count(0) == 1u);
	BOOST_CHECK(plane_indices.count(1) == 1u);
	BOOST_CHECK(plane_indices.count(2) == 1u);
	BOOST_CHECK(plane_indices.count(3) == 1u);

	edge::JointPtr const &joint_ptr = model->joints()[0];
	BOOST_REQUIRE(joint_ptr);
	BOOST_REQUIRE_EQUAL(joint_ptr->transforms.size(), 1u);
	BOOST_CHECK_EQUAL(joint_ptr->transforms[0].sid, "gltf_node_transform");
	BOOST_CHECK_SMALL(std::abs(joint_ptr->matrix.ij[3][0] - 1.0f), 1e-6f);
	BOOST_CHECK_SMALL(std::abs(joint_ptr->matrix.ij[3][1] - 3.0f), 1e-6f);
	BOOST_CHECK_SMALL(std::abs(joint_ptr->matrix.ij[3][2] - 0.5f), 1e-6f);
}

BOOST_AUTO_TEST_CASE(joint)
{
	std::shared_ptr<edge::AnimatedModelPTBNJWT> model = load_skinned_model_from_asset("test_gltf_loder_joint.glb");
	check_common_loaded_model(*model);

	BOOST_REQUIRE_EQUAL(model->meshes().size(), 1u);
	BOOST_REQUIRE_EQUAL(model->joints().size(), 3u);
	BOOST_REQUIRE_EQUAL(model->animations().size(), 2u);
	BOOST_CHECK_EQUAL(model->animations()[0]->name, "ArmatureAction");
	BOOST_CHECK_EQUAL(model->animations()[1]->name, "ArmatureAction.001");

	BOOST_REQUIRE(model->joints()[0]->parent.lock() == model->joints()[2]);
	BOOST_REQUIRE(model->joints()[1]->parent.lock() == model->joints()[2]);
	BOOST_REQUIRE(!model->joints()[2]->parent.lock());

	std::size_t roots_count = 0;
	for (auto &joint : model->joints())
	{
		if (!joint->parent.lock())
		{
			roots_count++;
		}
	}
	BOOST_REQUIRE_EQUAL(roots_count, 1u);

	edge::AnimatedMeshPTBNJWT const *mesh = dynamic_cast<edge::AnimatedMeshPTBNJWT const *>(model->meshes()[0].get());
	BOOST_REQUIRE(mesh);
	BOOST_REQUIRE_EQUAL(mesh->vertices().size(), 3u);
	BOOST_REQUIRE_EQUAL(mesh->indices().size(), 3u);

	BOOST_REQUIRE_EQUAL(mesh->joint_index_transform().size(), 1u);
	BOOST_CHECK_LT(mesh->joint_index_transform()[0], model->joints().size());
	BOOST_REQUIRE_EQUAL(mesh->inverted_bind_matrices().size(), model->joints().size());

	bool has_weighted_vertex = false;
	for (auto const &v : mesh->vertices())
	{
		float weight_sum = 0.0f;
		for (std::size_t j = 0; j < 8; ++j)
		{
			weight_sum += v.weights.i[j];
		}
		BOOST_CHECK_SMALL(std::abs(weight_sum - 1.0f), 1e-4f);
		if (weight_sum > 0.0f)
		{
			has_weighted_vertex = true;
		}
	}
	BOOST_CHECK(has_weighted_vertex);

	model->pose(-1000.0f);
	std::vector<rove::matrix<4,4>> joint_matrices_start(model->joints().size());
	for (std::size_t i = 0; i < model->joints().size(); ++i)
	{
		joint_matrices_start[i] = model->joints()[i]->matrix;
	}

	model->pose(1000.0f);
	bool has_joint_motion = false;
	for (std::size_t i = 0; i < model->joints().size(); ++i)
	{
		if (matrix_max_abs_delta(joint_matrices_start[i], model->joints()[i]->matrix) > 1e-4f)
		{
			has_joint_motion = true;
			break;
		}
	}
	BOOST_CHECK(has_joint_motion);
}

// (test/make_test_skin_errors.py)
BOOST_AUTO_TEST_CASE(a_joint_index_outside_the_skin_is_an_error)
{
	// not a weight moved onto joint 0
	expect_skinned_load_error_contains("test_skin_bad_joint_index.glb", "joint 5");
}

BOOST_AUTO_TEST_CASE(joints_without_weights_are_an_error)
{
	expect_skinned_load_error_contains("test_skin_joints_no_weights.glb", "WEIGHTS_0");
}

BOOST_AUTO_TEST_CASE(a_skin_of_more_joints_than_a_draw_has_matrices_for_is_an_error)
{
	// joint indices are a byte in a vertex and a draw has 256 joint matrices: joint 256 would be joint 0
	expect_skinned_load_error_contains("test_skin_257_joints.glb", "257 joints");
}

BOOST_AUTO_TEST_CASE(animation)
{
	std::shared_ptr<edge::AnimatedModelPTBNJWT> model = load_skinned_model_from_asset("test_gltf_loder_animation.glb");
	check_common_loaded_model(*model);
	BOOST_REQUIRE_EQUAL(model->meshes().size(), 1u);
	BOOST_REQUIRE_EQUAL(model->joints().size(), 5u);
	BOOST_REQUIRE_EQUAL(model->animations().size(), 1u);
	BOOST_CHECK_EQUAL(model->animations()[0]->name, "ArmatureAction");
	BOOST_REQUIRE(!model->animations()[0]->samplers.empty());

	BOOST_REQUIRE(model->joints()[0]->parent.lock() == model->joints()[1]);
	BOOST_REQUIRE(model->joints()[1]->parent.lock() == model->joints()[2]);
	BOOST_REQUIRE(model->joints()[2]->parent.lock() == model->joints()[4]);
	BOOST_REQUIRE(model->joints()[3]->parent.lock() == model->joints()[4]);
	BOOST_REQUIRE(!model->joints()[4]->parent.lock());

	edge::AnimatedMeshPTBNJWT const *mesh = dynamic_cast<edge::AnimatedMeshPTBNJWT const *>(model->meshes().front().get());
	BOOST_REQUIRE(mesh);
	BOOST_REQUIRE_EQUAL(mesh->joint_index_transform().size(), 3u);
	BOOST_REQUIRE_EQUAL(mesh->joint_index_transform()[0], 2u);
	BOOST_REQUIRE_EQUAL(mesh->joint_index_transform()[1], 1u);
	BOOST_REQUIRE_EQUAL(mesh->joint_index_transform()[2], 0u);

	model->pose(-1000.0f);
	std::vector<rove::matrix<4,4>> joint_matrices_start(model->joints().size());
	for (std::size_t i = 0; i < model->joints().size(); ++i)
	{
		joint_matrices_start[i] = model->joints()[i]->matrix;
	}
	std::vector<edge::VERTEX_PTBNJWT> const vertices_start = mesh->skinned_vertices(model->joints());

	model->pose(1000.0f);
	std::vector<edge::VERTEX_PTBNJWT> const vertices_end = mesh->skinned_vertices(model->joints());

	bool has_joint_motion = false;
	for (std::size_t i = 0; i < model->joints().size(); ++i)
	{
		if (matrix_max_abs_delta(joint_matrices_start[i], model->joints()[i]->matrix) > 1e-4f)
		{
			has_joint_motion = true;
			break;
		}
	}
	BOOST_CHECK(has_joint_motion);

	BOOST_REQUIRE_EQUAL(vertices_start.size(), vertices_end.size());
	BOOST_REQUIRE_EQUAL(vertices_start.size(), 4u);

	bool has_weighted_vertex = false;
	for (auto const &vertex : mesh->vertices())
	{
		float weight_sum = 0.0f;
		for (std::size_t j = 0; j < 8; ++j)
		{
			weight_sum += vertex.weights.i[j];
		}
		if (weight_sum > 0.0f)
		{
			has_weighted_vertex = true;
			break;
		}
	}
	BOOST_CHECK(has_weighted_vertex);
}

BOOST_AUTO_TEST_CASE(animation_cubic_spline_non_linear)
{
	std::shared_ptr<edge::ModelBuilder> model = load_model_from_asset("test_gltf_loder_animation_cubic_valid.gltf");
	check_common_loaded_model(*model);

	BOOST_REQUIRE_EQUAL(model->joints().size(), 1u);
	BOOST_REQUIRE_EQUAL(model->animations().size(), 1u);
	BOOST_REQUIRE_EQUAL(model->animations()[0]->samplers.size(), 1u);

	model->pose(0.0f);
	float const x0 = model->joints()[0]->matrix.ij[3][0];
	model->pose(0.5f);
	float const xmid = model->joints()[0]->matrix.ij[3][0];
	model->pose(1.0f);
	float const x1 = model->joints()[0]->matrix.ij[3][0];

	BOOST_CHECK_SMALL(std::abs(x0 - 0.0f), 1e-4f);
	BOOST_CHECK_SMALL(std::abs(x1 - 1.0f), 1e-4f);

	float const linear_mid = 0.5f * (x0 + x1);
	BOOST_CHECK_GT(xmid, linear_mid + 0.1f);
	BOOST_CHECK_LT(xmid, x1);
}

BOOST_AUTO_TEST_CASE(animation_cubic_spline_invalid_count)
{
	expect_load_error_contains(
		"test_gltf_loder_animation_cubic_invalid_count.gltf",
		"cubic-spline vec3 output count");
}

BOOST_AUTO_TEST_CASE(animation_invalid_times)
{
	expect_load_error_contains(
		"test_gltf_loder_animation_invalid_times.gltf",
		"strictly increasing");
}

// glTF's rotations are unit quaternions; one of no length (or not a number) has no rotation in it.  It was taken as the identity
// (the turn stood still) and now rove refuses it, so the loader says which file and key it is.
BOOST_AUTO_TEST_CASE(a_rotation_key_that_is_no_rotation_is_an_error_that_names_it)
{
	expect_load_error_contains("test_gltf_loder_animation_zero_rotation.gltf", "rotation keyframe 1");
	expect_load_error_contains("test_gltf_loder_animation_nan_rotation.gltf", "rotation keyframe 1");
}

BOOST_AUTO_TEST_CASE(animation_weights_path)
{
	std::shared_ptr<edge::ModelBuilder> model = load_model_from_asset("test_gltf_loder_animation_weights_path.gltf");
	check_common_loaded_model(*model);

	BOOST_REQUIRE_EQUAL(model->animations().size(), 1u);
	BOOST_REQUIRE_EQUAL(model->animations()[0]->weight_samplers.size(), 1u);

	edge::MeshBuilder const *mesh = dynamic_cast<edge::MeshBuilder const *>(model->meshes()[0].get());
	BOOST_REQUIRE(mesh);
	BOOST_REQUIRE(!mesh->vertex_buffer().empty());

	model->pose(0.0f);
	float const x0 = mesh->vertex_buffer()[0].position.x;

	model->pose(0.5f);
	float const xmid = mesh->vertex_buffer()[0].position.x;

	model->pose(1.0f);
	float const x1 = mesh->vertex_buffer()[0].position.x;

	BOOST_CHECK_LT(x0, xmid);
	BOOST_CHECK_LT(xmid, x1);
	BOOST_CHECK_SMALL(std::abs(x1 - x0 - 0.5f), 1e-4f);
}

BOOST_AUTO_TEST_CASE(materials)
{
	std::shared_ptr<edge::ModelBuilder> model = load_model_from_asset("test_gltf_loder_materials.glb");
	check_common_loaded_model(*model);
	BOOST_REQUIRE_EQUAL(model->meshes().size(), 2u);
	BOOST_REQUIRE_EQUAL(model->joints().size(), 2u);
	BOOST_REQUIRE_EQUAL(model->animations().size(), 0u);
	BOOST_REQUIRE_EQUAL(model->materials().size(), 5u);

	std::set<std::string> material_names;
	std::size_t textured_materials = 0;
	for (std::size_t i = 0; i < model->materials().size(); ++i)
	{
		BOOST_REQUIRE(model->materials()[i]);
		material_names.insert(model->materials()[i]->name());
		if (model->materials()[i]->diffuse_textures_count() > 0)
		{
			textured_materials++;
		}
	}

	BOOST_CHECK(material_names.count("lambert1") == 1u);
	BOOST_CHECK(material_names.count("phong1") == 1u);
	BOOST_CHECK(material_names.count("blinn1") == 1u);
	BOOST_CHECK(material_names.count("phong2") == 1u);
	BOOST_CHECK(material_names.count("blinn2") == 1u);
	BOOST_CHECK_EQUAL(textured_materials, 0u);

	for (auto &base : model->meshes())
	{
		edge::MeshBuilder const *mesh = dynamic_cast<edge::MeshBuilder const *>(base.get());
		BOOST_REQUIRE(mesh);
		BOOST_REQUIRE_EQUAL(mesh->vertex_buffer().size(), 9u);
		BOOST_REQUIRE_EQUAL(mesh->index_buffer().size(), 9u);
		BOOST_REQUIRE_EQUAL(mesh->triangles().size(), 3u);
	}

	{
		edge::MeshBuilder const *mesh = dynamic_cast<edge::MeshBuilder const *>(model->meshes()[0].get());
		BOOST_REQUIRE(mesh);

		edge::Mesh::Triangles const &t0 = mesh->triangles()[0];
		edge::Mesh::Triangles const &t1 = mesh->triangles()[1];
		edge::Mesh::Triangles const &t2 = mesh->triangles()[2];

		BOOST_REQUIRE(t0.material_ptr);
		BOOST_REQUIRE(t1.material_ptr);
		BOOST_REQUIRE(t2.material_ptr);
		BOOST_CHECK_EQUAL(t0.material_ptr->name(), "lambert1");
		BOOST_CHECK_EQUAL(t1.material_ptr->name(), "phong1");
		BOOST_CHECK_EQUAL(t2.material_ptr->name(), "blinn1");

		BOOST_CHECK_EQUAL(t0.triangles_count, 1u);
		BOOST_CHECK_EQUAL(t0.first_index_offset, 0u);
		BOOST_CHECK_EQUAL(t0.minimal_vertex_index, 0u);
		BOOST_CHECK_EQUAL(t0.vertices_count, 3u);

		BOOST_CHECK_EQUAL(t1.triangles_count, 1u);
		BOOST_CHECK_EQUAL(t1.first_index_offset, 3u);
		BOOST_CHECK_EQUAL(t1.minimal_vertex_index, 3u);
		BOOST_CHECK_EQUAL(t1.vertices_count, 3u);

		BOOST_CHECK_EQUAL(t2.triangles_count, 1u);
		BOOST_CHECK_EQUAL(t2.first_index_offset, 6u);
		BOOST_CHECK_EQUAL(t2.minimal_vertex_index, 6u);
		BOOST_CHECK_EQUAL(t2.vertices_count, 3u);
	}

	{
		edge::MeshBuilder const *mesh = dynamic_cast<edge::MeshBuilder const *>(model->meshes()[1].get());
		BOOST_REQUIRE(mesh);

		edge::Mesh::Triangles const &t0 = mesh->triangles()[0];
		edge::Mesh::Triangles const &t1 = mesh->triangles()[1];
		edge::Mesh::Triangles const &t2 = mesh->triangles()[2];

		BOOST_REQUIRE(t0.material_ptr);
		BOOST_REQUIRE(t1.material_ptr);
		BOOST_REQUIRE(t2.material_ptr);
		BOOST_CHECK_EQUAL(t0.material_ptr->name(), "lambert1");
		BOOST_CHECK_EQUAL(t1.material_ptr->name(), "phong2");
		BOOST_CHECK_EQUAL(t2.material_ptr->name(), "blinn2");

		BOOST_CHECK_EQUAL(t0.triangles_count, 1u);
		BOOST_CHECK_EQUAL(t0.first_index_offset, 0u);
		BOOST_CHECK_EQUAL(t0.minimal_vertex_index, 0u);
		BOOST_CHECK_EQUAL(t0.vertices_count, 3u);

		BOOST_CHECK_EQUAL(t1.triangles_count, 1u);
		BOOST_CHECK_EQUAL(t1.first_index_offset, 3u);
		BOOST_CHECK_EQUAL(t1.minimal_vertex_index, 3u);
		BOOST_CHECK_EQUAL(t1.vertices_count, 3u);

		BOOST_CHECK_EQUAL(t2.triangles_count, 1u);
		BOOST_CHECK_EQUAL(t2.first_index_offset, 6u);
		BOOST_CHECK_EQUAL(t2.minimal_vertex_index, 6u);
		BOOST_CHECK_EQUAL(t2.vertices_count, 3u);
	}
}

BOOST_AUTO_TEST_CASE(tbn)
{
	std::shared_ptr<edge::ModelBuilder> model = load_model_from_asset("test_gltf_loder_tbn.glb");
	check_common_loaded_model(*model);
	BOOST_REQUIRE_EQUAL(model->meshes().size(), 1u);
	BOOST_REQUIRE_EQUAL(model->joints().size(), 1u);
	BOOST_REQUIRE_EQUAL(model->animations().size(), 0u);

	edge::MeshBuilder const *mesh = dynamic_cast<edge::MeshBuilder const *>(model->meshes()[0].get());
	BOOST_REQUIRE(mesh);
	BOOST_REQUIRE_EQUAL(mesh->vertex_buffer().size(), 3u);
	BOOST_REQUIRE_EQUAL(mesh->index_buffer().size(), 3u);

	for (auto const &v : mesh->vertex_buffer())
	{
			BOOST_CHECK_SMALL(std::abs(v.normal.length_sq() - 1.0f), 1e-4f);
		check_axis_aligned_unit_normal(v.normal);
		BOOST_CHECK_GT(v.tangent.length_sq(), 0.9f);
		BOOST_CHECK_GT(v.binormal.length_sq(), 0.9f);
		BOOST_CHECK_SMALL(std::abs(rove::dot(v.normal, v.tangent)), 1e-4f);
		BOOST_CHECK_SMALL(std::abs(rove::dot(v.normal, v.binormal)), 1e-4f);
		BOOST_CHECK_SMALL(std::abs(rove::dot(v.tangent, v.binormal)), 1e-4f);
		BOOST_CHECK_GT(std::abs(rove::dot(rove::cross(v.normal, v.tangent), v.binormal)), 0.9f);
	}

	std::set<unsigned> tbn_indices;
	for (unsigned int i : mesh->index_buffer())
	{
		tbn_indices.insert(i);
	}
	BOOST_CHECK_EQUAL(tbn_indices.size(), 3u);
	BOOST_CHECK(tbn_indices.count(0) == 1u);
	BOOST_CHECK(tbn_indices.count(1) == 1u);
	BOOST_CHECK(tbn_indices.count(2) == 1u);
}

BOOST_AUTO_TEST_CASE(directions)
{
	std::shared_ptr<edge::ModelBuilder> model = load_model_from_asset("test_gltf_loder_directions.glb");
	check_common_loaded_model(*model);

	BOOST_REQUIRE_EQUAL(model->meshes().size(), 1u);
	BOOST_REQUIRE_EQUAL(model->joints().size(), 1u);
	BOOST_REQUIRE_EQUAL(model->animations().size(), 0u);
	BOOST_REQUIRE_EQUAL(model->materials().size(), 1u);
	BOOST_CHECK_EQUAL(model->materials()[0]->name(), "lambert1");
	BOOST_REQUIRE_EQUAL(model->materials()[0]->diffuse_textures_count(), 1u);
	BOOST_REQUIRE(model->materials()[0]->diffuse_texture());
	BOOST_CHECK_GT(model->materials()[0]->diffuse_texture()->width(), 0);
	BOOST_CHECK_GT(model->materials()[0]->diffuse_texture()->height(), 0);
}

// (test/make_test_texture_transform.py) A material's textures read one UV set, moved as KHR_texture_transform says.
namespace
{

void check_uvs(std::string const &asset, std::vector<std::pair<float, float>> const &expected)
{
	std::shared_ptr<edge::ModelBuilder> model = load_model_from_asset(asset);
	BOOST_REQUIRE_EQUAL(model->meshes().size(), 1u);
	auto const *mesh = dynamic_cast<edge::MeshBuilder const *>(model->meshes()[0].get());
	BOOST_REQUIRE(mesh);
	BOOST_REQUIRE_EQUAL(mesh->vertex_buffer().size(), expected.size());
	for (std::size_t i = 0; i < expected.size(); ++i)
	{
		rove::vec<2> const &uv = mesh->vertex_buffer()[i].texcoords;
		BOOST_TEST_INFO("vertex " << i << ": " << uv.x << ", " << uv.y);
		BOOST_CHECK_SMALL(uv.x - expected[i].first, 1e-5f);
		BOOST_CHECK_SMALL(uv.y - expected[i].second, 1e-5f);
	}
}

}

BOOST_AUTO_TEST_CASE(a_texture_transform_moves_the_uvs)
{
	check_uvs("test_uv_transform.gltf", {{0.5f, 0.25f}, {2.5f, 0.25f}, {2.5f, 3.25f}, {0.5f, 3.25f}});
}

BOOST_AUTO_TEST_CASE(a_texture_transform_turns_the_uvs)
{
	// glTF's translation * rotation * scale of (u, v, 1): u' = 0.5 + sin * 3 v, v' = 0.25 - sin * 2 u
	check_uvs("test_uv_transform_turned.gltf", {{0.5f, 0.25f}, {0.5f, -1.75f}, {3.5f, -1.75f}, {3.5f, 0.25f}});
}

BOOST_AUTO_TEST_CASE(a_material_reading_texcoord_1_has_those_uvs)
{
	check_uvs("test_uv_set_1.gltf", {{10.0f, 20.0f}, {11.0f, 20.0f}, {11.0f, 21.0f}, {10.0f, 21.0f}});
}

BOOST_AUTO_TEST_CASE(a_material_whose_textures_read_different_uvs_is_an_error)
{
	expect_load_error_contains("test_uv_disagree.gltf", "UV");
}

BOOST_AUTO_TEST_CASE(a_normal_map_keeps_its_scale)
{
	std::shared_ptr<edge::ModelBuilder> model = load_model_from_asset("test_uv_set_1.gltf");
	BOOST_REQUIRE_EQUAL(model->materials().size(), 1u);
	BOOST_CHECK_EQUAL(model->materials()[0]->normal_scale(), 0.5f);
	BOOST_CHECK_EQUAL(load_model_from_asset("test_uv_transform.gltf")->materials()[0]->normal_scale(), 1.0f);
}

BOOST_AUTO_TEST_CASE(a_glTF_naming_what_it_has_not_is_an_error)
{
	// node 0's child 5 is not there: refused whole, with fastgltf's word for it, not imported without it
	expect_load_error_contains("test_gltf_bad_index.gltf", "test_gltf_bad_index.gltf");
	std::shared_ptr<edge::ModelBuilder> model = make_model();
	try
	{
		model->load_from_gltf(make_asset_path("test_gltf_bad_index.gltf"));
	}
	catch (edge::GltfLoader::load_error const &error)
	{
		BOOST_CHECK_MESSAGE(std::string(error.what()).find("code") == std::string::npos, "a bare error code: " << error.what());
	}
}

BOOST_AUTO_TEST_CASE(a_scene_without_nodes_keeps_the_numbers_of_the_scenes_after_it)
{
	// Blender writes a scene of nothing first (a bake scene) and the one to show second: scene 1 is the second scene
	// (fastgltf dropped a scene without "nodes", and scene 1 was not there: etc/patches)
	std::shared_ptr<edge::ModelBuilder> model = load_model_from_asset("test_gltf_scene_after_an_empty_one.gltf");
	BOOST_REQUIRE_EQUAL(model->meshes().size(), 1u);
	BOOST_CHECK_EQUAL(model->meshes()[0]->name(), "triangle");
}

BOOST_AUTO_TEST_SUITE_END()
