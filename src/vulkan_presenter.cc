// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include "vulkan_presenter.h"

#include <chrono>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>

#include "log.h"
#include "vulkan_commands.h"
#include "vulkan_error.h"
#include "vulkan_image_layout.h"
#include "vulkan_texture.h"

namespace edge
{

VulkanPresenter::VulkanPresenter(VkInstance instance, VkPhysicalDevice physical_device, VkDevice device, VkQueue queue,
	uint32_t queue_family, VkCommandPool command_pool, bool swapchain_extension_enabled):
	instance_(instance),
	physical_device_(physical_device),
	device_(device),
	queue_(queue),
	queue_family_(queue_family),
	command_pool_(command_pool),
	swapchain_extension_enabled_(swapchain_extension_enabled)
{
}

VulkanPresenter::~VulkanPresenter()
{
	destroy_swapchain();
	if (surface_ != VK_NULL_HANDLE)
	{
		vkDestroySurfaceKHR(instance_, surface_, nullptr);
	}
}


char const *transform_name(VkSurfaceTransformFlagBitsKHR t)
{
	switch (t)
	{
	case VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR: return "identity";
	case VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR: return "rotate 90";
	case VK_SURFACE_TRANSFORM_ROTATE_180_BIT_KHR: return "rotate 180";
	case VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR: return "rotate 270";
	case VK_SURFACE_TRANSFORM_HORIZONTAL_MIRROR_BIT_KHR: return "mirror";
	case VK_SURFACE_TRANSFORM_HORIZONTAL_MIRROR_ROTATE_90_BIT_KHR: return "mirror, rotate 90";
	case VK_SURFACE_TRANSFORM_HORIZONTAL_MIRROR_ROTATE_180_BIT_KHR: return "mirror, rotate 180";
	case VK_SURFACE_TRANSFORM_HORIZONTAL_MIRROR_ROTATE_270_BIT_KHR: return "mirror, rotate 270";
	default: return "inherit";
	}
}

char const *present_mode_name(VkPresentModeKHR mode)
{
	switch (mode)
	{
	case VK_PRESENT_MODE_IMMEDIATE_KHR: return "immediate";
	case VK_PRESENT_MODE_MAILBOX_KHR: return "mailbox";
	case VK_PRESENT_MODE_FIFO_KHR: return "fifo";
	case VK_PRESENT_MODE_FIFO_RELAXED_KHR: return "fifo relaxed";
	default: return "other";
	}
}

SwapchainGeometry choose_swapchain_geometry(VkSurfaceCapabilitiesKHR const &caps)
{
	SwapchainGeometry geometry = {caps.currentTransform, caps.currentExtent};
	bool const turned = caps.currentTransform == VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR
		|| caps.currentTransform == VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR
		|| caps.currentTransform == VK_SURFACE_TRANSFORM_HORIZONTAL_MIRROR_ROTATE_90_BIT_KHR
		|| caps.currentTransform == VK_SURFACE_TRANSFORM_HORIZONTAL_MIRROR_ROTATE_270_BIT_KHR;
	if (turned && caps.currentExtent.width != 0xFFFFFFFFu && (caps.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR))
	{
		geometry.transform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
		geometry.extent = {caps.currentExtent.height, caps.currentExtent.width};
	}
	return geometry;
}

bool swapchain_fits(SwapchainGeometry const &made, VkSurfaceCapabilitiesKHR const &now)
{
	SwapchainGeometry const wanted = choose_swapchain_geometry(now);
	return wanted.extent.width != 0xFFFFFFFFu && wanted.transform == made.transform && wanted.extent.width == made.extent.width
		&& wanted.extent.height == made.extent.height;
}

bool VulkanPresenter::suboptimal_matters() const
{
	VkSurfaceCapabilitiesKHR now;
	check_vk_result(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device_, surface_, &now), "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
	return !swapchain_fits(made_for_, now);
}

void VulkanPresenter::attach(VkSurfaceKHR surface, bool vsync)
{
	if (surface == VK_NULL_HANDLE)
	{
		throw std::invalid_argument("attach_surface: a null surface");
	}
	if (surface_ != VK_NULL_HANDLE)
	{
		throw std::logic_error("attach_surface: a surface is attached already");
	}
	if (!swapchain_extension_enabled_)
	{
		throw std::runtime_error("attach_surface: the device has no " VK_KHR_SWAPCHAIN_EXTENSION_NAME);
	}

	// a surface lost already (its window taken back between being made and attached) is taken, and lost: to be detached as any other
	VkBool32 supported = VK_FALSE;
	VkResult const asked = vkGetPhysicalDeviceSurfaceSupportKHR(physical_device_, queue_family_, surface, &supported);
	if (asked == VK_ERROR_SURFACE_LOST_KHR)
	{
		surface_ = surface;
		vsync_ = vsync;
		lose("vkGetPhysicalDeviceSurfaceSupportKHR");
		return;
	}
	check_vk_result(asked, "vkGetPhysicalDeviceSurfaceSupportKHR");
	if (!supported)
	{
		throw std::runtime_error("attach_surface: the graphics queue of the device cannot present to this surface");
	}

	surface_ = surface;
	vsync_ = vsync;
	SwapchainSupport support;
	try
	{
		support = query_swapchain_support();
	}
	catch (vulkan_error const &e)
	{
		if (e.result() != VK_ERROR_SURFACE_LOST_KHR)
		{
			surface_ = VK_NULL_HANDLE;          // (the caller still owns it)
			throw;
		}
		lose(e.operation().c_str());
		return;
	}
	std::string modes;
	for (VkPresentModeKHR mode : support.present_modes)
		modes += (modes.empty() ? "" : ", ") + std::string(present_mode_name(mode));
	EDGE_LOG_INFO("surface attached: {} formats, present modes {}, vsync {}, graphics queue family {} presents to it", support.formats.size(), modes,
		vsync, queue_family_);
}

void VulkanPresenter::set_vsync(bool vsync)
{
	if (surface_ == VK_NULL_HANDLE)
	{
		throw std::logic_error("set_vsync: no surface is attached");
	}
	if (vsync == vsync_)
	{
		return;
	}
	vsync_ = vsync;
	stale_ = true;
	stale_why_ = vsync ? "vsync was switched on" : "vsync was switched off";
}

void VulkanPresenter::detach()
{
	if (surface_ == VK_NULL_HANDLE)
	{
		throw std::logic_error("detach_surface: no surface is attached");
	}
	destroy_swapchain();
	lost_ = false;
	vkDestroySurfaceKHR(instance_, surface_, nullptr);
	surface_ = VK_NULL_HANDLE;
}

void VulkanPresenter::wait()
{
	if (copying_ == VK_NULL_HANDLE)
	{
		return;
	}
	check_vk_result(vkWaitForFences(device_, 1, &done_, VK_TRUE, UINT64_MAX), "vkWaitForFences(present)");
	check_vk_result(vkResetFences(device_, 1, &done_), "vkResetFences(present)");
	vkFreeCommandBuffers(device_, command_pool_, 1, &copying_);
	copying_ = VK_NULL_HANDLE;
}

void VulkanPresenter::destroy_swapchain()
{
	vkDeviceWaitIdle(device_);
	if (copying_ != VK_NULL_HANDLE)
	{
		vkFreeCommandBuffers(device_, command_pool_, 1, &copying_);
		copying_ = VK_NULL_HANDLE;
	}
	for (auto &semaphore : blitted_)
	{
		vkDestroySemaphore(device_, semaphore, nullptr);
	}
	blitted_.clear();
	if (acquired_ != VK_NULL_HANDLE)
	{
		vkDestroySemaphore(device_, acquired_, nullptr);
		acquired_ = VK_NULL_HANDLE;
	}
	if (done_ != VK_NULL_HANDLE)
	{
		vkDestroyFence(device_, done_, nullptr);
		done_ = VK_NULL_HANDLE;
	}
	if (swapchain_ != VK_NULL_HANDLE)
	{
		vkDestroySwapchainKHR(device_, swapchain_, nullptr);
		swapchain_ = VK_NULL_HANDLE;
	}
	images_.clear();
}

VulkanPresenter::SwapchainSupport VulkanPresenter::query_swapchain_support() const
{
	SwapchainSupport support;
	std::memset(&support.capabilities, 0, sizeof(support.capabilities));
	if (surface_ == VK_NULL_HANDLE)
	{
		return support;
	}

	check_vk_result(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device_, surface_, &support.capabilities),
		"vkGetPhysicalDeviceSurfaceCapabilitiesKHR");

	uint32_t format_count = 0;
	check_vk_result(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device_, surface_, &format_count, nullptr),
		"vkGetPhysicalDeviceSurfaceFormatsKHR(count)");
	if (format_count > 0)
	{
		support.formats.resize(format_count);
		check_vk_result(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device_, surface_, &format_count,
			&support.formats[0]),
			"vkGetPhysicalDeviceSurfaceFormatsKHR(list)");
	}

	uint32_t present_mode_count = 0;
	check_vk_result(vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device_, surface_, &present_mode_count, nullptr),
		"vkGetPhysicalDeviceSurfacePresentModesKHR(count)");
	if (present_mode_count > 0)
	{
		support.present_modes.resize(present_mode_count);
		check_vk_result(vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device_, surface_, &present_mode_count,
			&support.present_modes[0]),
			"vkGetPhysicalDeviceSurfacePresentModesKHR(list)");
	}

	return support;
}

void VulkanPresenter::make_swapchain(int width, int height, std::string const &why)
{
	auto const started = std::chrono::steady_clock::now();
	// Nothing of the old swapchain may still be in use, its presentation included.
	check_vk_result(vkDeviceWaitIdle(device_), "vkDeviceWaitIdle");

	SwapchainSupport const support = query_swapchain_support();
	VkSurfaceCapabilitiesKHR const &caps = support.capabilities;
	if (!(caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT))
	{
		throw std::runtime_error("the surface's images cannot be copied to");
	}

	// 8-bit colour, not sRGB: the frame is in display encoding already and is copied as it is.
	VkSurfaceFormatKHR format = {VK_FORMAT_UNDEFINED, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
	for (auto f : support.formats)
	{
			if (f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR
			&& (f.format == VK_FORMAT_B8G8R8A8_UNORM || (f.format == VK_FORMAT_R8G8B8A8_UNORM && format.format == VK_FORMAT_UNDEFINED)))
		{
			format = f;
			if (f.format == VK_FORMAT_B8G8R8A8_UNORM)
			{
				break;
			}
		}
	}
	if (format.format == VK_FORMAT_UNDEFINED)
	{
		throw std::runtime_error("the surface has no 8-bit UNORM format in the sRGB colour space");
	}
	VkFormatProperties format_properties;
	vkGetPhysicalDeviceFormatProperties(physical_device_, format.format, &format_properties);
	if (!(format_properties.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT))
	{
		throw std::runtime_error("the surface's format cannot be blitted to");
	}

	// FIFO is the one mode every surface has.  Without vsync the first of immediate and mailbox it offers.
	VkPresentModeKHR mode = VK_PRESENT_MODE_FIFO_KHR;
	if (!vsync_)
	{
		for (auto present_mode : support.present_modes)
		{
			if (present_mode == VK_PRESENT_MODE_IMMEDIATE_KHR)
			{
				mode = VK_PRESENT_MODE_IMMEDIATE_KHR;
				break;
			}
			if (present_mode == VK_PRESENT_MODE_MAILBOX_KHR)
			{
				mode = VK_PRESENT_MODE_MAILBOX_KHR;
			}
		}
	}

	// A surface that says its size (currentExtent) is the size of the window; one that does not (Wayland) takes what
	// the application asked for, within what it can do.
	SwapchainGeometry const geometry = choose_swapchain_geometry(caps);
	VkExtent2D extent = geometry.extent;
	if (extent.width == 0xFFFFFFFFu)
	{
		extent.width = static_cast<uint32_t>(width);
		extent.height = static_cast<uint32_t>(height);
		if (extent.width < caps.minImageExtent.width || extent.height < caps.minImageExtent.height
			|| extent.width > caps.maxImageExtent.width || extent.height > caps.maxImageExtent.height)
		{
			throw std::runtime_error("a window of " + std::to_string(width) + "x" + std::to_string(height)
				+ " pixels is outside what the surface can show");
		}
	}

	uint32_t images = caps.minImageCount + 1;
	if (caps.maxImageCount > 0 && images > caps.maxImageCount)
	{
		images = caps.maxImageCount;
	}

	VkCompositeAlphaFlagBitsKHR alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	if (!(caps.supportedCompositeAlpha & alpha))
	{
		alpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
		if (!(caps.supportedCompositeAlpha & alpha))
		{
			throw std::runtime_error("the surface composites neither as opaque nor as inherited");
		}
	}

	VkSwapchainCreateInfoKHR info;
	std::memset(&info, 0, sizeof(info));
	info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
	info.surface = surface_;
	info.minImageCount = images;
	info.imageFormat = format.format;
	info.imageColorSpace = format.colorSpace;
	info.imageExtent = extent;
	info.imageArrayLayers = 1;
	info.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
	info.preTransform = geometry.transform;
	info.compositeAlpha = alpha;
	info.presentMode = mode;
	info.clipped = VK_TRUE;
	info.oldSwapchain = swapchain_;

	VkSwapchainKHR swapchain = VK_NULL_HANDLE;
	check_vk_result(vkCreateSwapchainKHR(device_, &info, nullptr, &swapchain), "vkCreateSwapchainKHR");

	// The old one is retired by the new one's creation, and nothing is using it any more.
	for (auto &semaphore : blitted_)
	{
		vkDestroySemaphore(device_, semaphore, nullptr);
	}
	blitted_.clear();
	if (swapchain_ != VK_NULL_HANDLE)
	{
		vkDestroySwapchainKHR(device_, swapchain_, nullptr);
	}
	swapchain_ = swapchain;
	extent_ = extent;
	made_for_ = {geometry.transform, extent};
	++swapchains_made_;
	// a swapchain made on each of several presents in a row waits for the device to idle every frame: it was a quarter of a phone's frame
	remake_run_ = last_made_at_ != 0 && presents_ - last_made_at_ <= 1 ? remake_run_ + 1 : 0;
	last_made_at_ = presents_;
	EDGE_LOG_INFO("swapchain {} made ({}): {}x{}, {} images, format {} (VkFormat), {} present, pre-transform {}; the surface is {}x{}, {}, "
		"supports 0x{:x}, {}..{} images; took {:.1f} ms", swapchains_made_, why, extent.width, extent.height, images, static_cast<int>(format.format),
		present_mode_name(mode), transform_name(geometry.transform), caps.currentExtent.width, caps.currentExtent.height,
		transform_name(caps.currentTransform), caps.supportedTransforms, caps.minImageCount, caps.maxImageCount,
		std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count());
	if (remake_run_ == 3)
	{
		EDGE_LOG_WARN("the swapchain was made on {} presents in a row ({}): each waits for the device to idle, and the frame is slow for it",
			remake_run_ + 1, why);
	}
	width_ = width;
	height_ = height;
	stale_ = false;

	uint32_t count = 0;
	check_vk_result(vkGetSwapchainImagesKHR(device_, swapchain, &count, nullptr), "vkGetSwapchainImagesKHR(count)");
	images_.resize(count);
	check_vk_result(vkGetSwapchainImagesKHR(device_, swapchain, &count, &images_[0]), "vkGetSwapchainImagesKHR(list)");

	VkSemaphoreCreateInfo semaphore_info;
	std::memset(&semaphore_info, 0, sizeof(semaphore_info));
	semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
	for (uint32_t i = 0; i < count; ++i)
	{
		VkSemaphore semaphore = VK_NULL_HANDLE;
		check_vk_result(vkCreateSemaphore(device_, &semaphore_info, nullptr, &semaphore), "vkCreateSemaphore");
		blitted_.push_back(semaphore);
	}
	if (acquired_ == VK_NULL_HANDLE)
	{
		check_vk_result(vkCreateSemaphore(device_, &semaphore_info, nullptr, &acquired_), "vkCreateSemaphore");
		VkFenceCreateInfo fence_info;
		std::memset(&fence_info, 0, sizeof(fence_info));
		fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
		check_vk_result(vkCreateFence(device_, &fence_info, nullptr, &done_), "vkCreateFence");
	}
}

bool VulkanPresenter::present(TexturePtr const &frame, int width, int height)
{
	if (surface_ == VK_NULL_HANDLE)
	{
		throw std::logic_error("present: no surface was attached");
	}
	if (lost_)
	{
		throw std::logic_error("present: the surface was lost: detach it and attach the window's next");
	}
	VulkanTexture *const source = dynamic_cast<VulkanTexture *>(frame.get());
	if (!source || source->device() != device_)
	{
		throw std::invalid_argument("present: not a texture of this renderer");
	}
	if (width < 0 || height < 0)
	{
		throw std::invalid_argument("present: a window of " + std::to_string(width) + "x" + std::to_string(height) + " pixels");
	}
	VkImageLayout const old_layout = source->current_layout();
	// the layouts a render target is left in; checked before an image is acquired, which a throw after it would leave held
	if (old_layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL && old_layout != VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
		&& old_layout != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
	{
		throw std::logic_error("present: the frame's image is in layout " + std::to_string(static_cast<int>(old_layout))
			+ ", which it cannot be copied from (never rendered into?)");
	}
	VkFormatProperties format_properties;
	vkGetPhysicalDeviceFormatProperties(physical_device_, source->vk_format(), &format_properties);
	if (!(format_properties.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT))
	{
		throw std::runtime_error("present: the frame's format cannot be blitted from");
	}
	if (width == 0 || height == 0)
	{
		return false;
	}
	// the last copy waited on the semaphore an image is acquired with now
	wait();

	// (making a swapchain asks the surface, which may answer that it is lost)
	auto made = [&](std::string const &why)
	{
		try
		{
			make_swapchain(width, height, why);
			return true;
		}
		catch (vulkan_error const &e)
		{
			if (e.result() != VK_ERROR_SURFACE_LOST_KHR)
				throw;
			lose(e.operation().c_str());
			return false;
		}
	};
	++presents_;
	if (swapchain_ == VK_NULL_HANDLE || stale_ || width_ != width || height_ != height)
	{
		std::string why = stale_why_;
		if (swapchain_ == VK_NULL_HANDLE)
			why = "the first";
		else if (width_ != width || height_ != height)
			why = "the window is " + std::to_string(width) + "x" + std::to_string(height) + ", was " + std::to_string(width_) + "x" + std::to_string(height_);
		if (!made(why))
			return false;
	}

	uint32_t index = 0;
	PresentAnswer answer = present_answer(vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX, acquired_, VK_NULL_HANDLE, &index),
		"vkAcquireNextImageKHR");
	if (answer == PresentAnswer::OutOfDate)
	{
		if (!made("acquiring an image answered VK_ERROR_OUT_OF_DATE_KHR"))
			return false;
		answer = present_answer(vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX, acquired_, VK_NULL_HANDLE, &index),
			"vkAcquireNextImageKHR");
		if (answer == PresentAnswer::OutOfDate)          // (out of date again, on a swapchain just made)
			check_vk_result(VK_ERROR_OUT_OF_DATE_KHR, "vkAcquireNextImageKHR");
	}
	if (answer == PresentAnswer::Lost)
	{
		lose("vkAcquireNextImageKHR");
		return false;
	}
	bool stale = answer == PresentAnswer::Suboptimal && suboptimal_matters();
	if (stale)
		stale_why_ = "acquiring an image answered VK_SUBOPTIMAL_KHR and the surface asks for another geometry";

	VkImage const target = images_[index];
	VkCommandBuffer const command_buffer = begin_one_time_commands(device_, command_pool_);

	// the image arrives after the semaphore, so the barrier waits at the stage the semaphore does
	ImageUse const blitted_into = {VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT};
	record_image_barrier(command_buffer, target, {VK_IMAGE_LAYOUT_UNDEFINED, 0, VK_PIPELINE_STAGE_TRANSFER_BIT}, blitted_into);

	if (old_layout != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
	{
		record_image_layout_transition(command_buffer, source->image(), old_layout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
	}

	VkImageBlit region;
	std::memset(&region, 0, sizeof(region));
	region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	region.srcSubresource.layerCount = 1;
	region.srcOffsets[1] = {source->width(), source->height(), 1};
	region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	region.dstSubresource.layerCount = 1;
	region.dstOffsets[1] = {static_cast<int32_t>(extent_.width), static_cast<int32_t>(extent_.height), 1};
	bool const same_size = source->width() == static_cast<int>(extent_.width)
		&& source->height() == static_cast<int>(extent_.height);
	vkCmdBlitImage(command_buffer, source->image(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, target,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region, same_size ? VK_FILTER_NEAREST : VK_FILTER_LINEAR);

	record_image_barrier(command_buffer, target, blitted_into, {VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, 0, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT});

	if (old_layout != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
	{
		record_image_layout_transition(command_buffer, source->image(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, old_layout);
	}
	check_vk_result(vkEndCommandBuffer(command_buffer), "vkEndCommandBuffer");

	VkPipelineStageFlags const wait_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
	VkSubmitInfo submit;
	std::memset(&submit, 0, sizeof(submit));
	submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit.waitSemaphoreCount = 1;
	submit.pWaitSemaphores = &acquired_;
	submit.pWaitDstStageMask = &wait_stage;
	submit.commandBufferCount = 1;
	submit.pCommandBuffers = &command_buffer;
	submit.signalSemaphoreCount = 1;
	submit.pSignalSemaphores = &blitted_[index];
	check_vk_result(vkQueueSubmit(queue_, 1, &submit, done_), "vkQueueSubmit(present)");
	copying_ = command_buffer;          // waited for by the next present

	VkPresentInfoKHR present_info;
	std::memset(&present_info, 0, sizeof(present_info));
	present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	present_info.waitSemaphoreCount = 1;
	present_info.pWaitSemaphores = &blitted_[index];
	present_info.swapchainCount = 1;
	present_info.pSwapchains = &swapchain_;
	present_info.pImageIndices = &index;
	answer = present_answer(vkQueuePresentKHR(queue_, &present_info), "vkQueuePresentKHR");
	if (answer == PresentAnswer::Lost)
	{
		lose("vkQueuePresentKHR");
		return false;
	}
	if (answer == PresentAnswer::OutOfDate)
	{
		stale = true;       // shown or not, the next frame gets a new swapchain
		stale_why_ = "presenting answered VK_ERROR_OUT_OF_DATE_KHR";
	}
	else if (answer == PresentAnswer::Suboptimal)
	{
		// shown; a new swapchain only when the surface asks for another geometry
		if (!stale && suboptimal_matters())
		{
			stale = true;
			stale_why_ = "presenting answered VK_SUBOPTIMAL_KHR and the surface asks for another geometry";
		}
		else if (!stale && !suboptimal_logged_)
		{
			suboptimal_logged_ = true;
			EDGE_LOG_INFO("presenting answers VK_SUBOPTIMAL_KHR, and the swapchain is what the surface asks for: kept (said once; a turned panel "
				"made for no transform does this on every present)");
		}
	}
	stale_ = stale;
	return true;
}

void VulkanPresenter::lose(char const *what)
{
	EDGE_LOG_WARN("the surface was lost ({} answered VK_ERROR_SURFACE_LOST_KHR): its swapchain let go, nothing presented until it is detached",
		what);
	wait();
	destroy_swapchain();
	lost_ = true;
}

PresentAnswer present_answer(VkResult result, char const *what)
{
	switch (result)
	{
	case VK_SUCCESS: return PresentAnswer::Done;
	case VK_SUBOPTIMAL_KHR: return PresentAnswer::Suboptimal;
	case VK_ERROR_OUT_OF_DATE_KHR: return PresentAnswer::OutOfDate;
	case VK_ERROR_SURFACE_LOST_KHR: return PresentAnswer::Lost;
	default: throw vulkan_error(what, result);
	}
}

}
