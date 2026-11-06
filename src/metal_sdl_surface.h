// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

#include "metal_context.h"

struct SDL_Window;

namespace edge
{

/// An SDL window's Metal layer, for a MetalRenderer to show its frames in (edge_sdl, with SDL3; the part of the Metal backend that knows
/// SDL, as gles_sdl_context.h is of the GLES one).  The window was made with SDL_WINDOW_METAL.  It keeps the view SDL makes for the
/// layer for as long as it lives: drop it after the renderer, before the window.
class MetalSDLSurface
{
public:
	/// std::invalid_argument for no window; std::runtime_error with SDL's message when the window has no Metal layer (the video
	/// driver has none: SDL's offscreen one).
	explicit MetalSDLSurface(SDL_Window *window);
	~MetalSDLSurface();
	MetalSDLSurface(MetalSDLSurface const &) = delete;
	MetalSDLSurface &operator=(MetalSDLSurface const &) = delete;

	/// The window's CAMetalLayer, as a void *.
	void *layer() const { return layer_; }
	/// A context on the system's GPU that shows frames in this window.
	MetalContextPtr make_context() const;

private:
	void *view_ = nullptr;
	void *layer_ = nullptr;
};

}
