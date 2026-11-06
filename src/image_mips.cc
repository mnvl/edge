// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include "image_mips.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

namespace edge
{

namespace
{

using bgra8 = rove::bgra<std::uint8_t>;

using Quad = Texel;

std::array<float, 256> const &srgb_decode_table()
{
	static std::array<float, 256> const table = [] {
		std::array<float, 256> t{};
		for (int i = 0; i < 256; ++i)
		{
			t[size_t(i)] = srgb_decode(float(i) / 255.0f);
		}
		return t;
	}();
	return table;
}

Quad decode(bgra8 const &p, MipKind kind)
{
	Quad q;
	float const rgb[3] = {float(p.r), float(p.g), float(p.b)};
	for (int i = 0; i < 3; ++i)
	{
		switch (kind)
		{
		case MipKind::Linear: q.c[i] = rgb[i] / 255.0f; break;
		case MipKind::Srgb: q.c[i] = srgb_decode_table()[size_t(rgb[i])]; break;
		case MipKind::Normal: q.c[i] = rgb[i] * 2.0f / 255.0f - 1.0f; break;
		}
	}
	q.c[3] = float(p.a) / 255.0f;
	return q;
}

std::uint8_t quantise(float unit)
{
	// a value in 0..1 up to the rounding of the sums that made it, which rounds to 0..255 all the same
	return static_cast<std::uint8_t>(std::lround(unit * 255.0f));
}

bgra8 encode(Quad const &q, MipKind kind)
{
	float rgb[3] = {q.c[0], q.c[1], q.c[2]};
	switch (kind)
	{
	case MipKind::Linear:
		break;
	case MipKind::Srgb:
		for (float &v : rgb)
			v = srgb_encode(v);
		break;
	case MipKind::Normal:
	{
		float const length = std::sqrt(rgb[0] * rgb[0] + rgb[1] * rgb[1] + rgb[2] * rgb[2]);
		if (length < 1e-6f)
		{
			// directions that cancel have no mean: the surface, flat in the mean, faces up
			rgb[0] = rgb[1] = 0.0f;
			rgb[2] = 1.0f;
		}
		else
		{
			for (float &v : rgb)
				v /= length;
		}
		for (float &v : rgb)
			v = v * 0.5f + 0.5f;
		break;
	}
	}
	return bgra8(quantise(rgb[2]), quantise(rgb[1]), quantise(rgb[0]), quantise(q.c[3]));
}

/// The sums a result texel is made of: the texels' numbers (Quad), and their red, green and blue weighed by their alpha (Srgb's coverage).
struct Sums
{
	float plain[4] = {0, 0, 0, 0};
	float covered[3] = {0, 0, 0};

	void add(Quad const &q, float weight)
	{
		for (int c = 0; c < 4; ++c)
			plain[c] += q.c[c] * weight;
		for (int c = 0; c < 3; ++c)
			covered[c] += q.c[c] * q.c[3] * weight;
	}

	void add(Sums const &s, float weight)
	{
		for (int c = 0; c < 4; ++c)
			plain[c] += s.plain[c] * weight;
		for (int c = 0; c < 3; ++c)
			covered[c] += s.covered[c] * weight;
	}

	/// The mean: Srgb's light weighed by the coverage, so that a texel nobody sees (a cut-out's clear surround, an atlas's empty space) lends
	/// the covered ones none of its colour; where nothing is covered the plain mean (the colour a cut-out is bled with), and every other
	/// kind's numbers as they are.
	Quad mean(MipKind kind) const
	{
		Quad q;
		for (int c = 0; c < 4; ++c)
			q.c[c] = plain[c];
		if (kind == MipKind::Srgb && plain[3] > 0)
			for (int c = 0; c < 3; ++c)
				q.c[c] = covered[c] / plain[3];
		return q;
	}
};

/// One result texel's share of the source texels along an axis.
struct Tap
{
	int index;
	float weight;
};

/// For each of @p dest_size texels the source texels (of @p source_size) it is made of and their weights, which add up to 1.
std::vector<std::vector<Tap>> axis_taps(int source_size, int dest_size)
{
	std::vector<std::vector<Tap>> taps;
	taps.resize(size_t(dest_size));
	for (int i = 0; i < dest_size; ++i)
	{
		if (dest_size <= source_size)
		{
			// the area the texel covers: source texel k and destination texel i cut at multiples of source_size * dest_size, so that the
			// parts are whole numbers
			for (int k = 0; k < source_size; ++k)
			{
				long const lo = std::max(long(k) * dest_size, long(i) * source_size);
				long const hi = std::min(long(k + 1) * dest_size, long(i + 1) * source_size);
				if (hi > lo)
					taps[size_t(i)].push_back({k, float(hi - lo) / float(source_size)});
			}
		}
		else
		{
			// between the centres of the two nearest source texels, the edge's texel repeated beyond the first and last
			float const centre = (float(i) + 0.5f) * float(source_size) / float(dest_size) - 0.5f;
			int const first = int(std::floor(centre));
			float const along = centre - float(first);
			int const a = std::clamp(first, 0, source_size - 1), b = std::clamp(first + 1, 0, source_size - 1);
			taps[size_t(i)].push_back({a, 1.0f - along});
			taps[size_t(i)].push_back({b, along});
		}
	}
	return taps;
}

}

Texel decode_texel(bgra8 texel, MipKind kind)
{
	return decode(texel, kind);
}

bgra8 encode_texel(Texel const &texel, MipKind kind)
{
	return encode(texel, kind);
}

Image<bgra8> resize(ImageView<bgra8 const> image, int dw, int dh, MipKind kind)
{
	int const sw = image.width(), sh = image.height();
	if (sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0)
	{
		throw std::invalid_argument("resizing an image of " + std::to_string(sw) + " x " + std::to_string(sh) + " to " + std::to_string(dw) +
			" x " + std::to_string(dh));
	}
	std::vector<std::vector<Tap>> const across = axis_taps(sw, dw), down = axis_taps(sh, dh);

	// across: each row of the source into dw texels
	std::vector<Sums> rows(size_t(dw) * size_t(sh));
	for (int y = 0; y < sh; ++y)
	{
		for (int i = 0; i < dw; ++i)
		{
			Sums sum;
			for (Tap const &tap : across[size_t(i)])
				sum.add(decode(image(tap.index, y), kind), tap.weight);
			rows[size_t(y) * size_t(dw) + size_t(i)] = sum;
		}
	}

	// down: each column of those into dh texels
	Image<bgra8> result(dw, dh);
	for (int j = 0; j < dh; ++j)
	{
		for (int i = 0; i < dw; ++i)
		{
			Sums sum;
			for (Tap const &tap : down[size_t(j)])
				sum.add(rows[size_t(tap.index) * size_t(dw) + size_t(i)], tap.weight);
			result(i, j) = encode(sum.mean(kind), kind);
		}
	}
	return result;
}

Image<bgra8> next_mip(ImageView<bgra8 const> level, MipKind kind)
{
	int const sw = level.width(), sh = level.height();
	if (sw <= 0 || sh <= 0)
	{
		throw std::invalid_argument("the mip under an image of " + std::to_string(sw) + " x " + std::to_string(sh));
	}
	if (sw == 1 && sh == 1)
	{
		throw std::invalid_argument("the mip under a level of one texel");
	}
	return resize(level, std::max(1, sw / 2), std::max(1, sh / 2), kind);
}

MipChain::MipChain(Image<bgra8> image, MipKind kind):
	kind_(kind)
{
	levels_.push_back(std::move(image));
	while (levels_.back().width() > 1 || levels_.back().height() > 1)
	{
		levels_.push_back(next_mip(levels_.back(), kind));
	}
}

namespace
{

/// The texel of the level at (u, v), the four around it blended in the quantity of the kind.
Texel bilinear(Image<bgra8> const &level, float u, float v, MipKind kind)
{
	int const w = level.width(), h = level.height();
	float const x = u * float(w) - 0.5f, y = v * float(h) - 0.5f;
	float const fx0 = std::floor(x), fy0 = std::floor(y);
	float const ax = x - fx0, ay = y - fy0;
	auto wrap = [](float f, int n) {
		int const i = int(std::fmod(f, float(n)));
		return i < 0 ? i + n : i;
	};
	int const x0 = wrap(fx0, w), x1 = wrap(fx0 + 1.0f, w), y0 = wrap(fy0, h), y1 = wrap(fy0 + 1.0f, h);
	Texel const t00 = decode(level(x0, y0), kind), t10 = decode(level(x1, y0), kind);
	Texel const t01 = decode(level(x0, y1), kind), t11 = decode(level(x1, y1), kind);
	Texel result;
	for (int c = 0; c < 4; ++c)
	{
		float const top = t00.c[c] + (t10.c[c] - t00.c[c]) * ax, bottom = t01.c[c] + (t11.c[c] - t01.c[c]) * ax;
		result.c[c] = top + (bottom - top) * ay;
	}
	return result;
}

}

bgra8 MipChain::sample(float u, float v, float lod) const
{
	// a level of detail that is not a number or beyond the chain is what the sampler of a GPU makes of it: the nearest level there is
	float const top = float(levels_.size() - 1);
	float const at = lod >= 0 ? (lod < top ? lod : top) : 0.0f;
	int const l0 = int(std::floor(at));
	int const l1 = std::min(l0 + 1, int(levels_.size()) - 1);
	float const f = at - float(l0);
	Texel const a = bilinear(levels_[size_t(l0)], u, v, kind_);
	if (f == 0.0f || l1 == l0)
		return encode(a, kind_);
	Texel const b = bilinear(levels_[size_t(l1)], u, v, kind_);
	Texel mixed;
	for (int c = 0; c < 4; ++c)
		mixed.c[c] = a.c[c] + (b.c[c] - a.c[c]) * f;
	return encode(mixed, kind_);
}

}
