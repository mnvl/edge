// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "serialized_model.h"

namespace edge
{

struct AtlasOptions
{
	enum class Mode
	{
		/// Each material's textures as they are in a cell of its own (a texture that tiles cannot).
		Grid,
		/// The meshes unwrapped anew by xatlas, every triangle of the model with its own place in the atlas, at the density its material's
		/// texture had; the textures drawn into it (tiling ones too, with no more texels than the texture has).
		Xatlas,
	};
	Mode mode = Mode::Grid;

	/// The most texels along a side of an atlas; the atlas is the smallest power of two square (or rectangle of half the height) that holds the
	/// materials' cells, and they are made smaller, all by the same factor, until its width is no more than this.
	int size = 2048;
	/// Materials (by name) left as they are, with their own textures and coordinates: what reads the coordinates of a mesh for more than
	/// a picture (a cloth that finds its grid in them) must not have them moved.
	std::vector<std::string> keep;
};

/// Packs the textures of the model's materials into one texture for each way a texture is used (colour, metallic-roughness, normal
/// map, emissive), so that the model needs one set of them however many materials it has, and moves the texture coordinates of each
/// material's triangles into its cell.  The materials stay as they are with only their textures changed (a material has no texture
/// in a role it had none in, and keeps its own factors, which only multiply its own cell).
///
/// A material takes a square cell of its own as big as a power of two nearest the side of its largest texture; materials of the same
/// textures share one.  Cells fit the atlas perfectly (largest first, in Z-order).  A cell has a gutter along its edge, the texture's
/// edge texels repeated, so that filtering and mips do not bring a neighbour's texels in.  A texture that is not square is stretched to the cell.
///
/// A material is left as it is, with its own textures, when it is named in AtlasOptions::keep, has a texture that is block-compressed already, brings more than one
/// diffuse texture or blend weights, or has triangles with texture coordinates outside 0..1 (a texture that tiles cannot be packed
/// in a cell).  Nothing is done to a model with fewer than two different sets of textures to pack.  Needs the textures as 8 bits a texel (A8R8G8B8
/// or X8R8G8B8) and meshes with no levels of detail yet: make those after.
///
/// Returns the number of materials packed.  std::runtime_error, the model left as it was, for a vertex that two materials that are
/// packed use (it cannot be in two cells), a mesh with levels of detail, colour textures that are not all sRGB or all not, a texture
/// that is not 8 bits a texel or whose texels are not as many as its size, a material that names a texture that is not there, a
/// mesh whose morph targets move texture coordinates of packed materials, and more materials than cells of 4 x 4 texels fit in `size`.
std::size_t atlas_materials(SerializedModel &model, AtlasOptions const &options);

}
