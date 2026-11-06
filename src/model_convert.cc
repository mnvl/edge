// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include "model_convert.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <system_error>

#include "animated_model.h"
#include "log.h"
#include "mesh_lod.h"
#include "mesh_tiles.h"
#include "model_textures.h"
#include "null_renderer.h"
#include "scheduler.h"
#include "serialized_model.h"
#include "static_model.h"

namespace edge
{

namespace
{

/// The options of a conversion as a .edge file keeps them (SerializedModel::options): every one, by name, in one order.
std::string describe(GltfOptions const &options, LodOptions const &lods, float tile_metres, TextureOptions const &textures,
	ConvertOptions const &convert)
{
	auto const number = [](double value) {
		char text[32];
		std::snprintf(text, sizeof(text), "%.9g", value);
		return std::string(text);
	};
	std::string ratios;
	for (float ratio : lods.ratios)
		ratios += (ratios.empty() ? "" : ",") + number(ratio);
	std::string keep;
	for (std::string const &material : textures.atlas_keep)
		keep += (keep.empty() ? "" : ",") + material;
	char const *const atlas = textures.atlas == TextureOptions::Atlas::None ? "none" : textures.atlas == TextureOptions::Atlas::Grid ? "grid" : "xatlas";
	return std::string("compress=") + (options.compress_textures ? "1" : "0") + " srgb_color=" + (options.srgb_color ? "1" : "0") +
		" mipmaps=" + (options.mipmaps ? "1" : "0") + " occlusion=" + (options.occlusion ? "1" : "0") + " pose_margin=" + number(options.pose_margin) + " codec=" +
		(textures.codec == TextureCodec::BC7 ? "bc7" : "astc") + " lods=" + ratios + " lod_min_triangles=" + std::to_string(lods.min_triangles) +
		" lod_lock_border=" + (lods.lock_border ? "1" : "0") + " tile=" + number(tile_metres) + " atlas=" + atlas + " atlas_size=" +
		std::to_string(textures.atlas_size) + " atlas_keep=" + keep + " emissive=" + (textures.emissive ? "1" : "0") + " skinned_colors=" +
		(convert.skinned_colors ? "1" : "0");
}

template <class M>
std::vector<std::size_t> convert_as(RendererPtr const &renderer, std::string const &gltf_file, std::string const &temporary, GltfOptions const &options,
	LodOptions const &lods, float tile_metres, TextureOptions const &textures, ConvertOptions const &how, bool bake)
{
	auto model = std::make_shared<M>(renderer, std::make_shared<Scheduler>(renderer));
	model->load_from_gltf(gltf_file, options);
	SerializedModel serialized = model->serialize();
	if (tile_metres > 0)
	{
		split_into_tiles(serialized, tile_metres);
	}
	if (!textures.emissive)
	{
		drop_emissive(serialized);
	}
	if (textures.atlas != TextureOptions::Atlas::None)
	{
		AtlasOptions atlas;
		atlas.mode = textures.atlas == TextureOptions::Atlas::Grid ? AtlasOptions::Mode::Grid : AtlasOptions::Mode::Xatlas;
		atlas.size = textures.atlas_size;
		atlas.keep = textures.atlas_keep;
		atlas_materials(serialized, atlas);
	}
	for (SerializedMesh &mesh : serialized.meshes)
	{
		add_lods(mesh, lods);
	}
	if (bake)
	{
		bake_textures(serialized, textures.codec);
	}
	serialized.options = describe(options, lods, tile_metres, textures, how);
	Model::write_file(serialized, temporary);
	std::size_t levels = 1;
	for (SerializedMesh const &mesh : serialized.meshes)
	{
		levels = std::max(levels, mesh.lods.size() + 1);
	}
	std::vector<std::size_t> triangles(levels, 0);
	for (SerializedMesh const &mesh : serialized.meshes)
	{
		for (std::size_t level = 0; level < levels; ++level)
		{
			auto const &groups = level == 0 || mesh.lods.empty() ? mesh.triangles : mesh.lods[std::min(level, mesh.lods.size()) - 1].triangles;
			for (SerializedTriangles const &group : groups)
			{
				triangles[level] += group.triangles_count;
			}
		}
	}
	return triangles;
}

}

std::vector<std::size_t> convert_gltf_to_edge(std::string const &gltf_file, std::string const &edge_file, GltfOptions const &options, LodOptions const &lods,
	float tile_metres, TextureOptions const &textures, ConvertOptions const &convert)
{
	RendererPtr renderer(new NullRenderer(1, 1));

	if (options.compress_textures && options.mipmaps)
	{
		throw std::invalid_argument("glTF options: mipmaps need compress_textures = false");
	}
	if (!options.compress_textures && textures.codec == TextureCodec::ASTC)
	{
		throw std::invalid_argument("texture options: ASTC needs compress_textures");
	}
	// the textures are read as they are and made BC7 with their mips here (bake_textures()), not BC1 or BC3 without mips by the loader
	bool const bake = options.compress_textures;
	GltfOptions keeping = options;
	keeping.keep_cpu_copy = true;
	keeping.compress_textures = false;
	std::string const temporary = edge_file + ".tmp";
	std::vector<std::size_t> triangles;
	try
	{
		GltfContents const contents = gltf_contents(gltf_file);
		if (contents.skin)
		{
			if (contents.vertex_colors && convert.skinned_colors)
			{
				triangles = convert_as<AnimatedModelPTBNJWTC>(renderer, gltf_file, temporary, keeping, lods, tile_metres, textures, convert, bake);
			}
			else
			{
				if (contents.vertex_colors)
				{
					EDGE_LOG_WARN("{}: a skinned glTF's vertex colors (COLOR_0) are dropped (ConvertOptions::skinned_colors keeps them)", gltf_file);
				}
				triangles = convert_as<AnimatedModelPTBNJWT>(renderer, gltf_file, temporary, keeping, lods, tile_metres, textures, convert, bake);
			}
		}
		else if (contents.vertex_colors)
		{
			triangles = convert_as<StaticModelPTBNTC>(renderer, gltf_file, temporary, keeping, lods, tile_metres, textures, convert, bake);
		}
		else
		{
			triangles = convert_as<StaticModelPTBNT>(renderer, gltf_file, temporary, keeping, lods, tile_metres, textures, convert, bake);
		}
		std::filesystem::rename(temporary, edge_file);
		return triangles;
	}
	catch (...)
	{
		std::error_code ignored;
		std::filesystem::remove(temporary, ignored);
		throw;
	}
}

}
