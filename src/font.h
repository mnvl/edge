// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

#include <filesystem>
#include <memory>
#include <string>

#include <rove/vec.h>

#include "forward.h"
#include "image.h"

namespace edge
{

/// A line of text as an 8-bit coverage mask, with the ink's bounds in it (the mask is the whole
/// line: from the pen's start, ascent() rows over the baseline and descent() under it).
/// The bounds are zero if the line has no ink (spaces).
struct TextMask
{
	Image<uint8_t> coverage;
	int left = 0, top = 0, right = 0, bottom = 0;     // right and bottom exclusive
};

/// A TrueType or OpenType font at one size.  It draws nothing itself: rasterise() gives the pixels
/// of a line, and the free functions below make textures and draw calls of them.
class Font
{
public:
	/// A font of `pixels` from the top of an em to its bottom (the size CSS and FreeType call
	/// pixel size).  std::runtime_error if the file is missing or not a font that can be read.
	Font(std::filesystem::path const &path, float pixels);
	~Font();
	Font(Font &&) noexcept;
	Font &operator=(Font &&) noexcept;

	/// Rows from the baseline up to the top of a line, and down to its bottom (both
	/// rounded outwards, so a glyph is never cut).
	int ascent() const;
	int descent() const;

	/// The pen's way along the line, in pixels, kerning included.  Text is UTF-8 (std::invalid_argument for what is not); a character
	/// the font has no glyph for is its missing glyph, as the font draws it.
	float advance(std::string const &line) const;

	/// The line as the font draws it (UTF-8, as advance()), the glyphs placed at
	/// fractions of a pixel; where glyphs overlap, the more covered wins.  The mask is
	/// ceil(advance) + 2 wide and ascent() + descent() + 1 high, whatever the ink.
	TextMask rasterise(std::string const &line) const;

private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};

/// Deprecated: a texture and a draw call for each line of text is no way to draw text (no atlas, no layout); rasterise() into an
/// atlas of the UI's own, or Dear ImGui (edge_imgui), draws it.  No game calls these.
///
/// The line as a texture of the colours (r, g, b, a) blended by its coverage over the background's,
/// as big as its mask; a texel of the background for a line of nothing.
[[deprecated("a texture per line of text: rasterise() into an atlas, or Dear ImGui (edge_imgui)")]]
TexturePtr make_text_texture(RendererPtr const &renderer, Font const &font, std::string const &line,
	rove::vec<4> const &text_color, rove::vec<4> const &background_color);

// 1) Points must be passed in clockwise order:
//   p1 --- p2
//   |       |
//   p4 --- p3
//
// 2) User should set camera for draw call and commit() it.
[[deprecated("a texture and a draw call per line of text: rasterise() into an atlas, or Dear ImGui (edge_imgui)")]]
DrawCallPtr make_text_draw_call(RendererPtr const &renderer, Font const &font, rove::vec<3> const &p1,
	rove::vec<3> const &p2, rove::vec<3> const &p3, rove::vec<3> const &p4, std::string const &line,
	rove::vec<4> const &text_color, rove::vec<4> const &background_color);

}
