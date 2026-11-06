// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

// What the Metal backend's .mm files share: the Objective-C objects behind the C++ headers' Impl structs.  Not for a C++ file.

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#include <TargetConditionals.h>

#include "metal_buffer.h"
#include "metal_context.h"
#include "metal_render_target.h"
#include "metal_texture.h"

namespace edge
{

struct MetalContext::Impl
{
	id<MTLDevice> device = nil;
	id<MTLCommandQueue> queue = nil;
	CAMetalLayer *layer = nil;
};

struct MetalBufferStore::Impl
{
	id<MTLBuffer> buffer = nil;
	uint64_t last_use = 0;
};

struct MetalTexture::Impl
{
	id<MTLTexture> texture = nil;
	MTLPixelFormat format = MTLPixelFormatInvalid;
	uint64_t last_use = 0;
};

/// The Metal object behind a texture / buffer of this backend (std::invalid_argument naming @p what for one of another).
inline id<MTLTexture> metal_texture_of(MetalTexture const &texture) { return texture.impl().texture; }

/// The sdk the shader libraries are built for (EdgeMetal.cmake's <sdk>).
inline char const *metal_sdk()
{
#if TARGET_OS_SIMULATOR
	return "iphonesimulator";
#elif TARGET_OS_IPHONE
	return "iphoneos";
#else
	return "macosx";
#endif
}

}
