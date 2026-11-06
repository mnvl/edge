// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include "model_atlas_common.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>

#include "model_textures.h"
#include "pixel.h"

namespace edge::atlas_detail
{

void fail(std::string const &what)
{
	throw std::runtime_error("atlas: " + what);
}

TextureSet texture_set(SerializedMaterial const &m)
{
	return {m.diffuse_textures.empty() ? -1 : m.diffuse_textures[0], m.metallic_roughness, m.normal_map, m.emissive};
}

bool has_texture(TextureSet const &set)
{
	return std::any_of(set.begin(), set.end(), [](std::int64_t t) { return t >= 0; });
}

MipKind kind_of(Role role, bool srgb)
{
	if (role == NORMAL)
		return MipKind::Normal;
	return (role == COLOUR || role == EMISSIVE) && srgb ? MipKind::Srgb : MipKind::Linear;
}

void validate(SerializedModel const &model)
{
	std::int64_t const textures_count = std::int64_t(model.textures.size());
	for (SerializedMaterial const &m : model.materials)
	{
		for (std::int64_t t : {m.texture_blending_weights, m.normal_map, m.metallic_roughness, m.emissive})
			if (t < -1 || t >= textures_count)
				fail("material '" + m.name + "' names texture " + std::to_string(t) + " of " + std::to_string(textures_count));
		for (std::int64_t t : m.diffuse_textures)
			if (t < 0 || t >= textures_count)
				fail("material '" + m.name + "' names texture " + std::to_string(t) + " of " + std::to_string(textures_count));
	}
	std::int64_t const materials_count = std::int64_t(model.materials.size());
	for (SerializedMesh const &mesh : model.meshes)
	{
		if (!mesh.lods.empty())
			fail("mesh '" + mesh.name + "' has levels of detail: pack the textures first");
		for (SerializedTriangles const &group : mesh.triangles)
		{
			if (group.material_index < -1 || group.material_index >= materials_count)
				fail("a group of mesh '" + mesh.name + "' has material " + std::to_string(group.material_index) + " of " + std::to_string(materials_count));
			if (group.first_index_offset + 3 * group.triangles_count > mesh.indices.size())
				fail("a group of mesh '" + mesh.name + "' reaches past its indices");
			for (size_t i = group.first_index_offset; i < group.first_index_offset + 3 * group.triangles_count; ++i)
				if (mesh.indices[i] >= mesh.vertices.size())
					fail("mesh '" + mesh.name + "' has an index past its vertices");
		}
	}
}

std::vector<bool> find_packable(SerializedModel const &model)
{
	std::vector<bool> packable(model.materials.size(), false);
	for (size_t i = 0; i < model.materials.size(); ++i)
	{
		SerializedMaterial const &m = model.materials[i];
		TextureSet const set = texture_set(m);
		if (!has_texture(set) || m.diffuse_textures.size() > 1 || m.texture_blending_weights >= 0)
			continue;
		bool compressed = false;
		for (std::int64_t t : set)
		{
			if (t < 0)
				continue;
			SerializedTexture const &texture = model.textures[size_t(t)];
			if (is_block_compressed(texture.pixel_format))
			{
				compressed = true;
				continue;
			}
			if (texture.pixel_format != PixelFormat::A8R8G8B8 && texture.pixel_format != PixelFormat::X8R8G8B8)
				fail("texture '" + texture.filename + "' is of pixel format " + to_string(texture.pixel_format) + ", not 8 bits a texel");
			size_t const need = size_t(std::max(texture.width, 0)) * size_t(std::max(texture.height, 0)) * 4;
			if (texture.width <= 0 || texture.height <= 0 || texture.pixels.size() != need)
				fail("texture '" + texture.filename + "' has " + std::to_string(texture.pixels.size()) + " bytes of texels, " + std::to_string(need) +
					" needed for " + std::to_string(texture.width) + " x " + std::to_string(texture.height));
		}
		packable[i] = !compressed;
	}
	return packable;
}

Image<bgra8> picture_of(SerializedTexture const &texture)
{
	Image<bgra8> image(texture.width, texture.height);
	std::memcpy(static_cast<void *>(image.pixels()), texture.pixels.data(), image.count() * sizeof(bgra8));
	if (texture.pixel_format == PixelFormat::X8R8G8B8)
	{
		for (int y = 0; y < image.height(); ++y)
			for (int x = 0; x < image.width(); ++x)
				image(x, y).a = 255;
	}
	return image;
}

Roles roles_of(SerializedModel const &model, std::vector<TextureSet> const &sets)
{
	Roles roles;
	std::array<int, ROLES> seen;
	seen.fill(-1);
	for (TextureSet const &set : sets)
	{
		for (int r = 0; r < ROLES; ++r)
		{
			if (set[size_t(r)] < 0)
				continue;
			roles.present[size_t(r)] = true;
			int const is = model.textures[size_t(set[size_t(r)])].srgb ? 1 : 0;
			if (seen[size_t(r)] != -1 && seen[size_t(r)] != is)
				fail("the textures of one role are sRGB and not");
			seen[size_t(r)] = is;
			roles.srgb[size_t(r)] = is == 1;
		}
	}
	return roles;
}

std::array<std::int64_t, ROLES> add_atlas_textures(SerializedModel &model, std::array<Image<bgra8>, ROLES> const &images, Roles const &roles)
{
	static char const *const NAMES[ROLES] = {"atlas.colour", "atlas.metallic_roughness", "atlas.normal", "atlas.emissive"};
	std::array<std::int64_t, ROLES> index;
	for (int r = 0; r < ROLES; ++r)
	{
		index[size_t(r)] = -1;
		if (!roles.present[size_t(r)])
			continue;
		Image<bgra8> const &image = images[size_t(r)];
		SerializedTexture t;
		t.filename = NAMES[r];
		t.width = image.width();
		t.height = image.height();
		t.pixel_format = PixelFormat::A8R8G8B8;
		t.srgb = roles.srgb[size_t(r)];
		t.pixels.resize(image.count() * sizeof(bgra8));
		std::memcpy(t.pixels.data(), image.pixels(), t.pixels.size());
		index[size_t(r)] = std::int64_t(model.textures.size());
		model.textures.push_back(std::move(t));
	}
	return index;
}

void repoint_materials(SerializedModel &model, std::vector<bool> const &packed, std::array<std::int64_t, ROLES> const &atlas)
{
	for (size_t i = 0; i < model.materials.size(); ++i)
	{
		if (!packed[i])
			continue;
		SerializedMaterial &m = model.materials[i];
		TextureSet const set = texture_set(m);
		if (!m.diffuse_textures.empty())
			m.diffuse_textures[0] = atlas[COLOUR];
		if (set[METALLIC_ROUGHNESS] >= 0)
			m.metallic_roughness = atlas[METALLIC_ROUGHNESS];
		if (set[NORMAL] >= 0)
			m.normal_map = atlas[NORMAL];
		if (set[EMISSIVE] >= 0)
			m.emissive = atlas[EMISSIVE];
	}
	remove_unused_textures(model);
}

}
