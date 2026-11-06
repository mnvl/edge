// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include "mesh_tiles.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>

namespace edge
{

void split_into_tiles(SerializedModel &model, float tile_metres)
{
	if (!(tile_metres > 0) || !std::isfinite(tile_metres))
	{
		throw std::invalid_argument("tiles of " + std::to_string(tile_metres) + " metres");
	}
	if (model.meshes.size() != 1 || model.joints.size() != 1 || model.joints[0].mesh_index != 0)
	{
		throw std::invalid_argument("only a model of one mesh on one joint can be cut into tiles");
	}
	SerializedMesh const &mesh = model.meshes[0];
	SerializedJoint const &joint = model.joints[0];
	if (mesh.serialization_type.rfind("static_mesh", 0) != 0 || mesh.pose_bounds || !mesh.joint_index_transform.empty())
	{
		throw std::invalid_argument("mesh '" + mesh.name + "' is skinned: only a static mesh can be cut into tiles");
	}
	if (!mesh.morph_target_deltas.empty())
	{
		throw std::invalid_argument("mesh '" + mesh.name + "' has morph targets: it cannot be cut into tiles");
	}
	if (!mesh.lods.empty())
	{
		throw std::invalid_argument("mesh '" + mesh.name + "' has levels of detail already: cut it into tiles first");
	}

	// the tile of each triangle, by its centre, group by group
	using Square = std::pair<long, long>;
	struct Tile
	{
		std::vector<std::vector<unsigned>> groups;          // the whole mesh's indices, a list for each group of the mesh
	};
	std::map<Square, Tile> tiles;
	for (std::size_t g = 0; g < mesh.triangles.size(); ++g)
	{
		SerializedTriangles const &group = mesh.triangles[g];
		for (std::size_t i = group.first_index_offset; i < group.first_index_offset + group.triangles_count * 3; i += 3)
		{
			float cx = 0, cz = 0;
			for (std::size_t k = 0; k < 3; ++k)
			{
				cx += mesh.vertices.at(mesh.indices.at(i + k)).position.x / 3;
				cz += mesh.vertices.at(mesh.indices.at(i + k)).position.z / 3;
			}
			Tile &tile = tiles[{static_cast<long>(std::floor(cx / tile_metres)), static_cast<long>(std::floor(cz / tile_metres))}];
			tile.groups.resize(mesh.triangles.size());
			tile.groups[g].insert(tile.groups[g].end(), mesh.indices.begin() + static_cast<long>(i), mesh.indices.begin() + static_cast<long>(i) + 3);
		}
	}
	// the names count from the first column and the first row that have a tile
	long first_column = 0, first_row = 0;
	bool any = false;
	for (auto const &entry : tiles)
	{
		first_column = any ? std::min(first_column, entry.first.first) : entry.first.first;
		first_row = any ? std::min(first_row, entry.first.second) : entry.first.second;
		any = true;
	}

	std::vector<SerializedMesh> meshes;
	std::vector<SerializedJoint> joints;
	for (auto const &[square, tile] : tiles)
	{
		SerializedMesh part;
		part.serialization_type = mesh.serialization_type;
		part.id = mesh.id;
		part.name = mesh.name + "." + std::to_string(square.first - first_column) + "." + std::to_string(square.second - first_row);
		part.bind_shape_matrix = mesh.bind_shape_matrix;
		part.inverted_bind_matrices = mesh.inverted_bind_matrices;
		part.always_visible = mesh.always_visible;
		for (SerializedCustomAttribute const &attribute : mesh.custom_attributes)
		{
			part.custom_attributes.push_back({attribute.name, attribute.components, {}});
		}
		// the vertices the tile uses, in the mesh's order
		std::vector<char> used(mesh.vertices.size(), 0);
		for (std::vector<unsigned> const &indices : tile.groups)
		{
			for (unsigned index : indices)
			{
				used[index] = 1;
			}
		}
		std::vector<unsigned> renumbered(mesh.vertices.size(), 0);
		for (std::size_t v = 0; v < mesh.vertices.size(); ++v)
		{
			if (!used[v])
			{
				continue;
			}
			renumbered[v] = static_cast<unsigned>(part.vertices.size());
			part.vertices.push_back(mesh.vertices[v]);
			for (std::size_t a = 0; a < mesh.custom_attributes.size(); ++a)
			{
				SerializedCustomAttribute const &attribute = mesh.custom_attributes[a];
				part.custom_attributes[a].values.insert(part.custom_attributes[a].values.end(),
					attribute.values.begin() + static_cast<long>(v * attribute.components), attribute.values.begin() + static_cast<long>((v + 1) * attribute.components));
			}
		}
		for (std::size_t g = 0; g < tile.groups.size(); ++g)
		{
			if (tile.groups[g].empty())
			{
				continue;
			}
			SerializedTriangles made;
			made.material_index = mesh.triangles[g].material_index;
			made.minimal_vertex_index = 0;
			made.vertices_count = part.vertices.size();
			made.first_index_offset = part.indices.size();
			made.triangles_count = tile.groups[g].size() / 3;
			for (unsigned index : tile.groups[g])
			{
				part.indices.push_back(renumbered[index]);
			}
			part.triangles.push_back(made);
		}
		SerializedJoint tile_joint = joint;
		tile_joint.index = joints.size();
		tile_joint.id = joint.id + "." + std::to_string(joints.size());
		tile_joint.name = part.name;
		tile_joint.parent_index = -1;
		tile_joint.child_indices.clear();
		tile_joint.mesh_index = static_cast<std::int64_t>(meshes.size());
		joints.push_back(tile_joint);
		meshes.push_back(std::move(part));
	}
	if (meshes.empty())
	{
		throw std::invalid_argument("mesh '" + mesh.name + "' has no triangles to cut into tiles");
	}
	model.meshes = std::move(meshes);
	model.joints = std::move(joints);
}

}
