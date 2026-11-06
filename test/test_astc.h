// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

// ASTC blocks decoded by astc-encoder's decoder, for the tests of what the encoder and the model converter make.

#include <boost/test/unit_test.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

#include <astcenc.h>
#include <rove/vec.h>

#include "image.h"

namespace edge
{

/// The @p width x @p height image the ASTC blocks of @p block x @p block texels at @p blocks (@p size bytes) make, decoded as sRGB
/// where @p srgb.
inline Image<rove::bgra<std::uint8_t>> decode_astc(std::uint8_t const *blocks, std::size_t size, int width, int height, int block, bool srgb)
{
	int const bx = (width + block - 1) / block, by = (height + block - 1) / block;
	BOOST_REQUIRE_EQUAL(size, std::size_t(bx) * by * 16);
	astcenc_config config;
	BOOST_REQUIRE_EQUAL(astcenc_config_init(srgb ? ASTCENC_PRF_LDR_SRGB : ASTCENC_PRF_LDR, unsigned(block), unsigned(block), 1, ASTCENC_PRE_FASTEST,
		ASTCENC_FLG_DECOMPRESS_ONLY, &config), ASTCENC_SUCCESS);
	astcenc_context *context = nullptr;
	BOOST_REQUIRE_EQUAL(astcenc_context_alloc(&config, 1, &context), ASTCENC_SUCCESS);
	std::vector<std::uint8_t> rgba(std::size_t(width) * height * 4);
	void *slices[1] = {rgba.data()};
	astcenc_image image{unsigned(width), unsigned(height), 1, ASTCENC_TYPE_U8, slices};
	astcenc_swizzle const same{ASTCENC_SWZ_R, ASTCENC_SWZ_G, ASTCENC_SWZ_B, ASTCENC_SWZ_A};
	astcenc_error const error = astcenc_decompress_image(context, blocks, size, &image, &same, 0);
	astcenc_context_free(context);
	BOOST_REQUIRE_EQUAL(error, ASTCENC_SUCCESS);
	Image<rove::bgra<std::uint8_t>> out(width, height);
	for (int y = 0; y < height; ++y)
		for (int x = 0; x < width; ++x)
		{
			std::uint8_t const *p = &rgba[(std::size_t(y) * width + x) * 4];
			out(x, y) = rove::bgra<std::uint8_t>(p[2], p[1], p[0], p[3]);
		}
	return out;
}

}
