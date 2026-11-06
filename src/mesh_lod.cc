// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include "mesh_lod.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <unordered_map>
#include <stdexcept>
#include <string>

#include <meshoptimizer.h>

namespace edge
{

namespace
{

// what the error counts besides the position, which is measured against the mesh's extent: a normal turned all the way round, a texture
// coordinate run across the whole texture, a skin weight changed from 0 to 1, each cost about this much of the extent
constexpr float NORMAL_WEIGHT = 0.5f;
constexpr float TEXCOORD_WEIGHT = 1.0f;
constexpr float SKIN_WEIGHT = 1.0f;
constexpr std::size_t ATTRIBUTES = 3 + 2 + 4;

struct Vec
{
	double x, y, z;
};
Vec operator-(Vec a, Vec b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec operator+(Vec a, Vec b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec operator*(Vec a, double k) { return {a.x * k, a.y * k, a.z * k}; }
double dot(Vec a, Vec b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

/// The distance from point p to triangle abc (Ericson, Real-Time Collision Detection).
double distance_to_triangle(Vec p, Vec a, Vec b, Vec c)
{
	Vec const ab = b - a, ac = c - a, ap = p - a;
	double const d1 = dot(ab, ap), d2 = dot(ac, ap);
	Vec closest = a;
	if (d1 <= 0 && d2 <= 0)
	{
		closest = a;
	}
	else
	{
		Vec const bp = p - b;
		double const d3 = dot(ab, bp), d4 = dot(ac, bp);
		Vec const cp = p - c;
		double const d5 = dot(ab, cp), d6 = dot(ac, cp);
		double const vc = d1 * d4 - d3 * d2, vb = d5 * d2 - d1 * d6, va = d3 * d6 - d5 * d4;
		if (d3 >= 0 && d4 <= d3)
		{
			closest = b;
		}
		else if (vc <= 0 && d1 >= 0 && d3 <= 0)
		{
			closest = a + ab * (d1 / (d1 - d3));
		}
		else if (d6 >= 0 && d5 <= d6)
		{
			closest = c;
		}
		else if (vb <= 0 && d2 >= 0 && d6 <= 0)
		{
			closest = a + ac * (d2 / (d2 - d6));
		}
		else if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0)
		{
			closest = b + (c - b) * ((d4 - d3) / ((d4 - d3) + (d5 - d6)));
		}
		else
		{
			double const denominator = 1.0 / (va + vb + vc);
			closest = a + ab * (vb * denominator) + ac * (vc * denominator);
		}
	}
	Vec const d = p - closest;
	return std::sqrt(dot(d, d));
}

/// The farthest any of `points` (vertex numbers) is from the surface of `indices` (triangles over `positions`): a uniform grid of the triangles,
/// searched in rings of cells round the point until nothing farther out can be nearer.
double farthest_from_surface(std::vector<float> const &positions, std::vector<unsigned> const &points, std::vector<unsigned> const &indices)
{
	if (indices.empty())
	{
		return points.empty() ? 0.0 : std::numeric_limits<double>::infinity();
	}
	auto const at = [&](unsigned v) { return Vec{positions[v * 3], positions[v * 3 + 1], positions[v * 3 + 2]}; };
	Vec lo = at(indices[0]), hi = lo;
	double edge_sum = 0;
	for (std::size_t i = 0; i < indices.size(); ++i)
	{
		Vec const p = at(indices[i]);
		lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)};
		hi = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
		Vec const d = p - at(indices[i / 3 * 3 + (i + 1) % 3]);
		edge_sum += std::sqrt(dot(d, d));
	}
	double const cell = std::max(edge_sum / double(indices.size()), 1e-9);
	auto const cell_of = [&](double v, double low) { return static_cast<long>(std::floor((v - low) / cell)); };
	auto const key = [](long x, long y, long z) {
		return (static_cast<std::uint64_t>(x + 1048576) << 42) | (static_cast<std::uint64_t>(y + 1048576) << 21) | static_cast<std::uint64_t>(z + 1048576);
	};
	std::unordered_map<std::uint64_t, std::vector<unsigned>> grid;
	for (std::size_t t = 0; t < indices.size(); t += 3)
	{
		Vec const a = at(indices[t]), b = at(indices[t + 1]), c = at(indices[t + 2]);
		long const x0 = cell_of(std::min({a.x, b.x, c.x}), lo.x), x1 = cell_of(std::max({a.x, b.x, c.x}), lo.x);
		long const y0 = cell_of(std::min({a.y, b.y, c.y}), lo.y), y1 = cell_of(std::max({a.y, b.y, c.y}), lo.y);
		long const z0 = cell_of(std::min({a.z, b.z, c.z}), lo.z), z1 = cell_of(std::max({a.z, b.z, c.z}), lo.z);
		for (long x = x0; x <= x1; ++x)
			for (long y = y0; y <= y1; ++y)
				for (long z = z0; z <= z1; ++z)
					grid[key(x, y, z)].push_back(static_cast<unsigned>(t));
	}
	long const span = std::max({cell_of(hi.x, lo.x), cell_of(hi.y, lo.y), cell_of(hi.z, lo.z)}) + 2;
	double worst = 0;
	for (unsigned v : points)
	{
		Vec const p = at(v);
		long const cx = cell_of(p.x, lo.x), cy = cell_of(p.y, lo.y), cz = cell_of(p.z, lo.z);
		double best = std::numeric_limits<double>::infinity();
		for (long ring = 0; ring <= span + std::max({std::labs(cx), std::labs(cy), std::labs(cz)}) + 2; ++ring)
		{
			for (long x = cx - ring; x <= cx + ring; ++x)
				for (long y = cy - ring; y <= cy + ring; ++y)
					for (long z = cz - ring; z <= cz + ring; ++z)
					{
						if (std::max({std::labs(x - cx), std::labs(y - cy), std::labs(z - cz)}) != ring)
							continue;
						auto const found = grid.find(key(x, y, z));
						if (found == grid.end())
							continue;
						for (unsigned t : found->second)
							best = std::min(best, distance_to_triangle(p, at(indices[t]), at(indices[t + 1]), at(indices[t + 2])));
					}
			if (best <= double(ring) * cell)
				break;
		}
		worst = std::max(worst, best);
	}
	return worst;
}

void validate(SerializedMesh const &mesh, LodOptions const &options)
{
	float previous = 1;
	for (float ratio : options.ratios)
	{
		if (!(ratio > 0 && ratio < previous))
		{
			throw std::invalid_argument("a level of detail's ratio is " + std::to_string(ratio) + ": the ratios must be in (0, 1) and fall");
		}
		previous = ratio;
	}
	if (!mesh.lods.empty())
	{
		throw std::logic_error("mesh '" + mesh.name + "' has levels of detail already");
	}
	for (SerializedTriangles const &group : mesh.triangles)
	{
		if (group.first_index_offset + group.triangles_count * 3 > mesh.indices.size())
		{
			throw std::runtime_error("mesh '" + mesh.name + "' has a triangle group that runs past its indices");
		}
	}
	for (unsigned index : mesh.indices)
	{
		if (index >= mesh.vertices.size())
		{
			throw std::runtime_error("mesh '" + mesh.name + "' has an index past its vertices");
		}
	}
}

}

void add_lods(SerializedMesh &mesh, LodOptions const &options)
{
	validate(mesh, options);
	std::size_t total = 0;
	for (SerializedTriangles const &group : mesh.triangles)
	{
		total += group.triangles_count;
	}
	if (options.ratios.empty() || total < options.min_triangles || total == 0)
	{
		return;
	}

	std::size_t const count = mesh.vertices.size();
	std::vector<float> positions(count * 3), attributes(count * ATTRIBUTES);
	for (std::size_t v = 0; v < count; ++v)
	{
		GenericVertex const &vertex = mesh.vertices[v];
		for (std::size_t k = 0; k < 3; ++k)
		{
			positions[v * 3 + k] = vertex.position.i[k];
			attributes[v * ATTRIBUTES + k] = vertex.normal.i[k];
		}
		attributes[v * ATTRIBUTES + 3] = vertex.texcoords.i[0];
		attributes[v * ATTRIBUTES + 4] = vertex.texcoords.i[1];
		for (std::size_t k = 0; k < 4; ++k)
		{
			attributes[v * ATTRIBUTES + 5 + k] = vertex.weights.i[k];
		}
	}
	float const weights[ATTRIBUTES] = {NORMAL_WEIGHT, NORMAL_WEIGHT, NORMAL_WEIGHT, TEXCOORD_WEIGHT, TEXCOORD_WEIGHT,
		SKIN_WEIGHT, SKIN_WEIGHT, SKIN_WEIGHT, SKIN_WEIGHT};

	// a vertex two groups use is where two materials meet: it stays, in both
	std::vector<unsigned char> uses(count, 0), lock(count, 0);
	for (SerializedTriangles const &group : mesh.triangles)
	{
		std::vector<unsigned char> mine(count, 0);
		for (std::size_t i = group.first_index_offset; i < group.first_index_offset + group.triangles_count * 3; ++i)
		{
			mine[mesh.indices[i]] = 1;
		}
		for (std::size_t v = 0; v < count; ++v)
		{
			uses[v] = static_cast<unsigned char>(uses[v] + mine[v]);
			lock[v] = uses[v] > 1;
		}
	}

	std::size_t previous = total;
	float previous_error = 0;
	std::vector<unsigned> const source = mesh.indices;       // level 0 is where every level is made from
	std::vector<unsigned> level0_vertices;                    // the vertices level 0 uses: how far each is from a level is how far the level is
	{
		std::vector<char> seen(count, 0);
		for (SerializedTriangles const &group : mesh.triangles)
			for (std::size_t i = group.first_index_offset; i < group.first_index_offset + group.triangles_count * 3; ++i)
				if (!seen[source[i]])
				{
					seen[source[i]] = 1;
					level0_vertices.push_back(source[i]);
				}
	}
	for (float ratio : options.ratios)
	{
		SerializedLod lod;
		std::size_t level_triangles = 0;
		std::vector<unsigned> level_indices;
		for (SerializedTriangles const &group : mesh.triangles)
		{
			SerializedTriangles made = group;
			made.first_index_offset = mesh.indices.size() + level_indices.size();
			std::size_t const have = group.triangles_count * 3;
			std::size_t const want = std::max<std::size_t>(3, std::size_t(std::ceil(double(group.triangles_count) * double(ratio))) * 3);
			std::vector<unsigned> out(have);
			std::size_t const got = have == 0 ? 0 : meshopt_simplifyWithAttributes(out.data(), source.data() + group.first_index_offset, have,
				positions.data(), count, sizeof(float) * 3, attributes.data(), sizeof(float) * ATTRIBUTES, weights, ATTRIBUTES, lock.data(),
				want, 1.0f, options.lock_border ? meshopt_SimplifyLockBorder : 0, nullptr);
			out.resize(got);
			made.triangles_count = got / 3;
			level_triangles += made.triangles_count;
			level_indices.insert(level_indices.end(), out.begin(), out.end());
			lod.triangles.push_back(made);
		}
		if (level_triangles >= previous)
		{
			break;
		}
		previous = level_triangles;
		lod.error = static_cast<float>(farthest_from_surface(positions, level0_vertices, level_indices));
		// a level that could shrink no more than the one before may report less error than it did (meshoptimizer measures what it did), but its
		// surface is at least as far from the original
		lod.error = std::max(lod.error, previous_error);
		previous_error = lod.error;
		mesh.indices.insert(mesh.indices.end(), level_indices.begin(), level_indices.end());
		mesh.lods.push_back(std::move(lod));
	}
}

}
