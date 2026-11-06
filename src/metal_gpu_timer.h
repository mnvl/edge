// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

#include <memory>

#include "gpu_timer.h"
#include "metal_context.h"

namespace edge
{

/// Per-pass GPU durations on Metal: the counter sample buffer of the device's timestamp set, sampled at the start of each pass's vertex
/// stage and the end of its fragment stage (stage boundaries, what Apple GPUs sample at), converted to microseconds with the CPU and
/// GPU timestamps the device gives.  A frame's durations are read when its command buffer has completed (its completion handler), so
/// they come after finish_frame() or a frame or two after submit_frame().  Not available() where the device lacks the timestamp
/// counter set or sampling at stage boundaries.
class MetalGpuTimer: public GpuTimer {
public:
	explicit MetalGpuTimer(MetalContextPtr const &context);
	~MetalGpuTimer() override;

	struct Impl;
	Impl &impl() const { return *impl_; }

	/// A frame begins: one that was begun while timing is on is timed.  @p frame: Renderer::frame_serial().
	void begin_frame(uint64_t frame);
	/// Whether the frame begun is timed, and room for another pass in it: sets the sample attachment of @p pass_descriptor (an
	/// MTLRenderPassDescriptor, as a void *) and counts the pass.
	void begin_pass(void *pass_descriptor);
	/// The frame's command buffer (an id<MTLCommandBuffer> as a void *) is to be committed: reads its samples when it completes.
	void end_frame(void *command_buffer);
	/// The frame ended last is done (finish_frame() waited for it): its durations are read now, not when the completion handler runs.
	void frame_finished();
	/// Records the frames whose command buffers have completed since (their handlers run on Metal's threads and leave the samples here).
	void collect();

protected:
	void start() override;
	void stop() override;

private:
	MetalContextPtr context_;
	std::unique_ptr<Impl> impl_;
};

}
