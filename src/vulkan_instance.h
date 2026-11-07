// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <vulkan/vulkan.h>

namespace edge
{

/// The process's Vulkan instance, which its renderers share (and enumerate_vulkan_physical_devices() with them): the first to ask makes
/// it, and the last holder lets it go (a renderer's VulkanDeviceContext holds it, so a texture that outlives its renderer keeps it).  A
/// driver may keep something of every instance made for as long as the process lives (NVIDIA keeps a GPU client, so a
/// long-running simulator may keep one renderer a process), so a process makes one where it made one a renderer.  The devices stay a renderer's own.
///
/// What an instance is made with is decided once, by the one that makes it: its extensions (create_vulkan_instance: edge's own, and the
/// application's, a window system's).  edge sets no layers (the loader's VK_INSTANCE_LAYERS is read when it is made).  A renderer that
/// asks for an extension the live instance was made without is refused, not given an instance without it: an instance cannot be given
/// extensions once made.  One that asks for fewer is given the instance as it is.
///
/// Made, taken and let go from any thread (a lock is held while it is made or looked up); a renderer uses it, as its own device, from
/// the thread that drives it.
class VulkanInstance
{
public:
	/// The live instance, or one made now with @p application_extensions when there is none.  std::logic_error naming an extension of
	/// @p application_extensions the live instance was made without (make first the renderer that needs it, or hold an instance made
	/// with it); std::runtime_error naming one the loader does not have, as when it is made.
	static std::shared_ptr<VulkanInstance> acquire(std::vector<std::string> const &application_extensions);

	/// Instances made in this process so far, and whether a renderer holds one: what a test counts.
	static uint64_t made_count();
	static bool alive();
	/// Destroys the kept instance if nothing holds it: the next renderer makes another.  The instance otherwise stays for the process.
	static void release_unused();

	~VulkanInstance();
	VulkanInstance(VulkanInstance const &) = delete;
	VulkanInstance &operator=(VulkanInstance const &) = delete;

	VkInstance get() const { return instance_; }
	/// The extensions it was made with, edge's and the application's.
	std::vector<std::string> const &extensions() const { return extensions_; }
	bool has_extension(std::string const &name) const;
	/// VK_KHR_surface is on it: a swapchain can be had.
	bool surface_extension() const { return surface_extension_; }

private:
	VulkanInstance() = default;
	VkInstance instance_ = VK_NULL_HANDLE;
	std::vector<std::string> extensions_;
	bool surface_extension_ = false;
};

using VulkanInstancePtr = std::shared_ptr<VulkanInstance>;

}
