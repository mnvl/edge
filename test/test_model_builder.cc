// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <memory>
#include <set>
#include <string>

#include <boost/test/unit_test.hpp>

#include "model_builder.h"
#include "null_renderer.h"
#include "scheduler.h"
#include "serialized_model.h"
#include "static_model.h"
#include "test_fixture.h"

namespace
{

std::shared_ptr<edge::ModelBuilder> make_model()
{
	edge::RendererPtr renderer(new edge::NullRenderer(320, 200));

	edge::SchedulerPtr scheduler(new edge::Scheduler(renderer));
	return std::make_shared<edge::ModelBuilder>(renderer, scheduler);
}

std::shared_ptr<edge::StaticModelPTBNT> make_static_model()
{
	edge::RendererPtr renderer(new edge::NullRenderer(320, 200));

	edge::SchedulerPtr scheduler(new edge::Scheduler(renderer));
	return std::make_shared<edge::StaticModelPTBNT>(renderer, scheduler);
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

std::filesystem::path make_roundtrip_output_path(std::string const &label)
{
	std::uint64_t const stamp = static_cast<std::uint64_t>(
		std::chrono::steady_clock::now().time_since_epoch().count());
	return std::filesystem::temp_directory_path() /
		("edge_model_builder_" + label + "_" + std::to_string(stamp) + ".bin");
}

void check_vec2_close(rove::vec<2> const &lhs, rove::vec<2> const &rhs, float epsilon = 1e-6f)
{
	BOOST_CHECK_SMALL(std::abs(lhs.x - rhs.x), epsilon);
	BOOST_CHECK_SMALL(std::abs(lhs.y - rhs.y), epsilon);
}

void check_vec3_close(rove::vec<3> const &lhs, rove::vec<3> const &rhs, float epsilon = 1e-6f)
{
	BOOST_CHECK_SMALL(std::abs(lhs.x - rhs.x), epsilon);
	BOOST_CHECK_SMALL(std::abs(lhs.y - rhs.y), epsilon);
	BOOST_CHECK_SMALL(std::abs(lhs.z - rhs.z), epsilon);
}

void check_mat4_close(rove::matrix<4,4> const &lhs, rove::matrix<4,4> const &rhs, float epsilon = 1e-6f)
{
	for (std::size_t col = 0; col < 4; ++col)
	{
		for (std::size_t row = 0; row < 4; ++row)
		{
			BOOST_CHECK_SMALL(std::abs(lhs.ij[col][row] - rhs.ij[col][row]), epsilon);
		}
	}
}

void check_material_equal(edge::MaterialPtr const &lhs, edge::MaterialPtr const &rhs)
{
	BOOST_REQUIRE(lhs);
	BOOST_REQUIRE(rhs);
	BOOST_CHECK_EQUAL(lhs->name(), rhs->name());
	BOOST_CHECK(lhs->cull_mode() == rhs->cull_mode());
	BOOST_CHECK_EQUAL(lhs->zwrite_enabled(), rhs->zwrite_enabled());
	BOOST_CHECK_EQUAL(lhs->alpha_blend_enabled(), rhs->alpha_blend_enabled());
	BOOST_CHECK(lhs->src_blend_mode() == rhs->src_blend_mode());
	BOOST_CHECK(lhs->dest_blend_mode() == rhs->dest_blend_mode());

	for (std::size_t i = 0; i < edge::Material::EXTRA_PARAMETERS_COUNT; ++i)
	{
		rove::vec<4> const &a = lhs->extra_parameter(i);
		rove::vec<4> const &b = rhs->extra_parameter(i);
		BOOST_CHECK_SMALL(std::abs(a.x - b.x), 1e-6f);
		BOOST_CHECK_SMALL(std::abs(a.y - b.y), 1e-6f);
		BOOST_CHECK_SMALL(std::abs(a.z - b.z), 1e-6f);
		BOOST_CHECK_SMALL(std::abs(a.w - b.w), 1e-6f);
	}
}

std::ptrdiff_t find_joint_index(edge::ModelBuilder const &model, edge::JointPtr const &joint_ptr)
{
	if (!joint_ptr)
	{
		return -1;
	}

	for (std::size_t i = 0; i < model.joints().size(); ++i)
	{
		if (model.joints()[i] == joint_ptr)
		{
			return static_cast<std::ptrdiff_t>(i);
		}
	}
	return -1;
}

std::ptrdiff_t find_mesh_index(edge::ModelBuilder const &model, edge::MeshPtr const &mesh_ptr)
{
	if (!mesh_ptr)
	{
		return -1;
	}

	for (std::size_t i = 0; i < model.meshes().size(); ++i)
	{
		if (model.meshes()[i] == mesh_ptr)
		{
			return static_cast<std::ptrdiff_t>(i);
		}
	}
	return -1;
}

void check_mesh_equal(edge::MeshBuilder const &lhs, edge::MeshBuilder const &rhs)
{
	BOOST_CHECK_EQUAL(lhs.id(), rhs.id());
	BOOST_CHECK_EQUAL(lhs.name(), rhs.name());
	BOOST_REQUIRE_EQUAL(lhs.vertex_buffer().size(), rhs.vertex_buffer().size());
	BOOST_REQUIRE_EQUAL(lhs.index_buffer().size(), rhs.index_buffer().size());
	BOOST_REQUIRE_EQUAL(lhs.triangles().size(), rhs.triangles().size());
	BOOST_REQUIRE_EQUAL(lhs.joint_index_transform().size(), rhs.joint_index_transform().size());
	BOOST_REQUIRE_EQUAL(lhs.inverted_bind_matrices().size(), rhs.inverted_bind_matrices().size());

	for (std::size_t i = 0; i < lhs.vertex_buffer().size(); ++i)
	{
		edge::MeshBuilder::vertex_type const &a = lhs.vertex_buffer()[i];
		edge::MeshBuilder::vertex_type const &b = rhs.vertex_buffer()[i];
		check_vec3_close(a.position, b.position);
		check_vec3_close(a.normal, b.normal);
		check_vec3_close(a.tangent, b.tangent);
		check_vec3_close(a.binormal, b.binormal);
		check_vec2_close(a.texcoords, b.texcoords);
	}

	for (std::size_t i = 0; i < lhs.index_buffer().size(); ++i)
	{
		BOOST_CHECK_EQUAL(lhs.index_buffer()[i], rhs.index_buffer()[i]);
	}

	for (std::size_t i = 0; i < lhs.triangles().size(); ++i)
	{
		edge::Mesh::Triangles const &a = lhs.triangles()[i];
		edge::Mesh::Triangles const &b = rhs.triangles()[i];
		BOOST_CHECK_EQUAL(a.minimal_vertex_index, b.minimal_vertex_index);
		BOOST_CHECK_EQUAL(a.vertices_count, b.vertices_count);
		BOOST_CHECK_EQUAL(a.first_index_offset, b.first_index_offset);
		BOOST_CHECK_EQUAL(a.triangles_count, b.triangles_count);

		BOOST_REQUIRE(a.material_ptr);
		BOOST_REQUIRE(b.material_ptr);
		BOOST_CHECK_EQUAL(a.material_ptr->name(), b.material_ptr->name());
	}

	for (std::size_t i = 0; i < lhs.joint_index_transform().size(); ++i)
	{
		BOOST_CHECK_EQUAL(lhs.joint_index_transform()[i], rhs.joint_index_transform()[i]);
	}

	for (std::size_t i = 0; i < lhs.inverted_bind_matrices().size(); ++i)
	{
		check_mat4_close(lhs.inverted_bind_matrices()[i], rhs.inverted_bind_matrices()[i]);
	}
}

void check_joint_equal(edge::ModelBuilder const &lhs_model, edge::ModelBuilder const &rhs_model,
	edge::JointPtr const &lhs, edge::JointPtr const &rhs)
{
	BOOST_REQUIRE(lhs);
	BOOST_REQUIRE(rhs);
	BOOST_CHECK_EQUAL(lhs->index, rhs->index);
	BOOST_CHECK_EQUAL(lhs->id, rhs->id);
	BOOST_CHECK_EQUAL(lhs->name, rhs->name);
	check_mat4_close(lhs->matrix, rhs->matrix);

	BOOST_REQUIRE_EQUAL(lhs->transforms.size(), rhs->transforms.size());
	for (std::size_t i = 0; i < lhs->transforms.size(); ++i)
	{
		BOOST_CHECK_EQUAL(lhs->transforms[i].sid, rhs->transforms[i].sid);
		check_mat4_close(lhs->transforms[i].matrix, rhs->transforms[i].matrix);
	}

	std::ptrdiff_t const lhs_parent_index = find_joint_index(lhs_model, lhs->parent.lock());
	std::ptrdiff_t const rhs_parent_index = find_joint_index(rhs_model, rhs->parent.lock());
	BOOST_CHECK_EQUAL(lhs_parent_index, rhs_parent_index);

	std::ptrdiff_t const lhs_mesh_index = find_mesh_index(lhs_model, lhs->mesh.lock());
	std::ptrdiff_t const rhs_mesh_index = find_mesh_index(rhs_model, rhs->mesh.lock());
	BOOST_CHECK_EQUAL(lhs_mesh_index, rhs_mesh_index);
}

void check_model_roundtrip_equal(edge::ModelBuilder const &lhs, edge::ModelBuilder const &rhs)
{
	BOOST_CHECK_EQUAL(lhs.meshes().size(), rhs.meshes().size());
	BOOST_CHECK_EQUAL(lhs.joints().size(), rhs.joints().size());
	BOOST_CHECK_EQUAL(lhs.materials().size(), rhs.materials().size());
	BOOST_CHECK_EQUAL(lhs.animations().size(), rhs.animations().size());

	BOOST_REQUIRE_EQUAL(lhs.materials().size(), rhs.materials().size());
	for (std::size_t i = 0; i < lhs.materials().size(); ++i)
	{
		check_material_equal(lhs.materials()[i], rhs.materials()[i]);
	}

	BOOST_REQUIRE_EQUAL(lhs.meshes().size(), rhs.meshes().size());
	for (std::size_t i = 0; i < lhs.meshes().size(); ++i)
	{
		edge::MeshBuilder const *lhs_mesh = dynamic_cast<edge::MeshBuilder const *>(lhs.meshes()[i].get());
		edge::MeshBuilder const *rhs_mesh = dynamic_cast<edge::MeshBuilder const *>(rhs.meshes()[i].get());
		BOOST_REQUIRE(lhs_mesh);
		BOOST_REQUIRE(rhs_mesh);
		check_mesh_equal(*lhs_mesh, *rhs_mesh);
	}

	BOOST_REQUIRE_EQUAL(lhs.joints().size(), rhs.joints().size());
	for (std::size_t i = 0; i < lhs.joints().size(); ++i)
	{
		check_joint_equal(lhs, rhs, lhs.joints()[i], rhs.joints()[i]);
	}
}

void run_roundtrip_check(std::string const &asset_name, std::string const &label)
{
	std::shared_ptr<edge::ModelBuilder> source = load_model_from_asset(asset_name);

	std::filesystem::path const output_path = make_roundtrip_output_path(label);
	source->save_to_file(output_path.string());
	BOOST_REQUIRE(std::filesystem::exists(output_path));
	BOOST_CHECK_GT(std::filesystem::file_size(output_path), 0u);

	std::shared_ptr<edge::ModelBuilder> loaded = make_model();
	loaded->load_from_file(output_path.string());

	std::error_code remove_error;
	std::filesystem::remove(output_path, remove_error);

	check_model_roundtrip_equal(*source, *loaded);
}

}

BOOST_AUTO_TEST_SUITE(model_builder)

BOOST_AUTO_TEST_CASE(save_load_roundtrip_plane)
{
	run_roundtrip_check("test_gltf_loder_plane.glb", "plane");
}

BOOST_AUTO_TEST_CASE(save_load_roundtrip_materials)
{
	run_roundtrip_check("test_gltf_loder_materials.glb", "materials");
}

/// The file keeps the vertices the morph targets are offsets from: a morphed mesh is morphed once after loading, not twice.
BOOST_AUTO_TEST_CASE(a_morphed_mesh_comes_back_morphed_once)
{
	std::shared_ptr<edge::ModelBuilder> source = make_model();
	edge::MeshBuilderPtr mesh = source->add_mesh_builder();
	std::vector<edge::MeshBuilder::vertex_type> vertices(3);
	std::memset(vertices.data(), 0, sizeof(vertices[0]) * vertices.size());
	vertices[1].position.set(1.0f, 0.0f, 0.0f);
	vertices[2].position.set(0.0f, 1.0f, 0.0f);
	mesh->set_geometry(vertices, {0, 1, 2});
	edge::GenericVertex delta;
	std::memset(&delta, 0, sizeof(delta));
	delta.position.set(0.0f, 4.0f, 0.0f);
	mesh->set_morph_targets({{delta, delta, delta}}, edge::GenericVertex::FORMAT_POSITION);
	mesh->set_morph_weights({0.5f});

	std::filesystem::path const output_path = make_roundtrip_output_path("morphed");
	source->save_to_file(output_path.string());
	std::shared_ptr<edge::ModelBuilder> loaded = make_model();
	loaded->load_from_file(output_path.string());
	std::shared_ptr<edge::StaticModelPTBNT> loaded_static = make_static_model();
	loaded_static->load_from_file(output_path.string());
	std::error_code remove_error;
	std::filesystem::remove(output_path, remove_error);

	auto const &builder = dynamic_cast<edge::MeshBuilder const &>(*loaded->meshes().at(0));
	auto const &static_mesh = dynamic_cast<edge::StaticMeshPTBNT const &>(*loaded_static->meshes().at(0));
	float const base[] = {0.0f, 0.0f, 1.0f};
	for (std::size_t i = 0; i < 3; ++i)
	{
		BOOST_CHECK_SMALL(builder.vertex_buffer()[i].position.y - (base[i] + 2.0f), 1e-5f);
		BOOST_CHECK_SMALL(static_mesh.vertices()[i].position.y - base[i], 1e-5f);     // its base vertices, the weights apart
	}
	edge::SerializedMesh geometry;
	static_mesh.save_geometry(geometry);
	BOOST_CHECK(geometry.morph_weights == std::vector<float>{0.5f});
}

BOOST_AUTO_TEST_CASE(save_load_roundtrip_static_model)
{
	std::shared_ptr<edge::StaticModelPTBNT> source = make_static_model();
	source->load_from_gltf(make_asset_path("test_gltf_loder_plane.glb"));

	std::filesystem::path const output_path = make_roundtrip_output_path("static_model");
	source->save_to_file(output_path.string());
	BOOST_REQUIRE(std::filesystem::exists(output_path));
	BOOST_CHECK_GT(std::filesystem::file_size(output_path), 0u);

	std::shared_ptr<edge::StaticModelPTBNT> loaded = make_static_model();
	loaded->load_from_file(output_path.string());

	std::error_code remove_error;
	std::filesystem::remove(output_path, remove_error);

	BOOST_CHECK_EQUAL(source->meshes().size(), loaded->meshes().size());
	BOOST_CHECK_EQUAL(source->joints().size(), loaded->joints().size());
	BOOST_CHECK_EQUAL(source->materials().size(), loaded->materials().size());
	BOOST_REQUIRE_EQUAL(source->meshes().size(), loaded->meshes().size());
	BOOST_REQUIRE_EQUAL(source->joints().size(), loaded->joints().size());

	for (std::size_t i = 0; i < source->meshes().size(); ++i)
	{
		BOOST_CHECK(dynamic_cast<edge::StaticMeshPTBNT const *>(source->meshes()[i].get()));
		BOOST_CHECK(dynamic_cast<edge::StaticMeshPTBNT const *>(loaded->meshes()[i].get()));
		BOOST_CHECK_EQUAL(source->meshes()[i]->id(), loaded->meshes()[i]->id());
		BOOST_CHECK_EQUAL(source->meshes()[i]->name(), loaded->meshes()[i]->name());
		BOOST_CHECK_EQUAL(source->meshes()[i]->triangles().size(), loaded->meshes()[i]->triangles().size());
	}

	for (std::size_t i = 0; i < source->joints().size(); ++i)
	{
		BOOST_CHECK_EQUAL(source->joints()[i]->id, loaded->joints()[i]->id);
		BOOST_CHECK_EQUAL(source->joints()[i]->name, loaded->joints()[i]->name);
	}
}

BOOST_AUTO_TEST_CASE(gltf_base_color_factor_loaded)
{
	auto model = load_model_from_asset("test_gltf_loder_joint.glb");
	BOOST_REQUIRE(!model->materials().empty());

	// test_gltf_loder_joint.glb has baseColorFactor = [0.4, 0.4, 0.4, 0]
	rove::vec<4> const &bcf = model->materials()[0]->base_color_factor();
	BOOST_CHECK_CLOSE(bcf.x, 0.4f, 1.0f);
	BOOST_CHECK_CLOSE(bcf.y, 0.4f, 1.0f);
	BOOST_CHECK_CLOSE(bcf.z, 0.4f, 1.0f);
}

BOOST_AUTO_TEST_SUITE_END()
