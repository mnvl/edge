// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include "vulkan_gpu_timer.h"

#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "vulkan_error.h"

namespace edge
{

namespace
{

/// The bits of a timestamp @p queue_family of @p physical_device writes (0: none), and its nanoseconds a tick.
std::pair<uint32_t, double> timestamps(VkPhysicalDevice physical_device, uint32_t queue_family)
{
	uint32_t families_count = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &families_count, nullptr);
	std::vector<VkQueueFamilyProperties> families(families_count);
	vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &families_count, families.data());
	uint32_t const valid_bits = queue_family < families_count ? families[queue_family].timestampValidBits : 0;
	// timestampPeriod is the number of nanoseconds per tick; ~1.0 on RTX-class GPUs.
	VkPhysicalDeviceProperties props;
	vkGetPhysicalDeviceProperties(physical_device, &props);
	return {valid_bits, static_cast<double>(props.limits.timestampPeriod)};
}

std::string timestamps_unavailable(VkPhysicalDevice physical_device, uint32_t queue_family)
{
	auto const [valid_bits, period_ns] = timestamps(physical_device, queue_family);
	if (valid_bits == 0 || period_ns <= 0.0)
	{
		return "the GPU's queue family " + std::to_string(queue_family) + " writes no timestamps";
	}
	return std::string();
}

}

VulkanGpuTimer::VulkanGpuTimer(VkDevice device, VkPhysicalDevice physical_device, uint32_t queue_family, uint32_t slots):
	GpuTimer(timestamps_unavailable(physical_device, queue_family)),
	device_(device),
	physical_device_(physical_device),
	queue_family_(queue_family),
	pool_(VK_NULL_HANDLE),
	slots_(slots),
	count_(slots, 0),
	timed_(slots, false),
	period_ns_(0.0),
	tick_mask_(0)
{
}

VulkanGpuTimer::~VulkanGpuTimer()
{
	if (pool_ != VK_NULL_HANDLE)
	{
		vkDestroyQueryPool(device_, pool_, nullptr);
	}
}

// (only when available(): the queue family writes timestamps)
void VulkanGpuTimer::start()
{
	if (pool_ != VK_NULL_HANDLE)
	{
		return;
	}
	auto const [valid_bits, period_ns] = timestamps(physical_device_, queue_family_);
	period_ns_ = period_ns;
	tick_mask_ = valid_bits >= 64 ? ~uint64_t(0) : (uint64_t(1) << valid_bits) - 1;

	VkQueryPoolCreateInfo info;
	std::memset(&info, 0, sizeof(info));
	info.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
	info.queryType = VK_QUERY_TYPE_TIMESTAMP;
	info.queryCount = QUERY_CAPACITY * slots_;
	check_vk_result(vkCreateQueryPool(device_, &info, nullptr, &pool_), "vkCreateQueryPool(timestamps)");
}

void VulkanGpuTimer::stop()
{
	timed_.assign(slots_, false);
}

void VulkanGpuTimer::begin_frame(VkCommandBuffer command_buffer, uint32_t slot)
{
	recording_ = slot;
	count_[slot] = 0;
	timed_[slot] = enabled();
	if (!enabled())
	{
		return;
	}
	vkCmdResetQueryPool(command_buffer, pool_, slot * QUERY_CAPACITY, QUERY_CAPACITY);
}

// TOP-of-pipe before vkCmdBeginRenderPass, so the layout transition and clear count against the pass.  Written only
// with room for the pair, so a pass is timed whole or not at all.
void VulkanGpuTimer::begin_pass(VkCommandBuffer command_buffer)
{
	uint32_t &count = count_[recording_];
	if (timed_[recording_] && count + 2 <= QUERY_CAPACITY)
	{
		vkCmdWriteTimestamp(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, pool_, recording_ * QUERY_CAPACITY + count);
		count++;
	}
}

// BOTTOM-of-pipe after vkCmdEndRenderPass, so the implicit subpass-end barriers count against this pass.
void VulkanGpuTimer::end_pass(VkCommandBuffer command_buffer)
{
	uint32_t &count = count_[recording_];
	if (timed_[recording_] && count + 1 <= QUERY_CAPACITY)
	{
		vkCmdWriteTimestamp(command_buffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, pool_, recording_ * QUERY_CAPACITY + count);
		count++;
	}
}

// Queries come back in the order written: pass i began at ticks[2i] and ended at ticks[2i + 1].
void VulkanGpuTimer::read_back(uint32_t slot, uint64_t frame)
{
	uint32_t const count = count_[slot];
	bool const timed = timed_[slot];
	count_[slot] = 0;
	timed_[slot] = false;
	// a frame begun untimed, or tracing switched off since
	if (!timed || !enabled())
	{
		return;
	}
	std::vector<uint64_t> ticks(count);
	if (count > 0)
	{
		check_vk_result(vkGetQueryPoolResults(device_, pool_, slot * QUERY_CAPACITY, count, ticks.size() * sizeof(uint64_t), ticks.data(),
			sizeof(uint64_t), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT), "vkGetQueryPoolResults(timestamps)");
	}
	std::vector<double> times;
	for (uint32_t i = 0; i + 1 < count; i += 2)
	{
		uint64_t const ticks_taken = (ticks[i + 1] - ticks[i]) & tick_mask_;
		times.push_back(static_cast<double>(ticks_taken) * period_ns_ / 1000.0);
	}
	record(frame, std::move(times));
}

}
