// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

#include <vulkan/vulkan.h>
#include <string>
#include <vector>

#include "texture.h"

namespace edge
{

/// What a swapchain asks of a surface: the transform it is made with and the size of its images.
struct SwapchainGeometry
{
	VkSurfaceTransformFlagBitsKHR transform;
	VkExtent2D extent;
};

/// Shows rendered frames in a window: owns the window's surface and the swapchain made from it, and copies a frame
/// into the swapchain's next image.  The renderer is its facade and refuses calls inside a frame.  Made after the
/// device and destroyed before it (and before the instance the surface came from).
class VulkanPresenter {
public:
	/// queue is the graphics queue, which presents too.  swapchain_extension_enabled: the device was made with
	/// VK_KHR_swapchain.
	VulkanPresenter(VkInstance instance, VkPhysicalDevice physical_device, VkDevice device, VkQueue queue,
		uint32_t queue_family, VkCommandPool command_pool, bool swapchain_extension_enabled);
	~VulkanPresenter();
	VulkanPresenter(VulkanPresenter const &) = delete;
	VulkanPresenter &operator=(VulkanPresenter const &) = delete;

	bool has_surface() const { return surface_ != VK_NULL_HANDLE; }

	/// Takes over the surface (destroyed with the presenter or on detach; when this throws the caller still owns it).  A surface
	/// lost already is taken and lost (surface_lost()).  std::invalid_argument for a null one; std::logic_error with a surface
	/// attached already; std::runtime_error when the device cannot present, or its queue cannot present to this surface.
	void attach(VkSurfaceKHR surface, bool vsync);
	/// Swapchain first, then the surface.  std::logic_error without a surface.
	void detach();
	/// Changes whether the presentation waits for the display's refresh; the swapchain is made again with the next present().
	/// std::logic_error without a surface.
	void set_vsync(bool vsync);
	bool vsync() const { return vsync_; }

	/// Copies frame, scaled to width x height (the window's size in pixels), into the window, and returns without
	/// waiting for the copy: the next present() waits for it (the GPU copies in the order it was given work, so the frame
	/// can be rendered into again at once).  The frame keeps the layout it had.  False, showing nothing, for a window
	/// without area, and when the surface is lost (surface_lost(): the window was taken back, as a phone's is when its
	/// app goes to the background; the swapchain is let go and the surface is to be detached, the window's next attached).
	/// std::logic_error without a surface, on a lost one, or for an image in a layout it cannot be copied from;
	/// std::invalid_argument for a texture of another renderer or a negative size.
	bool present(TexturePtr const &frame, int width, int height);
	/// Whether the surface attached was lost presenting (until it is detached).
	bool surface_lost() const { return lost_; }
	/// Waits for the last copy present() made, if any is still running.
	void wait();

private:
	struct SwapchainSupport
	{
		VkSurfaceCapabilitiesKHR capabilities;
		std::vector<VkSurfaceFormatKHR> formats;
		std::vector<VkPresentModeKHR> present_modes;
	};

	SwapchainSupport query_swapchain_support() const;
	void make_swapchain(int width, int height, std::string const &why);
	void destroy_swapchain();
	/// The surface lost (`what` answered so): the copy waited for, the swapchain let go.
	void lose(char const *what);

	VkInstance instance_;
	VkPhysicalDevice physical_device_;
	VkDevice device_;
	VkQueue queue_;
	uint32_t queue_family_;
	VkCommandPool command_pool_;
	bool swapchain_extension_enabled_;

	VkSurfaceKHR surface_ = VK_NULL_HANDLE;
	// A swapchain of the surface's window, made at the first present and again when the window's size changed or the
	// surface says it is out of date.
	VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
	VkExtent2D extent_ = {0, 0};            // of the swapchain's images
	SwapchainGeometry made_for_ = {VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR, {0, 0}};      // what the swapchain was made with
	int width_ = 0, height_ = 0;            // what the application asked for: the window's size in pixels
	std::vector<VkImage> images_;
	std::vector<VkSemaphore> blitted_;      // one per image: signalled by the copy, waited for by the presentation
	VkSemaphore acquired_ = VK_NULL_HANDLE;
	VkFence done_ = VK_NULL_HANDLE;
	VkCommandBuffer copying_ = VK_NULL_HANDLE;     // the last copy, done_ not waited for yet
	bool vsync_ = true;
	bool suboptimal_matters() const;        // VK_SUBOPTIMAL_KHR was answered: whether the surface asks for another geometry than the swapchain has
	uint64_t presents_ = 0;                 // frames presented so far
	uint64_t swapchains_made_ = 0;
	uint64_t last_made_at_ = 0;             // the present (counted from 1) that made the last swapchain
	int remake_run_ = 0;                    // swapchains made on presents that followed each other
	bool suboptimal_logged_ = false;
	std::string stale_why_;                 // what made stale_ true, for the log
	bool stale_ = false;                    // presented, but the surface wants a new swapchain before the next one
	bool lost_ = false;                     // the surface was lost (VK_ERROR_SURFACE_LOST_KHR): nothing is presented until it is detached
};

/// What acquiring an image of a swapchain or presenting one answered: done, done but no longer as the surface would have it, the swapchain
/// out of date (to be made again), or the surface lost (its window taken back).
enum class PresentAnswer
{
	Done,
	Suboptimal,
	OutOfDate,
	Lost,
};

/// `result` of `what` (vkAcquireNextImageKHR, vkQueuePresentKHR) as a PresentAnswer; vulkan_error (std::runtime_error, naming `what`) for any
/// other result: the device's.
PresentAnswer present_answer(VkResult result, char const *what);

/// The surface's own size (currentExtent) and transform, as capabilities report them, to the geometry of the swapchain.  A phone's panel is
/// portrait however it is held: the surface says so with a 90 or 270 degree currentTransform and its extent in the panel's orientation.  The
/// swapchain asks for no transform instead (the compositor turns the picture) with the extent turned back, so that what the application draws
/// is upright; a surface that cannot do that is taken as it is.  A surface with no size of its own (currentExtent 0xFFFFFFFF) is left
/// to the caller.
SwapchainGeometry choose_swapchain_geometry(VkSurfaceCapabilitiesKHR const &capabilities);

/// Whether a swapchain made for `made` is what the surface asks for now: the geometry it would choose from `now` is the same, size and
/// transform.  A driver that answers every present with VK_SUBOPTIMAL_KHR (a turned panel, asked for no transform) is not a reason to make
/// the swapchain again while this holds.  A surface with no size of its own cannot say, and is taken as not fitting.
bool swapchain_fits(SwapchainGeometry const &made, VkSurfaceCapabilitiesKHR const &now);

}
