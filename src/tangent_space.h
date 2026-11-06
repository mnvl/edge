// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

#include <vector>

#include "generic_vertex.h"

namespace edge
{

/// Compute tangent and binormal vectors for a triangle mesh.
///
/// Requires FORMAT_NORMAL and FORMAT_TEXCOORDS in vertex_format.
/// Skips computation if tangent+binormal are already present.
/// On success, sets FORMAT_TANGENT and FORMAT_BINORMAL in vertex_format.
void generate_tangent_space(std::vector<GenericVertex> &vertices,
	std::vector<unsigned> const &indices,
	GenericVertex::format_type &vertex_format);

}
