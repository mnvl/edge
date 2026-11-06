// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include "metal_renderer_impl.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

#include "material.h"
#include "pixel.h"
#include "shader_program.h"

namespace edge
{

namespace
{

char const *const SHOW_SOURCE =
	"#include <metal_stdlib>\n"
	"using namespace metal;\n"
	"struct Out { float4 position [[position]]; float2 uv; };\n"
	"vertex Out show_vertex(uint id [[vertex_id]]) {\n"
	"  float2 p = float2(float((id << 1) & 2), float(id & 2));\n"
	"  Out out; out.uv = float2(p.x, 1.0 - p.y); out.position = float4(p * 2.0 - 1.0, 0.0, 1.0); return out;\n"
	"}\n"
	"fragment float4 show_fragment(Out in [[stage_in]], texture2d<float> frame [[texture(0)]], sampler s [[sampler(0)]]) {\n"
	"  return float4(frame.sample(s, in.uv).rgb, 1.0);\n"
	"}\n";

std::string read_text(std::string const &path)
{
	std::ifstream in(path, std::ios::binary);
	if (!in)
		throw std::runtime_error("cannot read shader " + path);
	std::ostringstream text;
	text << in.rdbuf();
	return text.str();
}

bool is_spirv(std::string const &name)
{
	return name.size() > 4 && name.compare(name.size() - 4, 4, ".spv") == 0;
}

std::string stem(std::string const &spirv_name)
{
	return spirv_name.substr(0, spirv_name.size() - 4);
}

/// What shader_cross --msl wrote beside a shader: <stem>.msl.map.
void read_map(std::string const &path, MetalProgram::Stage &stage)
{
	std::istringstream in(read_text(path));
	for (std::string kind; in >> kind;)
	{
		if (kind == "stage")
		{
			std::string name;
			in >> name;
		}
		else if (kind == "ubo")
		{
			std::string name;
			int index = 0;
			in >> name >> index;
			stage.ubos.push_back(index);
		}
		else if (kind == "sampler")
		{
			std::string name;
			MetalProgram::Sampler sampler{};
			in >> name >> sampler.texture >> sampler.sampler;
			stage.samplers.push_back(sampler);
		}
		else if (kind == "push")
		{
			int index = 0;
			in >> index;
			if (index != static_cast<int>(MetalRenderer::PUSH_BUFFER))
				throw std::runtime_error(path + ": push constants at buffer " + std::to_string(index) + ", not "
					+ std::to_string(MetalRenderer::PUSH_BUFFER));
			stage.push = true;
		}
		else
			throw std::runtime_error(path + ": a line of " + kind + "?");
		if (!in)
			throw std::runtime_error(path + ": a line of " + kind + " is cut short");
	}
}

id<MTLFunction> load_function(MetalContext &context, std::string const &library_path)
{
	NSError *error = nil;
	NSURL *url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:library_path.c_str()]];
	id<MTLLibrary> library = [context.impl().device newLibraryWithURL:url error:&error];
	if (!library)
		throw std::runtime_error("cannot load the shader library " + library_path + ": "
			+ (error ? [error.localizedDescription UTF8String] : "unknown error"));
	id<MTLFunction> function = [library newFunctionWithName:@"main0"];
	if (!function)
		throw std::runtime_error("the shader library " + library_path + " has no function main0");
	return function;
}

}

bool MetalProgram::reads(int binding) const
{
	for (Stage const *stage : {&vertex_stage, &fragment_stage})
		if (std::find(stage->ubos.begin(), stage->ubos.end(), binding) != stage->ubos.end())
			return true;
	return false;
}

MetalArena::Piece MetalArena::allocate(id<MTLDevice> device, size_t size)
{
	size_t const aligned = (size + 255) & ~size_t(255);
	for (;;)
	{
		if (chunk == chunks.size())
		{
			id<MTLBuffer> buffer = [device newBufferWithLength:std::max<size_t>(aligned, 4u << 20) options:MTLResourceStorageModeShared];
			if (!buffer)
				throw std::runtime_error("Metal could not make a buffer for a frame's uniforms");
			chunks.push_back(buffer);
		}
		id<MTLBuffer> buffer = chunks[chunk];
		if (used + aligned <= buffer.length)
		{
			Piece piece{buffer, used, static_cast<char *>(buffer.contents) + used};
			used += aligned;
			return piece;
		}
		++chunk;
		used = 0;
	}
}

MetalRenderer::Impl::~Impl()
{
	// the frames in flight are done before the semaphore goes (dispatch aborts on one released below its starting value)
	dispatch_semaphore_wait(slots, DISPATCH_TIME_FOREVER);
	dispatch_semaphore_wait(slots, DISPATCH_TIME_FOREVER);
	dispatch_semaphore_signal(slots);
	dispatch_semaphore_signal(slots);
}

std::shared_ptr<MetalProgram> MetalRenderer::Impl::program(Renderer &, std::string const &vertex, std::string const &fragment)
{
	auto const key = std::make_pair(vertex, fragment);
	auto found = programs.find(key);
	if (found != programs.end())
		return found->second;
	if (!is_spirv(vertex) || !is_spirv(fragment))
		throw std::invalid_argument("Metal shaders " + vertex + " and " + fragment + ": a Metal program is made of SPIR-V names (.spv)");
	auto const path = [&](std::string const &name, std::string const &suffix)
	{
		std::filesystem::path const p(stem(name));
		return ((p.is_absolute() ? p : std::filesystem::path(shader_dir) / p).string()) + suffix;
	};
	auto result = std::make_shared<MetalProgram>();
	read_map(path(vertex, ".msl.map"), result->vertex_stage);
	read_map(path(fragment, ".msl.map"), result->fragment_stage);
	result->vertex = load_function(*context, path(vertex, std::string(".") + metal_sdk() + ".metallib"));
	result->fragment = load_function(*context, path(fragment, std::string(".") + metal_sdk() + ".metallib"));
	programs[key] = result;
	return result;
}

MetalRenderer::MetalRenderer(int width, int height, std::string const &shader_dir, MetalContextPtr const &context):
	impl_(new Impl),
	width_(width),
	height_(height)
{
	if (!context)
		throw std::invalid_argument("a Metal renderer needs a context");
	if (width <= 0 || height <= 0)
		throw std::invalid_argument("a renderer of " + std::to_string(width) + " x " + std::to_string(height));
	Impl &impl = *impl_;
	impl.context = context;
	impl.shader_dir = shader_dir;
	impl.slots = dispatch_semaphore_create(2);
	id<MTLDevice> device = context->impl().device;
	impl.frame_constants = [device newBufferWithLength:FRAME_CONSTANTS_SIZE options:MTLResourceStorageModeShared];
	impl.zeros = [device newBufferWithLength:16384 options:MTLResourceStorageModeShared];
	impl.default_floats = [device newBufferWithLength:16 options:MTLResourceStorageModeShared];
	impl.default_ints = [device newBufferWithLength:16 options:MTLResourceStorageModeShared];
	for (id<MTLBuffer> buffer : {impl.frame_constants, impl.zeros, impl.default_floats, impl.default_ints})
	{
		if (!buffer)
			throw std::runtime_error("Metal could not make the renderer's buffers");
		std::memset(buffer.contents, 0, buffer.length);
	}
	static_cast<float *>(impl.default_floats.contents)[3] = 1.0f;       // (an attribute a vertex lacks reads (0, 0, 0, 1), as in GL)

	auto const sampler = [&](MTLSamplerMinMagFilter filter, MTLSamplerMipFilter mip, MTLSamplerAddressMode address, bool compare, NSUInteger anisotropy = 1)
	{
		MTLSamplerDescriptor *descriptor = [[MTLSamplerDescriptor alloc] init];
		descriptor.minFilter = descriptor.magFilter = filter;
		descriptor.mipFilter = mip;
		descriptor.sAddressMode = descriptor.tAddressMode = descriptor.rAddressMode = address;
		descriptor.maxAnisotropy = anisotropy;
		if (compare)
			descriptor.compareFunction = MTLCompareFunctionLessEqual;
		id<MTLSamplerState> state = [device newSamplerStateWithDescriptor:descriptor];
		if (!state)
			throw std::runtime_error("Metal could not make a sampler state");
		return state;
	};
	// what the Vulkan renderer's descriptors have: the material's sampler repeats, the global textures' is clamped; the 32-bit float
	// formats are filtered only on some devices, so they are read by the nearest texel
	// (the material's linear one is anisotropic, 8 as Vulkan's)
	impl.material_linear = sampler(MTLSamplerMinMagFilterLinear, MTLSamplerMipFilterLinear, MTLSamplerAddressModeRepeat, false, 8);
	impl.material_nearest = sampler(MTLSamplerMinMagFilterNearest, MTLSamplerMipFilterNearest, MTLSamplerAddressModeRepeat, false);
	impl.global_linear = sampler(MTLSamplerMinMagFilterLinear, MTLSamplerMipFilterNotMipmapped, MTLSamplerAddressModeClampToEdge, false);
	impl.global_nearest = sampler(MTLSamplerMinMagFilterNearest, MTLSamplerMipFilterNotMipmapped, MTLSamplerAddressModeClampToEdge, false);
	impl.compare = sampler(MTLSamplerMinMagFilterLinear, MTLSamplerMipFilterNotMipmapped, MTLSamplerAddressModeClampToEdge, true);

	for (bool write : {false, true})
	{
		MTLDepthStencilDescriptor *descriptor = [[MTLDepthStencilDescriptor alloc] init];
		descriptor.depthCompareFunction = MTLCompareFunctionLessEqual;
		descriptor.depthWriteEnabled = write;
		(write ? impl.depth_write : impl.depth_read) = [device newDepthStencilStateWithDescriptor:descriptor];
	}
	impl.timer.reset(new MetalGpuTimer(context));
}

namespace
{
void commit(MetalRenderer::Impl &impl);
}

MetalRenderer::~MetalRenderer()
{
	// a frame left open (an exception went through the host's frame): its pass ended and the frame handed over, so that its slot is given back
	// when the GPU is done with it and the wait for the slots in ~Impl() ends
	if (impl_->encoder)
	{
		[impl_->encoder endEncoding];
		impl_->encoder = nil;
	}
	if (impl_->commands)
		commit(*impl_);
	// the shown frame is dropped, and the target and the textures go before the context
	default_render_target_.reset();
	impl_->white_texture.reset();
	for (auto &texture : impl_->global_textures)
		texture.reset();
}

int MetalRenderer::screen_width() const { return width_; }
int MetalRenderer::screen_height() const { return height_; }
MetalContextPtr const &MetalRenderer::context() const { return impl_->context; }
uint64_t MetalRenderer::frame_serial() const { return impl_->frame_serial; }
uint64_t MetalRenderer::previous_block_writes() const { return impl_->previous_block_writes; }
GpuTimer &MetalRenderer::gpu_timer() const { return *impl_->timer; }

void MetalRenderer::resize(int width, int height)
{
	if (width <= 0 || height <= 0)
		throw std::invalid_argument("a renderer of " + std::to_string(width) + " x " + std::to_string(height));
	width_ = width;
	height_ = height;
	default_render_target_.reset();     // made again at the new size when asked for
}

void MetalRenderer::do_begin_frame()
{
	Impl &impl = *impl_;
	dispatch_semaphore_wait(impl.slots, DISPATCH_TIME_FOREVER);
	try
	{
		impl.context->throw_gpu_error();
	}
	catch (...)
	{
		dispatch_semaphore_signal(impl.slots);
		throw;
	}
	++impl.frame_serial;
	impl.context->set_current_frame(impl.frame_serial);
	impl.arenas[impl.frame_serial % 3].reset();
	impl.commands = [impl.context->impl().queue commandBuffer];
	impl.timer->begin_frame(impl.frame_serial);
}

namespace
{

void commit(MetalRenderer::Impl &impl)
{
	id<MTLCommandBuffer> commands = impl.commands;
	impl.commands = nil;
	MetalContextPtr const context = impl.context;
	uint64_t const serial = impl.frame_serial;
	dispatch_semaphore_t slots = impl.slots;
	impl.timer->end_frame((__bridge void *)commands);
	[commands addCompletedHandler:^(id<MTLCommandBuffer> done) {
		if (done.error)
			context->report_gpu_error([done.error.localizedDescription UTF8String]);
		context->frame_completed(serial);
		dispatch_semaphore_signal(slots);
	}];
	[commands commit];
}

}

void MetalRenderer::do_submit_frame()
{
	commit(*impl_);
}

void MetalRenderer::do_finish_frame()
{
	Impl &impl = *impl_;
	id<MTLCommandBuffer> commands = impl.commands;
	commit(impl);
	[commands waitUntilCompleted];
	impl.context->frame_completed(impl.frame_serial);
	impl.timer->frame_finished();
	if (commands.error)
		throw std::runtime_error(std::string("Metal: the frame failed: ") + [commands.error.localizedDescription UTF8String]);
	impl.context->throw_gpu_error();
}

void MetalRenderer::do_begin_pass(RenderTargetPtr const &target, RenderPassClearState const &clear, uint32_t)
{
	Impl &impl = *impl_;
	auto *metal_target = dynamic_cast<MetalRenderTarget *>(target.get());
	if (!metal_target || metal_target->context() != impl.context)
		throw std::invalid_argument("a Metal render pass into a target of another renderer");
	int const layer = std::max(metal_target->layer_index(), 0);
	MTLRenderPassDescriptor *pass = [MTLRenderPassDescriptor renderPassDescriptor];
	auto const& colors = metal_target->colors();
	for (size_t i = 0; i < colors.size(); ++i)
	{
		MTLRenderPassColorAttachmentDescriptor *attachment = pass.colorAttachments[i];
		attachment.texture = colors[i]->impl().texture;
		attachment.slice = layer;
		attachment.loadAction = clear.clear_color ? MTLLoadActionClear : MTLLoadActionLoad;
		attachment.clearColor = MTLClearColorMake(clear.clear_color_r, clear.clear_color_g, clear.clear_color_b, clear.clear_color_a);
		attachment.storeAction = MTLStoreActionStore;
		colors[i]->used();
	}
	MetalTexture &depth = *metal_target->depth();
	pass.depthAttachment.texture = depth.impl().texture;
	pass.depthAttachment.slice = depth.array_layers() > 0 ? layer : 0;       // (the depth of a target of layers that nobody reads is one image)
	pass.depthAttachment.loadAction = clear.clear_depth ? MTLLoadActionClear : MTLLoadActionLoad;
	pass.depthAttachment.clearDepth = clear.clear_depth_value;
	pass.depthAttachment.storeAction = MTLStoreActionStore;
	depth.used();
	impl.timer->begin_pass((__bridge void *)pass);
	impl.encoder = [impl.commands renderCommandEncoderWithDescriptor:pass];
	if (!impl.encoder)
		throw std::runtime_error("Metal gives no render encoder");
	impl.target = metal_target;
}

void MetalRenderer::do_end_pass()
{
	[impl_->encoder endEncoding];
	impl_->encoder = nil;
	impl_->target = nullptr;
}

void MetalRenderer::do_set_viewport(int x, int y, int width, int height)
{
	MTLViewport viewport = {double(x), double(y), double(width), double(height), 0.0, 1.0};
	[impl_->encoder setViewport:viewport];
	[impl_->encoder setScissorRect:MTLScissorRect{NSUInteger(x), NSUInteger(y), NSUInteger(width), NSUInteger(height)}];
}

VertexBufferPtr MetalRenderer::create_vertex_buffer(VertexTraits const *vertex_traits, size_t size, BufferUsage)
{
	return std::make_shared<MetalVertexBuffer>(impl_->context, vertex_traits, size);
}

IndexBufferPtr MetalRenderer::create_index_buffer(IndexFormat index_format, size_t size, BufferUsage)
{
	return std::make_shared<MetalIndexBuffer>(impl_->context, index_format, size);
}

TexturePtr MetalRenderer::create_texture(int width, int height, PixelFormat pixel_format, TextureUsage const &usage)
{
	return std::make_shared<MetalTexture>(impl_->context, width, height, pixel_format, usage);
}

void MetalRenderer::do_set_frame_constants(void const *data, size_t size)
{
	Impl &impl = *impl_;
	// A new buffer each time: the frames in flight keep the one they were encoded with.  The whole block: what this one does not cover is
	// zero, as on Vulkan, not what an earlier, longer one left.
	id<MTLBuffer> block = [impl.context->impl().device newBufferWithLength:FRAME_CONSTANTS_SIZE options:MTLResourceStorageModeShared];
	if (!block)
		throw std::runtime_error("Metal could not make the frame constants");
	std::memset(block.contents, 0, FRAME_CONSTANTS_SIZE);
	if (size)
		std::memcpy(block.contents, data, size);
	impl.frame_constants = block;
}

void MetalRenderer::do_set_global_texture(unsigned slot, TexturePtr const &texture, bool compare)
{
	if (texture)
	{
		auto *metal_texture = dynamic_cast<MetalTexture *>(texture.get());
		if (!metal_texture || metal_texture->context() != impl_->context)
			throw std::invalid_argument("a Metal global texture must be a texture of this renderer");
	}
	impl_->global_textures[slot] = texture;
	impl_->global_compare[slot] = compare;
}

RenderTargetPtr MetalRenderer::default_render_target()
{
	if (!default_render_target_)
		default_render_target_ = create_render_target(width_, height_, PixelFormat::A8R8G8B8);
	return default_render_target_;
}

MaterialPtr MetalRenderer::create_material()
{
	return std::make_shared<Material>(shared_from_this());
}

RenderTargetPtr MetalRenderer::do_create_render_target(int width, int height, RenderTargetFormat const &format)
{
	Impl &impl = *impl_;
	// (Apple GPUs: 16384 texels a side, 2048 layers)
	if (format.layers > 2048)
		throw std::invalid_argument("a render target of " + std::to_string(format.layers) + " layers: this device has at most 2048");
	if (format.cube && width > 16384)
		throw std::invalid_argument("a cube render target of " + std::to_string(width) + " pixel faces: this device's are at most 16384");
	if (width > 16384 || height > 16384)
		throw std::invalid_argument("a render target of " + std::to_string(width) + " x " + std::to_string(height)
			+ ": this device's are at most 16384 a side");
	MetalTexture::Layers layers;
	layers.count = format.cube ? 6 : format.layers;
	layers.cube = format.cube;
	std::vector<std::shared_ptr<MetalTexture>> colors;
	for (PixelFormat pixel_format : format.color_formats)
		colors.push_back(layers.count ? std::make_shared<MetalTexture>(impl.context, width, height, pixel_format, layers)
			: std::make_shared<MetalTexture>(impl.context, width, height, pixel_format, TextureUsage()));
	auto depth = format.readable_depth
		? std::make_shared<MetalTexture>(impl.context, width, height, MetalTexture::Depth(), layers, true) : nullptr;
	return std::make_shared<MetalRenderTarget>(impl.context, width, height, colors, depth);
}

namespace
{

/// Draws @p texture over the window layer's next drawable, stretched to @p window_width x @p window_height; swap_window() presents it.
void show_texture(MetalRenderer::Impl &impl, id<MTLTexture> texture, int window_width, int window_height)
{
	CAMetalLayer *layer = impl.context->impl().layer;
	id<MTLDevice> device = impl.context->impl().device;
	if (!impl.show_pipeline)
	{
		NSError *error = nil;
		id<MTLLibrary> library = [device newLibraryWithSource:[NSString stringWithUTF8String:SHOW_SOURCE] options:nil error:&error];
		if (!library)
			throw std::runtime_error(std::string("Metal: show_frame's shaders do not compile: ") + [error.localizedDescription UTF8String]);
		MTLRenderPipelineDescriptor *descriptor = [[MTLRenderPipelineDescriptor alloc] init];
		descriptor.vertexFunction = [library newFunctionWithName:@"show_vertex"];
		descriptor.fragmentFunction = [library newFunctionWithName:@"show_fragment"];
		descriptor.colorAttachments[0].pixelFormat = layer.pixelFormat;
		impl.show_pipeline = [device newRenderPipelineStateWithDescriptor:descriptor error:&error];
		if (!impl.show_pipeline)
			throw std::runtime_error(std::string("Metal: show_frame's pipeline: ") + [error.localizedDescription UTF8String]);
		MTLSamplerDescriptor *sampler = [[MTLSamplerDescriptor alloc] init];
		sampler.minFilter = sampler.magFilter = MTLSamplerMinMagFilterLinear;
		sampler.sAddressMode = sampler.tAddressMode = MTLSamplerAddressModeClampToEdge;
		impl.show_sampler = [device newSamplerStateWithDescriptor:sampler];
	}
	layer.drawableSize = CGSizeMake(window_width, window_height);
	id<CAMetalDrawable> drawable = [layer nextDrawable];
	if (!drawable)
		throw std::runtime_error("Metal: the window layer gives no drawable");
	MTLRenderPassDescriptor *pass = [MTLRenderPassDescriptor renderPassDescriptor];
	pass.colorAttachments[0].texture = drawable.texture;
	pass.colorAttachments[0].loadAction = MTLLoadActionDontCare;
	pass.colorAttachments[0].storeAction = MTLStoreActionStore;
	id<MTLCommandBuffer> commands = [impl.context->impl().queue commandBuffer];
	id<MTLRenderCommandEncoder> encoder = [commands renderCommandEncoderWithDescriptor:pass];
	[encoder setRenderPipelineState:impl.show_pipeline];
	[encoder setFragmentTexture:texture atIndex:0];
	[encoder setFragmentSamplerState:impl.show_sampler atIndex:0];
	[encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
	[encoder endEncoding];
	impl.shown_commands = commands;
	impl.shown_drawable = drawable;
}

}

void MetalRenderer::show_frame(void const *frame, int width, int height, int window_width, int window_height)
{
	Impl &impl = *impl_;
	if (!impl.context->hosted())
		throw std::logic_error("Metal show_frame without a host window");
	if (in_frame())
		throw std::logic_error("Metal show_frame inside a frame");
	if (width <= 0 || height <= 0 || window_width <= 0 || window_height <= 0)
		throw std::invalid_argument("a frame of " + std::to_string(width) + " x " + std::to_string(height) + " shown in a window of "
			+ std::to_string(window_width) + " x " + std::to_string(window_height));
	if (!impl.frame_texture || impl.frame_texture.width != NSUInteger(width) || impl.frame_texture.height != NSUInteger(height))
	{
		MTLTextureDescriptor *descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
			width:width height:height mipmapped:NO];
		descriptor.usage = MTLTextureUsageShaderRead;
		descriptor.storageMode = MTLStorageModeShared;
		impl.frame_texture = [impl.context->impl().device newTextureWithDescriptor:descriptor];
	}
	// (a new texture while the last frame shown may still be read: bytes are B, G, R, X, which BGRA8 samples as they lie)
	[impl.frame_texture replaceRegion:MTLRegionMake2D(0, 0, width, height) mipmapLevel:0 withBytes:frame bytesPerRow:size_t(width) * 4];
	show_texture(impl, impl.frame_texture, window_width, window_height);
}

void MetalRenderer::show_frame(TexturePtr const &frame, int window_width, int window_height)
{
	Impl &impl = *impl_;
	if (!impl.context->hosted())
		throw std::logic_error("Metal show_frame without a host window");
	if (in_frame())
		throw std::logic_error("Metal show_frame inside a frame");
	auto *texture = dynamic_cast<MetalTexture *>(frame.get());
	if (!texture || texture->context() != impl.context)
		throw std::invalid_argument("Metal show_frame: a texture of this renderer's");
	if (window_width <= 0 || window_height <= 0)
		throw std::invalid_argument("a frame shown in a window of " + std::to_string(window_width) + " x " + std::to_string(window_height));
	texture->used();
	show_texture(impl, texture->impl().texture, window_width, window_height);
}

void MetalRenderer::swap_window(double min_interval)
{
	Impl &impl = *impl_;
	if (!impl.context->hosted())
		throw std::logic_error("Metal swap_window without a host window");
	if (!impl.shown_commands)
		throw std::logic_error("Metal swap_window without a frame shown");
	if (min_interval < 0)
		throw std::invalid_argument("Metal swap_window: a negative interval");
	if (min_interval > 0)
		[impl.shown_commands presentDrawable:impl.shown_drawable afterMinimumDuration:min_interval];
	else
		[impl.shown_commands presentDrawable:impl.shown_drawable];
	[impl.shown_commands commit];
	impl.shown_commands = nil;
	impl.shown_drawable = nil;
}

}
