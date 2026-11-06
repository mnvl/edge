// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include "metal_impl.h"

#include <stdexcept>

namespace edge
{

namespace
{

void check_device(id<MTLDevice> device)
{
	if (!device)
		throw std::runtime_error("no Metal device");
	if (![device supportsFamily:MTLGPUFamilyApple2])
		throw std::runtime_error(std::string("the Metal device ") + [device.name UTF8String]
			+ " is not an Apple GPU (family 2 and later): the Metal backend is for Apple silicon");
}

}

MetalContext::MetalContext():
	impl_(new Impl)
{
	impl_->device = MTLCreateSystemDefaultDevice();
	check_device(impl_->device);
	impl_->queue = [impl_->device newCommandQueue];
	if (!impl_->queue)
		throw std::runtime_error("Metal gives no command queue");
}

MetalContext::MetalContext(void *layer):
	MetalContext()
{
	if (!layer)
		throw std::invalid_argument("a hosted Metal context needs the window's CAMetalLayer");
	impl_->layer = (__bridge CAMetalLayer *)layer;
	impl_->layer.device = impl_->device;
	impl_->layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
}

MetalContext::~MetalContext() = default;

std::string MetalContext::device_name() const
{
	return [impl_->device.name UTF8String];
}

bool MetalContext::supports_astc() const
{
	return [impl_->device supportsFamily:MTLGPUFamilyApple2];
}

bool MetalContext::supports_bc() const
{
	return impl_->device.supportsBCTextureCompression;
}

bool MetalContext::hosted() const
{
	return impl_->layer != nil;
}

void MetalContext::frame_completed(uint64_t frame)
{
	uint64_t seen = completed_;
	while (frame > seen && !completed_.compare_exchange_weak(seen, frame))
		;
}

void MetalContext::report_gpu_error(std::string const &what)
{
	std::lock_guard<std::mutex> lock(error_mutex_);
	if (error_.empty())
		error_ = what;
}

void MetalContext::throw_gpu_error()
{
	std::string what;
	{
		std::lock_guard<std::mutex> lock(error_mutex_);
		what.swap(error_);
	}
	if (!what.empty())
		throw std::runtime_error("Metal: a command buffer failed: " + what);
}

}
