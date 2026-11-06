// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include <algorithm>
#include <cmath>
#include <fstream>
#include <span>
#include <initializer_list>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

#include "texture.h"
#include "material.h"
#include "uber_draw_call.h"
#include "renderer.h"
#include "stb_truetype.h"
#include "font.h"

namespace edge
{

namespace
{

/// The code points of UTF-8 text.  What is not UTF-8 is std::invalid_argument: a byte that starts nothing, a character cut off, one
/// written longer than it needs, a surrogate half, a code point past U+10FFFF.  (Each byte was a Latin-1 character.)
std::vector<int> code_points(std::string const &text)
{
	std::vector<int> out;
	out.reserve(text.size());
	auto const refuse = [&](size_t at, char const *why)
	{
		throw std::invalid_argument("text that is not UTF-8 (" + std::string(why) + " at byte " + std::to_string(at) + ")");
	};
	for (size_t i = 0; i < text.size();)
	{
		unsigned char const lead = static_cast<unsigned char>(text[i]);
		int length, cp;
		if (lead < 0x80) { length = 1; cp = lead; }
		else if ((lead & 0xe0) == 0xc0) { length = 2; cp = lead & 0x1f; }
		else if ((lead & 0xf0) == 0xe0) { length = 3; cp = lead & 0x0f; }
		else if ((lead & 0xf8) == 0xf0) { length = 4; cp = lead & 0x07; }
		else refuse(i, "a byte that starts no character");
		if (i + length > text.size())
			refuse(i, "a character cut off");
		for (int k = 1; k < length; ++k)
		{
			unsigned char const next = static_cast<unsigned char>(text[i + k]);
			if ((next & 0xc0) != 0x80)
				refuse(i + k, "a character's byte that does not continue it");
			cp = (cp << 6) | (next & 0x3f);
		}
		static int const least[] = {0, 0, 0x80, 0x800, 0x10000};
		if (cp < least[length])
			refuse(i, "a character written longer than it needs");
		if (cp >= 0xd800 && cp <= 0xdfff)
			refuse(i, "a surrogate half");
		if (cp > 0x10ffff)
			refuse(i, "a code point past U+10FFFF");
		out.push_back(cp);
		i += length;
	}
	return out;
}

}

struct Font::Impl
{
	std::vector<unsigned char> data;
	stbtt_fontinfo info;
	float scale;
	int ascent, descent;
};

Font::Font(std::filesystem::path const &path, float pixels): impl_(std::make_unique<Impl>())
{
	std::ifstream file(path, std::ios::binary);
	if (!file)
		throw std::runtime_error(path.string() + ": no such font");
	impl_->data.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
	int const offset = stbtt_GetFontOffsetForIndex(impl_->data.data(), 0);
	if (offset < 0 || !stbtt_InitFont(&impl_->info, impl_->data.data(), offset))
		throw std::runtime_error(path.string() + ": not a font stb_truetype reads");
	impl_->scale = stbtt_ScaleForMappingEmToPixels(&impl_->info, pixels);
	int ascent = 0, descent = 0, gap = 0;
	stbtt_GetFontVMetrics(&impl_->info, &ascent, &descent, &gap);
	impl_->ascent = int(std::ceil(ascent * impl_->scale));             // as FreeType rounds its metrics: out
	impl_->descent = int(std::ceil(-descent * impl_->scale));
}

Font::~Font() = default;
Font::Font(Font &&) noexcept = default;
Font &Font::operator=(Font &&) noexcept = default;

int Font::ascent() const
{
	return impl_->ascent;
}

int Font::descent() const
{
	return impl_->descent;
}

float Font::advance(std::string const &line) const
{
	std::vector<int> const text = code_points(line);
	float x = 0;
	for (size_t i = 0; i < text.size(); ++i)
	{
		int const c = text[i];
		int advance = 0, lsb = 0;
		stbtt_GetCodepointHMetrics(&impl_->info, c, &advance, &lsb);
		x += advance * impl_->scale;
		if (i + 1 < text.size())
			x += impl_->scale * stbtt_GetCodepointKernAdvance(&impl_->info, c, text[i + 1]);
	}
	return x;
}

TextMask Font::rasterise(std::string const &line) const
{
	stbtt_fontinfo const &info = impl_->info;
	float const scale = impl_->scale;
	std::vector<int> const text = code_points(line);
	TextMask mask{Image<uint8_t>(int(std::ceil(advance(line))) + 2, impl_->ascent + impl_->descent + 1)};
	int const width = mask.coverage.width(), height = mask.coverage.height();
	std::vector<unsigned char> glyph;
	float pen = 0;
	for (size_t i = 0; i < text.size(); ++i)
	{
		int const c = text[i];
		float const shift = pen - std::floor(pen);
		int x0, y0, x1, y1;
		stbtt_GetCodepointBitmapBoxSubpixel(&info, c, scale, scale, shift, 0, &x0, &y0, &x1, &y1);
		int const w = x1 - x0, h = y1 - y0;
		if (w > 0 && h > 0)
		{
			glyph.assign(size_t(w) * h, 0);
			stbtt_MakeCodepointBitmapSubpixel(&info, glyph.data(), w, h, w, scale, scale, shift, 0, c);
			int const left = int(std::floor(pen)) + x0, top = impl_->ascent + y0;
			for (int gy = 0; gy < h; ++gy)
				for (int gx = 0; gx < w; ++gx)
				{
					int const mx = left + gx, my = top + gy;
					if (mx < 0 || my < 0 || mx >= width || my >= height)
						continue;
					uint8_t &m = mask.coverage(mx, my);
					m = std::max(m, glyph[size_t(gy) * w + gx]);            // glyphs that touch: the more covered
				}
		}
		int advance = 0, lsb = 0;
		stbtt_GetCodepointHMetrics(&info, c, &advance, &lsb);
		pen += advance * scale;
		if (i + 1 < text.size())
			pen += scale * stbtt_GetCodepointKernAdvance(&info, c, text[i + 1]);
	}
	mask.left = width;
	mask.top = height;
	for (int my = 0; my < height; ++my)
		for (int mx = 0; mx < width; ++mx)
			if (mask.coverage(mx, my))
			{
				mask.left = std::min(mask.left, mx);
				mask.top = std::min(mask.top, my);
				mask.right = std::max(mask.right, mx + 1);
				mask.bottom = std::max(mask.bottom, my + 1);
			}
	if (mask.right == 0)                                           // no ink: spaces
		mask.left = mask.top = 0;
	return mask;
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"          // the deprecated helpers' own definitions, one calling the other
TexturePtr make_text_texture(RendererPtr const &renderer, Font const &font, std::string const &line,
	rove::vec<4> const &text_color, rove::vec<4> const &background_color)
{
	// the colours are (r, g, b, a); the texture's bytes B, G, R, A
	auto const byte = [](float v) { return static_cast<unsigned char>(v * 255); };
	unsigned char const bg[4] = {byte(background_color.z), byte(background_color.y), byte(background_color.x),
		byte(background_color.w)};
	unsigned char const fg[4] = {byte(text_color.z), byte(text_color.y), byte(text_color.x), byte(text_color.w)};

	if (line.empty())
	{
		TexturePtr texture = renderer->create_texture(1, 1, DEFAULT_PIXEL_FORMAT);
		auto texels = texture->write(Contents::DISCARD);
		std::copy(bg, bg + 4, texels.as<unsigned char>().begin());
		texels.unmap();
		return texture;
	}

	TextMask const mask = font.rasterise(line);
	int const width = mask.coverage.width(), height = mask.coverage.height();
	TexturePtr texture = renderer->create_texture(width, height, DEFAULT_PIXEL_FORMAT);
	auto texels = texture->write(Contents::DISCARD);
	unsigned char *dst = texels.as<unsigned char>().data();
	for (int y = 0; y < height; ++y)
		for (int x = 0; x < width; ++x)
		{
			unsigned char const a = mask.coverage(x, y);
			unsigned char *out = dst + 4 * (size_t(y) * width + x);
			for (int k = 0; k < 4; ++k)
				out[k] = static_cast<unsigned char>((fg[k] * a + bg[k] * (255 - a)) / 255);
		}
	texels.unmap();
	return texture;
}

DrawCallPtr make_text_draw_call(RendererPtr const &renderer, Font const &font, rove::vec<3> const &p1,
	rove::vec<3> const &p2, rove::vec<3> const &p3, rove::vec<3> const &p4, std::string const &line,
	rove::vec<4> const &text_color, rove::vec<4> const &background_color)
{
	TexturePtr texture = make_text_texture(renderer, font, line, text_color, background_color);

	MaterialPtr material = renderer->create_material();
	material->set_diffuse_texture(texture);

	std::shared_ptr<VertexArrayPT> vertex_array = renderer->create_vertex_array<VERTEX_PT>(4);
	{
		auto vertices = vertex_array->write(Contents::DISCARD);
		std::span<VERTEX_PT> const v = vertices.as<VERTEX_PT>();
		v[0].position = p1;
		v[0].texcoords.set(0, 0);
		v[1].position = p2;
		v[1].texcoords.set(1, 0);
		v[2].position = p3;
		v[2].texcoords.set(1, 1);
		v[3].position = p4;
		v[3].texcoords.set(0, 1);
		vertices.unmap();
	}

	std::shared_ptr<IndexArray16> index_array = renderer->create_index_array<INDEX_U16>(6);
	{
		auto indices = index_array->write(Contents::DISCARD);
		std::ranges::copy(std::initializer_list<INDEX_U16>{0, 1, 2, 0, 2, 3}, indices.as<INDEX_U16>().begin());
		indices.unmap();
	}

	UberDrawCallPtr draw_call = renderer->create_uber_draw_call();
	draw_call->set_always_visible(true);
	draw_call->set_vertex_buffer(vertex_array->vertex_buffer());
	draw_call->set_index_buffer(index_array->index_buffer());
	draw_call->set_material(material);
	draw_call->set_triangles_count(2);
	return draw_call;
}
#pragma GCC diagnostic pop

}
