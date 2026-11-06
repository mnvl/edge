// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include <vulkan/vulkan.h>

namespace edge
{

/// A VulkanRenderer's frames in flight: a share of the host's memory for each (the renderer's: regions of its uniform buffers, its
/// staging arena, its timer's queries), the command buffer of the frame drawn from it and the fence that says the GPU has drawn it,
/// and the serials of the frames submitted and known drawn.  The host writes a frame into the current share, submits it, and moves on
/// to the next; a share is waited for before it is written again.  When a frame is known drawn its share is retired: the ring frees its
/// command buffer and calls the renderer's retire(slot, serial), which gives the share's memory back.
class VulkanFrameRing
{
public:
	using Retire = std::function<void (uint32_t, uint64_t)>;

	/// @p frames shares (1 or more), their fences made on @p device; command buffers come from and go back to @p pool.
	VulkanFrameRing(VkDevice device, VkCommandPool pool, uint32_t frames, Retire retire);
	/// Frees what the ring holds: the GPU is idle (the renderer waited for it).
	~VulkanFrameRing();
	VulkanFrameRing(VulkanFrameRing const &) = delete;
	VulkanFrameRing &operator=(VulkanFrameRing const &) = delete;

	uint32_t size() const { return static_cast<uint32_t>(frames_.size()); }
	/// The share the host writes the next frame into.
	uint32_t current() const { return current_; }
	/// The current share's frame records into @p commands (begun, from the pool).
	void begin(VkCommandBuffer commands);
	/// Ends the current share's command buffer and submits it on @p queue as frame @p serial, signalling its fence; the next share
	/// becomes current.
	void submit(VkQueue queue, uint64_t serial);
	/// Waits for the frame in @p slot, if one is in flight there, and retires it.
	void wait(uint32_t slot);
	/// Waits for every frame in flight, the oldest first (its times are read before a later one's).
	void wait_all();
	/// Retires every frame in flight without waiting: for when the queue is known idle.
	void retire_all();

	/// The last frame submitted, and the last known drawn (0: none).
	uint64_t submitted() const { return submitted_; }
	uint64_t completed() const { return completed_; }
	/// Milliseconds the host has waited for frames the GPU had not drawn yet.
	double gpu_wait_ms() const { return gpu_wait_ms_; }

private:
	struct Frame
	{
		VkCommandBuffer commands = VK_NULL_HANDLE;
		VkFence drawn = VK_NULL_HANDLE;
		uint64_t serial = 0;           // the frame submitted with it; 0: none in flight
	};

	VkDevice device_;
	VkCommandPool pool_;
	Retire retire_;
	std::vector<Frame> frames_;
	uint32_t current_ = 0;
	uint64_t submitted_ = 0;
	uint64_t completed_ = 0;
	double gpu_wait_ms_ = 0;

	void retire(uint32_t slot);
};

}
