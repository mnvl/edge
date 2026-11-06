// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

// Texture files without a renderer: a picture's bytes (what stb_image reads: PNG, JPEG, ...) or a DDS's as the texels of a texture
// (SerializedTexture), which import_texture() puts on a renderer.  Texture::load_from_memory() and glTF loading make their textures so.

#include <cstddef>

#include "serialized_model.h"

namespace edge
{

/// How a picture becomes texels.
struct TextureDecoding
{
	/// BC1 (opaque) or BC3 (an alpha under 250 somewhere), when both sides are multiples of 4 (otherwise the 8-bit texels, and a
	/// warning).  Not with mipmaps.
	bool compress = false;
	/// sRGB-encoded colour (TextureUsage::srgb).  A DDS is sRGB when it says so or this does, but BC5 (numbers) never.
	bool srgb = false;
	/// The GPU makes the mips (TextureUsage::mipmaps); not of a DDS, which brings its own.
	bool mipmaps = false;
	/// The texture keeps its CPU copy (TextureUsage::keep_cpu_copy).  With it, a plain 8-bit texture is X8R8G8B8, as edge always made
	/// it; an sRGB, mipmapped or GPU-only one is A8R8G8B8.
	bool keep_cpu_copy = true;
};

/// The texels of the picture or DDS file of @p size bytes at @p data: a picture's B, G, R, A rows (row 0 first), compressed if asked;
/// a DDS's blocks and levels as they are (SerializedTexture::provided_mip_levels).  The filename is left empty.
/// std::invalid_argument for compress with mipmaps; std::runtime_error for bytes that are neither, or a DDS of an array, a cube or a
/// format edge does not sample.
SerializedTexture decode_texture(void const *data, std::size_t size, TextureDecoding const &how);

/// glTF's occlusion packed into a metallic-roughness texture (glTF's ORM: occlusion red, roughness green, metallic blue), of two pictures'
/// bytes (as decode_texture() takes them): red the occlusion picture's red, green, blue and alpha the metallic-roughness picture's, or
/// 255 without one (@p metallic_roughness null: the material's factors as they are).  Of the larger width and the larger height of the
/// two, a picture of another size sampled as the GPU samples a texture that repeats (bilinear between texel centres).  Made as @p how
/// says, as decode_texture() makes a picture (compressed, mipmapped ...).  std::runtime_error for bytes that are no picture: a DDS too
/// (its blocks have no texels to pack); std::invalid_argument for compress with mipmaps.
SerializedTexture pack_occlusion(void const *metallic_roughness, std::size_t metallic_roughness_size, void const *occlusion,
	std::size_t occlusion_size, TextureDecoding const &how);

/// What a DDS file's header says, and where its data starts.
struct DdsHeader
{
	int width, height, levels, layers;
	PixelFormat format{};
	bool srgb;
	std::size_t offset;
	/// Bytes of the data: every level of every layer.
	std::size_t data_size() const;
};

/// The header of the DDS file of @p size bytes at @p data, checked against its size.  std::runtime_error for one edge does not read.
DdsHeader read_dds(void const *data, std::size_t size);

}
