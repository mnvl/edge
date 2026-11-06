// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "gltf_loader.h"
#include "mesh_lod.h"
#include "model_atlas.h"
#include "image_compress.h"

namespace edge
{

/// What is done to a model's textures besides making them block-compressed with their mips (GltfOptions::compress_textures).
struct TextureOptions
{
	/// BC7 (the desktop's) or ASTC (phones'), bake_textures(); ASTC needs GltfOptions::compress_textures.
	TextureCodec codec = TextureCodec::BC7;
	enum class Atlas
	{
		None,       // each material keeps its textures
		Grid,       // the textures of the materials in one set (atlas_materials()), every texture a cell of the atlas
		Xatlas,     // the same, the meshes unwrapped anew by xatlas and the textures drawn into the atlas (AtlasOptions::Mode::Xatlas)
	};
	Atlas atlas = Atlas::None;
	/// The most texels along a side of an atlas (AtlasOptions::size).
	int atlas_size = 2048;
	/// Materials left out of the atlas (AtlasOptions::keep).
	std::vector<std::string> atlas_keep;
	/// False: no material glows (drop_emissive()), which is no emissive texture to keep.
	bool emissive = true;
};

/// What a conversion makes besides (convert_gltf_to_edge()).
struct ConvertOptions
{
	/// A skinned glTF's vertex colors (COLOR_0) are kept: an AnimatedModelPTBNJWTC file.  Otherwise they are left out with a warning,
	/// and the file is an AnimatedModelPTBNJWT one, the skinned type a game loads.
	bool skinned_colors = false;
};

/// Makes the .edge file @p edge_file from the glTF file @p gltf_file: loads it with @p options (the CPU copy of the textures is
/// kept whatever they say, since it is what is saved) and writes the model, with the pose boxes of its skinned meshes.  The
/// model is an AnimatedModelPTBNJWT when the glTF has a skin (AnimatedModelPTBNJWTC with vertex colors, when @p convert keeps them), a
/// StaticModelPTBNTC when it has vertex colors (COLOR_0) and a StaticModelPTBNT when neither, the kinds a game loads through ModelCache
/// (load_model() any of them); a file of the one kind does not load as another.  The file keeps the options (SerializedModel::options).  The same input gives the same bytes.
///
/// With GltfOptions::compress_textures (the default) the textures are made BC7 with their whole mip chain, colour as sRGB when
/// GltfOptions::srgb_color (bake_textures(), which says how each is averaged); a texture a glTF brings compressed (MSFT_texture_dds) is kept
/// as it is.  Without it they are kept as the 8-bit texels they were decoded to, and GltfOptions::mipmaps has the GPU make their mips;
/// mipmaps with compress_textures is std::invalid_argument, and so is TextureOptions::codec ASTC without compress_textures.
///
/// The file is written whole or not at all: a failed conversion leaves @p edge_file as it was (or absent), so that a build
/// that goes by file times never takes a broken file for a finished one.  Throws std::runtime_error (GltfLoader::load_error
/// for a glTF that cannot be read).
///
/// With @p textures asking for it the textures are packed into an atlas, and the emissive ones left out, before the meshes are tiled and given
/// levels of detail (tiling first, then the atlas, then the levels).
///
/// With a @p tile_metres above zero the model (one static mesh, a ground) is cut into tiles of that side first (split_into_tiles()).
/// With @p lods that has ratios, every mesh gets levels of detail (add_lods()) before it is written.  Returns the triangles of the whole
/// model at each level (level 0 first), meshes that have fewer levels counting their last at the levels they lack.
std::vector<std::size_t> convert_gltf_to_edge(std::string const &gltf_file, std::string const &edge_file, GltfOptions const &options, LodOptions const &lods = LodOptions(),
	float tile_metres = 0, TextureOptions const &textures = TextureOptions(), ConvertOptions const &convert = ConvertOptions());

}
