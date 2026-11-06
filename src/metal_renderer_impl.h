// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

// The renderer's guts, shared by metal_renderer.mm and metal_draw.mm.  Not for a C++ file.

#import <dispatch/dispatch.h>
#include <array>
#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include "metal_gpu_timer.h"
#include "metal_impl.h"
#include "metal_renderer.h"

namespace edge
{

/// A pair of shaders and what tools/shader_cross wrote about each (its --msl map).
struct MetalProgram
{
	struct Sampler
	{
		int texture;      // Vulkan's binding, texture(N)
		int sampler;      // the stage's sampler state number
	};
	struct Stage
	{
		std::vector<int> ubos;       // buffer indices (= Vulkan bindings)
		std::vector<Sampler> samplers;
		bool push = false;
	};
	id<MTLFunction> vertex = nil, fragment = nil;
	Stage vertex_stage, fragment_stage;
	bool reads(int binding) const;
};

/// Where a draw's uniform data goes: the frame's arena of shared memory, 256-byte aligned pieces.
struct MetalArena
{
	struct Piece
	{
		id<MTLBuffer> buffer;
		size_t offset;
		void *pointer;
	};
	std::vector<id<MTLBuffer>> chunks;
	size_t chunk = 0, used = 0;

	Piece allocate(id<MTLDevice> device, size_t size);
	void reset() { chunk = 0; used = 0; }
};

struct MetalPipelineKey
{
	MetalProgram const *program;
	void const *traits;
	std::array<int, 8> colors;
	int depth;
	int blend;            // 0: none; else src, dest and preserve_destination_alpha packed
	bool operator<(MetalPipelineKey const &o) const
	{
		return std::tie(program, traits, colors, depth, blend) < std::tie(o.program, o.traits, o.colors, o.depth, o.blend);
	}
};

struct MetalRenderer::Impl
{
	MetalContextPtr context;
	std::string shader_dir;
	dispatch_semaphore_t slots;
	uint64_t frame_serial = 0;
	id<MTLCommandBuffer> commands = nil;
	id<MTLRenderCommandEncoder> encoder = nil;
	MetalRenderTarget *target = nullptr;
	MetalArena arenas[3];
	id<MTLBuffer> frame_constants = nil, zeros = nil, default_floats = nil, default_ints = nil;
	id<MTLSamplerState> material_linear = nil, material_nearest = nil, global_linear = nil, global_nearest = nil, compare = nil;
	id<MTLDepthStencilState> depth_write = nil, depth_read = nil;
	std::map<std::pair<std::string, std::string>, std::shared_ptr<MetalProgram>> programs;
	std::map<MetalPipelineKey, id<MTLRenderPipelineState>> pipelines;
	TexturePtr white_texture;
	TexturePtr global_textures[Renderer::GLOBAL_TEXTURES_COUNT];
	bool global_compare[Renderer::GLOBAL_TEXTURES_COUNT] = {};
	uint64_t previous_block_writes = 0;
	std::unique_ptr<MetalGpuTimer> timer;
	// show_frame
	id<MTLRenderPipelineState> show_pipeline = nil;
	id<MTLSamplerState> show_sampler = nil;
	id<MTLTexture> frame_texture = nil;
	id<MTLCommandBuffer> shown_commands = nil;
	id<CAMetalDrawable> shown_drawable = nil;

	~Impl();
	std::shared_ptr<MetalProgram> program(Renderer &renderer, std::string const &vertex, std::string const &fragment);
	id<MTLRenderPipelineState> pipeline(MetalProgram const &program, VertexTraits const &traits, MetalRenderTarget const &target,
		Material const &material);
};

}
