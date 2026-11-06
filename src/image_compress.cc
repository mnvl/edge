// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include "image_compress.h"

#include <algorithm>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>

#include <astcenc.h>
#include <bc7enc.h>

namespace edge
{

namespace
{

using bgra8 = rove::bgra<std::uint8_t>;

void encode_block_rows(ImageView<bgra8 const> image, bc7enc_compress_block_params const &params, int first_row, int end_row,
	int blocks_x, std::uint8_t *out)
{
	for (int by = first_row; by < end_row; ++by)
	{
		for (int bx = 0; bx < blocks_x; ++bx)
		{
			color_rgba texels[16];
			for (int y = 0; y < 4; ++y)
			{
				for (int x = 0; x < 4; ++x)
				{
					// past the edge: the last texel again
					bgra8 const &p = image(std::min(bx * 4 + x, image.width() - 1), std::min(by * 4 + y, image.height() - 1));
					texels[y * 4 + x] = {{p.r, p.g, p.b, p.a}};
				}
			}
			bc7enc_compress_block(out + (size_t(by) * size_t(blocks_x) + size_t(bx)) * BC7ENC_BLOCK_SIZE, texels, &params);
		}
	}
}

}

std::vector<std::uint8_t> compress_bc7(ImageView<bgra8 const> image, BlockContent content)
{
	if (image.width() <= 0 || image.height() <= 0)
	{
		throw std::invalid_argument("BC7 blocks of an image of no pixels");
	}
	static std::once_flag tables;
	std::call_once(tables, bc7enc_compress_block_init);

	bc7enc_compress_block_params params;
	bc7enc_compress_block_params_init(&params);
	if (content == BlockContent::Colour)
		bc7enc_compress_block_params_init_perceptual_weights(&params);
	else
		bc7enc_compress_block_params_init_linear_weights(&params);

	int const blocks_x = (image.width() + 3) / 4, blocks_y = (image.height() + 3) / 4;
	std::vector<std::uint8_t> blocks(size_t(blocks_x) * size_t(blocks_y) * BC7ENC_BLOCK_SIZE);

	// rows of blocks to the cores; every block is encoded by itself, so the bytes do not depend on how many there are
	int const workers = std::max(1, std::min(blocks_y, int(std::thread::hardware_concurrency())));
	std::vector<std::thread> threads;
	for (int w = 1; w < workers; ++w)
	{
		threads.emplace_back(encode_block_rows, image, std::cref(params), blocks_y * w / workers, blocks_y * (w + 1) / workers, blocks_x, blocks.data());
	}
	encode_block_rows(image, params, 0, blocks_y / workers, blocks_x, blocks.data());
	for (std::thread &t : threads)
		t.join();
	return blocks;
}

std::vector<std::uint8_t> compress_astc(ImageView<bgra8 const> image, int block, bool srgb)
{
	if (block != 4 && block != 6 && block != 8)
		throw std::invalid_argument("ASTC blocks of " + std::to_string(block) + " texels a side: 4, 6 or 8");
	if (image.width() <= 0 || image.height() <= 0)
		throw std::invalid_argument("ASTC blocks of an image of no pixels");
	auto const check = [](astcenc_error error, char const *what) {
		if (error != ASTCENC_SUCCESS)
			throw std::runtime_error(std::string("astc-encoder: ") + what + ": " + astcenc_get_error_string(error));
	};

	// the rows packed, B, G, R, A as edge keeps them (the swizzle hands the encoder R, G, B, A)
	std::vector<std::uint8_t> texels(size_t(image.width()) * size_t(image.height()) * 4);
	for (int y = 0; y < image.height(); ++y)
		std::memcpy(texels.data() + size_t(y) * size_t(image.width()) * 4, &image(0, y), size_t(image.width()) * 4);
	void *slices[1] = {texels.data()};
	astcenc_image source{unsigned(image.width()), unsigned(image.height()), 1, ASTCENC_TYPE_U8, slices};
	astcenc_swizzle const swizzle{ASTCENC_SWZ_B, ASTCENC_SWZ_G, ASTCENC_SWZ_R, ASTCENC_SWZ_A};

	astcenc_config config;
	check(astcenc_config_init(srgb ? ASTCENC_PRF_LDR_SRGB : ASTCENC_PRF_LDR, unsigned(block), unsigned(block), 1, ASTCENC_PRE_MEDIUM, 0, &config),
		"config");
	int const blocks_x = (image.width() + block - 1) / block, blocks_y = (image.height() + block - 1) / block;
	unsigned const workers = std::max(1u, std::min(unsigned(blocks_x * blocks_y), std::thread::hardware_concurrency()));
	astcenc_context *raw = nullptr;
	check(astcenc_context_alloc(&config, workers, &raw), "context");
	std::unique_ptr<astcenc_context, void (*)(astcenc_context *)> const context(raw, astcenc_context_free);

	// the encoder hands the blocks out to the threads that call it; each block is encoded by itself, so the bytes do not depend on
	// how many there are
	std::vector<std::uint8_t> blocks(size_t(blocks_x) * size_t(blocks_y) * 16);
	std::vector<astcenc_error> errors(workers, ASTCENC_SUCCESS);
	auto const work = [&](unsigned index) {
		errors[index] = astcenc_compress_image(context.get(), &source, &swizzle, blocks.data(), blocks.size(), index);
	};
	std::vector<std::thread> threads;
	for (unsigned w = 1; w < workers; ++w)
		threads.emplace_back(work, w);
	work(0);
	for (std::thread &t : threads)
		t.join();
	for (astcenc_error error : errors)
		check(error, "compress");
	return blocks;
}

}
