// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

#include "serialized_model.h"

namespace edge
{

/// Cuts the one static mesh of @p model (a ground) into a mesh for each square of side @p tile_metres, on a grid over the mesh's own x and z
/// (y is up), that holds the centre of a triangle: so each is culled, and given its level of detail (add_lods()), by itself.  A triangle goes whole
/// to the tile of its centre, none is lost, doubled or changed, and the tiles touch along the triangles' edges, the vertices there being in both
/// (the same position, normal, texture coordinates).  A tile holds only the vertices it uses, in the mesh's order, with their custom attributes,
/// and a group for each group of the mesh it has triangles in, with the group's material.  Each tile is a mesh of its own named
/// "<name>.<column>.<row>" on a root joint of its own, with the matrix of the mesh's joint (the grid is on the mesh's own x and z, whatever the
/// joint does with them).
///
/// Throws std::invalid_argument, the model left as it was, for a tile that is not a positive number, a model that is not one mesh on one joint,
/// a mesh that is skinned, has morph targets or has levels of detail already (cut first, simplify after).
void split_into_tiles(SerializedModel &model, float tile_metres);

}
