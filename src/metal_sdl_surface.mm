// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include "metal_sdl_surface.h"

#include <stdexcept>
#include <string>

#include <SDL3/SDL.h>
#include <SDL3/SDL_metal.h>

namespace edge
{

MetalSDLSurface::MetalSDLSurface(SDL_Window *window)
{
	if (!window)
		throw std::invalid_argument("a Metal surface needs an SDL window");
	view_ = SDL_Metal_CreateView(window);
	if (!view_)
		throw std::runtime_error(std::string("SDL gives the window no Metal view: ") + SDL_GetError());
	layer_ = SDL_Metal_GetLayer(view_);
	if (!layer_)
	{
		SDL_Metal_DestroyView(view_);
		throw std::runtime_error(std::string("SDL gives the window's Metal view no layer: ") + SDL_GetError());
	}
}

MetalSDLSurface::~MetalSDLSurface()
{
	SDL_Metal_DestroyView(view_);
}

MetalContextPtr MetalSDLSurface::make_context() const
{
	return std::make_shared<MetalContext>(layer_);
}

}
