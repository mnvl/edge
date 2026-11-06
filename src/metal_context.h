// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

namespace edge
{

/// A Metal device and its one command queue, which the renderer and everything it makes share (they hold it, so that it outlives them).
/// Optionally with a CAMetalLayer of the host's window, which MetalRenderer::show_frame() draws into.  Frames are numbered by the
/// renderer (Renderer::frame_serial()); the context knows how far the GPU has got, so that a buffer or texture the CPU is to write
/// can tell whether a command buffer still to run has it (busy()) and then be written as a new object, never in place.
///
/// The headers of the Metal backend are plain C++; the Objective-C is in the .mm files (metal_impl.h).
class MetalContext
{
public:
	/// On the system's default device (Apple silicon: its one GPU).  std::runtime_error when there is none, or when it lacks Apple
	/// GPU family 2 (the A8 and later, and the Macs of Apple silicon: ASTC, eight colour attachments).
	MetalContext();
	/// With the host's window: @p layer is its CAMetalLayer (a void * so that this header is C++), which gets this device.
	explicit MetalContext(void *layer);
	~MetalContext();
	MetalContext(MetalContext const &) = delete;
	MetalContext &operator=(MetalContext const &) = delete;

	std::string device_name() const;
	/// Whether the device samples ASTC (every Apple GPU) and BC (Apple-silicon Macs; not an iPhone) textures.
	bool supports_astc() const;
	bool supports_bc() const;
	/// Whether there is a window layer to show frames in.
	bool hosted() const;

	/// The frame the renderer is building or built last; the number resources it draws with are marked with.
	uint64_t current_frame() const { return current_; }
	void set_current_frame(uint64_t frame) { current_ = frame; }
	/// Frames up to this one are done by the GPU.
	uint64_t completed_frames() const { return completed_; }
	void frame_completed(uint64_t frame);
	/// Whether a frame not done yet has used something last marked with @p used_in.
	bool busy(uint64_t used_in) const { return used_in > completed_; }

	/// The error a command buffer ended with (reported by its completion handler), once: std::runtime_error if there is one.
	void throw_gpu_error();
	void report_gpu_error(std::string const &what);

	struct Impl;
	Impl &impl() const { return *impl_; }

private:
	std::unique_ptr<Impl> impl_;
	uint64_t current_ = 0;
	std::atomic<uint64_t> completed_{0};
	std::mutex error_mutex_;
	std::string error_;
};

using MetalContextPtr = std::shared_ptr<MetalContext>;

}
