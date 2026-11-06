// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

// Pixels on the CPU, rows from the top: an Image owns them, an ImageView
// looks at someone's (an Image's, a texture's CPU copy, a window's surface).
//
// A pixel is whatever is trivially copied: a scalar (a byte of coverage, a
// float of depth), a rove::vec of components that are no colour, or a
// colour named in the order of its memory (rove::bgra<uint8_t>, which is
// what edge's 8-bit textures keep; rove::rgba<float>).  The order is the
// pixel's type, so an image in one order is not taken for one in another.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <type_traits>
#include <utility>
#include <vector>
#include <rove/vec.h>

namespace edge
{

namespace detail
{

/// std::invalid_argument unless pixels of pixel_size and pixel_alignment,
/// width by height of them in rows pitch bytes apart, can be at pixels.
void check_image_view(void const *pixels, int width, int height, std::ptrdiff_t pitch,
	size_t pixel_size, size_t pixel_alignment);
/// width * height; std::invalid_argument unless both are positive.
size_t count_image_pixels(int width, int height);
[[noreturn]] void throw_off_image(int x, int y, int width, int height);

}

/// Someone else's pixels, not kept alive by the view.  P is const if they
/// are only to be read.
template <class P>
class ImageView {
public:
	using pixel_type = P;

	/// pitch: the bytes from a row to the next, no fewer than its pixels
	/// take.  std::invalid_argument for a negative size, rows that overlap,
	/// no pixels where there should be some, or pixels and a pitch that put
	/// a P where it cannot be.  A view of no pixels may be of nullptr.
	ImageView(P *pixels, int width, int height, std::ptrdiff_t pitch):
		pixels_(pixels),
		width_(width),
		height_(height),
		pitch_(pitch)
	{
		detail::check_image_view(pixels, width, height, pitch, sizeof(P), alignof(P));
	}

	/// Pixels that may be written may be read.
	template <class Q> requires (!std::is_const_v<Q>) && std::is_same_v<Q const, P>
	ImageView(ImageView<Q> const &view):
		pixels_(view.pixels()),
		width_(view.width()),
		height_(view.height()),
		pitch_(view.pitch())
	{
	}

	int width() const { return width_; }
	int height() const { return height_; }
	std::ptrdiff_t pitch() const { return pitch_; }
	/// The top left pixel; the row under it is pitch() bytes on.
	P *pixels() const { return pixels_; }

	/// The first pixel of row y, unchecked.
	P *row(int y) const
	{
		using byte = std::conditional_t<std::is_const_v<P>, unsigned char const, unsigned char>;
		return reinterpret_cast<P *>(reinterpret_cast<byte *>(pixels_) + y * pitch_);
	}

	/// Unchecked.
	P &operator ()(int x, int y) const { return row(y)[x]; }

	/// std::out_of_range off the image.
	P &at(int x, int y) const
	{
		if (x < 0 || x >= width_ || y < 0 || y >= height_)
		{
			detail::throw_off_image(x, y, width_, height_);
		}
		return row(y)[x];
	}

private:
	P *pixels_;
	int width_, height_;
	std::ptrdiff_t pitch_;
};

template <class P>
class Image {
	static_assert(std::is_trivially_copyable_v<P> && !std::is_const_v<P>,
		"an image's pixels are copied as bytes");

public:
	using pixel_type = P;

	/// No pixels, 0 by 0: what a moved-from image is, too.
	Image():
		width_(0),
		height_(0)
	{
	}

	/// std::invalid_argument unless width and height are positive.
	Image(int width, int height, P const &fill = P()):
		width_(width),
		height_(height),
		pixels_(detail::count_image_pixels(width, height), fill)
	{
	}

	/// A copy of what the view looks at, without its rows' padding.
	explicit Image(ImageView<P const> const &view):
		width_(view.width()),
		height_(view.height()),
		pixels_(static_cast<size_t>(view.width()) * static_cast<size_t>(view.height()))
	{
		for (int y = 0; y < height_; ++y)
		{
			std::memcpy(static_cast<void *>(&pixels_[static_cast<size_t>(y) * width_]), view.row(y),
				static_cast<size_t>(width_) * sizeof(P));
		}
	}

	Image(Image const &) = default;
	Image &operator =(Image const &) = default;

	Image(Image &&image) noexcept:
		width_(std::exchange(image.width_, 0)),
		height_(std::exchange(image.height_, 0)),
		pixels_(std::move(image.pixels_))
	{
		image.pixels_.clear();
	}

	Image &operator =(Image &&image) noexcept
	{
		if (this != &image)
		{
			width_ = std::exchange(image.width_, 0);
			height_ = std::exchange(image.height_, 0);
			pixels_ = std::move(image.pixels_);
			image.pixels_.clear();
		}
		return *this;
	}

	int width() const { return width_; }
	int height() const { return height_; }
	/// width() * height().
	size_t count() const { return pixels_.size(); }
	/// Row after row from the top, nothing between them.
	P *pixels() { return pixels_.data(); }
	P const *pixels() const { return pixels_.data(); }

	/// Unchecked.
	P &operator ()(int x, int y) { return pixels_[static_cast<size_t>(y) * width_ + x]; }
	P const &operator ()(int x, int y) const { return pixels_[static_cast<size_t>(y) * width_ + x]; }

	/// std::out_of_range off the image.
	P &at(int x, int y)
	{
		return const_cast<P &>(std::as_const(*this).at(x, y));
	}

	P const &at(int x, int y) const
	{
		if (x < 0 || x >= width_ || y < 0 || y >= height_)
		{
			detail::throw_off_image(x, y, width_, height_);
		}
		return (*this)(x, y);
	}

	/// Views of the image, good while it is neither moved nor destroyed.
	operator ImageView<P>()
	{
		return ImageView<P>(pixels_.data(), width_, height_, static_cast<std::ptrdiff_t>(width_) * sizeof(P));
	}

	operator ImageView<P const>() const
	{
		return ImageView<P const>(pixels_.data(), width_, height_, static_cast<std::ptrdiff_t>(width_) * sizeof(P));
	}

private:
	int width_, height_;
	std::vector<P> pixels_;
};

/// A file stb_image reads (PNG, JPEG, BMP, TGA, PNM, ...) as pixels of P,
/// which says both what is kept and in which order: uint8_t (grey),
/// rove::rgb<uint8_t>, rove::bgr<uint8_t>, rove::rgba<uint8_t> or
/// rove::bgra<uint8_t>.  A file with fewer channels is widened (grey into
/// each colour, alpha opaque) and one with more narrowed (alpha dropped,
/// colour to its luminance), as stb_image does.  std::runtime_error naming
/// the file if it is missing or not an image.
template <class P> Image<P> load_image(std::filesystem::path const &path) = delete;
template <> Image<uint8_t> load_image<uint8_t>(std::filesystem::path const &path);
template <> Image<rove::rgb<uint8_t>> load_image<rove::rgb<uint8_t>>(std::filesystem::path const &path);
template <> Image<rove::bgr<uint8_t>> load_image<rove::bgr<uint8_t>>(std::filesystem::path const &path);
template <> Image<rove::rgba<uint8_t>> load_image<rove::rgba<uint8_t>>(std::filesystem::path const &path);
template <> Image<rove::bgra<uint8_t>> load_image<rove::bgra<uint8_t>>(std::filesystem::path const &path);

/// The pixels of @p view as a PNG at @p path, of the pixel types load_image reads (the colour channels in the file's order,
/// whichever order the view's memory has them in).  std::invalid_argument for a path that does not end in .png or a view of no
/// pixels, std::runtime_error naming the file if it cannot be written.
template <class P> void save_image(std::filesystem::path const &path, ImageView<P const> const &view) = delete;
template <> void save_image<uint8_t>(std::filesystem::path const &path, ImageView<uint8_t const> const &view);
template <> void save_image<rove::rgb<uint8_t>>(std::filesystem::path const &path, ImageView<rove::rgb<uint8_t> const> const &view);
template <> void save_image<rove::bgr<uint8_t>>(std::filesystem::path const &path, ImageView<rove::bgr<uint8_t> const> const &view);
template <> void save_image<rove::rgba<uint8_t>>(std::filesystem::path const &path, ImageView<rove::rgba<uint8_t> const> const &view);
template <> void save_image<rove::bgra<uint8_t>>(std::filesystem::path const &path, ImageView<rove::bgra<uint8_t> const> const &view);

/// sRGB's transfer, IEC 61966-2-1: the linear light of an encoded value (0 to 1), and the encoding of linear light (edge_brdf.glsl's
/// edge_srgb_decode, edge_srgb_encode).
float srgb_decode(float encoded);
float srgb_encode(float linear);

}
