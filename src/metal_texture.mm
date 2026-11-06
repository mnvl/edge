// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include "metal_impl.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>

#include "pixel.h"

namespace edge
{

namespace
{

struct MetalFormat
{
	MTLPixelFormat format;
	size_t bytes;          // a texel's, or a block's
	int block;             // texels along a side of a block (1: uncompressed)
	bool bc = false;
	bool astc = false;
};

MetalFormat metal_format(PixelFormat pixel_format, bool srgb)
{
	if (srgb && pixel_format != PixelFormat::X8R8G8B8 && pixel_format != PixelFormat::A8R8G8B8
		&& !(is_block_compressed(pixel_format) && pixel_format != PixelFormat::BC5))
		throw std::invalid_argument("an sRGB texture of pixel format " + to_string(pixel_format));
	switch (pixel_format)
	{
	case PixelFormat::X8R8G8B8:
	case PixelFormat::A8R8G8B8:      return {srgb ? MTLPixelFormatBGRA8Unorm_sRGB : MTLPixelFormatBGRA8Unorm, 4, 1};
	case PixelFormat::R16G16B16A16F: return {MTLPixelFormatRGBA16Float, 8, 1};
	case PixelFormat::R32G32B32A32F: return {MTLPixelFormatRGBA32Float, 16, 1};
	case PixelFormat::R32F:          return {MTLPixelFormatR32Float, 4, 1};
	case PixelFormat::BC1:           return {srgb ? MTLPixelFormatBC1_RGBA_sRGB : MTLPixelFormatBC1_RGBA, 8, 4, true};
	case PixelFormat::BC3:           return {srgb ? MTLPixelFormatBC3_RGBA_sRGB : MTLPixelFormatBC3_RGBA, 16, 4, true};
	case PixelFormat::BC5:           return {MTLPixelFormatBC5_RGUnorm, 16, 4, true};
	case PixelFormat::BC7:           return {srgb ? MTLPixelFormatBC7_RGBAUnorm_sRGB : MTLPixelFormatBC7_RGBAUnorm, 16, 4, true};
	case PixelFormat::ASTC_4x4:      return {srgb ? MTLPixelFormatASTC_4x4_sRGB : MTLPixelFormatASTC_4x4_LDR, 16, 4, false, true};
	case PixelFormat::ASTC_6x6:      return {srgb ? MTLPixelFormatASTC_6x6_sRGB : MTLPixelFormatASTC_6x6_LDR, 16, 6, false, true};
	case PixelFormat::ASTC_8x8:      return {srgb ? MTLPixelFormatASTC_8x8_sRGB : MTLPixelFormatASTC_8x8_LDR, 16, 8, false, true};
	default:
		throw std::logic_error("Metal backend: pixel format " + to_string(pixel_format) + " is not implemented yet");
	}
}

size_t row_bytes(MetalFormat const &format, int width)
{
	return static_cast<size_t>((width + format.block - 1) / format.block) * format.bytes;
}

}

MetalTexture::MetalTexture(MetalContextPtr const &context, int width, int height, PixelFormat pixel_format, TextureUsage const &usage):
	Texture(width, height, pixel_format, usage),
	context_(context),
	impl_(new Impl)
{
	if (width <= 0 || height <= 0)
		throw std::invalid_argument("a texture of " + std::to_string(width) + " x " + std::to_string(height));
	if (usage.array_layers)
		throw std::logic_error("Metal backend: array textures are not implemented yet");
	MetalFormat const format = metal_format(pixel_format, usage.srgb);
	if (format.bc && !context->supports_bc())
		throw std::runtime_error("a texture of pixel format " + to_string(pixel_format) + " is block-compressed (BC), which this device lacks");
	if (format.astc && !context->supports_astc())
		throw std::runtime_error("a texture of pixel format " + to_string(pixel_format) + " is ASTC, which this device lacks");
	impl_->format = format.format;
	make_texture(0);
	cpu_.assign(cpu_size(), 0);
}

void MetalTexture::make_texture(int array_length)
{
	MetalFormat const format = depth_ ? MetalFormat{MTLPixelFormatDepth32Float, 4, 1} : metal_format(pixel_format_, usage_.srgb);
	MTLTextureDescriptor *descriptor = [[MTLTextureDescriptor alloc] init];
	descriptor.pixelFormat = format.format;
	descriptor.width = width_;
	descriptor.height = height_;
	descriptor.mipmapLevelCount = mip_levels_;
	if (cube_)
		descriptor.textureType = MTLTextureTypeCube;
	else if (array_length)
	{
		descriptor.textureType = MTLTextureType2DArray;
		descriptor.arrayLength = array_length;
	}
	bool const compressed = format.bc || format.astc;
	descriptor.usage = compressed ? MTLTextureUsageShaderRead : (MTLTextureUsageShaderRead | MTLTextureUsageRenderTarget);
	descriptor.storageMode = depth_ ? MTLStorageModePrivate : MTLStorageModeShared;
	impl_->texture = [context_->impl().device newTextureWithDescriptor:descriptor];
	if (!impl_->texture)
		throw std::runtime_error("Metal could not make a texture of pixel format " + to_string(pixel_format_) + ", "
			+ std::to_string(width_) + " x " + std::to_string(height_));
	impl_->last_use = 0;
}

namespace
{

TextureUsage layered_usage(int layers)
{
	TextureUsage usage;
	usage.keep_cpu_copy = false;
	usage.array_layers = layers;
	return usage;
}

}

MetalTexture::MetalTexture(MetalContextPtr const &context, int width, int height, PixelFormat pixel_format, Layers layers):
	Texture(width, height, pixel_format, layered_usage(layers.count)),
	context_(context),
	impl_(new Impl)
{
	if (width <= 0 || height <= 0)
		throw std::invalid_argument("a texture of " + std::to_string(width) + " x " + std::to_string(height));
	if (layers.count < 1 || (layers.cube && layers.count != 6))
		throw std::logic_error("a render target's texture of " + std::to_string(layers.count) + " layers");
	MetalFormat const format = metal_format(pixel_format, false);
	if (format.bc || format.astc)
		throw std::invalid_argument("a render target's colour of block-compressed pixel format " + to_string(pixel_format));
	cube_ = layers.cube;
	impl_->format = format.format;
	make_texture(layers.cube ? 0 : layers.count);
}

MetalTexture::MetalTexture(MetalContextPtr const &context, int width, int height, Depth, Layers layers, bool readable):
	Texture(width, height, PixelFormat::R32F, layered_usage(layers.count)),
	context_(context),
	impl_(new Impl),
	depth_(true),
	readable_depth_(readable)
{
	if (width <= 0 || height <= 0)
		throw std::invalid_argument("a depth texture of " + std::to_string(width) + " x " + std::to_string(height));
	cube_ = layers.cube;
	impl_->format = MTLPixelFormatDepth32Float;
	make_texture(layers.cube ? 0 : layers.count);
}

MetalTexture::~MetalTexture() = default;

void MetalTexture::used()
{
	impl_->last_use = context_->current_frame();
}

const void *MetalTexture::do_lock() const
{
	if (cpu_.empty())
		throw std::logic_error("a texture without its CPU copy cannot be read");
	return cpu_.data();
}

void MetalTexture::do_unlock() const
{
}

void *MetalTexture::do_lock(bool)
{
	if (cpu_.empty())
		throw std::logic_error("a texture without its CPU copy cannot be locked");
	return cpu_.data();
}

void MetalTexture::do_unlock()
{
	if (cpu_.empty())
		return;
	MetalFormat const format = metal_format(pixel_format_, usage_.srgb);
	if (context_->busy(impl_->last_use))
		make_texture(0);      // a command buffer still to run samples the old one: it keeps it
	int const levels = std::max(1, usage_.provided_mip_levels);
	size_t offset = 0;
	for (int level = 0; level < levels; ++level)
	{
		int const w = std::max(1, width_ >> level), h = std::max(1, height_ >> level);
		size_t const size = level_size(width_, height_, pixel_format_, level);
		[impl_->texture replaceRegion:MTLRegionMake2D(0, 0, w, h) mipmapLevel:level withBytes:cpu_.data() + offset
			bytesPerRow:row_bytes(format, w)];
		offset += size;
	}
	if (usage_.mipmaps && mip_levels_ > 1)
	{
		id<MTLCommandBuffer> commands = [context_->impl().queue commandBuffer];
		id<MTLBlitCommandEncoder> blit = [commands blitCommandEncoder];
		[blit generateMipmapsForTexture:impl_->texture];
		[blit endEncoding];
		[commands commit];
		[commands waitUntilCompleted];
		context_->throw_gpu_error();
	}
	if (!usage_.keep_cpu_copy)
	{
		cpu_.clear();
		cpu_.shrink_to_fit();
	}
}

void MetalTexture::download()
{
	Texture::download();
	if (cpu_.empty())
		throw std::logic_error("a texture without its CPU copy cannot be downloaded");
	MetalFormat const format = metal_format(pixel_format_, usage_.srgb);
	if (format.bc || format.astc)
		throw std::logic_error("a block-compressed texture cannot be downloaded");
	// what the queue has been given is drawn before the texels are read: a command buffer behind it is done when the ones before are
	id<MTLCommandBuffer> commands = [context_->impl().queue commandBuffer];
	[commands commit];
	[commands waitUntilCompleted];
	context_->throw_gpu_error();
	[impl_->texture getBytes:cpu_.data() bytesPerRow:row_bytes(format, width_) fromRegion:MTLRegionMake2D(0, 0, width_, height_)
		mipmapLevel:0];
}

}
