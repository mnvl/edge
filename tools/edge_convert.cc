// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

// edge_convert: makes .edge model files from glTF files (edge/src/model_convert.h).
//
//   edge_convert [--srgb-color] [--mipmaps] [--no-compress] [--astc] [--pose-margin FRACTION]
//                [--lods RATIO,RATIO,...] [--lod-min-triangles N] [--lod-lock-border] [--tile METRES]
//                [--atlas none|grid|xatlas] [--atlas-size N] [--atlas-keep MATERIAL]... [--no-emissive] [--no-occlusion] [--skinned-colors]
//                <in.glb> <out.edge>
//   edge_convert --inputs <in.glb>
//   edge_convert --version
//
// The options are GltfOptions (the textures are made BC7 with their whole mip chain, ASTC with --astc for phones, colour as sRGB with --srgb-color; --no-compress keeps
// them as 8-bit texels instead, and --mipmaps, which needs --no-compress, has the GPU make their mips; --pose-margin grows the pose boxes of skinned meshes, see
// GltfOptions::pose_margin) and LodOptions (--lods gives every mesh a coarser version for each ratio of its triangles, falling, e.g.
// 0.5,0.2,0.06; a mesh of fewer than --lod-min-triangles, 300 when not given, gets none; --lod-lock-border keeps the vertices on a
// mesh's open edge at every level).  --atlas grid packs the textures of the model's materials into one set, each in a cell of its own, and --atlas
// xatlas unwraps the meshes anew and draws the textures into the atlas at the density they had (nothing of a texture that tiles repeated),
// both as large as --atlas-size texels along a side at most (2048 when not given; model_atlas.h); --no-emissive leaves the light of the materials out (no emissive texture, no
// emissive colour), --no-occlusion the occlusion maps (GltfOptions::occlusion: they are packed into the red of the metallic-roughness
// texture, and one that cannot be is an error), --skinned-colors keeps a skinned glTF's vertex colors (an AnimatedModelPTBNJWTC file; ConvertOptions).  --atlas-keep leaves the named material (as often as given) out of the atlas, with its own textures and coordinates.  --tile cuts a model of one static mesh, a ground, into tiles of that side first (mesh_tiles.h).  Exits 0 when the file is written, 1 with a message on stderr when it
// is not.
//
// --inputs prints the files a model is made from, one a line (GltfContents::files: the glTF, its buffers' and images' files), for a build
// to remake it when one of them changes (etc/cmake/EdgeModels.cmake); 1 with a message for a glTF that cannot be read or names a file
// that is not there.  --version prints "edge_convert .edge format N pipeline H": the version of the files it writes and the pipeline that
// makes them (pipeline.h), which every file it makes keeps.

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <string>
#include <vector>

#include "gltf_loader.h"
#include "model_convert.h"
#include "pipeline.h"
#include "serialized_model.h"

int main(int argc, char **argv)
{
	if (argc == 2 && std::string(argv[1]) == "--version")
	{
		std::printf("edge_convert .edge format %u pipeline %s\n", unsigned(edge::MODEL_BINARY_VERSION), edge::pipeline_hash());
		return 0;
	}
	if (argc >= 2 && std::string(argv[1]) == "--inputs")
	{
		if (argc != 3)
		{
			std::fprintf(stderr, "usage: edge_convert --inputs <in.glb>\n");
			return 1;
		}
		try
		{
			for (auto const &file : edge::gltf_contents(argv[2]).files)
				std::printf("%s\n", file.string().c_str());
		}
		catch (std::exception const &e)
		{
			std::fprintf(stderr, "edge_convert: %s\n", e.what());
			return 1;
		}
		return 0;
	}
	edge::GltfOptions options;
	edge::LodOptions lods;
	float tile_metres = 0;
	edge::TextureOptions textures;
	edge::ConvertOptions convert;
	std::vector<std::string> files;
	for (int i = 1; i < argc; ++i)
	{
		std::string const a = argv[i];
		if (a == "--pose-margin")
		{
			char *end = nullptr;
			if (++i < argc)
				options.pose_margin = std::strtof(argv[i], &end);
			if (i == argc || end == argv[i] || *end != '\0')
			{
				std::fprintf(stderr, "edge_convert: --pose-margin takes a number\n");
				return 1;
			}
		}
		else if (a == "--lods")
		{
			if (++i == argc)
			{
				std::fprintf(stderr, "edge_convert: --lods takes ratios, e.g. 0.5,0.2\n");
				return 1;
			}
			std::string list = argv[i];
			for (std::size_t from = 0; from <= list.size();)
			{
				std::size_t const comma = std::min(list.find(',', from), list.size());
				std::string const word = list.substr(from, comma - from);
				char *end = nullptr;
				float const ratio = std::strtof(word.c_str(), &end);
				if (word.empty() || *end != '\0')
				{
					std::fprintf(stderr, "edge_convert: --lods takes ratios, e.g. 0.5,0.2; '%s' is not a number\n", word.c_str());
					return 1;
				}
				lods.ratios.push_back(ratio);
				from = comma + 1;
			}
		}
		else if (a == "--lod-min-triangles")
		{
			char *end = nullptr;
			if (++i < argc)
				lods.min_triangles = std::strtoul(argv[i], &end, 10);
			if (i == argc || end == argv[i] || *end != '\0')
			{
				std::fprintf(stderr, "edge_convert: --lod-min-triangles takes a whole number\n");
				return 1;
			}
		}
		else if (a == "--lod-lock-border")
			lods.lock_border = true;
		else if (a == "--tile")
		{
			char *end = nullptr;
			if (++i < argc)
				tile_metres = std::strtof(argv[i], &end);
			if (i == argc || end == argv[i] || *end != '\0' || !(tile_metres > 0))
			{
				std::fprintf(stderr, "edge_convert: --tile takes a number of metres above zero\n");
				return 1;
			}
		}
		else if (a == "--atlas")
		{
			std::string const kind = ++i < argc ? argv[i] : "";
			if (kind == "none")
				textures.atlas = edge::TextureOptions::Atlas::None;
			else if (kind == "grid")
				textures.atlas = edge::TextureOptions::Atlas::Grid;
			else if (kind == "xatlas")
				textures.atlas = edge::TextureOptions::Atlas::Xatlas;
			else
			{
				std::fprintf(stderr, "edge_convert: --atlas takes none, grid or xatlas\n");
				return 1;
			}
		}
		else if (a == "--atlas-size")
		{
			char *end = nullptr;
			if (++i < argc)
				textures.atlas_size = int(std::strtol(argv[i], &end, 10));
			if (i == argc || end == argv[i] || *end != '\0' || textures.atlas_size < 4)
			{
				std::fprintf(stderr, "edge_convert: --atlas-size takes a whole number of texels, 4 or more\n");
				return 1;
			}
		}
		else if (a == "--atlas-keep")
		{
			if (++i == argc)
			{
				std::fprintf(stderr, "edge_convert: --atlas-keep takes the name of a material\n");
				return 1;
			}
			textures.atlas_keep.push_back(argv[i]);
		}
		else if (a == "--no-emissive")
			textures.emissive = false;
		else if (a == "--no-occlusion")
			options.occlusion = false;
		else if (a == "--srgb-color")
			options.srgb_color = true;
		else if (a == "--mipmaps")
			options.mipmaps = true;
		else if (a == "--no-compress")
			options.compress_textures = false;
		else if (a == "--astc")
			textures.codec = edge::TextureCodec::ASTC;
		else if (a == "--skinned-colors")
			convert.skinned_colors = true;
		else if (a.rfind("--", 0) == 0)
		{
			std::fprintf(stderr, "edge_convert: unknown option %s\n", a.c_str());
			return 1;
		}
		else
			files.push_back(a);
	}
	if (files.size() != 2)
	{
		std::fprintf(stderr, "usage: edge_convert [--srgb-color] [--mipmaps] [--no-compress] [--astc] [--pose-margin FRACTION] [--lods RATIO,...] [--lod-min-triangles N] [--lod-lock-border] [--tile METRES] [--atlas none|grid|xatlas] [--atlas-size N] [--atlas-keep MATERIAL]... [--no-emissive] [--no-occlusion] [--skinned-colors] <in.glb> <out.edge>\n");
		return 1;
	}
	try
	{
		std::vector<std::size_t> const triangles = edge::convert_gltf_to_edge(files[0], files[1], options, lods, tile_metres, textures, convert);
		if (!lods.ratios.empty())
		{
			std::printf("%s: triangles at each level:", files[1].c_str());
			for (std::size_t t : triangles)
				std::printf(" %zu", t);
			std::printf("\n");
		}
	}
	catch (std::exception const &e)
	{
		std::fprintf(stderr, "edge_convert: %s: %s\n", files[0].c_str(), e.what());
		return 1;
	}
	return 0;
}
