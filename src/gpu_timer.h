// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace edge
{

/// GPU durations of a renderer's passes, frame by frame: what every backend's timer has in common (VulkanGpuTimer, GLESGpuTimer, the
/// null renderer's, which cannot time), and what Renderer::gpu_trace_enabled() and pass_times_us() answer from.
///
/// A frame's durations come once the GPU has drawn it, which is after the frame is ended, often frames after: on Vulkan when the
/// renderer next waits for it (finish_frame(), or a begin_frame() that takes its share back with frames in flight), on OpenGL ES when
/// the driver has them (after finish_frame() where it does, a few frames later on WebGL 2, whose results never come in the task that
/// asked for them).  So they are asked for by the frame's number (Renderer::frame_serial()), and kept for the last HISTORY frames
/// read back.  A frame whose time the GPU could not measure (OpenGL ES's disjoint: its clock changed, or another context took it) has
/// no durations at all: disjoint(frame) says so, and they never come.
class GpuTimer {
public:
	/// Passes timed in a frame at most; those after them are drawn, not timed.
	static constexpr uint32_t MAX_PASSES = 32;
	/// Frames read back whose durations (or disjoint) are kept.
	static constexpr size_t HISTORY = 16;

	virtual ~GpuTimer() = default;
	GpuTimer(GpuTimer const &) = delete;
	GpuTimer &operator=(GpuTimer const &) = delete;

	/// Whether the GPU can time passes; unavailable_reason() says why not (empty when it can).
	bool available() const { return unavailable_.empty(); }
	std::string const &unavailable_reason() const { return unavailable_; }

	/// Times the passes of the frames begun from now on.  std::runtime_error saying why when the GPU cannot (!available()).  Turning it
	/// off drops the durations kept and the frames still to be read back.
	void set_enabled(bool enabled);
	bool enabled() const { return enabled_; }

	/// The durations (microseconds, in begin_pass order) of the latest frame read back that has them; empty while off and before.
	std::vector<double> const &last_pass_times_us() const { return last_pass_times_us_; }
	/// The frame the last durations are of; 0 for none.
	uint64_t last_frame() const { return last_frame_; }
	/// The durations of `frame`, one of the last HISTORY frames read back; none before it is read back, after, for a frame begun
	/// untimed, and for a disjoint one.
	std::optional<std::vector<double>> pass_times_us(uint64_t frame) const;
	/// Whether `frame`, one of the last HISTORY read back, was disjoint: timed, and its durations are not to be had.
	bool disjoint(uint64_t frame) const;

protected:
	/// @p unavailable: why the GPU cannot time, empty when it can.
	explicit GpuTimer(std::string unavailable): unavailable_(std::move(unavailable)) {}
	/// Each time it is turned on: makes or resets what timing needs.  std::runtime_error when it cannot (it stays off).
	virtual void start() = 0;
	/// Each time it is turned off: drops what is still to be read back.
	virtual void stop() = 0;
	/// A frame read back: its durations, or that it was disjoint.
	void record(uint64_t frame, std::vector<double> times);
	void record_disjoint(uint64_t frame);

private:
	std::string unavailable_;
	bool enabled_ = false;
	std::vector<double> last_pass_times_us_;
	uint64_t last_frame_ = 0;
	std::deque<std::pair<uint64_t, std::optional<std::vector<double>>>> history_;      // by frame; none: disjoint
	void keep(uint64_t frame, std::optional<std::vector<double>> times);
};

}
