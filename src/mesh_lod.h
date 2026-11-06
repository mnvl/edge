// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

#include <cstddef>
#include <vector>

#include "serialized_model.h"

namespace edge
{

struct LodOptions
{
	/// The share of level 0's triangles each further level aims at, each between 0 and 1 (not either) and smaller than the one before.
	std::vector<float> ratios;
	/// A mesh of fewer triangles than this (all its groups together) gets no levels: it costs nothing to draw.
	std::size_t min_triangles = 300;
	/// The vertices on the mesh's open edge (where a triangle has no neighbour) are kept at every level: for meshes that meet others along it, the tiles
	/// of a ground (mesh_tiles.h), so that no crack opens between a tile at one level and its neighbour at another.
	bool lock_border = false;
};

/// Gives @p mesh levels of detail: for each ratio of @p options a version of every triangle group of the mesh with about that share of its
/// triangles, made by edge collapse (meshoptimizer) with the texture coordinates, normals and skin weights counted in the error, so that a seam of the
/// texture or a joint's influence is kept for as long as it can be.  The levels use the mesh's own vertices (a skinned mesh needs no
/// more joints, weights or clips): their indices are appended to the mesh's, and each level is a list of groups with the material and
/// vertex range of the group it comes from, in the same order.  Vertices that two groups share are not moved or dropped, so that
/// no crack opens between the materials of a level.  Level 0, the mesh as it is, is not touched.
///
/// A level that does not have fewer triangles than the one before (nothing more could be taken away) is left out, and so are all after it.
/// Its `error` is how far the level is from level 0: the farthest any vertex of level 0 is from the level's surface, in the mesh's own units (measured,
/// not what the simplifier says), and no less than the level before's.  Throws std::invalid_argument for
/// ratios that are not in (0, 1) and falling, std::logic_error for a mesh that has levels already, std::runtime_error for a group that
/// lies outside the mesh's indices or an index outside its vertices.
void add_lods(SerializedMesh &mesh, LodOptions const &options);

}
