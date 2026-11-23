// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

// Contract of morph targets: data a mesh has for its application (Mesh::morph_targets_count(), BasicStaticMesh::morph_target_deltas()),
// which edge does not play (static_mesh.h says why).  They come from a glTF and go through a .edge file as they are; a file that is
// not drawn as it should be without playing them -- weights other than zero, weights that are animated -- is refused by name, never
// drawn unmorphed.  The files test_format11_morph_*.edge are of edge b5d2ec2, the last that played them:
// convert_gltf_to_edge() of test_gltf_loder_animation_weights_path.gltf without its animation (targets), that with default weights of 0.5
// and the mesh named "smile" (weights), and the file itself (animation).

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <boost/test/unit_test.hpp>

#include "generic_vertex.h"
#include "mesh_builder.h"
#include "model_builder.h"
#include "model_cache.h"
#include "null_renderer.h"
#include "serialized_model.h"
#include "static_mesh.h"
#include "static_model.h"
#include "test_fixture.h"
#include "vertex.h"
#include "vertex_buffer.h"

using namespace edge;

namespace
{

using StaticMesh = BasicStaticMesh<VERTEX_PTBNT>;

std::string asset(std::string const &name)
{
	return TestFixture::graphic_sources_path() + "test/data/" + name;
}

std::string text_of(std::string const &path)
{
	std::ifstream in(path, std::ios::binary);
	BOOST_REQUIRE_MESSAGE(in, path);
	return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

struct Fixture
{
	std::shared_ptr<NullRenderer> renderer = std::make_shared<NullRenderer>(64, 64);
	std::filesystem::path const dir = unique_temp_directory("edge_morph_targets_");

	~Fixture()
	{
		std::error_code ignored;
		std::filesystem::remove_all(dir, ignored);
	}

	std::string out(std::string const &name) const { return (dir / name).string(); }

	/// The triangle with one target, which moves every vertex by (0.5, 0, 0), its text @p edit-ed: without its animation, unless it is
	/// asked for.
	std::string gltf(std::string const &name, std::function<void(std::string &)> const &edit = {}, bool animated = false) const
	{
		std::string text = text_of(asset("test_gltf_loder_animation_weights_path.gltf"));
		if (!animated)
		{
			std::size_t const from = text.find("\"animations\":"), to = text.find("\"buffers\":");
			BOOST_REQUIRE(from != std::string::npos && to != std::string::npos && from < to);
			text.erase(from, to - from);
		}
		if (edit)
			edit(text);
		std::ofstream(out(name), std::ios::binary) << text;
		return out(name);
	}

	std::shared_ptr<StaticModelPTBNT> loaded(std::string const &file) const
	{
		auto model = std::make_shared<StaticModelPTBNT>(renderer);
		if (file.ends_with(".edge"))
			model->load_from_file(file);
		else
			model->load_from_gltf(file);
		return model;
	}

	/// Loading @p file says each of @p words.
	void refused(std::string const &file, std::vector<std::string> const &words) const
	{
		for (bool builder : {false, true})
			try
			{
				if (builder)
				{
					auto model = std::make_shared<ModelBuilder>(renderer);
					if (file.ends_with(".edge"))
						model->load_from_file(file);
					else
						model->load_from_gltf(file);
				}
				else
				{
					loaded(file);
				}
				BOOST_ERROR(file << " loaded");
			}
			catch (std::runtime_error const &e)
			{
				for (std::string const &word : words)
					BOOST_CHECK_MESSAGE(std::string(e.what()).find(word) != std::string::npos, "'" << word << "' not in: " << e.what());
			}
	}
};

StaticMesh &mesh_of(std::shared_ptr<StaticModelPTBNT> const &model)
{
	return dynamic_cast<StaticMesh &>(*model->meshes().at(0));
}

void replace(std::string &text, std::string const &what, std::string const &with)
{
	std::size_t const at = text.find(what);
	BOOST_REQUIRE_MESSAGE(at != std::string::npos, what);
	text.replace(at, what.size(), with);
}

/// One target that moves each of three vertices by (0.5, 0, 0).
void check_targets(StaticMesh const &mesh)
{
	BOOST_TEST(mesh.morph_targets_count() == 1u);
	BOOST_REQUIRE(mesh.morph_target_deltas().size() == 1u);
	BOOST_REQUIRE(mesh.morph_target_deltas()[0].size() == 3u);
	for (VERTEX_PTBNT const &delta : mesh.morph_target_deltas()[0])
		BOOST_CHECK(delta.position == rove::vec<3>(0.5f, 0, 0));
	BOOST_REQUIRE(mesh.vertices().size() == 3u);
	BOOST_CHECK(mesh.vertices()[0].position == rove::vec<3>(0, 0, 0));
	BOOST_CHECK(mesh.vertices()[1].position == rove::vec<3>(1, 0, 0));
	BOOST_CHECK(mesh.vertices()[2].position == rove::vec<3>(0, 1, 0));
	// What is drawn is the vertices.
	auto const drawn = std::as_const(*mesh.vertex_buffer()).read();
	BOOST_CHECK(drawn.data() == reinterpret_cast<std::byte const *>(mesh.vertices().data()));
}

}

BOOST_FIXTURE_TEST_SUITE(morph_targets, Fixture)

BOOST_AUTO_TEST_CASE(a_glTFs_targets_are_the_meshs_data)
{
	auto const model = loaded(gltf("targets.gltf"));
	check_targets(mesh_of(model));
	BOOST_TEST(model->animations().empty());
}

BOOST_AUTO_TEST_CASE(a_mesh_without_targets_has_none)
{
	auto const model = loaded(asset("cylinder_30_degrees.glb"));
	BOOST_TEST(mesh_of(model).morph_targets_count() == 0u);
	BOOST_TEST(mesh_of(model).morph_target_deltas().empty());
}

BOOST_AUTO_TEST_CASE(they_go_through_a_file)
{
	std::string const source = gltf("targets.gltf");
	loaded(source)->save_to_file(out("static.edge"));
	auto const builder = std::make_shared<ModelBuilder>(renderer);
	builder->load_from_gltf(source);
	BOOST_TEST(builder->meshes().at(0)->morph_targets_count() == 1u);
	builder->save_to_file(out("builder.edge"));
	for (std::string const name : {"static.edge", "builder.edge"})
	{
		SerializedModel const file = Model::read_file(out(name));
		BOOST_TEST(file.version == MODEL_BINARY_VERSION);
		BOOST_TEST(file.version >= 12u);
		check_targets(mesh_of(loaded(out(name))));
		// and through a builder again
		auto const again = std::make_shared<ModelBuilder>(renderer);
		again->load_from_file(out(name));
		again->save_to_file(out("again.edge"));
		check_targets(mesh_of(loaded(out("again.edge"))));
	}
}

BOOST_AUTO_TEST_CASE(instances_share_them_and_the_buffers)
{
	ModelCache cache(renderer);
	std::string const source = gltf("targets.gltf");
	auto const a = cache.load<StaticModelPTBNT>(source), b = cache.load<StaticModelPTBNT>(source);
	BOOST_CHECK(&mesh_of(a).morph_target_deltas() == &mesh_of(b).morph_target_deltas());
	BOOST_CHECK(mesh_of(a).vertex_buffer() == mesh_of(b).vertex_buffer());
	check_targets(mesh_of(b));
}

BOOST_AUTO_TEST_CASE(other_vertices_are_not_the_ones_they_move)
{
	ModelCache cache(renderer);
	std::string const source = gltf("targets.gltf");
	auto const a = cache.load<StaticModelPTBNT>(source), b = cache.load<StaticModelPTBNT>(source);
	std::vector<GenericVertex> vertices(5);
	mesh_of(b).set_vertices(vertices.data(), VERTEX_PTBNT::FORMAT, vertices.size());
	BOOST_TEST(mesh_of(b).morph_targets_count() == 0u);
	check_targets(mesh_of(a));
	// as many vertices as before keep them
	std::vector<GenericVertex> moved(3);
	mesh_of(a).set_vertices(moved.data(), VERTEX_PTBNT::FORMAT, moved.size());
	BOOST_TEST(mesh_of(a).morph_targets_count() == 1u);
}

BOOST_AUTO_TEST_CASE(a_builders_new_geometry_has_none)
{
	auto const builder = std::make_shared<ModelBuilder>(renderer);
	builder->load_from_gltf(gltf("targets.gltf"));
	auto &mesh = dynamic_cast<MeshBuilder &>(*builder->meshes().at(0));
	std::vector<VERTEX_PTBNT> const vertices = mesh.vertex_buffer();
	std::vector<MeshBuilder::index_type> const indices = mesh.index_buffer();
	mesh.append_geometry(vertices, indices);
	BOOST_TEST(mesh.morph_targets_count() == 0u);
	BOOST_TEST(mesh.vertex_buffer().size() == 6u);
}

// -- what would have to be played ---------------------------------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(weights_that_are_animated_are_refused)
{
	refused(gltf("animated.gltf", {}, true), {"weights_anim", "morph", "weights"});
}

BOOST_AUTO_TEST_CASE(default_weights_that_are_not_zero_are_refused)
{
	for (std::string const weight : {"0.25", "-1.0"})
		refused(gltf("mesh.gltf", [&](std::string &text) {
			replace(text, "\"meshes\":[{", "\"meshes\":[{\"name\":\"smile\",");
			replace(text, "\"weights\":[0.0]", "\"weights\":[" + weight + "]");
		}), {"smile", "morph", weight.substr(0, 2)});
}

BOOST_AUTO_TEST_CASE(no_default_weights_are_zero)
{
	check_targets(mesh_of(loaded(gltf("none.gltf", [&](std::string &text) { replace(text, "\"weights\":[0.0],", ""); }))));
}

BOOST_AUTO_TEST_CASE(a_file_of_before_has_its_targets)
{
	BOOST_TEST(text_of(asset("test_format11_morph_targets.edge")).at(4) == 11);
	check_targets(mesh_of(loaded(asset("test_format11_morph_targets.edge"))));
}

BOOST_AUTO_TEST_CASE(a_file_of_before_that_would_have_to_be_played_is_refused)
{
	refused(asset("test_format11_morph_weights.edge"), {"smile", "morph", "0.5"});
	refused(asset("test_format11_morph_animation.edge"), {"weights_anim", "morph"});
}

BOOST_AUTO_TEST_CASE(a_target_of_another_count_than_the_vertices_is_refused)
{
	loaded(gltf("targets.gltf"))->save_to_file(out("ok.edge"));
	for (int change : {-1, 1})
	{
		SerializedModel model = Model::read_file(out("ok.edge"));
		auto &target = model.meshes.at(0).morph_target_deltas.at(0);
		target.resize(std::size_t(int(target.size()) + change));
		Model::write_file(model, out("bad.edge"));
		refused(out("bad.edge"), {"morph target", "3 vertices"});
	}
}

BOOST_AUTO_TEST_SUITE_END()
