// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

// Contract of split_into_tiles(): a static model of one mesh (a ground) becomes a model of many, one for each square of a grid on the ground that
// holds the centre of a triangle.  No triangle is lost, made twice or changed; a tile holds only the vertices it uses, in the order the
// mesh had them, with whatever else the vertices carry (custom attributes); the material of each group is kept.  Every tile is a mesh of its own
// on a joint of its own, so that it is culled and given its level of detail by itself.

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>
#include <tuple>
#include <vector>

#include <boost/test/unit_test.hpp>

#include "mesh_tiles.h"
#include "serialized_model.h"

namespace
{

using P = std::tuple<float, float, float>;
using TriangleSet = std::set<std::tuple<P, P, P>>;      // each triangle as its corners sorted, so that winding and rotation do not matter

/// A model of one static mesh: a w x d grid of squares (two triangles each, 1 m, from (0, 0) on x and z, y a bump), in two groups: the squares of
/// the left half are material 0, the rest material 1.  A custom attribute `_ID` carries each vertex's number.
edge::SerializedModel ground(int w, int d)
{
	edge::SerializedModel model;
	model.materials.resize(2);
	edge::SerializedMesh mesh;
	mesh.serialization_type = "static_mesh:PTBNT";
	mesh.name = "ground";
	edge::SerializedCustomAttribute id;
	id.name = "_ID";
	for (int z = 0; z <= d; ++z)
		for (int x = 0; x <= w; ++x)
		{
			edge::GenericVertex v{};
			v.position = rove::vec<3>(float(x), 0.3f * std::sin(0.7f * float(x)) * std::cos(0.5f * float(z)), float(z));
			v.normal = rove::vec<3>(0, 1, 0);
			v.texcoords = rove::vec<2>(float(x) / float(w), float(z) / float(d));
			mesh.vertices.push_back(v);
			id.values.push_back(float(mesh.vertices.size() - 1));
		}
	mesh.custom_attributes.push_back(id);
	std::vector<unsigned> parts[2];
	for (int z = 0; z < d; ++z)
		for (int x = 0; x < w; ++x)
		{
			unsigned const a = unsigned(z * (w + 1) + x), b = a + 1, c = a + unsigned(w) + 1, e = c + 1;
			auto &to = parts[x < w / 2 ? 0 : 1];
			for (unsigned i : {a, c, b, b, c, e})
				to.push_back(i);
		}
	for (int m = 0; m < 2; ++m)
	{
		edge::SerializedTriangles g;
		g.material_index = m;
		g.first_index_offset = mesh.indices.size();
		g.triangles_count = parts[m].size() / 3;
		g.vertices_count = mesh.vertices.size();
		mesh.triangles.push_back(g);
		mesh.indices.insert(mesh.indices.end(), parts[m].begin(), parts[m].end());
	}
	model.meshes.push_back(mesh);
	edge::SerializedJoint joint;
	joint.id = "root";
	joint.name = "ground";
	joint.mesh_index = 0;
	joint.matrix.identity();
	model.joints.push_back(joint);
	return model;
}

P at(edge::SerializedMesh const &mesh, unsigned i)
{
	auto const &p = mesh.vertices[i].position;
	return {p.x, p.y, p.z};
}

TriangleSet triangles_of(edge::SerializedMesh const &mesh, std::vector<edge::SerializedTriangles> const &groups)
{
	TriangleSet set;
	for (auto const &g : groups)
		for (size_t i = g.first_index_offset; i < g.first_index_offset + g.triangles_count * 3; i += 3)
		{
			std::vector<P> corners{at(mesh, mesh.indices[i]), at(mesh, mesh.indices[i + 1]), at(mesh, mesh.indices[i + 2])};
			std::sort(corners.begin(), corners.end());
			set.insert({corners[0], corners[1], corners[2]});
		}
	return set;
}

size_t count_of(edge::SerializedMesh const &mesh)
{
	size_t n = 0;
	for (auto const &g : mesh.triangles)
		n += g.triangles_count;
	return n;
}

}

BOOST_AUTO_TEST_SUITE(mesh_tiles)

BOOST_AUTO_TEST_CASE(a_ground_becomes_one_mesh_for_each_square_that_has_a_triangle_centre_and_no_triangle_is_lost_or_doubled)
{
	edge::SerializedModel model = ground(20, 12);
	edge::SerializedMesh const whole = model.meshes[0];
	edge::split_into_tiles(model, 8.0f);
	BOOST_CHECK_EQUAL(model.meshes.size(), 6u);                  // 3 x 2 squares of 8 m over 20 x 12
	size_t total = 0;
	TriangleSet seen;
	for (auto const &mesh : model.meshes)
	{
		total += count_of(mesh);
		for (auto const &t : triangles_of(mesh, mesh.triangles))
			BOOST_CHECK_MESSAGE(seen.insert(t).second, "a triangle is in two tiles");
	}
	BOOST_CHECK_EQUAL(total, 20u * 12u * 2u);
	BOOST_CHECK(seen == triangles_of(whole, whole.triangles));
}

BOOST_AUTO_TEST_CASE(a_triangle_is_in_the_tile_of_its_centre)
{
	edge::SerializedModel model = ground(20, 12);
	edge::split_into_tiles(model, 8.0f);
	for (auto const &mesh : model.meshes)
	{
		float lo_x = 1e9f, hi_x = -1e9f;
		for (auto const &g : mesh.triangles)
			for (size_t i = g.first_index_offset; i < g.first_index_offset + g.triangles_count * 3; i += 3)
			{
				float cx = 0, cz = 0;
				for (int k = 0; k < 3; ++k)
				{
					cx += mesh.vertices[mesh.indices[i + size_t(k)]].position.x / 3;
					cz += mesh.vertices[mesh.indices[i + size_t(k)]].position.z / 3;
				}
				lo_x = std::min(lo_x, std::floor(cx / 8.0f));
				hi_x = std::max(hi_x, std::floor(cx / 8.0f));
				BOOST_CHECK(std::floor(cz / 8.0f) >= 0);
			}
		BOOST_CHECK_EQUAL(lo_x, hi_x);          // all the centres of a tile are in one column of squares
	}
}

BOOST_AUTO_TEST_CASE(a_tile_holds_only_the_vertices_it_uses_with_their_custom_attributes_and_the_order_the_mesh_had)
{
	edge::SerializedModel model = ground(20, 12);
	edge::SerializedMesh const whole = model.meshes[0];
	edge::split_into_tiles(model, 8.0f);
	size_t vertices = 0;
	for (auto const &mesh : model.meshes)
	{
		vertices += mesh.vertices.size();
		std::set<unsigned> used(mesh.indices.begin(), mesh.indices.end());
		BOOST_CHECK_EQUAL(used.size(), mesh.vertices.size());
		BOOST_REQUIRE_EQUAL(mesh.custom_attributes.size(), 1u);
		BOOST_REQUIRE_EQUAL(mesh.custom_attributes[0].values.size(), mesh.vertices.size());
		float previous = -1;
		for (size_t v = 0; v < mesh.vertices.size(); ++v)
		{
			float const id = mesh.custom_attributes[0].values[v];       // the vertex's number in the whole mesh
			BOOST_CHECK_GT(id, previous);
			previous = id;
			BOOST_REQUIRE(mesh.vertices[v].position == whole.vertices[size_t(id)].position);
			BOOST_CHECK(mesh.vertices[v].texcoords == whole.vertices[size_t(id)].texcoords);
		}
	}
	BOOST_CHECK_GT(vertices, whole.vertices.size());      // the vertices on the borders are in two tiles or four
}

BOOST_AUTO_TEST_CASE(the_materials_of_the_groups_are_kept_and_a_group_a_tile_does_not_reach_is_not_there)
{
	edge::SerializedModel model = ground(20, 12);
	edge::split_into_tiles(model, 8.0f);
	size_t only_one = 0;
	for (auto const &mesh : model.meshes)
	{
		std::set<std::int64_t> materials;
		size_t offset_end = 0;
		for (auto const &g : mesh.triangles)
		{
			BOOST_CHECK_GT(g.triangles_count, 0u);
			BOOST_CHECK(materials.insert(g.material_index).second);
			BOOST_CHECK_GE(g.first_index_offset, offset_end);
			offset_end = g.first_index_offset + g.triangles_count * 3;
			BOOST_CHECK_EQUAL(g.vertices_count, mesh.vertices.size());
			BOOST_CHECK_EQUAL(g.minimal_vertex_index, 0u);
			// the left half of the ground is material 0 (squares to x = 10)
			for (size_t i = g.first_index_offset; i < offset_end; i += 3)
			{
				float const cx = (mesh.vertices[mesh.indices[i]].position.x + mesh.vertices[mesh.indices[i + 1]].position.x + mesh.vertices[mesh.indices[i + 2]].position.x) / 3;
				BOOST_CHECK_EQUAL(g.material_index, cx < 10.0f ? 0 : 1);
			}
		}
		only_one += materials.size() == 1;
		BOOST_CHECK_EQUAL(offset_end, mesh.indices.size());
	}
	BOOST_CHECK_GT(only_one, 0u);
}

BOOST_AUTO_TEST_CASE(every_tile_has_a_joint_of_its_own_and_the_names_tell_the_square)
{
	edge::SerializedModel model = ground(20, 12);
	edge::split_into_tiles(model, 8.0f);
	BOOST_REQUIRE_EQUAL(model.joints.size(), model.meshes.size());
	std::set<std::int64_t> meshes;
	std::set<std::string> names;
	for (size_t j = 0; j < model.joints.size(); ++j)
	{
		BOOST_CHECK_EQUAL(model.joints[j].index, j);
		BOOST_CHECK_EQUAL(model.joints[j].parent_index, -1);
		BOOST_CHECK(model.joints[j].child_indices.empty());
		BOOST_REQUIRE_GE(model.joints[j].mesh_index, 0);
		BOOST_CHECK(meshes.insert(model.joints[j].mesh_index).second);
		for (int c = 0; c < 4; ++c)
			for (int r = 0; r < 4; ++r)
				BOOST_CHECK_EQUAL(model.joints[j].matrix.ij[c][r], c == r ? 1.0f : 0.0f);
		BOOST_CHECK(names.insert(model.meshes[size_t(model.joints[j].mesh_index)].name).second);
	}
	BOOST_CHECK(names.count("ground.0.0"));
	BOOST_CHECK(names.count("ground.2.1"));
}

BOOST_AUTO_TEST_CASE(the_tiles_stand_where_the_mesh_stood_each_on_the_matrix_of_its_joint)
{
	edge::SerializedModel model = ground(20, 12);
	model.joints[0].matrix.ij[3][0] = 5.0f;
	model.joints[0].matrix.ij[3][1] = -2.0f;
	edge::split_into_tiles(model, 8.0f);
	for (auto const &joint : model.joints)
	{
		BOOST_CHECK_EQUAL(joint.matrix.ij[3][0], 5.0f);
		BOOST_CHECK_EQUAL(joint.matrix.ij[3][1], -2.0f);
		BOOST_CHECK_EQUAL(joint.matrix.ij[0][0], 1.0f);
	}
}

BOOST_AUTO_TEST_CASE(a_tile_a_good_deal_bigger_than_the_ground_leaves_it_whole)
{
	edge::SerializedModel model = ground(20, 12);
	edge::SerializedMesh const whole = model.meshes[0];
	edge::split_into_tiles(model, 100.0f);
	BOOST_REQUIRE_EQUAL(model.meshes.size(), 1u);
	BOOST_CHECK_EQUAL(count_of(model.meshes[0]), count_of(whole));
}

BOOST_AUTO_TEST_CASE(a_ground_that_is_not_in_the_positive_quarter_is_tiled_by_floor_not_by_truncation)
{
	edge::SerializedModel model = ground(20, 12);
	for (auto &v : model.meshes[0].vertices)
	{
		v.position.x -= 10.0f;
		v.position.z -= 6.0f;
	}
	edge::split_into_tiles(model, 8.0f);
	size_t total = 0;
	for (auto const &mesh : model.meshes)
		total += count_of(mesh);
	BOOST_CHECK_EQUAL(total, 480u);
	BOOST_CHECK(model.meshes.size() >= 6u);
}

BOOST_AUTO_TEST_CASE(a_model_that_is_not_one_static_mesh_is_refused_and_left_as_it_was)
{
	edge::SerializedModel two = ground(8, 8);
	two.meshes.push_back(two.meshes[0]);
	BOOST_CHECK_THROW(edge::split_into_tiles(two, 4.0f), std::invalid_argument);
	BOOST_CHECK_EQUAL(two.meshes.size(), 2u);

	edge::SerializedModel skinned = ground(8, 8);
	skinned.meshes[0].serialization_type = "animated_mesh:PTBNJWT";
	BOOST_CHECK_THROW(edge::split_into_tiles(skinned, 4.0f), std::invalid_argument);

	edge::SerializedModel morphed = ground(8, 8);
	morphed.meshes[0].morph_target_deltas.push_back(morphed.meshes[0].vertices);
	morphed.meshes[0].morph_weights.push_back(0);
	BOOST_CHECK_THROW(edge::split_into_tiles(morphed, 4.0f), std::invalid_argument);

	edge::SerializedModel leveled = ground(8, 8);
	leveled.meshes[0].lods.resize(1);
	BOOST_CHECK_THROW(edge::split_into_tiles(leveled, 4.0f), std::invalid_argument);       // tile first, then the levels

	for (float bad : {0.0f, -4.0f, std::nanf(""), INFINITY})
	{
		edge::SerializedModel m = ground(8, 8);
		BOOST_CHECK_THROW(edge::split_into_tiles(m, bad), std::invalid_argument);
		BOOST_CHECK_EQUAL(m.meshes.size(), 1u);
	}
}

BOOST_AUTO_TEST_SUITE_END()
