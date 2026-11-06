// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include "metal_impl.h"

#include <atomic>
#include <mutex>
#include <thread>
#include <unistd.h>

#include "metal_gpu_timer.h"

namespace edge
{

namespace
{

/// Why the device cannot time passes; empty when it can.
std::string unavailable_reason_of(id<MTLDevice> device)
{
	if (![device supportsCounterSampling:MTLCounterSamplingPointAtStageBoundary])
		return "this Metal device cannot sample counters at the boundaries of a pass's stages";
	for (id<MTLCounterSet> set in device.counterSets)
		if ([set.name isEqualToString:MTLCommonCounterSetTimestamp])
			return "";
	return "this Metal device has no timestamp counter set";
}

}

struct MetalGpuTimer::Impl
{
	struct Frame
	{
		uint64_t number = 0;
		id<MTLCounterSampleBuffer> samples = nil;
		uint32_t passes = 0;
		std::atomic<bool> taken{false};      // read, by the handler or by frame_finished()
	};
	struct Done
	{
		uint64_t number;
		std::vector<double> times;
		bool disjoint;
	};

	id<MTLDevice> device = nil;
	double us_per_tick = 0;
	std::shared_ptr<Frame> recording, submitted;
	std::mutex mutex;
	std::vector<Done> done;

	/// A buffer for a frame's samples, new each time: one used before holds the samples of its passes, which a pass that runs no stage
	/// does not overwrite.
	id<MTLCounterSampleBuffer> take_buffer()
	{
		MTLCounterSampleBufferDescriptor *descriptor = [[MTLCounterSampleBufferDescriptor alloc] init];
		for (id<MTLCounterSet> set in device.counterSets)
			if ([set.name isEqualToString:MTLCommonCounterSetTimestamp])
				descriptor.counterSet = set;
		descriptor.storageMode = MTLStorageModeShared;
		descriptor.sampleCount = 2 * GpuTimer::MAX_PASSES;
		NSError *error = nil;
		id<MTLCounterSampleBuffer> buffer = [device newCounterSampleBufferWithDescriptor:descriptor error:&error];
		if (!buffer)
			throw std::runtime_error(std::string("Metal: a counter sample buffer cannot be made: ") + [error.localizedDescription UTF8String]);
		return buffer;
	}

	/// The frame's passes' durations; disjoint where the GPU wrote no timestamp.
	Done read(Frame const &frame)
	{
		Done result{frame.number, {}, false};
		if (frame.passes)
		{
			NSData *data = [frame.samples resolveCounterRange:NSMakeRange(0, 2 * frame.passes)];
			auto const *stamps = static_cast<MTLCounterResultTimestamp const *>(data.bytes);
			if (!data || data.length < 2 * frame.passes * sizeof(MTLCounterResultTimestamp))
				result.disjoint = true;
			else
				for (uint32_t i = 0; i < frame.passes && !result.disjoint; ++i)
				{
					uint64_t const begin = stamps[2 * i].timestamp, end = stamps[2 * i + 1].timestamp;
					if (begin == MTLCounterErrorValue || end == MTLCounterErrorValue || (end && end < begin))
						result.disjoint = true;
					else if (!end)
						// a sample the GPU never wrote (the buffer starts zeroed): a pass with no draws runs no fragment stage, so it
						// has no end to sample; it took no time on the GPU
						result.times.push_back(0.0);
					else
						result.times.push_back(double(end - begin) * us_per_tick);
				}
		}
		return result;
	}
};

MetalGpuTimer::MetalGpuTimer(MetalContextPtr const &context):
	GpuTimer(unavailable_reason_of(context->impl().device)),
	context_(context),
	impl_(new Impl)
{
	impl_->device = context->impl().device;
}

MetalGpuTimer::~MetalGpuTimer() = default;

void MetalGpuTimer::start()
{
	// the GPU's ticks in microseconds: how far the GPU's clock goes while the CPU's goes a few milliseconds
	MTLTimestamp cpu0 = 0, gpu0 = 0, cpu1 = 0, gpu1 = 0;
	[impl_->device sampleTimestamps:&cpu0 gpuTimestamp:&gpu0];
	usleep(5000);
	[impl_->device sampleTimestamps:&cpu1 gpuTimestamp:&gpu1];
	if (gpu1 <= gpu0 || cpu1 <= cpu0)
		throw std::runtime_error("Metal: the device's clocks do not advance, so passes cannot be timed");
	impl_->us_per_tick = double(cpu1 - cpu0) / double(gpu1 - gpu0) / 1000.0;
}

void MetalGpuTimer::stop()
{
	std::lock_guard<std::mutex> lock(impl_->mutex);
	impl_->recording.reset();
	impl_->submitted.reset();
	impl_->done.clear();
}

void MetalGpuTimer::collect()
{
	std::vector<Impl::Done> done;
	{
		std::lock_guard<std::mutex> lock(impl_->mutex);
		done.swap(impl_->done);
	}
	for (auto &frame : done)
	{
		if (!enabled())
			continue;
		if (frame.disjoint)
			record_disjoint(frame.number);
		else
			record(frame.number, std::move(frame.times));
	}
}

void MetalGpuTimer::begin_frame(uint64_t frame)
{
	collect();
	impl_->recording.reset();
	if (!enabled())
		return;
	impl_->recording = std::make_shared<Impl::Frame>();
	impl_->recording->number = frame;
	impl_->recording->samples = impl_->take_buffer();
}

void MetalGpuTimer::begin_pass(void *pass_descriptor)
{
	auto const &frame = impl_->recording;
	if (!frame || frame->passes >= MAX_PASSES)
		return;
	MTLRenderPassDescriptor *pass = (__bridge MTLRenderPassDescriptor *)pass_descriptor;
	MTLRenderPassSampleBufferAttachmentDescriptor *attachment = pass.sampleBufferAttachments[0];
	attachment.sampleBuffer = frame->samples;
	attachment.startOfVertexSampleIndex = 2 * frame->passes;
	attachment.endOfFragmentSampleIndex = 2 * frame->passes + 1;
	++frame->passes;
}

void MetalGpuTimer::end_frame(void *command_buffer)
{
	impl_->submitted = std::move(impl_->recording);
	std::shared_ptr<Impl::Frame> frame = impl_->submitted;
	if (!frame)
		return;
	Impl *impl = impl_.get();
	id<MTLCommandBuffer> commands = (__bridge id<MTLCommandBuffer>)command_buffer;
	[commands addCompletedHandler:^(id<MTLCommandBuffer>) {
		if (frame->taken.exchange(true))
			return;
		Impl::Done result = impl->read(*frame);
		std::lock_guard<std::mutex> lock(impl->mutex);
		impl->done.push_back(std::move(result));
	}];
}

void MetalGpuTimer::frame_finished()
{

	std::shared_ptr<Impl::Frame> frame = std::move(impl_->submitted);
	if (frame && !frame->taken.exchange(true))
	{
		Impl::Done result = impl_->read(*frame);
		std::lock_guard<std::mutex> lock(impl_->mutex);
		impl_->done.push_back(std::move(result));
	}
	collect();
}

}
