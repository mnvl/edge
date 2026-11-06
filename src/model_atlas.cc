// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include "model_atlas.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include "image.h"
#include "image_mips.h"
#include "log.h"
#include "model_atlas_common.h"
#include "model_atlas_xatlas.h"

namespace edge
{

namespace
{

using namespace atlas_detail;

constexpr int MIN_CELL = 4;                 // one block
constexpr int MAX_GUTTER = 8;
constexpr float UV_SLACK = 1e-3f;           // how far past 0..1 a coordinate may be and the surface not tile

struct Cell
{
	TextureSet textures;
	int size = 0;               // texels along a side, a power of two
	int x = 0, y = 0;           // of its top left texel in the atlas
	int gutter() const { return std::min(MAX_GUTTER, size / 8); }
	int content() const { return size - 2 * gutter(); }
};

int nearest_power_of_two(int n)
{
	return 1 << std::max(0, int(std::lround(std::log2(double(n)))));
}

/// The cell of the texels' position @p m along the Z-order curve: bits of m alternately into x and y.
void unmorton(unsigned m, int &x, int &y)
{
	x = y = 0;
	for (int bit = 0; bit < 16; ++bit)
	{
		x |= int((m >> (2 * bit)) & 1u) << bit;
		y |= int((m >> (2 * bit + 1)) & 1u) << bit;
	}
}

/// Whether the cells of @p sizes (a side each) fit an atlas of at most @p limit texels along a side, and its size if so: the smallest
/// square, or a rectangle of half the height if that holds them (the Z-order fills the top half first).
bool fits(std::vector<int> const &sizes, int limit, int &width, int &height)
{
	int const unit = *std::min_element(sizes.begin(), sizes.end());
	long units = 0;
	for (int s : sizes)
		units += long(s / unit) * long(s / unit);
	long along = 1;
	while (along * along < units)
		along *= 2;
	width = int(along) * unit;
	height = units * 2 <= along * along ? width / 2 : width;
	return width <= limit;
}

/// The picture of the cell's role in the atlas: the texture made the size of the cell's content, set in the cell with the texels of its
/// edge repeated in the gutter.
void paint(Image<bgra8> &atlas, Cell const &cell, Image<bgra8> const &picture, MipKind kind)
{
	int const g = cell.gutter(), c = cell.content();
	Image<bgra8> const content = resize(picture, c, c, kind);
	for (int y = 0; y < cell.size; ++y)
		for (int x = 0; x < cell.size; ++x)
			atlas(cell.x + x, cell.y + y) = content(std::clamp(x - g, 0, c - 1), std::clamp(y - g, 0, c - 1));
}

std::size_t atlas_in_cells(SerializedModel &model, AtlasOptions const &options, std::vector<bool> packable)
{
	// the vertices each material's triangles use: a material whose coordinates leave 0..1 tiles its texture, and is not packed
	std::vector<bool> tiles(model.materials.size(), false);
	for (SerializedMesh const &mesh : model.meshes)
	{
		for (SerializedTriangles const &group : mesh.triangles)
		{
			if (group.material_index < 0 || !packable[size_t(group.material_index)])
				continue;
			for (size_t i = group.first_index_offset; i < group.first_index_offset + 3 * group.triangles_count; ++i)
			{
				rove::vec<2> const &uv = mesh.vertices[mesh.indices[i]].texcoords;
				if (!(uv.x >= -UV_SLACK && uv.x <= 1 + UV_SLACK && uv.y >= -UV_SLACK && uv.y <= 1 + UV_SLACK))
					tiles[size_t(group.material_index)] = true;
			}
		}
	}
	for (size_t i = 0; i < model.materials.size(); ++i)
	{
		if (packable[i] && tiles[i])
		{
			EDGE_LOG_WARN("atlas: material '{}' is left with its own textures: its texture coordinates leave 0..1", model.materials[i].name);
			packable[i] = false;
		}
	}

	// a cell for each different set of textures
	std::vector<Cell> cells;
	std::map<TextureSet, size_t> cell_of;
	std::vector<std::int64_t> cell_of_material(model.materials.size(), -1);
	for (size_t i = 0; i < model.materials.size(); ++i)
	{
		if (!packable[i])
			continue;
		TextureSet const set = texture_set(model.materials[i]);
		auto const found = cell_of.find(set);
		if (found == cell_of.end())
		{
			Cell cell;
			cell.textures = set;
			int largest = 1;
			for (std::int64_t t : set)
				if (t >= 0)
					largest = std::max({largest, model.textures[size_t(t)].width, model.textures[size_t(t)].height});
			cell.size = std::max(MIN_CELL, nearest_power_of_two(largest));
			cell_of_material[i] = std::int64_t(cells.size());
			cell_of[set] = cells.size();
			cells.push_back(cell);
		}
		else
		{
			cell_of_material[i] = std::int64_t(found->second);
		}
	}
	if (cells.size() < 2)
	{
		return 0;
	}

	// which cell each vertex of each mesh goes to; a vertex that goes to two, or to one and is wanted by a material that is not packed, cannot
	std::vector<std::vector<std::int64_t>> owner(model.meshes.size());
	for (size_t m = 0; m < model.meshes.size(); ++m)
	{
		SerializedMesh const &mesh = model.meshes[m];
		owner[m].assign(mesh.vertices.size(), -1);
		for (SerializedTriangles const &group : mesh.triangles)
		{
			if (group.material_index < 0)
				continue;
			size_t const material = size_t(group.material_index);
			if (!has_texture(texture_set(model.materials[material])))
				continue;
			// -2: a material with a texture that is not packed
			std::int64_t const cell = packable[material] ? cell_of_material[material] : -2;
			for (size_t i = group.first_index_offset; i < group.first_index_offset + 3 * group.triangles_count; ++i)
			{
				std::int64_t &mine = owner[m][mesh.indices[i]];
				if (mine != -1 && mine != cell && (mine >= 0 || cell >= 0))
					fail("vertex " + std::to_string(mesh.indices[i]) + " of mesh '" + mesh.name + "' is used by materials that take different cells (or by one that is not packed)");
				if (mine == -1 || cell >= 0)
					mine = cell;
			}
		}
		for (std::vector<GenericVertex> const &target : mesh.morph_target_deltas)
			for (size_t v = 0; v < target.size() && v < owner[m].size(); ++v)
				if (owner[m][v] >= 0 && (target[v].texcoords.x != 0 || target[v].texcoords.y != 0))
					fail("a morph target of mesh '" + mesh.name + "' moves the texture coordinates of vertex " + std::to_string(v));
	}

	// sizes: all made smaller by the same factor until they fit
	std::vector<int> sizes;
	for (Cell const &c : cells)
		sizes.push_back(c.size);
	int limit = 1;
	while (limit * 2 <= options.size)
		limit *= 2;
	int width = 0, height = 0;
	while (!fits(sizes, limit, width, height))
	{
		if (std::all_of(sizes.begin(), sizes.end(), [](int s) { return s == MIN_CELL; }))
			fail(std::to_string(cells.size()) + " different sets of textures do not fit " + std::to_string(limit) + " x " + std::to_string(limit) + " in cells of 4 x 4");
		for (int &s : sizes)
			s = std::max(MIN_CELL, s / 2);
	}
	for (size_t i = 0; i < cells.size(); ++i)
		cells[i].size = sizes[i];

	// places: the largest first, along the Z-order curve, where each is on a multiple of its own area
	std::vector<size_t> order(cells.size());
	for (size_t i = 0; i < order.size(); ++i)
		order[i] = i;
	std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return cells[a].size > cells[b].size; });
	int const unit = *std::min_element(sizes.begin(), sizes.end());
	unsigned along = 0;
	for (size_t i : order)
	{
		int x = 0, y = 0;
		unmorton(along, x, y);
		cells[i].x = x * unit;
		cells[i].y = y * unit;
		along += unsigned(cells[i].size / unit) * unsigned(cells[i].size / unit);
	}

	// the atlases
	std::vector<TextureSet> sets;
	for (Cell const &c : cells)
		sets.push_back(c.textures);
	Roles const roles = roles_of(model, sets);
	std::array<Image<bgra8>, ROLES> atlases;
	std::map<std::int64_t, Image<bgra8>> pictures;
	for (int r = 0; r < ROLES; ++r)
	{
		if (!roles.present[size_t(r)])
			continue;
		atlases[size_t(r)] = Image<bgra8>(width, height, bgra8(0, 0, 0, 0));
		MipKind const kind = kind_of(Role(r), roles.srgb[size_t(r)]);
		for (Cell const &c : cells)
		{
			std::int64_t const t = c.textures[size_t(r)];
			if (t < 0)
				continue;
			auto picture = pictures.find(t);
			if (picture == pictures.end())
				picture = pictures.emplace(t, picture_of(model.textures[size_t(t)])).first;
			paint(atlases[size_t(r)], c, picture->second, kind);
		}
	}

	// nothing can fail from here: the model is changed
	std::array<std::int64_t, ROLES> const atlas = add_atlas_textures(model, atlases, roles);
	std::size_t packed = 0;
	for (bool p : packable)
		packed += p ? 1 : 0;
	repoint_materials(model, packable, atlas);

	for (size_t m = 0; m < model.meshes.size(); ++m)
	{
		SerializedMesh &mesh = model.meshes[m];
		for (size_t v = 0; v < mesh.vertices.size(); ++v)
		{
			if (owner[m][v] < 0)
				continue;
			Cell const &cell = cells[size_t(owner[m][v])];
			rove::vec<2> &uv = mesh.vertices[v].texcoords;
			float const content = float(cell.content());
			uv.set((float(cell.x + cell.gutter()) + uv.x * content) / float(width), (float(cell.y + cell.gutter()) + uv.y * content) / float(height));
		}
	}
	return packed;
}

}

std::size_t atlas_materials(SerializedModel &model, AtlasOptions const &options)
{
	if (options.size < MIN_CELL)
	{
		throw std::invalid_argument("an atlas of " + std::to_string(options.size) + " texels along a side");
	}
	validate(model);
	std::vector<bool> packable = find_packable(model);
	for (std::string const &name : options.keep)
		if (std::none_of(model.materials.begin(), model.materials.end(), [&](SerializedMaterial const &m) { return m.name == name; }))
			fail("no material is named '" + name + "' to be kept");
	for (size_t i = 0; i < model.materials.size(); ++i)
		if (std::find(options.keep.begin(), options.keep.end(), model.materials[i].name) != options.keep.end())
			packable[i] = false;
	switch (options.mode)
	{
	case AtlasOptions::Mode::Grid:
		return atlas_in_cells(model, options, std::move(packable));
	case AtlasOptions::Mode::Xatlas:
		return atlas_with_xatlas(model, options, std::move(packable));
	}
	throw std::invalid_argument("an atlas of a mode that is not one");
}

}
