// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace edge
{

class Model;
struct SerializedModel;

/// How a glTF file's textures are loaded.  The defaults are edge's original
/// behaviour.
struct GltfOptions
{
	/// Compress 8-bit textures to BC1/BC3 at load (no mips).
	bool compress_textures = true;
	/// Base colour and emissive textures are sRGB (sampled linear); normal,
	/// metallic-roughness and occlusion stay linear, as glTF says.
	bool srgb_color = false;
	/// Full mip chains (needs compress_textures = false).
	bool mipmaps = false;
	/// Textures keep their CPU copy (TextureUsage::keep_cpu_copy).
	bool keep_cpu_copy = true;
	/// glTF's occlusion maps (occlusionTexture) packed into the red of the metallic-roughness texture (glTF's ORM: occlusion red,
	/// roughness green, metallic blue; Material::occlusion), as glTF's ORM images have them already: two images are packed into one
	/// of the larger size, the smaller sampled as the GPU would.  Occlusion that cannot be packed -- of other texture coordinates than the
	/// material's other textures, of a DDS beside a picture, of a strength outside 0 to 1 -- is a load_error naming the file.  False
	/// leaves every occlusion map out (edge_convert --no-occlusion).
	bool occlusion = true;
	/// Grows the pose box of every skinned mesh (BasicAnimatedMesh::pose_bounds()) by this fraction of its size on each side.  The
	/// box holds the bind pose and every pose of every clip; a model that is posed otherwise (two clips mixed, a joint turned by
	/// code) can ask for room for that here.  Not negative.
	float pose_margin = 0;
};

/// What decides the type of model a glTF file makes.
struct GltfContents
{
	bool skin = false;              // a skin: an AnimatedModel, otherwise a StaticModel
	bool vertex_colors = false;     // COLOR_0 on any primitive: a StaticModelPTBNTC, which has a color for each vertex
	/// What the model is made from: the glTF file, then each file its buffers and images name (once, in the order they first come;
	/// absolute and lexically normal), what a build remakes the model after.  Not what the file holds (a .glb's chunk, a data: URI).
	std::vector<std::filesystem::path> files;
};

/// The GltfContents of the glTF file.  Reads the file's JSON only, not its buffers or images.  Throws GltfLoader::load_error for
/// a file that is missing or is not glTF, or that names a buffer or an image file that is not there or a URI that is not a file.
GltfContents gltf_contents(std::string const &filename);

class GltfLoader
{
public:
	class load_error: public std::runtime_error
	{
	public:
		explicit load_error(std::string const &message):
			std::runtime_error(message)
		{
		}
	};

	/// The model of the glTF file @p filename as the structures a .edge file holds, which Model::load_from_gltf() loads: textures (decoded,
	/// and compressed, sRGB, mipmapped as @p options say), materials, a joint for each node of the scene, meshes of @p mesh_type
	/// (Mesh::serialization_type() of the model's meshes; the model's mesh_serialization_type()) with no pose boxes yet, clips.
	/// Throws load_error for a file that cannot be read or is not one edge loads, std::invalid_argument for options that do not go
	/// together, std::runtime_error for a mesh the type cannot hold (no COLOR_0 for one with a color, or a part of a mesh with it).
	static SerializedModel read(std::string const &filename, GltfOptions const &options, std::string const &mesh_type);
};

}
