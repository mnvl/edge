// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include "model_textures.h"

#include <array>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>

#include "image.h"
#include "image_compress.h"
#include "image_mips.h"
#include "pixel.h"
#include "texture.h"

namespace edge
{

namespace
{

using bgra8 = rove::bgra<std::uint8_t>;

/// What a texture is for, which decides how it is averaged into its mips, how its error is weighed and its ASTC block.
enum Role
{
	COLOUR,
	NORMAL,
	DATA,
	MASK,           // blend weights: numbers, a sharp 4x4 ASTC block
	ROLES,
};

/// Every texture slot of the materials, with the role its texture has there.
template <class Materials, class Visit>
void for_each_slot(Materials &materials, Visit visit)
{
	for (auto &m : materials)
	{
		for (auto &t : m.diffuse_textures)
			visit(t, COLOUR);
		visit(m.texture_blending_weights, MASK);
		visit(m.normal_map, NORMAL);
		visit(m.metallic_roughness, DATA);
		visit(m.emissive, COLOUR);
	}
}

/// The ASTC block of a texture of @p role whose longer side is @p side texels: colour 6x6 (8x8 at 128 or less, where nobody sees the
/// detail), numbers 8x8, normal maps and masks 4x4.
int astc_block(Role role, int side)
{
	if (role == NORMAL || role == MASK)
		return 4;
	if (role == DATA)
		return 8;
	return side <= 128 ? 8 : 6;
}

/// The texture as BC7 or ASTC with every mip level; std::runtime_error if it is not of a format that can be.
SerializedTexture bake(SerializedTexture const &texture, Role role, TextureCodec codec)
{
	if (texture.pixel_format != PixelFormat::A8R8G8B8 && texture.pixel_format != PixelFormat::X8R8G8B8)
	{
		throw std::runtime_error("texture '" + texture.filename + "' is of pixel format " + to_string(texture.pixel_format) +
			", which is neither 8 bits a texel nor block-compressed already");
	}
	if (texture.provided_mip_levels > 1)
	{
		throw std::runtime_error("texture '" + texture.filename + "' brings " + std::to_string(texture.provided_mip_levels) + " mip levels of 8 bits a texel");
	}
	size_t const need = size_t(texture.width) * size_t(texture.height) * 4;
	if (texture.width <= 0 || texture.height <= 0 || texture.pixels.size() != need)
	{
		throw std::runtime_error("texture '" + texture.filename + "' has " + std::to_string(texture.pixels.size()) + " bytes of texels, " +
			std::to_string(need) + " needed for " + std::to_string(texture.width) + " x " + std::to_string(texture.height));
	}

	Image<bgra8> level(texture.width, texture.height);
	std::memcpy(static_cast<void *>(level.pixels()), texture.pixels.data(), need);
	if (texture.pixel_format == PixelFormat::X8R8G8B8)
	{
		// the fourth byte is nothing: the texture is opaque
		for (int y = 0; y < level.height(); ++y)
			for (int x = 0; x < level.width(); ++x)
				level(x, y).a = 255;
	}

	MipKind const kind = role == NORMAL ? MipKind::Normal : role == COLOUR && texture.srgb ? MipKind::Srgb : MipKind::Linear;
	BlockContent const content = role == COLOUR ? BlockContent::Colour : BlockContent::Data;

	int const block = astc_block(role, std::max(texture.width, texture.height));
	SerializedTexture result;
	result.filename = texture.filename;
	result.width = texture.width;
	result.height = texture.height;
	result.pixel_format = codec == TextureCodec::BC7 ? PixelFormat::BC7 : block == 4 ? PixelFormat::ASTC_4x4 : block == 6 ? PixelFormat::ASTC_6x6
		: PixelFormat::ASTC_8x8;
	result.srgb = texture.srgb;
	result.mipmaps = false;
	result.provided_mip_levels = 0;
	for (;;)
	{
		std::vector<std::uint8_t> const blocks = codec == TextureCodec::BC7 ? compress_bc7(level, content) : compress_astc(level, block, texture.srgb);
		result.pixels.insert(result.pixels.end(), blocks.begin(), blocks.end());
		++result.provided_mip_levels;
		if (level.width() == 1 && level.height() == 1)
			break;
		level = next_mip(level, kind);
	}
	return result;
}

}

void bake_textures(SerializedModel &model, TextureCodec codec)
{
	// (a mask is numbers as BC7: a texture of both is one)
	auto const role_of = [&](Role role) { return codec == TextureCodec::BC7 && role == MASK ? DATA : role; };
	// the roles each texture has, from the materials
	std::vector<std::array<bool, ROLES>> used(model.textures.size(), std::array<bool, ROLES>{});
	for_each_slot(std::as_const(model.materials), [&](std::int64_t slot, Role role) {
		if (slot == -1)
			return;
		if (slot < 0 || size_t(slot) >= model.textures.size())
		{
			throw std::runtime_error("a material names texture " + std::to_string(slot) + " of " + std::to_string(model.textures.size()));
		}
		used[size_t(slot)][role_of(role)] = true;
	});

	// the baked table, in the order of the old one: a texture once for each role it has, and once whatever its roles if it is
	// block-compressed already
	std::vector<SerializedTexture> textures;
	std::vector<std::array<std::int64_t, ROLES>> index(model.textures.size());
	for (size_t t = 0; t < model.textures.size(); ++t)
	{
		SerializedTexture const &texture = model.textures[t];
		bool const compressed = is_block_compressed(texture.pixel_format);
		bool const astc = texture.pixel_format == PixelFormat::ASTC_4x4 || texture.pixel_format == PixelFormat::ASTC_6x6 ||
			texture.pixel_format == PixelFormat::ASTC_8x8;
		if (compressed && codec == TextureCodec::ASTC && !astc)
		{
			throw std::runtime_error("texture '" + texture.filename + "' is block-compressed already in pixel format " +
				to_string(texture.pixel_format) + ", which is not ASTC: a phone cannot sample it, and it is not made ASTC again");
		}
		std::int64_t kept = -1;
		bool any = false;
		for (int role = 0; role < ROLES; ++role)
		{
			index[t][role] = -1;
			if (!used[t][role])
				continue;
			any = true;
			if (compressed)
			{
				if (kept < 0)
				{
					kept = std::int64_t(textures.size());
					textures.push_back(texture);
				}
				index[t][role] = kept;
			}
			else
			{
				index[t][role] = std::int64_t(textures.size());
				textures.push_back(bake(texture, Role(role), codec));
			}
		}
		if (!any)
		{
			throw std::runtime_error("texture '" + texture.filename + "' is used by no material");
		}
	}

	model.textures = std::move(textures);
	for_each_slot(model.materials, [&](auto &slot, Role role) {
		if (slot != -1)
			slot = index[size_t(slot)][role_of(role)];
	});
}

void remove_unused_textures(SerializedModel &model)
{
	std::vector<std::int64_t> renumbered(model.textures.size(), -1);
	for_each_slot(std::as_const(model.materials), [&](std::int64_t slot, Role) {
		if (slot == -1)
			return;
		if (slot < 0 || size_t(slot) >= model.textures.size())
		{
			throw std::runtime_error("a material names texture " + std::to_string(slot) + " of " + std::to_string(model.textures.size()));
		}
		renumbered[size_t(slot)] = 0;
	});
	std::vector<SerializedTexture> kept;
	for (size_t t = 0; t < model.textures.size(); ++t)
	{
		if (renumbered[t] == 0)
		{
			renumbered[t] = std::int64_t(kept.size());
			kept.push_back(std::move(model.textures[t]));
		}
	}
	model.textures = std::move(kept);
	for_each_slot(model.materials, [&](auto &slot, Role) {
		if (slot != -1)
			slot = renumbered[size_t(slot)];
	});
}

void drop_emissive(SerializedModel &model)
{
	for (SerializedMaterial &m : model.materials)
	{
		m.emissive = -1;
		m.emissive_factor = rove::vec<3>(0.0f, 0.0f, 0.0f);
	}
	remove_unused_textures(model);
}

}
