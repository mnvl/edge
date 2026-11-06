// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

// Block compression of 8-bit images, for the textures edge_convert makes: BC7 for the desktop (the encoder is bc7enc's, the decoder in
// its tests the same author's), ASTC for phones (ARM's astc-encoder, which decodes in the tests too).

#include <cstdint>
#include <vector>

#include <rove/vec.h>

#include "image.h"

namespace edge
{

/// What an image is, for the encoder to weigh its error by.
enum class BlockContent
{
	/// Colour, whose error is judged as an eye does: more in green than blue.
	Colour,
	/// Numbers (normals, metallic-roughness), every channel the same worth.
	Data,
};

/// The BC7 blocks (16 bytes, 4 x 4 texels each) of @p image, row of blocks after row of blocks from the top left: the bytes of
/// a PixelFormat::BC7 texture of its size.  A side that is not a multiple of 4 has the last texels repeated to fill its blocks.
/// Spread over the cores; the same image gives the same bytes however many there are.  std::invalid_argument for an image of no pixels.
std::vector<std::uint8_t> compress_bc7(ImageView<rove::bgra<std::uint8_t> const> image, BlockContent content);

/// The block compression a model's textures are made in (bake_textures()).
enum class TextureCodec
{
	BC7,        // the desktop's
	ASTC,       // phones', which lack BC
};

/// The ASTC blocks (LDR, 16 bytes, @p block x @p block texels each: 4, 6 or 8) of @p image, row of blocks after row of blocks from the
/// top left: the bytes of a PixelFormat::ASTC_4x4, ASTC_6x6 or ASTC_8x8 texture of its size.  @p srgb: the texture is sampled as sRGB,
/// and its error judged so.  A side that is not a multiple of the block has its blocks filled by the encoder.  Spread over the cores;
/// the same image gives the same bytes however many there are.  std::invalid_argument for another block or an image of no pixels.
std::vector<std::uint8_t> compress_astc(ImageView<rove::bgra<std::uint8_t> const> image, int block, bool srgb);

}
