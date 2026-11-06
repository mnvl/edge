// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

// The next mip level of an image, for the textures edge_convert makes.

#include <cstdint>
#include <vector>

#include <rove/vec.h>

#include "image.h"

namespace edge
{

/// How the texels of a texture are averaged into the level under it.
enum class MipKind
{
	/// The stored values are the quantity: red, green, blue and alpha each averaged as they are.  Metallic-roughness, blend weights.
	Linear,
	/// Red, green and blue are sRGB-encoded light: averaged in linear light, each texel's weighed by its coverage (a texel nobody sees lends no
	/// colour; where nothing is covered, the plain mean), and encoded again.  Alpha is coverage, averaged as it is.
	Srgb,
	/// Red, green and blue are a tangent-space direction (x * 0.5 + 0.5): averaged as vectors and made a unit vector again, so that a
	/// rough surface does not go flat in the distance by the length the average lost.  Alpha as it is.
	Normal,
};

/// A texel in the quantity a kind averages it in: red, green, blue, alpha.
struct Texel
{
	float c[4];
};

/// A texel as numbers to average (0..1 linear light for Srgb, a direction in -1..1 for Normal, the stored value over 255 for Linear) and back
/// (a direction is made a unit vector first, one that has no direction faces up).
Texel decode_texel(rove::bgra<std::uint8_t> texel, MipKind kind);
rove::bgra<std::uint8_t> encode_texel(Texel const &texel, MipKind kind);

/// An image and the levels under it down to one texel, to be sampled at any size it is wanted.
class MipChain
{
public:
	MipChain(Image<rove::bgra<std::uint8_t>> image, MipKind kind);

	int levels() const { return int(levels_.size()); }
	Image<rove::bgra<std::uint8_t>> const &level(int i) const { return levels_.at(size_t(i)); }

	/// The image at (u, v), 0..1 across it from its top left corner and repeating beyond, at level of detail @p lod (0: the image itself, each more
	/// is half the size; between two, both are sampled and blended; one above the last is the last): the four nearest texels of a level blended
	/// by how near the point is to each one's centre, as the GPU does for a repeating, linear sampler.
	rove::bgra<std::uint8_t> sample(float u, float v, float lod) const;

private:
	MipKind kind_;
	std::vector<Image<rove::bgra<std::uint8_t>>> levels_;
};

/// @p image in another size, each texel of the result the mean of the area it covers when that is smaller than the image, and the
/// interpolation between the nearest texels' centres (the image's edge repeated beyond them) when it is larger; averaged the way @p kind says.
/// Along each side on its own, so an image may get wider and shorter.  std::invalid_argument for a size that is not positive or an image of none.
Image<rove::bgra<std::uint8_t>> resize(ImageView<rove::bgra<std::uint8_t> const> image, int width, int height, MipKind kind);

/// The level under @p level: max(1, width / 2) by max(1, height / 2), each texel the mean of the area of @p level it covers: the
/// 2 x 2 under it, or on an odd side the texels the area reaches into by as much of them as it covers (a side of 5 gives 2 texels of
/// 2.5 each).  std::invalid_argument for a 1 x 1 level, which
/// has none under it; resize() to half the size.
Image<rove::bgra<std::uint8_t>> next_mip(ImageView<rove::bgra<std::uint8_t> const> level, MipKind kind);

}
