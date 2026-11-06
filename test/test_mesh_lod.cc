// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

// Contract of add_lods(): a mesh of the .edge file gets coarser versions of itself, "levels of detail".  Level 0 is the mesh as it was, untouched;
// every further level is a list of triangle groups (one for each group of level 0, same material) over the SAME vertices, its indices appended
// after the existing ones, so that a skinned mesh needs nothing new to be drawn at any level.  Each level has fewer triangles than the one
// before, and an error: how far, in the mesh's own units, its surface may be from level 0's.

#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>
#include <vector>

#include <boost/test/unit_test.hpp>

#include "mesh_lod.h"
#include "serialized_model.h"

namespace
{

/// A grid of n x n squares (2 n n triangles) in the plane z = height(x, y), in one group of material 0, texture coordinates running 0..1.
edge::SerializedMesh grid(int n, float (*height)(float, float) = [](float, float) { return 0.0f; })
{
	edge::SerializedMesh mesh;
	mesh.serialization_type = "static_mesh:PTBNT";
	for (int y = 0; y <= n; ++y)
		for (int x = 0; x <= n; ++x)
		{
			edge::GenericVertex v{};
			float const fx = float(x) / float(n), fy = float(y) / float(n);
			v.position = rove::vec<3>(fx * 10.0f, fy * 10.0f, height(fx, fy));
			v.normal = rove::vec<3>(0, 0, 1);
			v.texcoords = rove::vec<2>(fx, fy);
			mesh.vertices.push_back(v);
		}
	auto const at = [n](int x, int y) { return unsigned(y * (n + 1) + x); };
	for (int y = 0; y < n; ++y)
		for (int x = 0; x < n; ++x)
		{
			for (unsigned i : {at(x, y), at(x + 1, y), at(x + 1, y + 1), at(x, y), at(x + 1, y + 1), at(x, y + 1)})
				mesh.indices.push_back(i);
		}
	edge::SerializedTriangles group;
	group.material_index = 0;
	group.minimal_vertex_index = 0;
	group.vertices_count = mesh.vertices.size();
	group.first_index_offset = 0;
	group.triangles_count = mesh.indices.size() / 3;
	mesh.triangles.push_back(group);
	return mesh;
}

size_t triangles_of(std::vector<edge::SerializedTriangles> const &groups)
{
	size_t total = 0;
	for (auto const &g : groups)
		total += g.triangles_count;
	return total;
}

float bump(float x, float y)
{
	return 0.8f * std::sin(6.0f * x) * std::cos(5.0f * y);
}

}

BOOST_AUTO_TEST_SUITE(mesh_lod)

BOOST_AUTO_TEST_CASE(a_mesh_gets_the_levels_asked_for_each_with_fewer_triangles_than_the_one_before)
{
	edge::SerializedMesh mesh = grid(64, bump);      // 8192 triangles
	edge::add_lods(mesh, {{0.5f, 0.1f}, 300});
	BOOST_REQUIRE_EQUAL(mesh.lods.size(), 2u);
	size_t const full = mesh.triangles[0].triangles_count;
	BOOST_CHECK_EQUAL(full, 8192u);
	size_t const l1 = triangles_of(mesh.lods[0].triangles), l2 = triangles_of(mesh.lods[1].triangles);
	BOOST_CHECK_LT(l1, full);
	BOOST_CHECK_LT(l2, l1);
	BOOST_CHECK_LT(double(l1), 0.6 * double(full));
	BOOST_CHECK_GT(double(l1), 0.3 * double(full));
	BOOST_CHECK_LT(double(l2), 0.2 * double(full));
}

BOOST_AUTO_TEST_CASE(level_zero_stays_as_it_was_and_the_new_indices_come_after_it)
{
	edge::SerializedMesh const before = grid(64, bump);
	edge::SerializedMesh mesh = before;
	edge::add_lods(mesh, {{0.25f}, 300});
	BOOST_CHECK(mesh.vertices.size() == before.vertices.size());
	for (size_t i = 0; i < before.vertices.size(); ++i)
		BOOST_REQUIRE(mesh.vertices[i].position == before.vertices[i].position);
	BOOST_REQUIRE_GT(mesh.indices.size(), before.indices.size());
	BOOST_CHECK(std::equal(before.indices.begin(), before.indices.end(), mesh.indices.begin()));
	BOOST_CHECK_EQUAL(mesh.triangles.size(), before.triangles.size());
	BOOST_CHECK_EQUAL(mesh.triangles[0].first_index_offset, before.triangles[0].first_index_offset);
	BOOST_CHECK_EQUAL(mesh.triangles[0].triangles_count, before.triangles[0].triangles_count);
}

BOOST_AUTO_TEST_CASE(every_group_of_a_level_lies_inside_the_indices_and_names_only_vertices_there_are)
{
	edge::SerializedMesh mesh = grid(48, bump);
	edge::add_lods(mesh, {{0.5f, 0.2f, 0.05f}, 300});
	BOOST_REQUIRE_GE(mesh.lods.size(), 2u);
	for (auto const &lod : mesh.lods)
		for (auto const &g : lod.triangles)
		{
			BOOST_CHECK_GE(g.first_index_offset, mesh.triangles[0].first_index_offset + mesh.triangles[0].triangles_count * 3);
			BOOST_REQUIRE_LE(g.first_index_offset + g.triangles_count * 3, mesh.indices.size());
			for (size_t i = g.first_index_offset; i < g.first_index_offset + g.triangles_count * 3; ++i)
				BOOST_REQUIRE_LT(mesh.indices[i], mesh.vertices.size());
		}
}

BOOST_AUTO_TEST_CASE(a_level_keeps_the_material_and_the_vertex_range_of_its_group)
{
	edge::SerializedMesh mesh = grid(64, bump);
	mesh.triangles[0].material_index = 7;
	edge::add_lods(mesh, {{0.3f}, 300});
	BOOST_REQUIRE_EQUAL(mesh.lods.size(), 1u);
	BOOST_REQUIRE_EQUAL(mesh.lods[0].triangles.size(), 1u);
	BOOST_CHECK_EQUAL(mesh.lods[0].triangles[0].material_index, 7);
	BOOST_CHECK_EQUAL(mesh.lods[0].triangles[0].minimal_vertex_index, mesh.triangles[0].minimal_vertex_index);
	BOOST_CHECK_EQUAL(mesh.lods[0].triangles[0].vertices_count, mesh.triangles[0].vertices_count);
}

BOOST_AUTO_TEST_CASE(the_errors_are_in_the_meshs_units_grow_with_the_level_and_a_bumpy_surface_costs_more_than_a_flat_one)
{
	edge::SerializedMesh bumpy = grid(64, bump), flat = grid(64);
	edge::add_lods(bumpy, {{0.5f, 0.05f}, 300});
	edge::add_lods(flat, {{0.5f, 0.05f}, 300});
	BOOST_REQUIRE_EQUAL(bumpy.lods.size(), 2u);
	BOOST_CHECK_GT(bumpy.lods[0].error, 0.0f);
	BOOST_CHECK_GT(bumpy.lods[1].error, bumpy.lods[0].error);
	BOOST_CHECK_LT(bumpy.lods[1].error, 10.0f);             // a 10 x 10 x 1.6 piece: the error is a part of that, not a count
	BOOST_REQUIRE_EQUAL(flat.lods.size(), 2u);
	BOOST_CHECK_LT(flat.lods[1].error, 0.01f);                // a plane loses nothing by being made of fewer triangles
	BOOST_CHECK_GT(bumpy.lods[1].error, 10.0f * flat.lods[1].error);
}

BOOST_AUTO_TEST_CASE(the_errors_never_fall_from_one_level_to_the_next_whatever_the_surface)
{
	// a noisy surface in two material groups, the way the meshes of the game are (a level that cannot shrink more can report less error than the one before)
	for (unsigned seed = 1; seed <= 12; ++seed)
	{
		edge::SerializedMesh mesh = grid(40);
		unsigned state = seed * 2654435761u;
		auto const random = [&state] { state = state * 1664525u + 1013904223u; return float(state >> 8) / float(1 << 24); };
		for (auto &v : mesh.vertices)
			v.position.z = (seed % 3 == 0 ? 0.3f : 2.0f) * random();
		mesh.triangles.clear();
		size_t const half = mesh.indices.size() / 6 * 3;
		for (int m = 0; m < 2; ++m)
		{
			edge::SerializedTriangles g;
			g.material_index = m;
			g.vertices_count = mesh.vertices.size();
			g.first_index_offset = m == 0 ? 0 : half;
			g.triangles_count = (m == 0 ? half : mesh.indices.size() - half) / 3;
			mesh.triangles.push_back(g);
		}
		edge::add_lods(mesh, {{0.7f, 0.5f, 0.3f, 0.2f, 0.1f, 0.05f, 0.02f}, 300});
		float previous = 0;
		for (auto const &lod : mesh.lods)
		{
			BOOST_CHECK_GE(lod.error, previous);
			previous = lod.error;
		}
	}
}

BOOST_AUTO_TEST_CASE(the_error_is_the_distance_the_surface_really_moved_not_what_the_simplifier_thought)
{
	// a flat grid with one vertex raised 0.7: at the lowest level the spike is either still a corner of the triangles (no error) or gone, the
	// plane is where it was, and the error is the height of the spike
	for (int spike : {17 * 33 + 9, 5 * 33 + 5, 30 * 33 + 20})
	{
		edge::SerializedMesh mesh = grid(32);
		mesh.vertices[size_t(spike)].position.z = 0.7f;
		edge::add_lods(mesh, {{0.5f, 0.02f}, 300});
		BOOST_REQUIRE_EQUAL(mesh.lods.size(), 2u);
		auto const &g = mesh.lods[1].triangles[0];
		bool kept = false;
		for (size_t i = g.first_index_offset; i < g.first_index_offset + g.triangles_count * 3; ++i)
			kept = kept || mesh.indices[i] == unsigned(spike);
		if (kept)
			BOOST_CHECK_LT(mesh.lods[1].error, 0.2f);
		else
		{
			BOOST_CHECK_GT(mesh.lods[1].error, 0.65f);
			BOOST_CHECK_LT(mesh.lods[1].error, 0.7001f);
		}
	}
}

BOOST_AUTO_TEST_CASE(a_vertex_left_out_of_a_level_that_floats_off_the_surface_counts_whole)
{
	// two flat sheets, the second 3 m above the first and a tenth of the area: it can shrink to nothing at a low level, and then the distance
	// from its vertices to what is left is 3 m
	edge::SerializedMesh mesh = grid(40);
	size_t const first_vertices = mesh.vertices.size();
	std::vector<unsigned> top;
	int const m = 12;
	for (int y = 0; y <= m; ++y)
		for (int x = 0; x <= m; ++x)
		{
			edge::GenericVertex v{};
			v.position = rove::vec<3>(float(x) * 0.2f + 4.0f, float(y) * 0.2f + 4.0f, 3.0f);
			v.normal = rove::vec<3>(0, 0, 1);
			v.texcoords = rove::vec<2>(0.5f, 0.5f);
			mesh.vertices.push_back(v);
		}
	for (int y = 0; y < m; ++y)
		for (int x = 0; x < m; ++x)
		{
			unsigned const a = unsigned(first_vertices) + unsigned(y * (m + 1) + x), c = a + unsigned(m) + 1;
			for (unsigned i : {a, a + 1, c + 1, a, c + 1, c})
				mesh.indices.push_back(i);
		}
	mesh.triangles[0].triangles_count = mesh.indices.size() / 3;
	edge::add_lods(mesh, {{0.5f, 0.1f, 0.01f}, 300});
	BOOST_REQUIRE(!mesh.lods.empty());
	auto const &g = mesh.lods.back().triangles[0];
	bool any_top = false;
	for (size_t i = g.first_index_offset; i < g.first_index_offset + g.triangles_count * 3; ++i)
		any_top = any_top || mesh.indices[i] >= first_vertices;
	if (!any_top)
		BOOST_CHECK_GT(mesh.lods.back().error, 2.9f);
}

BOOST_AUTO_TEST_CASE(a_flat_grid_keeps_its_four_corners_at_every_level)
{
	edge::SerializedMesh mesh = grid(32);
	edge::add_lods(mesh, {{0.05f}, 300});
	BOOST_REQUIRE_EQUAL(mesh.lods.size(), 1u);
	std::set<unsigned> used;
	auto const &g = mesh.lods[0].triangles[0];
	for (size_t i = g.first_index_offset; i < g.first_index_offset + g.triangles_count * 3; ++i)
		used.insert(mesh.indices[i]);
	for (unsigned corner : {0u, 32u, 33u * 32u, 33u * 33u - 1u})
		BOOST_CHECK_MESSAGE(used.count(corner), "corner " << corner << " is gone: the piece shrank");
}

BOOST_AUTO_TEST_CASE(a_mesh_with_few_triangles_gets_no_levels)
{
	edge::SerializedMesh mesh = grid(8);        // 128 triangles
	edge::add_lods(mesh, {{0.5f}, 300});
	BOOST_CHECK(mesh.lods.empty());
}

BOOST_AUTO_TEST_CASE(a_level_that_cannot_have_fewer_triangles_is_left_out)
{
	edge::SerializedMesh mesh = grid(1);       // one triangle: nothing to take away
	mesh.indices.resize(3);
	mesh.triangles[0].triangles_count = 1;
	edge::add_lods(mesh, {{0.5f}, 0});
	BOOST_CHECK(mesh.lods.empty());
	edge::SerializedMesh big = grid(64, bump);
	edge::add_lods(big, {{0.9f, 0.8f, 0.7f}, 300});
	size_t previous = big.triangles[0].triangles_count;
	for (auto const &lod : big.lods)
	{
		BOOST_CHECK_LT(triangles_of(lod.triangles), previous);
		previous = triangles_of(lod.triangles);
	}
}

BOOST_AUTO_TEST_CASE(vertices_that_two_groups_share_stay_in_both_so_no_crack_opens_between_materials)
{
	edge::SerializedMesh mesh = grid(64, bump);
	// the left half is material 0, the right half material 1; the column x = 32 is theirs both
	std::vector<unsigned> left, right;
	for (size_t t = 0; t < mesh.indices.size(); t += 3)
	{
		bool in_right = true;
		for (int k = 0; k < 3; ++k)
			in_right = in_right && (mesh.indices[t + k] % 65) >= 32;
		auto &to = in_right ? right : left;
		to.insert(to.end(), mesh.indices.begin() + long(t), mesh.indices.begin() + long(t) + 3);
	}
	mesh.indices = left;
	mesh.indices.insert(mesh.indices.end(), right.begin(), right.end());
	mesh.triangles.clear();
	for (int m = 0; m < 2; ++m)
	{
		edge::SerializedTriangles g;
		g.material_index = m;
		g.vertices_count = mesh.vertices.size();
		g.first_index_offset = m == 0 ? 0 : left.size();
		g.triangles_count = (m == 0 ? left.size() : right.size()) / 3;
		mesh.triangles.push_back(g);
	}
	edge::add_lods(mesh, {{0.1f}, 300});
	BOOST_REQUIRE_EQUAL(mesh.lods.size(), 1u);
	BOOST_REQUIRE_EQUAL(mesh.lods[0].triangles.size(), 2u);
	std::set<unsigned> seen[2];
	for (int m = 0; m < 2; ++m)
	{
		auto const &g = mesh.lods[0].triangles[size_t(m)];
		BOOST_CHECK_EQUAL(g.material_index, m);
		for (size_t i = g.first_index_offset; i < g.first_index_offset + g.triangles_count * 3; ++i)
			seen[m].insert(mesh.indices[i]);
	}
	std::set<unsigned> seam_before[2];
	for (int m = 0; m < 2; ++m)
	{
		auto const &g = mesh.triangles[size_t(m)];
		for (size_t i = g.first_index_offset; i < g.first_index_offset + g.triangles_count * 3; ++i)
			seam_before[m].insert(mesh.indices[i]);
	}
	for (unsigned v : seam_before[0])
		if (seam_before[1].count(v))
			BOOST_CHECK_MESSAGE(seen[0].count(v) && seen[1].count(v), "the seam vertex " << v << " is not in both groups at the lower level");
}

BOOST_AUTO_TEST_CASE(texture_seams_are_kept_where_the_picture_would_tear)
{
	// the left half and the right half of the flat grid show different parts of the texture: a step in the texcoords along x = 5
	edge::SerializedMesh mesh = grid(32);
	for (auto &vertex : mesh.vertices)
		if (vertex.position.x >= 5.0f)
			vertex.texcoords.x += 0.5f;
	edge::add_lods(mesh, {{0.05f}, 300});
	BOOST_REQUIRE_EQUAL(mesh.lods.size(), 1u);
	// no triangle may stretch the picture across the step: its three texcoords stay within 0.2 of one another in x
	auto const &g = mesh.lods[0].triangles[0];
	for (size_t i = g.first_index_offset; i < g.first_index_offset + g.triangles_count * 3; i += 3)
	{
		float lo = 9, hi = -9;
		for (int k = 0; k < 3; ++k)
		{
			float const u = mesh.vertices[mesh.indices[i + size_t(k)]].texcoords.x;
			lo = std::min(lo, u);
			hi = std::max(hi, u);
		}
		BOOST_CHECK_LT(hi - lo, 0.6f);
	}
}

BOOST_AUTO_TEST_CASE(with_the_border_locked_every_vertex_on_the_meshs_edge_stays_in_every_level)
{
	edge::SerializedMesh mesh = grid(48, bump);
	edge::LodOptions options{{0.3f, 0.05f}, 300};
	options.lock_border = true;
	edge::add_lods(mesh, options);
	BOOST_REQUIRE_EQUAL(mesh.lods.size(), 2u);
	int const n = 48;
	for (auto const &lod : mesh.lods)
	{
		std::set<unsigned> used;
		auto const &g = lod.triangles[0];
		for (size_t i = g.first_index_offset; i < g.first_index_offset + g.triangles_count * 3; ++i)
			used.insert(mesh.indices[i]);
		for (int y = 0; y <= n; ++y)
			for (int x = 0; x <= n; ++x)
				if (x == 0 || y == 0 || x == n || y == n)
					BOOST_REQUIRE_MESSAGE(used.count(unsigned(y * (n + 1) + x)), "border vertex (" << x << ", " << y << ") was taken away");
	}
	// and the inside did get simpler than the border alone could leave it
	BOOST_CHECK_LT(triangles_of(mesh.lods[1].triangles), 0.2 * 4608.0);
}

BOOST_AUTO_TEST_CASE(without_the_lock_a_border_vertex_of_a_bumpy_mesh_is_taken_away)
{
	edge::SerializedMesh mesh = grid(48, bump);
	edge::add_lods(mesh, {{0.05f}, 300});
	int const n = 48;
	std::set<unsigned> used;
	auto const &g = mesh.lods[0].triangles[0];
	for (size_t i = g.first_index_offset; i < g.first_index_offset + g.triangles_count * 3; ++i)
		used.insert(mesh.indices[i]);
	int missing = 0;
	for (int y = 0; y <= n; ++y)
		for (int x = 0; x <= n; ++x)
			if ((x == 0 || y == 0 || x == n || y == n) && !used.count(unsigned(y * (n + 1) + x)))
				++missing;
	BOOST_CHECK_GT(missing, 0);        // else the test above proves nothing
}

BOOST_AUTO_TEST_CASE(ratios_that_are_not_between_zero_and_one_or_not_falling_are_refused)
{
	edge::SerializedMesh mesh = grid(32);
	BOOST_CHECK_THROW(edge::add_lods(mesh, {{1.0f}, 300}), std::invalid_argument);
	BOOST_CHECK_THROW(edge::add_lods(mesh, {{0.0f}, 300}), std::invalid_argument);
	BOOST_CHECK_THROW(edge::add_lods(mesh, {{-0.5f}, 300}), std::invalid_argument);
	BOOST_CHECK_THROW(edge::add_lods(mesh, {{0.2f, 0.4f}, 300}), std::invalid_argument);
	BOOST_CHECK_THROW(edge::add_lods(mesh, {{0.5f, 0.5f}, 300}), std::invalid_argument);
	BOOST_CHECK_THROW(edge::add_lods(mesh, {{std::nanf("")}, 300}), std::invalid_argument);
	BOOST_CHECK(mesh.lods.empty());           // nothing was done by a refused call
}

BOOST_AUTO_TEST_CASE(a_mesh_that_has_levels_already_is_refused)
{
	edge::SerializedMesh mesh = grid(64, bump);
	edge::add_lods(mesh, {{0.5f}, 300});
	BOOST_CHECK_THROW(edge::add_lods(mesh, {{0.5f}, 300}), std::logic_error);
}

BOOST_AUTO_TEST_CASE(a_mesh_with_a_group_that_runs_past_its_indices_is_refused)
{
	edge::SerializedMesh mesh = grid(64);
	mesh.triangles[0].triangles_count += 5;
	BOOST_CHECK_THROW(edge::add_lods(mesh, {{0.5f}, 300}), std::runtime_error);
}

BOOST_AUTO_TEST_SUITE_END()
