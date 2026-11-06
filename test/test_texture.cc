// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include <boost/test/unit_test.hpp>

#include "texture.h"
#include "pixel.h"
#include "null_renderer.h"

BOOST_AUTO_TEST_SUITE(texture)

BOOST_AUTO_TEST_CASE(a_texture_has_the_size_and_format_it_was_made_with)
{
	edge::RendererPtr renderer(new edge::NullRenderer(320, 200));
	edge::TexturePtr tex = renderer->create_texture(64, 32, edge::PixelFormat::X8R8G8B8);
	BOOST_CHECK_EQUAL(tex->width(), 64);
	BOOST_CHECK_EQUAL(tex->height(), 32);
	BOOST_CHECK_EQUAL(tex->pixel_format(), edge::PixelFormat::X8R8G8B8);
	tex = renderer->create_texture(128, 128, edge::PixelFormat::A8R8G8B8);
	BOOST_CHECK_EQUAL(tex->width(), 128);
	BOOST_CHECK_EQUAL(tex->pixel_format(), edge::PixelFormat::A8R8G8B8);
}

BOOST_AUTO_TEST_CASE(renderer_create_texture)
{
	edge::RendererPtr renderer(new edge::NullRenderer(320, 200));

	edge::TexturePtr tex = renderer->create_texture(64, 64,
		edge::PixelFormat::X8R8G8B8);
	BOOST_REQUIRE(tex);
	BOOST_CHECK_EQUAL(tex->width(), 64);
	BOOST_CHECK_EQUAL(tex->height(), 64);
}

BOOST_AUTO_TEST_CASE(pixel_format_constants)
{
	BOOST_CHECK_EQUAL(static_cast<unsigned>(edge::PixelFormat::X8R8G8B8), 1u);
	BOOST_CHECK_EQUAL(static_cast<unsigned>(edge::PixelFormat::A8R8G8B8), 2u);
	BOOST_CHECK_EQUAL(static_cast<unsigned>(edge::PixelFormat::R16G16B16A16F), 3u);
	BOOST_CHECK_EQUAL(static_cast<unsigned>(edge::PixelFormat::R32G32B32A32F), 4u);
	BOOST_CHECK_EQUAL(static_cast<unsigned>(edge::PixelFormat::BC1), 5u);
	BOOST_CHECK_EQUAL(static_cast<unsigned>(edge::PixelFormat::BC3), 6u);
}

BOOST_AUTO_TEST_CASE(bc_data_size)
{
	// BC1: 8 bytes per 4x4 block (4 bpp)
	BOOST_CHECK_EQUAL(edge::pixel_data_size(16, 16, edge::PixelFormat::BC1),
		4u * 4u * 8u);  // 4x4 blocks, 8 bytes each = 128

	// BC3: 16 bytes per 4x4 block (8 bpp)
	BOOST_CHECK_EQUAL(edge::pixel_data_size(16, 16, edge::PixelFormat::BC3),
		4u * 4u * 16u);  // 4x4 blocks, 16 bytes each = 256

	// Uncompressed: 32 bpp
	BOOST_CHECK_EQUAL(edge::pixel_data_size(16, 16, edge::PixelFormat::X8R8G8B8),
		16u * 16u * 4u);  // 1024

	// BC1 1024x1024
	BOOST_CHECK_EQUAL(edge::pixel_data_size(1024, 1024, edge::PixelFormat::BC1),
		256u * 256u * 8u);  // 524288 = 512 KB
}

BOOST_AUTO_TEST_CASE(is_block_compressed)
{
	BOOST_CHECK(!edge::is_block_compressed(edge::PixelFormat::X8R8G8B8));
	BOOST_CHECK(!edge::is_block_compressed(edge::PixelFormat::A8R8G8B8));
	BOOST_CHECK(edge::is_block_compressed(edge::PixelFormat::BC1));
	BOOST_CHECK(edge::is_block_compressed(edge::PixelFormat::BC3));
}

BOOST_AUTO_TEST_SUITE_END()
