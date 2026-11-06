// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

#include <vulkan/vulkan.h>
#include <cstdint>
#include <vector>

#include "gpu_timer.h"

namespace edge
{

/// Per-pass GPU durations from timestamp queries (a GpuTimer: the durations kept by frame are its).  Each begin_pass/end_pass pair
/// writes two queries into the frame's command buffer; read_back() turns them into one duration per pair once the frame is known to
/// be drawn.  Each frame in flight has its own queries.  The pool is made at the first enable, so a renderer that never traces pays
/// nothing.  Passes past the capacity are not timed.  Vulkan's timestamps are never disjoint.
class VulkanGpuTimer: public GpuTimer {
public:
	/// Two queries a pass.
	static constexpr uint32_t QUERY_CAPACITY = 2 * MAX_PASSES;

	/// Times what is recorded for the queue family @p queue_family, in up to `slots` frames in flight.  Not available() when the queue
	/// family writes no timestamps (set_enabled(true) is then std::runtime_error).
	VulkanGpuTimer(VkDevice device, VkPhysicalDevice physical_device, uint32_t queue_family, uint32_t slots = 1);
	~VulkanGpuTimer() override;

	/// Outside a render pass, once per frame: the frame in `slot` is recorded.
	void begin_frame(VkCommandBuffer command_buffer, uint32_t slot);
	/// Around vkCmdBeginRenderPass / vkCmdEndRenderPass.
	void begin_pass(VkCommandBuffer command_buffer);
	void end_pass(VkCommandBuffer command_buffer);
	/// Once the frame `frame` recorded in `slot` is drawn: its durations replace the last ones, unless those are of a later
	/// frame.  std::runtime_error when they cannot be read.
	void read_back(uint32_t slot, uint64_t frame);

protected:
	void start() override;
	/// Forgets which frames in flight were begun timed: read back, they give nothing (their queries are reset when their slot is used
	/// again).
	void stop() override;

private:
	VkDevice device_;
	VkPhysicalDevice physical_device_;
	uint32_t queue_family_;
	VkQueryPool pool_;
	uint32_t slots_;
	uint32_t recording_ = 0;                 // the slot of the frame recorded
	std::vector<uint32_t> count_;            // queries written, by slot
	std::vector<bool> timed_;                // the slot's frame was begun with tracing on
	double period_ns_;
	uint64_t tick_mask_;     // the bits of a timestamp the queue family writes: a counter that wrapped between two still subtracts
};

}
