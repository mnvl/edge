// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

// What the two ways of making an atlas (model_atlas.cc: a cell for each set of textures; model_atlas_xatlas.cc: an unwrapping of the meshes)
// have in common.  Not for anyone else.

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "image.h"
#include "image_mips.h"
#include "serialized_model.h"

namespace edge::atlas_detail
{

using bgra8 = rove::bgra<std::uint8_t>;

/// The ways a texture is used, each of which has an atlas of its own.
enum Role
{
	COLOUR,
	METALLIC_ROUGHNESS,
	NORMAL,
	EMISSIVE,
	ROLES,
};

/// The textures a material has in each role, -1 for none.
using TextureSet = std::array<std::int64_t, ROLES>;

/// std::runtime_error("atlas: " + what).
[[noreturn]] void fail(std::string const &what);

TextureSet texture_set(SerializedMaterial const &material);
bool has_texture(TextureSet const &set);

/// How a texture of the role is averaged and sampled.
MipKind kind_of(Role role, bool srgb);

/// std::runtime_error for what the model says that makes no sense (a texture, or a material, that is not there; a group that reaches
/// past its indices; an index past the vertices) and for a mesh that has levels of detail.
void validate(SerializedModel const &model);

/// The materials that can be packed: with a texture, no more than one diffuse texture, no blend weights, and no texture that is block-compressed
/// already.  std::runtime_error for a texture of such a material that is not 8 bits a texel, or whose texels are not as many as its size.
std::vector<bool> find_packable(SerializedModel const &model);

/// The texture's texels as an image (X8R8G8B8 made opaque).
Image<bgra8> picture_of(SerializedTexture const &texture);

/// What the atlases of the sets of textures in @p sets have: a role is present when any set has a texture in it; its textures are sRGB or
/// not (std::runtime_error when they are both).
struct Roles
{
	std::array<bool, ROLES> present{};
	std::array<bool, ROLES> srgb{};
};
Roles roles_of(SerializedModel const &model, std::vector<TextureSet> const &sets);

/// Adds a texture to the model for each role @p roles has, of the image, and returns their indices (-1 for a role it has not).
std::array<std::int64_t, ROLES> add_atlas_textures(SerializedModel &model, std::array<Image<bgra8>, ROLES> const &images, Roles const &roles);

/// The materials that are @p packed have the atlas textures where they had textures of their own, and the textures nothing uses are removed.
void repoint_materials(SerializedModel &model, std::vector<bool> const &packed, std::array<std::int64_t, ROLES> const &atlas);

}
