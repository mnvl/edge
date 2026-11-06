// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

#include <cstddef>
#include <iosfwd>
#include <string>
#include <rove/vec.h>

namespace edge
{

/// What a texture's texels are.  The 8-bit colour formats keep blue first in memory, the float ones red first.  (The values are
/// those the formats were known by: .edge files and the bindings keep them as numbers.)
enum class PixelFormat : unsigned {
	X8R8G8B8 = 1,
	A8R8G8B8 = 2,
	R16G16B16A16F = 3,
	R32G32B32A32F = 4,
	/// Single channel, 32-bit float.  A depth-only render target reads back a quarter of what R32G32B32A32F does for the same
	/// pixels, which is the whole point of having it: a depth or lidar camera writes one float and would discard the other three.
	R32F = 7,
	/// Single channel, 8-bit unsigned integer: a render target's stencil (RenderTarget::stencil_texture), read as a usampler2D.
	R8UI = 13,
	BC1 = 5,
	BC3 = 6,
	BC5 = 8,     // two channels (red, green): normal maps
	BC7 = 9,
	/// ASTC LDR (16 bytes per block of 4x4, 6x6 or 8x8 texels): the compressed format of phones, which lack BC.  sRGB per texture,
	/// as BC7.
	ASTC_4x4 = 10,
	ASTC_6x6 = 11,
	ASTC_8x8 = 12,
};

/// The number of @p format, for messages.
std::string to_string(PixelFormat format);
/// The number of @p format, written to @p out.
std::ostream &operator<<(std::ostream &out, PixelFormat format);

/// What a texture is made in when nothing says otherwise, and the same with an alpha.
inline constexpr PixelFormat DEFAULT_PIXEL_FORMAT = PixelFormat::X8R8G8B8;
inline constexpr PixelFormat DEFAULT_ALPHA_PIXEL_FORMAT = PixelFormat::A8R8G8B8;

/// The bits of a texel of @p format (of a block-compressed one: of a block divided by its texels, rounded).  std::runtime_error for a
/// value that is no format.
int bits_per_pixel(PixelFormat format);
/// The bytes of a @p width x @p height image of @p format: whole blocks for a block-compressed one.
std::size_t pixel_data_size(int width, int height, PixelFormat format);
bool is_block_compressed(PixelFormat format);
bool is_astc(PixelFormat format);
/// Texels along each side of a block (4 for BC; 1 for the uncompressed formats).
int block_extent(PixelFormat format);

/// The formats whose texels lie in memory as a P, for Texture::pixels().
/// The 8-bit formats keep blue first and the float ones red first, and a
/// colour is named as it lies: rove::bgra<uint8_t>, rove::rgba<float>,
/// rove::rgba<uint16_t> (a half is its 16 bits).  A plain rove::vec is four
/// components that are no colour, and R32F a float.  A P that is no
/// format's texel has no definition here.
template <class P> struct pixel_formats;

template <> struct pixel_formats<rove::bgra<unsigned char>> {
	static constexpr PixelFormat list[] = {PixelFormat::X8R8G8B8, PixelFormat::A8R8G8B8};
};

template <> struct pixel_formats<rove::vec<4, unsigned char>> {
	static constexpr PixelFormat list[] = {PixelFormat::X8R8G8B8, PixelFormat::A8R8G8B8};
};

template <> struct pixel_formats<rove::rgba<unsigned short>> {
	static constexpr PixelFormat list[] = {PixelFormat::R16G16B16A16F};
};

template <> struct pixel_formats<rove::vec<4, unsigned short>> {
	static constexpr PixelFormat list[] = {PixelFormat::R16G16B16A16F};
};

template <> struct pixel_formats<rove::rgba<float>> {
	static constexpr PixelFormat list[] = {PixelFormat::R32G32B32A32F};
};

template <> struct pixel_formats<rove::vec<4, float>> {
	static constexpr PixelFormat list[] = {PixelFormat::R32G32B32A32F};
};

template <> struct pixel_formats<float> {
	static constexpr PixelFormat list[] = {PixelFormat::R32F};
};

}
