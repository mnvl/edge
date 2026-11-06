// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

#include <string>
#include <vector>

#include "forward.h"

struct SDL_Window;

namespace edge
{

/// Showing a VulkanRenderer's frames in an SDL window (edge_sdl, with SDL3).

/// The instance extensions a VulkanRenderer is made with to take an SDL window's surface (SDL_Vulkan_GetInstanceExtensions).
/// std::logic_error before SDL's video is started; std::runtime_error with SDL's message when it has no Vulkan.
std::vector<std::string> sdl_vulkan_instance_extensions();

/// Makes @p window's Vulkan surface (the window made with SDL_WINDOW_VULKAN) and attaches it to @p renderer, made with
/// sdl_vulkan_instance_extensions() (VulkanRenderer::attach_surface; detach_surface() destroys it).  std::invalid_argument for no
/// window; std::runtime_error with SDL's message when the surface cannot be made; what attach_surface throws, the surface destroyed.
void attach_sdl_window(VulkanRenderer &renderer, SDL_Window *window, bool vsync);

}
