// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

// BC7 blocks decoded by bc7enc's decoder, for the tests of what the encoder and the model converter make.

#include <boost/test/unit_test.hpp>

#include <cstddef>
#include <cstdint>

#include <bc7decomp.h>
#include <rove/vec.h>

#include "image.h"

namespace edge
{

/// The @p width x @p height image the BC7 blocks at @p blocks (row of blocks after row from the top left, @p size bytes) make.
inline Image<rove::bgra<std::uint8_t>> decode_bc7(std::uint8_t const *blocks, std::size_t size, int width, int height)
{
	int const bx = (width + 3) / 4, by = (height + 3) / 4;
	BOOST_REQUIRE_EQUAL(size, std::size_t(bx) * by * 16);
	Image<rove::bgra<std::uint8_t>> image(width, height);
	for (int j = 0; j < by; ++j)
		for (int i = 0; i < bx; ++i)
		{
			bc7decomp::color_rgba texels[16];
			BOOST_REQUIRE(bc7decomp::unpack_bc7(blocks + (std::size_t(j) * bx + i) * 16, texels));
			for (int y = 0; y < 4; ++y)
				for (int x = 0; x < 4; ++x)
					if (i * 4 + x < width && j * 4 + y < height)
					{
						auto const &t = texels[y * 4 + x];
						image(i * 4 + x, j * 4 + y) = rove::bgra<std::uint8_t>(t.b, t.g, t.r, t.a);
					}
		}
	return image;
}

}
