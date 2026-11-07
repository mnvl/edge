// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include "vulkan_sdl_surface.h"

#include <memory>
#include <stdexcept>
#include <vector>

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include "vulkan_device.h"
#include "vulkan_instance.h"
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

std::string sdl_vulkan_device_for_window(SDL_Window *window)
{
	if (!window)
		throw std::invalid_argument("sdl_vulkan_device_for_window: no window");
	std::shared_ptr<VulkanInstance> const instance = VulkanInstance::acquire(sdl_vulkan_instance_extensions());
	VkSurfaceKHR surface = VK_NULL_HANDLE;
	if (!SDL_Vulkan_CreateSurface(window, instance->get(), nullptr, &surface))
		throw std::runtime_error(std::string("SDL_Vulkan_CreateSurface: ") + SDL_GetError());
	std::string found;
	uint32_t device_count = 0;
	vkEnumeratePhysicalDevices(instance->get(), &device_count, nullptr);
	std::vector<VkPhysicalDevice> devices(device_count);
	vkEnumeratePhysicalDevices(instance->get(), &device_count, devices.data());
	for (VkPhysicalDevice const device : devices)
	{
		uint32_t family_count = 0;
		vkGetPhysicalDeviceQueueFamilyProperties(device, &family_count, nullptr);
		std::vector<VkQueueFamilyProperties> families(family_count);
		vkGetPhysicalDeviceQueueFamilyProperties(device, &family_count, families.data());
		for (uint32_t family = 0; family < family_count && found.empty(); ++family)
		{
			VkBool32 can_present = VK_FALSE;
			if ((families[family].queueFlags & VK_QUEUE_GRAPHICS_BIT) &&
				vkGetPhysicalDeviceSurfaceSupportKHR(device, family, surface, &can_present) == VK_SUCCESS && can_present)
				found = vulkan_device_uuid(instance->get(), device);
		}
		if (!found.empty())
			break;
	}
	SDL_Vulkan_DestroySurface(instance->get(), surface, nullptr);
	if (found.empty())
		throw std::runtime_error("no Vulkan device can present to this window");
	return found;
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
