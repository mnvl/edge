// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include "gltf_loader.h"

#include "gltf_reader.h"

namespace edge
{

GltfContents gltf_contents(std::string const &filename)
{
	fastgltf::Asset const asset = read_gltf_json(filename);
	GltfContents contents;
	contents.skin = !asset.skins.empty();
	for (auto const &mesh : asset.meshes)
		for (auto const &primitive : mesh.primitives)
			contents.vertex_colors = contents.vertex_colors || find_attribute_accessor(primitive, "COLOR_0");
	contents.files = gltf_named_files(asset, filename);
	return contents;
}

SerializedModel GltfLoader::read(std::string const &filename, GltfOptions const &options, std::string const &mesh_type)
{
	return GltfReader(filename, options, mesh_type).read();
}

}
