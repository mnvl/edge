// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include "model_atlas_xatlas.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>

#include <xatlas.h>

#include "image.h"
#include "image_mips.h"
#include "log.h"
#include "model_atlas_common.h"

namespace edge
{

namespace
{

using namespace atlas_detail;

constexpr int PAD = 4;                      // texels round a chart, drawn with its edge
constexpr double MIN_TEXELS = 64.0;         // what a material whose texture the surfaces hardly use is given
constexpr double FILL = 0.7;                // of the atlas, the first try
constexpr int MAX_TRIES = 40;
constexpr float SHRINK = 0.85f;

struct AtlasDeleter
{
	void operator ()(xatlas::Atlas *a) const { xatlas::Destroy(a); }
};

/// What the faces of a material are in the model: where they are, how large (on the surface and in the texture's coordinates).
struct MaterialFaces
{
	double area = 0;                // of the surface
	double uv_area = 0;             // in texture coordinates: 1 for the whole picture once
	size_t faces = 0;
};

double cross_length(rove::vec<3> a, rove::vec<3> b)
{
	rove::vec<3> const c(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
	return std::sqrt(double(c.x) * c.x + double(c.y) * c.y + double(c.z) * c.z);
}

/// The faces of a mesh that go in the atlas: the face is the index of its first index over 3.
struct PackedFaces
{
	std::vector<size_t> faces;
	std::vector<size_t> materials;
};

}

std::size_t atlas_with_xatlas(SerializedModel &model, AtlasOptions const &options, std::vector<bool> packable)
{
	// the faces of each material
	std::vector<MaterialFaces> stats(model.materials.size());
	for (SerializedMesh const &mesh : model.meshes)
	{
		for (SerializedTriangles const &group : mesh.triangles)
		{
			if (group.material_index < 0 || !packable[size_t(group.material_index)])
				continue;
			MaterialFaces &s = stats[size_t(group.material_index)];
			for (size_t t = 0; t < group.triangles_count; ++t)
			{
				GenericVertex const &a = mesh.vertices[mesh.indices[group.first_index_offset + 3 * t]];
				GenericVertex const &b = mesh.vertices[mesh.indices[group.first_index_offset + 3 * t + 1]];
				GenericVertex const &c = mesh.vertices[mesh.indices[group.first_index_offset + 3 * t + 2]];
				s.area += 0.5 * cross_length(b.position - a.position, c.position - a.position);
				s.uv_area += 0.5 * std::abs(double(b.texcoords.x - a.texcoords.x) * double(c.texcoords.y - a.texcoords.y) -
					double(c.texcoords.x - a.texcoords.x) * double(b.texcoords.y - a.texcoords.y));
				++s.faces;
			}
		}
	}
	for (size_t i = 0; i < model.materials.size(); ++i)
	{
		if (!packable[i])
			continue;
		if (stats[i].faces == 0)
		{
			packable[i] = false;
		}
		else if (!(stats[i].area > 0))
		{
			EDGE_LOG_WARN("atlas: material '{}' is left with its own textures: its surfaces have no area", model.materials[i].name);
			packable[i] = false;
		}
	}
	std::set<TextureSet> distinct;
	for (size_t i = 0; i < model.materials.size(); ++i)
		if (packable[i])
			distinct.insert(texture_set(model.materials[i]));
	if (distinct.size() < 2)
	{
		return 0;
	}

	// what the texture of a material has to spare: as many texels as the surfaces of the material show of it, and no more than it has
	std::vector<double> density(model.materials.size(), 0.0);
	double total_texels = 0;
	for (size_t i = 0; i < model.materials.size(); ++i)
	{
		if (!packable[i])
			continue;
		double pixels = 0;
		for (std::int64_t t : texture_set(model.materials[i]))
			if (t >= 0)
				pixels = std::max(pixels, double(model.textures[size_t(t)].width) * double(model.textures[size_t(t)].height));
		double const texels = std::min(pixels, std::max(MIN_TEXELS, stats[i].uv_area * pixels));
		density[i] = std::sqrt(texels / stats[i].area);
		total_texels += texels;
	}

	// the faces that go in, as a mesh of loose triangles for each mesh of the model (each corner a vertex of its own, scaled to the density of
	// its face's material, so that one unit is one texel whatever the material)
	std::vector<PackedFaces> packed(model.meshes.size());
	std::unique_ptr<xatlas::Atlas, AtlasDeleter> atlas(xatlas::Create());
	std::vector<std::vector<float>> positions(model.meshes.size()), normals(model.meshes.size());
	std::vector<std::vector<std::uint32_t>> indices(model.meshes.size()), face_materials(model.meshes.size());
	std::vector<size_t> added;                          // the meshes of the model that are in the atlas, in the order of their AddMesh
	for (size_t m = 0; m < model.meshes.size(); ++m)
	{
		SerializedMesh const &mesh = model.meshes[m];
		for (SerializedTriangles const &group : mesh.triangles)
		{
			if (group.material_index < 0 || !packable[size_t(group.material_index)])
				continue;
			for (size_t t = 0; t < group.triangles_count; ++t)
			{
				size_t const first = group.first_index_offset + 3 * t;
				packed[m].faces.push_back(first / 3);
				packed[m].materials.push_back(size_t(group.material_index));
				float const scale = float(density[size_t(group.material_index)]);
				for (int c = 0; c < 3; ++c)
				{
					GenericVertex const &v = mesh.vertices[mesh.indices[first + size_t(c)]];
					positions[m].insert(positions[m].end(), {v.position.x * scale, v.position.y * scale, v.position.z * scale});
					normals[m].insert(normals[m].end(), {v.normal.x, v.normal.y, v.normal.z});
					indices[m].push_back(std::uint32_t(indices[m].size()));
				}
				face_materials[m].push_back(std::uint32_t(group.material_index));
			}
		}
		if (packed[m].faces.empty())
			continue;
		for (std::vector<GenericVertex> const &target : mesh.morph_target_deltas)
			for (unsigned long face : packed[m].faces)
				for (int c = 0; c < 3; ++c)
				{
					size_t const v = mesh.indices[3 * face + size_t(c)];
					if (v < target.size() && (target[v].texcoords.x != 0 || target[v].texcoords.y != 0))
						fail("a morph target of mesh '" + mesh.name + "' moves the texture coordinates of vertex " + std::to_string(v));
				}
		xatlas::MeshDecl decl;
		decl.vertexCount = std::uint32_t(positions[m].size() / 3);
		decl.vertexPositionData = positions[m].data();
		decl.vertexPositionStride = 3 * sizeof(float);
		decl.vertexNormalData = normals[m].data();
		decl.vertexNormalStride = 3 * sizeof(float);
		decl.indexData = indices[m].data();
		decl.indexCount = std::uint32_t(indices[m].size());
		decl.indexFormat = xatlas::IndexFormat::UInt32;
		decl.faceMaterialData = face_materials[m].data();
		decl.faceCount = std::uint32_t(packed[m].faces.size());
		xatlas::AddMeshError const error = xatlas::AddMesh(atlas.get(), decl, std::uint32_t(model.meshes.size()));
		if (error != xatlas::AddMeshError::Success)
			fail(std::string("xatlas refuses mesh '") + mesh.name + "': " + xatlas::StringForEnum(error));
		added.push_back(m);
	}

	xatlas::ComputeCharts(atlas.get());
	// as large as it is, which is as dense as the textures were, or as it takes to fit
	xatlas::PackOptions pack;
	pack.resolution = std::uint32_t(options.size);
	pack.padding = PAD;
	pack.bilinear = true;
	pack.blockAlign = true;
	float scale = float(std::min(1.0, std::sqrt(FILL * double(options.size) * double(options.size) / total_texels)));
	float const smallest = scale / 64.0f;
	for (int attempt = 0;; ++attempt)
	{
		pack.texelsPerUnit = scale;
		xatlas::PackCharts(atlas.get(), pack);
		if (atlas->atlasCount == 1)
			break;
		scale *= SHRINK;
		if (attempt == MAX_TRIES || scale < smallest)
			fail(std::to_string(distinct.size()) + " different sets of textures do not fit " + std::to_string(options.size) + " x " + std::to_string(options.size));
	}

	// the size of what is used
	float used_x = 0, used_y = 0;
	for (uint32_t k = 0; k < atlas->meshCount; ++k)
	{
		xatlas::Mesh const &out = atlas->meshes[k];
		for (uint32_t v = 0; v < out.vertexCount; ++v)
		{
			if (out.vertexArray[v].atlasIndex < 0)
				continue;
			used_x = std::max(used_x, out.vertexArray[v].uv[0]);
			used_y = std::max(used_y, out.vertexArray[v].uv[1]);
		}
	}
	auto round_up = [&](float used) { return std::min(options.size, (int(std::ceil(used)) + PAD + 3) / 4 * 4); };
	int const width = round_up(used_x), height = round_up(used_y);

	// each corner of each packed face is the vertex xatlas made for it
	for (size_t k = 0; k < added.size(); ++k)
	{
		size_t const m = added[k];
		xatlas::Mesh const &out = atlas->meshes[k];
		if (out.indexCount != indices[m].size())
			fail("xatlas made " + std::to_string(out.indexCount) + " indices of " + std::to_string(indices[m].size()));
		for (uint32_t i = 0; i < out.indexCount; ++i)
		{
			xatlas::Vertex const &v = out.vertexArray[out.indexArray[i]];
			if (v.xref != i)
				fail("xatlas did not keep the corners of the faces of mesh '" + model.meshes[m].name + "' in their order");
		}
	}

	// the pictures of the textures at the sizes they are wanted, and the atlases
	std::vector<TextureSet> sets(distinct.begin(), distinct.end());
	Roles const roles = roles_of(model, sets);
	std::map<std::pair<std::int64_t, int>, MipChain> chains;
	auto chain_of = [&](std::int64_t texture, int role) -> MipChain const & {
		auto found = chains.find({texture, role});
		if (found == chains.end())
			found = chains.emplace(std::make_pair(texture, role), MipChain(picture_of(model.textures[size_t(texture)]), kind_of(Role(role), model.textures[size_t(texture)].srgb))).first;
		return found->second;
	};
	std::array<Image<bgra8>, ROLES> atlases;
	for (int r = 0; r < ROLES; ++r)
		if (roles.present[size_t(r)])
			atlases[size_t(r)] = Image<bgra8>(width, height, bgra8(0, 0, 0, 0));
	std::vector<bool> covered(size_t(width) * size_t(height), false);

	for (size_t k = 0; k < added.size(); ++k)
	{
		size_t const m = added[k];
		SerializedMesh const &mesh = model.meshes[m];
		xatlas::Mesh const &out = atlas->meshes[k];
		for (size_t f = 0; f < packed[m].faces.size(); ++f)
		{
			xatlas::Vertex const *corner[3];
			rove::vec<2> old[3];
			for (int c = 0; c < 3; ++c)
			{
				corner[c] = &out.vertexArray[out.indexArray[3 * f + size_t(c)]];
				old[c] = mesh.vertices[mesh.indices[3 * packed[m].faces[f] + size_t(c)]].texcoords;
			}
			if (corner[0]->atlasIndex < 0 || corner[1]->atlasIndex < 0 || corner[2]->atlasIndex < 0)
				continue;       // a face with no area: xatlas left it out
			double const x0 = corner[0]->uv[0], y0 = corner[0]->uv[1], x1 = corner[1]->uv[0], y1 = corner[1]->uv[1], x2 = corner[2]->uv[0], y2 = corner[2]->uv[1];
			double const twice = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0);
			if (twice == 0)
				continue;
			double const old_twice = std::abs(double(old[1].x - old[0].x) * double(old[2].y - old[0].y) - double(old[2].x - old[0].x) * double(old[1].y - old[0].y));
			TextureSet const set = texture_set(model.materials[packed[m].materials[f]]);
			int const x_from = std::max(0, int(std::floor(std::min({x0, x1, x2})))), x_to = std::min(width - 1, int(std::ceil(std::max({x0, x1, x2}))));
			int const y_from = std::max(0, int(std::floor(std::min({y0, y1, y2})))), y_to = std::min(height - 1, int(std::ceil(std::max({y0, y1, y2}))));
			for (int y = y_from; y <= y_to; ++y)
			{
				for (int x = x_from; x <= x_to; ++x)
				{
					double const px = x + 0.5, py = y + 0.5;
					double const l1 = ((px - x0) * (y2 - y0) - (x2 - x0) * (py - y0)) / twice;
					double const l2 = ((x1 - x0) * (py - y0) - (px - x0) * (y1 - y0)) / twice;
					double const l0 = 1.0 - l1 - l2;
					if (l0 < -1e-6 || l1 < -1e-6 || l2 < -1e-6)
						continue;
					float const u = float(l0 * old[0].x + l1 * old[1].x + l2 * old[2].x), v = float(l0 * old[0].y + l1 * old[1].y + l2 * old[2].y);
					for (int r = 0; r < ROLES; ++r)
					{
						std::int64_t const t = set[size_t(r)];
						if (t < 0)
							continue;
						// as many of the picture's texels go to a texel of the atlas as the faces' areas say
						double const picture_texels = old_twice * double(model.textures[size_t(t)].width) * double(model.textures[size_t(t)].height);
						float const lod = picture_texels > 0 ? float(0.5 * std::log2(picture_texels / std::abs(twice))) : 0.0f;
						atlases[size_t(r)](x, y) = chain_of(t, r).sample(u, v, lod);
					}
					covered[size_t(y) * size_t(width) + size_t(x)] = true;
				}
			}
		}
	}

	// the edge of each chart drawn out past it, so that filtering and the mip levels do not bring in what is next to it
	for (int r = 0; r < ROLES; ++r)
	{
		if (!roles.present[size_t(r)])
			continue;
		std::vector<bool> have = covered;
		for (int pass = 0; pass < PAD; ++pass)
		{
			std::vector<bool> next = have;
			Image<bgra8> grown = atlases[size_t(r)];
			for (int y = 0; y < height; ++y)
				for (int x = 0; x < width; ++x)
				{
					if (have[size_t(y) * size_t(width) + size_t(x)])
						continue;
					int sum[4] = {0, 0, 0, 0}, count = 0;
					for (int dy = -1; dy <= 1; ++dy)
						for (int dx = -1; dx <= 1; ++dx)
						{
							int const nx = x + dx, ny = y + dy;
							if (nx < 0 || ny < 0 || nx >= width || ny >= height || !have[size_t(ny) * size_t(width) + size_t(nx)])
								continue;
							for (int c = 0; c < 4; ++c)
								sum[c] += atlases[size_t(r)](nx, ny).i[c];
							++count;
						}
					if (count == 0)
						continue;
					for (int c = 0; c < 4; ++c)
						grown(x, y).i[c] = std::uint8_t((sum[c] + count / 2) / count);
					next[size_t(y) * size_t(width) + size_t(x)] = true;
				}
			atlases[size_t(r)] = std::move(grown);
			have = std::move(next);
		}
	}

	// the meshes with a vertex for each corner of each packed face, and the others as they were
	std::vector<SerializedMesh> made;
	for (size_t k = 0; k < added.size(); ++k)
	{
		size_t const m = added[k];
		SerializedMesh mesh = model.meshes[m];
		xatlas::Mesh const &out = atlas->meshes[k];
		std::vector<bool> is_packed(mesh.indices.size() / 3, false);
		for (size_t face : packed[m].faces)
			is_packed[face] = true;

		// the vertices the faces that stay use come first, in their order, and then those of the packed faces, as they are met
		std::vector<size_t> origin;
		std::vector<rove::vec<2>> coordinates;
		std::vector<std::int64_t> kept(mesh.vertices.size(), -1);
		for (size_t face = 0; face < is_packed.size(); ++face)
			if (!is_packed[face])
				for (size_t c = 0; c < 3; ++c)
					kept[mesh.indices[3 * face + c]] = 0;
		for (size_t v = 0; v < mesh.vertices.size(); ++v)
			if (kept[v] == 0)
			{
				kept[v] = std::int64_t(origin.size());
				origin.push_back(v);
				coordinates.push_back(model.meshes[m].vertices[v].texcoords);
			}
		for (size_t face = 0; face < is_packed.size(); ++face)
			if (!is_packed[face])
				for (size_t c = 0; c < 3; ++c)
					mesh.indices[3 * face + c] = unsigned(kept[model.meshes[m].indices[3 * face + c]]);
		std::map<std::tuple<size_t, std::uint32_t, std::uint32_t>, unsigned> made_vertices;
		for (size_t f = 0; f < packed[m].faces.size(); ++f)
		{
			for (size_t c = 0; c < 3; ++c)
			{
				size_t const slot = 3 * packed[m].faces[f] + c;
				size_t const old_vertex = model.meshes[m].indices[slot];
				xatlas::Vertex const &v = out.vertexArray[out.indexArray[3 * f + c]];
				rove::vec<2> uv = model.meshes[m].vertices[old_vertex].texcoords;
				if (v.atlasIndex >= 0)
					uv = rove::vec<2>(v.uv[0] / float(width), v.uv[1] / float(height));
				std::uint32_t bits[2];
				std::memcpy(&bits[0], &uv.x, 4);
				std::memcpy(&bits[1], &uv.y, 4);
				auto const key = std::make_tuple(old_vertex, bits[0], bits[1]);
				auto found = made_vertices.find(key);
				if (found == made_vertices.end())
				{
					found = made_vertices.emplace(key, unsigned(origin.size())).first;
					origin.push_back(old_vertex);
					coordinates.push_back(uv);
				}
				mesh.indices[slot] = found->second;
			}
		}

		mesh.vertices.clear();
		for (size_t i = 0; i < origin.size(); ++i)
		{
			mesh.vertices.push_back(model.meshes[m].vertices[origin[i]]);
			mesh.vertices.back().texcoords = coordinates[i];
		}
		for (size_t t = 0; t < mesh.morph_target_deltas.size(); ++t)
		{
			std::vector<GenericVertex> const &old_target = model.meshes[m].morph_target_deltas[t];
			if (old_target.empty())
				continue;
			std::vector<GenericVertex> &target = mesh.morph_target_deltas[t];
			target.clear();
			for (size_t o : origin)
				target.push_back(old_target.at(o));
		}
		for (size_t a = 0; a < mesh.custom_attributes.size(); ++a)
		{
			SerializedCustomAttribute const &old_attribute = model.meshes[m].custom_attributes[a];
			SerializedCustomAttribute &attribute = mesh.custom_attributes[a];
			attribute.values.clear();
			for (size_t o : origin)
				attribute.values.insert(attribute.values.end(), old_attribute.values.begin() + std::ptrdiff_t(o * old_attribute.components),
					old_attribute.values.begin() + std::ptrdiff_t((o + 1) * old_attribute.components));
		}
		for (SerializedTriangles &group : mesh.triangles)
		{
			size_t lowest = mesh.vertices.size(), highest = 0;
			for (size_t i = group.first_index_offset; i < group.first_index_offset + 3 * group.triangles_count; ++i)
			{
				lowest = std::min<size_t>(lowest, mesh.indices[i]);
				highest = std::max<size_t>(highest, mesh.indices[i]);
			}
			if (group.triangles_count > 0)
			{
				group.minimal_vertex_index = lowest;
				group.vertices_count = highest - lowest + 1;
			}
		}
		made.push_back(std::move(mesh));
	}

	// nothing can fail from here: the model is changed
	std::array<std::int64_t, ROLES> const index = add_atlas_textures(model, atlases, roles);
	for (size_t k = 0; k < added.size(); ++k)
		model.meshes[added[k]] = std::move(made[k]);
	std::size_t count = 0;
	for (bool p : packable)
		count += p ? 1 : 0;
	repoint_materials(model, packable, index);
	return count;
}

}
