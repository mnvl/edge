// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include "vulkan_frame_ring.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>

#include "vulkan_error.h"

namespace edge
{

VulkanFrameRing::VulkanFrameRing(VkDevice device, VkCommandPool pool, uint32_t frames, Retire retire):
	device_(device),
	pool_(pool),
	retire_(std::move(retire)),
	frames_(frames)
{
	if (frames == 0)
	{
		throw std::invalid_argument("a frame ring of no frames");
	}
	try
	{
		for (Frame &frame : frames_)
		{
			VkFenceCreateInfo info;
			std::memset(&info, 0, sizeof(info));
			info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
			check_vk_result(vkCreateFence(device_, &info, nullptr, &frame.drawn), "vkCreateFence(frame)");
		}
	}
	catch (...)
	{
		for (Frame &frame : frames_)
			if (frame.drawn != VK_NULL_HANDLE)
				vkDestroyFence(device_, frame.drawn, nullptr);
		throw;
	}
}

VulkanFrameRing::~VulkanFrameRing()
{
	for (Frame &frame : frames_)
	{
		if (frame.commands != VK_NULL_HANDLE)
		{
			vkFreeCommandBuffers(device_, pool_, 1, &frame.commands);
		}
		vkDestroyFence(device_, frame.drawn, nullptr);
	}
}

void VulkanFrameRing::begin(VkCommandBuffer commands)
{
	Frame &frame = frames_[current_];
	if (frame.serial != 0 || frame.commands != VK_NULL_HANDLE)
	{
		throw std::logic_error("frame ring: share " + std::to_string(current_) + " is written while its frame is in flight or open");
	}
	frame.commands = commands;
}

void VulkanFrameRing::submit(VkQueue queue, uint64_t serial)
{
	Frame &frame = frames_[current_];
	check_vk_result(vkEndCommandBuffer(frame.commands), "vkEndCommandBuffer");
	VkSubmitInfo submit_info;
	std::memset(&submit_info, 0, sizeof(submit_info));
	submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit_info.commandBufferCount = 1;
	submit_info.pCommandBuffers = &frame.commands;
	check_vk_result(vkQueueSubmit(queue, 1, &submit_info, frame.drawn), "vkQueueSubmit(frame)");
	frame.serial = serial;
	submitted_ = serial;
	current_ = (current_ + 1) % size();
}

void VulkanFrameRing::wait(uint32_t slot)
{
	Frame const &frame = frames_[slot];
	if (frame.serial == 0)
	{
		return;
	}
	auto const started = std::chrono::steady_clock::now();
	check_vk_result(vkWaitForFences(device_, 1, &frame.drawn, VK_TRUE, UINT64_MAX), "vkWaitForFences(frame)");
	gpu_wait_ms_ += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
	retire(slot);
}

void VulkanFrameRing::wait_all()
{
	for (;;)
	{
		uint32_t oldest = 0;
		uint64_t oldest_serial = 0;
		for (uint32_t slot = 0; slot < size(); ++slot)
		{
			if (frames_[slot].serial != 0 && (oldest_serial == 0 || frames_[slot].serial < oldest_serial))
			{
				oldest = slot;
				oldest_serial = frames_[slot].serial;
			}
		}
		if (oldest_serial == 0)
		{
			return;
		}
		wait(oldest);
	}
}

void VulkanFrameRing::retire_all()
{
	for (uint32_t slot = 0; slot < size(); ++slot)
	{
		if (frames_[slot].serial != 0)
		{
			retire(slot);
		}
	}
}

void VulkanFrameRing::retire(uint32_t slot)
{
	Frame &frame = frames_[slot];
	check_vk_result(vkResetFences(device_, 1, &frame.drawn), "vkResetFences(frame)");
	vkFreeCommandBuffers(device_, pool_, 1, &frame.commands);
	// a fence of a submit says every earlier submit to the queue is done too
	completed_ = std::max(completed_, frame.serial);
	uint64_t const serial = frame.serial;
	frame.commands = VK_NULL_HANDLE;
	frame.serial = 0;
	retire_(slot, serial);
}

}
