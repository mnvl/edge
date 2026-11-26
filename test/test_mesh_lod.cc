// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

// Contract of add_lods(): a mesh of the .edge file gets coarser versions of itself, "levels of detail".  Level 0 is the mesh as it was, untouched;
// every further level is a list of triangle groups (one for each group of level 0, same material) over the SAME vertices, its indices appended
// after the existing ones, so that a skinned mesh needs nothing new to be drawn at any level.  Each level has fewer triangles than the one
// before, and an error: how far, in the mesh's own units, its surface may be from level 0's.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <cstring>
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



/// Appends to @p mesh a group of material @p material: an n x n grid over x in [0, x1], y in [0, 10] at z = height(x, y) + lift, its normals +Z
/// (-Z and wound the other way round, `flip`), its vertices its own.
void sheet(edge::SerializedMesh &mesh, int n, float x1, float lift, std::int64_t material, bool flip = false)
{
	unsigned const base = unsigned(mesh.vertices.size());
	for (int y = 0; y <= n; ++y)
		for (int x = 0; x <= n; ++x)
		{
			edge::GenericVertex v{};
			float const fx = float(x) / float(n), fy = float(y) / float(n);
			v.position = rove::vec<3>(fx * x1, fy * 10.0f, bump(fx * x1 / 10.0f, fy) + lift);
			v.normal = rove::vec<3>(0, 0, flip ? -1.0f : 1.0f);
			v.texcoords = rove::vec<2>(fx, fy);
			v.weights = rove::vec<8>(0.25f, 0.75f, 0, 0, 0, 0, 0, 0);
			mesh.vertices.push_back(v);
		}
	edge::SerializedTriangles group;
	group.material_index = material;
	group.minimal_vertex_index = base;
	group.vertices_count = mesh.vertices.size() - base;
	group.first_index_offset = mesh.indices.size();
	auto const at = [n, base](int x, int y) { return base + unsigned(y * (n + 1) + x); };
	for (int y = 0; y < n; ++y)
		for (int x = 0; x < n; ++x)
		{
			auto const square = flip ? std::vector<unsigned>{at(x, y), at(x + 1, y + 1), at(x + 1, y), at(x, y), at(x, y + 1), at(x + 1, y + 1)}
			                         : std::vector<unsigned>{at(x, y), at(x + 1, y), at(x + 1, y + 1), at(x, y), at(x + 1, y + 1), at(x, y + 1)};
			mesh.indices.insert(mesh.indices.end(), square.begin(), square.end());
		}
	group.triangles_count = (mesh.indices.size() - group.first_index_offset) / 3;
	mesh.serialization_type = "static_mesh:PTBNT";
	mesh.triangles.push_back(group);
}

/// Clothes: group 0 a bumpy sheet, group 1 the same bumps @p gap above it over x in [0, @p cover] (over the whole of it at 10).
edge::SerializedMesh clothed(float gap, float cover = 10.0f, int n = 48)
{
	edge::SerializedMesh mesh;
	sheet(mesh, n, 10.0f, 0.0f, 0);
	sheet(mesh, int(float(n) * cover / 10.0f), cover, gap, 1);
	return mesh;
}

/// The height of the triangles of @p group (a level's) over (x, y), none where they are not.
std::optional<float> height_at(edge::SerializedMesh const &mesh, edge::SerializedTriangles const &group, float x, float y)
{
	std::optional<float> found;
	for (size_t i = group.first_index_offset; i < group.first_index_offset + group.triangles_count * 3; i += 3)
	{
		rove::vec<3> const &a = mesh.vertices[mesh.indices[i]].position, &b = mesh.vertices[mesh.indices[i + 1]].position,
			&c = mesh.vertices[mesh.indices[i + 2]].position;
		float const d = (b.y - c.y) * (a.x - c.x) + (c.x - b.x) * (a.y - c.y);
		if (std::abs(d) < 1e-12f)
			continue;
		float const u = ((b.y - c.y) * (x - c.x) + (c.x - b.x) * (y - c.y)) / d, v = ((c.y - a.y) * (x - c.x) + (a.x - c.x) * (y - c.y)) / d;
		if (u < -1e-5f || v < -1e-5f || u + v > 1 + 1e-5f)
			continue;
		float const z = u * a.z + v * b.z + (1 - u - v) * c.z;
		found = found ? std::max(*found, z) : z;
	}
	return found;
}

/// The places level @p l's group 0 shows through its group 1 (a corner or the middle of a triangle of the inner at or over the outer
/// there), or the outer dips through the inner (a corner of the outer at or under it).
int shown_through(edge::SerializedMesh const &mesh, size_t l)
{
	auto const &inner = mesh.lods[l].triangles[0], &outer = mesh.lods[l].triangles[1];
	int shown = 0;
	auto const points = [&](edge::SerializedTriangles const &g, auto &&each) {
		for (size_t i = g.first_index_offset; i < g.first_index_offset + g.triangles_count * 3; i += 3)
		{
			rove::vec<3> const &a = mesh.vertices[mesh.indices[i]].position, &b = mesh.vertices[mesh.indices[i + 1]].position,
				&c = mesh.vertices[mesh.indices[i + 2]].position;
			for (rove::vec<3> const &p : {a, b, c, rove::vec<3>((a.x + b.x + c.x) / 3, (a.y + b.y + c.y) / 3, (a.z + b.z + c.z) / 3)})
				each(p);
		}
	};
	points(inner, [&](rove::vec<3> const &p) {
		if (std::optional<float> const over = height_at(mesh, outer, p.x, p.y); over && p.z >= *over)
			++shown;
	});
	points(outer, [&](rove::vec<3> const &p) {
		if (std::optional<float> const under = height_at(mesh, inner, p.x, p.y); under && p.z <= *under)
			++shown;
	});
	return shown;
}

/// The vertices the groups of level @p l name.
std::set<unsigned> used_by(edge::SerializedMesh const &mesh, size_t l, size_t group)
{
	std::set<unsigned> used;
	auto const &g = mesh.lods[l].triangles[group];
	for (size_t i = g.first_index_offset; i < g.first_index_offset + g.triangles_count * 3; ++i)
		used.insert(mesh.indices[i]);
	return used;
}

edge::LodOptions layered(std::vector<float> ratios)
{
	edge::LodOptions options{std::move(ratios), 300};
	options.layers = true;
	return options;
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

BOOST_AUTO_TEST_CASE(without_layers_a_sheet_close_under_another_shows_through_it_at_a_low_level)
{
	// (the premise of the layers' tests below: clothes a level's error deep or less are seen through, as the shorts were through a
	// player's shirt from a distance)
	edge::SerializedMesh mesh = clothed(0.02f);
	edge::add_lods(mesh, {{0.5f, 0.2f, 0.08f}, 300});
	BOOST_REQUIRE_EQUAL(mesh.lods.size(), 3u);
	BOOST_CHECK_GT(mesh.lods.back().error, 0.02f);
	BOOST_CHECK_GT(shown_through(mesh, 2), 0);
}

BOOST_AUTO_TEST_CASE(with_layers_a_group_under_another_never_shows_through_it_at_any_level)
{
	for (float gap : {0.002f, 0.02f, 0.05f})
	{
		edge::SerializedMesh mesh = clothed(gap);
		edge::add_lods(mesh, layered({0.5f, 0.2f, 0.08f}));
		BOOST_REQUIRE_EQUAL(mesh.lods.size(), 3u);
		for (size_t l = 0; l < mesh.lods.size(); ++l)
			BOOST_CHECK_MESSAGE(shown_through(mesh, l) == 0, "a gap of " << gap << ", level " << l + 1 << " (error " << mesh.lods[l].error << "): shown through at "
				<< shown_through(mesh, l) << " places");
	}
}

BOOST_AUTO_TEST_CASE(with_layers_level_zero_and_its_vertices_stay_as_they_were)
{
	edge::SerializedMesh const before = clothed(0.02f);
	edge::SerializedMesh mesh = before;
	edge::add_lods(mesh, layered({0.5f, 0.08f}));
	BOOST_REQUIRE(!mesh.lods.empty());
	BOOST_REQUIRE_GE(mesh.vertices.size(), before.vertices.size());
	BOOST_CHECK(std::equal(before.indices.begin(), before.indices.end(), mesh.indices.begin()));
	for (size_t v = 0; v < before.vertices.size(); ++v)
		BOOST_REQUIRE(std::memcmp(&before.vertices[v], &mesh.vertices[v], sizeof(edge::GenericVertex)) == 0);
	BOOST_CHECK_EQUAL(mesh.triangles.size(), 2u);
	for (size_t g = 0; g < 2; ++g)
	{
		BOOST_CHECK_EQUAL(mesh.triangles[g].first_index_offset, before.triangles[g].first_index_offset);
		BOOST_CHECK_EQUAL(mesh.triangles[g].triangles_count, before.triangles[g].triangles_count);
		BOOST_CHECK_EQUAL(mesh.triangles[g].minimal_vertex_index, before.triangles[g].minimal_vertex_index);
		BOOST_CHECK_EQUAL(mesh.triangles[g].vertices_count, before.triangles[g].vertices_count);
	}
}

BOOST_AUTO_TEST_CASE(with_layers_a_vertex_pulled_in_is_a_copy_of_one_of_its_group_moved_against_its_normal_and_nothing_else)
{
	edge::SerializedMesh const before = clothed(0.02f);
	edge::SerializedMesh mesh = before;
	edge::add_lods(mesh, layered({0.5f, 0.2f, 0.08f}));
	size_t const n = before.vertices.size();
	BOOST_REQUIRE_GT(mesh.vertices.size(), n);          // (else nothing was pulled in, and the test proves nothing)
	for (size_t l = 0; l < mesh.lods.size(); ++l)
		for (size_t group = 0; group < 2; ++group)
		{
			auto const &g = before.triangles[group];
			for (unsigned v : used_by(mesh, l, group))
			{
				if (v < n)
				{
					BOOST_CHECK(v >= g.minimal_vertex_index && v < g.minimal_vertex_index + g.vertices_count);
					continue;
				}
				edge::GenericVertex const &copy = mesh.vertices[v];
				bool found = false;
				for (size_t o = g.minimal_vertex_index; o < g.minimal_vertex_index + g.vertices_count && !found; ++o)
				{
					edge::GenericVertex moved = before.vertices[o];
					rove::vec<3> const d = copy.position - moved.position;
					float const in = -(d.x * moved.normal.x + d.y * moved.normal.y + d.z * moved.normal.z);
					rove::vec<3> const across = d + moved.normal * in;
					moved.position = copy.position;
					found = in > 0 && std::sqrt(across.x * across.x + across.y * across.y + across.z * across.z) < 1e-5f &&
						std::memcmp(&moved, &copy, sizeof(edge::GenericVertex)) == 0;
				}
				BOOST_CHECK_MESSAGE(found, "level " << l + 1 << ", group " << group << ": vertex " << v << " is no vertex of the group pulled in");
			}
		}
}

BOOST_AUTO_TEST_CASE(with_layers_nothing_is_pulled_in_where_no_other_group_is_over_it)
{
	// the outer covers x in [0, 4]: past it (and a level's reach) the inner is its own vertices; the outer has nothing over it anywhere
	edge::SerializedMesh mesh = clothed(0.02f, 4.0f);
	size_t const n = mesh.vertices.size();
	edge::add_lods(mesh, layered({0.5f, 0.2f, 0.08f}));
	BOOST_REQUIRE_EQUAL(mesh.lods.size(), 3u);
	bool pulled = false;
	for (size_t l = 0; l < mesh.lods.size(); ++l)
	{
		for (unsigned v : used_by(mesh, l, 0))
		{
			pulled = pulled || v >= n;
			BOOST_CHECK_MESSAGE(v < n || mesh.vertices[v].position.x <= 4.0f + 1e-4f, "level " << l + 1 << ": a vertex at x " << mesh.vertices[v].position.x << " pulled in");
		}
		for (unsigned v : used_by(mesh, l, 1))
			BOOST_CHECK_MESSAGE(v < n, "level " << l + 1 << ": the outer group's vertex " << v << " pulled in");
	}
	BOOST_CHECK(pulled);
}

BOOST_AUTO_TEST_CASE(with_layers_a_surface_that_faces_another_across_a_gap_is_not_pulled_in)
{
	// a sheet and another over it turned to face it (an arm by the body, not a shirt over a skin): neither is under the other
	edge::SerializedMesh mesh;
	sheet(mesh, 48, 10.0f, 0.0f, 0);
	sheet(mesh, 48, 10.0f, 0.02f, 1, true);
	size_t const n = mesh.vertices.size();
	edge::add_lods(mesh, layered({0.5f, 0.08f}));
	BOOST_REQUIRE(!mesh.lods.empty());
	BOOST_CHECK_EQUAL(mesh.vertices.size(), n);
}

BOOST_AUTO_TEST_CASE(with_layers_the_errors_are_those_of_the_levels_without_them)
{
	// (what is pulled in is under another surface: the levels look as far from level 0 as they did, and a camera picks them as before)
	edge::SerializedMesh plain = clothed(0.02f), mesh = plain;
	edge::add_lods(plain, {{0.5f, 0.2f, 0.08f}, 300});
	edge::add_lods(mesh, layered({0.5f, 0.2f, 0.08f}));
	BOOST_REQUIRE_EQUAL(mesh.lods.size(), plain.lods.size());
	for (size_t l = 0; l < mesh.lods.size(); ++l)
	{
		BOOST_CHECK_EQUAL(mesh.lods[l].error, plain.lods[l].error);
		BOOST_CHECK_EQUAL(triangles_of(mesh.lods[l].triangles), triangles_of(plain.lods[l].triangles));
	}
}

BOOST_AUTO_TEST_CASE(with_layers_a_group_of_a_level_spans_the_vertices_it_names)
{
	edge::SerializedMesh mesh = clothed(0.02f);
	edge::add_lods(mesh, layered({0.5f, 0.08f}));
	for (size_t l = 0; l < mesh.lods.size(); ++l)
		for (size_t group = 0; group < 2; ++group)
		{
			auto const &g = mesh.lods[l].triangles[group];
			BOOST_CHECK_LE(g.minimal_vertex_index + g.vertices_count, mesh.vertices.size());
			for (unsigned v : used_by(mesh, l, group))
				BOOST_CHECK(v >= g.minimal_vertex_index && v < g.minimal_vertex_index + g.vertices_count);
		}
}

BOOST_AUTO_TEST_CASE(with_layers_a_vertex_two_groups_share_is_never_pulled_in)
{
	// the inner sheet in two groups that share the column x = 5 (a seam of materials), all of it under a third: the seam stays one vertex in both
	edge::SerializedMesh mesh = clothed(0.02f);
	std::vector<unsigned> left, right;
	auto const &inner = mesh.triangles[0];
	for (size_t t = inner.first_index_offset; t < inner.first_index_offset + inner.triangles_count * 3; t += 3)
	{
		bool in_right = true;
		for (int k = 0; k < 3; ++k)
			in_right = in_right && (mesh.indices[t + k] % 49) >= 24;
		auto &to = in_right ? right : left;
		to.insert(to.end(), mesh.indices.begin() + long(t), mesh.indices.begin() + long(t) + 3);
	}
	std::vector<unsigned> indices = left;
	indices.insert(indices.end(), right.begin(), right.end());
	auto const &outer = mesh.triangles[1];
	std::vector<unsigned> const over(mesh.indices.begin() + long(outer.first_index_offset), mesh.indices.end());
	edge::SerializedTriangles a = inner, b = inner, c = outer;
	a.triangles_count = left.size() / 3;
	b.first_index_offset = left.size();
	b.triangles_count = right.size() / 3;
	b.material_index = 2;
	c.first_index_offset = indices.size();
	indices.insert(indices.end(), over.begin(), over.end());
	mesh.indices = indices;
	mesh.triangles = {a, b, c};
	edge::add_lods(mesh, layered({0.2f}));
	BOOST_REQUIRE_EQUAL(mesh.lods.size(), 1u);
	std::set<unsigned> const l = used_by(mesh, 0, 0), r = used_by(mesh, 0, 1);
	int seam = 0;
	for (unsigned v = 24; v < 49u * 49u; v += 49)
	{
		++seam;
		BOOST_CHECK_MESSAGE(l.count(v) && r.count(v), "the seam's vertex " << v << " is not itself in both groups");
	}
	BOOST_CHECK_GT(mesh.vertices.size(), 2u * 49u * 49u);          // (the rest of it was pulled in)
}

BOOST_AUTO_TEST_SUITE_END()
