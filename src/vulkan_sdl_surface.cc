// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include "vulkan_sdl_surface.h"

#include <stdexcept>

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include "vulkan_renderer.h"

namespace edge
{

std::vector<std::string> sdl_vulkan_instance_extensions()
{
	if (!SDL_WasInit(SDL_INIT_VIDEO))
		throw std::logic_error("sdl_vulkan_instance_extensions: SDL's video is not started");
	Uint32 count = 0;
	char const *const *names = SDL_Vulkan_GetInstanceExtensions(&count);
	if (!names)
		throw std::runtime_error(std::string("SDL_Vulkan_GetInstanceExtensions: ") + SDL_GetError());
	return std::vector<std::string>(names, names + count);
}

void attach_sdl_window(VulkanRenderer &renderer, SDL_Window *window, bool vsync)
{
	if (!window)
		throw std::invalid_argument("attach_sdl_window: no window");
	VkSurfaceKHR surface = VK_NULL_HANDLE;
	if (!SDL_Vulkan_CreateSurface(window, renderer.instance(), nullptr, &surface))
		throw std::runtime_error(std::string("SDL_Vulkan_CreateSurface: ") + SDL_GetError());
	try
	{
		renderer.attach_surface(surface, vsync);
	}
	catch (...)
	{
		SDL_Vulkan_DestroySurface(renderer.instance(), surface, nullptr);     // not taken over
		throw;
	}
}

}
