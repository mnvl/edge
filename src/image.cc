// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include <algorithm>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "stb_image.h"
#include "stb_image_write.h"

#include "image.h"

namespace edge
{

namespace detail
{

void check_image_view(void const *pixels, int width, int height, std::ptrdiff_t pitch,
	size_t pixel_size, size_t pixel_alignment)
{
	std::string const what = std::to_string(width) + " x " + std::to_string(height) + " pixels of "
		+ std::to_string(pixel_size) + " bytes, rows " + std::to_string(pitch) + " bytes apart";
	if (width < 0 || height < 0)
	{
		throw std::invalid_argument("an image view of a negative size: " + what);
	}
	if (pitch < static_cast<std::ptrdiff_t>(static_cast<size_t>(width) * pixel_size))
	{
		throw std::invalid_argument("an image view whose rows overlap: " + what);
	}
	if (width > 0 && height > 0 && !pixels)
	{
		throw std::invalid_argument("an image view of no pixels: " + what);
	}
	if (reinterpret_cast<std::uintptr_t>(pixels) % pixel_alignment != 0
		|| static_cast<size_t>(pitch) % pixel_alignment != 0)
	{
		throw std::invalid_argument("an image view of misaligned pixels: " + what);
	}
}

size_t count_image_pixels(int width, int height)
{
	if (width <= 0 || height <= 0)
	{
		throw std::invalid_argument("an image of " + std::to_string(width) + " x " + std::to_string(height)
			+ " pixels");
	}
	return static_cast<size_t>(width) * static_cast<size_t>(height);
}

void throw_off_image(int x, int y, int width, int height)
{
	throw std::out_of_range("pixel (" + std::to_string(x) + ", " + std::to_string(y) + ") is off an image of "
		+ std::to_string(width) + " x " + std::to_string(height));
}

}

namespace
{

// stb_image decodes R, G, B, A; blue_first swaps the first and third of
// each pixel on the way.
template <class P>
Image<P> decode(std::filesystem::path const &path, bool blue_first)
{
	int const channels = static_cast<int>(sizeof(P));
	if (!std::filesystem::is_regular_file(path))
	{
		throw std::runtime_error("failed to load image: " + path.string() + " (no such file)");
	}

	int w = 0, h = 0, file_channels = 0;
	std::unique_ptr<stbi_uc, void (*)(void *)> const data(
		stbi_load(path.string().c_str(), &w, &h, &file_channels, channels), stbi_image_free);
	if (!data)
	{
		throw std::runtime_error("failed to load image: " + path.string() + " (" + stbi_failure_reason() + ")");
	}

	Image<P> image(w, h);
	std::memcpy(static_cast<void *>(image.pixels()), data.get(), image.count() * sizeof(P));
	if (blue_first)
	{
		unsigned char *bytes = reinterpret_cast<unsigned char *>(image.pixels());
		for (size_t i = 0; i < image.count(); ++i)
		{
			std::swap(bytes[i * channels], bytes[i * channels + 2]);
		}
	}
	return image;
}

}

template <> Image<uint8_t> load_image<uint8_t>(std::filesystem::path const &path)
{
	return decode<uint8_t>(path, false);
}

template <> Image<rove::rgb<uint8_t>> load_image<rove::rgb<uint8_t>>(std::filesystem::path const &path)
{
	return decode<rove::rgb<uint8_t>>(path, false);
}

template <> Image<rove::bgr<uint8_t>> load_image<rove::bgr<uint8_t>>(std::filesystem::path const &path)
{
	return decode<rove::bgr<uint8_t>>(path, true);
}

template <> Image<rove::rgba<uint8_t>> load_image<rove::rgba<uint8_t>>(std::filesystem::path const &path)
{
	return decode<rove::rgba<uint8_t>>(path, false);
}

template <> Image<rove::bgra<uint8_t>> load_image<rove::bgra<uint8_t>>(std::filesystem::path const &path)
{
	return decode<rove::bgra<uint8_t>>(path, true);
}

namespace
{

/// The rows of @p view packed, each pixel's channels reversed where @p swap (B, G, R to R, G, B), to stb_image_write's PNG.
template <class P> void write_png(std::filesystem::path const &path, ImageView<P const> const &view, int channels, bool swap)
{
	if (path.extension() != ".png")
		throw std::invalid_argument("an image is saved as a PNG: " + path.string() + " is not named .png");
	if (view.width() <= 0 || view.height() <= 0)
		throw std::invalid_argument("an image of no pixels saved as " + path.string());
	std::vector<unsigned char> packed(size_t(view.width()) * size_t(view.height()) * size_t(channels));
	for (int y = 0; y < view.height(); ++y)
	{
		auto const *row = reinterpret_cast<unsigned char const *>(&view(0, y));
		unsigned char *out = packed.data() + size_t(y) * size_t(view.width()) * size_t(channels);
		std::copy(row, row + size_t(view.width()) * size_t(channels), out);
		if (swap)
			for (int x = 0; x < view.width(); ++x)
				std::swap(out[size_t(x) * size_t(channels)], out[size_t(x) * size_t(channels) + 2]);
	}
	if (!stbi_write_png(path.string().c_str(), view.width(), view.height(), channels, packed.data(), view.width() * channels))
		throw std::runtime_error("an image could not be written to " + path.string());
}

}

template <> void save_image<uint8_t>(std::filesystem::path const &path, ImageView<uint8_t const> const &view)
{
	write_png(path, view, 1, false);
}

template <> void save_image<rove::rgb<uint8_t>>(std::filesystem::path const &path, ImageView<rove::rgb<uint8_t> const> const &view)
{
	write_png(path, view, 3, false);
}

template <> void save_image<rove::bgr<uint8_t>>(std::filesystem::path const &path, ImageView<rove::bgr<uint8_t> const> const &view)
{
	write_png(path, view, 3, true);
}

template <> void save_image<rove::rgba<uint8_t>>(std::filesystem::path const &path, ImageView<rove::rgba<uint8_t> const> const &view)
{
	write_png(path, view, 4, false);
}

template <> void save_image<rove::bgra<uint8_t>>(std::filesystem::path const &path, ImageView<rove::bgra<uint8_t> const> const &view)
{
	write_png(path, view, 4, true);
}

float srgb_decode(float encoded)
{
	return encoded <= 0.04045f ? encoded / 12.92f : std::pow((encoded + 0.055f) / 1.055f, 2.4f);
}

float srgb_encode(float linear)
{
	return linear <= 0.0031308f ? 12.92f * linear : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
}

}
