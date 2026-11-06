// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include "metal_renderer_impl.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>

#include "camera.h"
#include "draw_call.h"
#include "material.h"
#include "pixel.h"
#include "shader_interface.h"
#include "shader_program.h"
#include "vertex_traits.h"

namespace edge
{

namespace
{

/// Where the vertex buffer's attributes a shader reads that the vertex has none of come from: buffers of constants, read with a step of 0.
constexpr unsigned DEFAULT_FLOATS_BUFFER = 28, DEFAULT_INTS_BUFFER = 29;

MTLBlendFactor blend_factor(Material::BlendMode mode)
{
	switch (mode)
	{
	case Material::BlendMode::ZERO:          return MTLBlendFactorZero;
	case Material::BlendMode::ONE:           return MTLBlendFactorOne;
	case Material::BlendMode::SRC_ALPHA:     return MTLBlendFactorSourceAlpha;
	case Material::BlendMode::INV_SRC_ALPHA: return MTLBlendFactorOneMinusSourceAlpha;
	case Material::BlendMode::DEST_ALPHA:    return MTLBlendFactorDestinationAlpha;
	case Material::BlendMode::INV_DEST_ALPHA:return MTLBlendFactorOneMinusDestinationAlpha;
	}
	throw std::logic_error("blend mode " + std::to_string(static_cast<int>(mode)));
}

MTLVertexFormat default_format(MTLDataType type)
{
	switch (type)
	{
	case MTLDataTypeFloat:  return MTLVertexFormatFloat;
	case MTLDataTypeFloat2: return MTLVertexFormatFloat2;
	case MTLDataTypeFloat3: return MTLVertexFormatFloat3;
	case MTLDataTypeFloat4: return MTLVertexFormatFloat4;
	case MTLDataTypeUInt:   return MTLVertexFormatUInt;
	case MTLDataTypeUInt2:  return MTLVertexFormatUInt2;
	case MTLDataTypeUInt3:  return MTLVertexFormatUInt3;
	case MTLDataTypeUInt4:  return MTLVertexFormatUInt4;
	case MTLDataTypeInt:    return MTLVertexFormatInt;
	case MTLDataTypeInt2:   return MTLVertexFormatInt2;
	case MTLDataTypeInt3:   return MTLVertexFormatInt3;
	case MTLDataTypeInt4:   return MTLVertexFormatInt4;
	default:
		throw std::logic_error("Metal backend: a vertex attribute of Metal data type " + std::to_string(type) + " is not implemented yet");
	}
}

bool is_integer(MTLDataType type)
{
	switch (type)
	{
	case MTLDataTypeUInt: case MTLDataTypeUInt2: case MTLDataTypeUInt3: case MTLDataTypeUInt4:
	case MTLDataTypeInt: case MTLDataTypeInt2: case MTLDataTypeInt3: case MTLDataTypeInt4:
		return true;
	default:
		return false;
	}
}

}

id<MTLRenderPipelineState> MetalRenderer::Impl::pipeline(MetalProgram const &program, VertexTraits const &traits,
	MetalRenderTarget const &target, Material const &material)
{
	MetalPipelineKey key{&program, &traits, {}, MTLPixelFormatDepth32Float, 0};
	key.colors.fill(MTLPixelFormatInvalid);
	for (size_t i = 0; i < target.colors().size(); ++i)
		key.colors[i] = static_cast<int>(target.colors()[i]->impl().format);
	bool const blend = material.alpha_blend_enabled();
	if (blend)
		key.blend = 1 + static_cast<int>(material.src_blend_mode()) * 16 + static_cast<int>(material.dest_blend_mode()) * 256
			+ (material.preserve_destination_alpha() ? 4096 : 0);
	auto found = pipelines.find(key);
	if (found != pipelines.end())
		return found->second;

	MTLRenderPipelineDescriptor *descriptor = [[MTLRenderPipelineDescriptor alloc] init];
	descriptor.vertexFunction = program.vertex;
	descriptor.fragmentFunction = program.fragment;
	descriptor.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
	for (size_t i = 0; i < target.colors().size(); ++i)
	{
		MTLRenderPipelineColorAttachmentDescriptor *attachment = descriptor.colorAttachments[i];
		attachment.pixelFormat = static_cast<MTLPixelFormat>(key.colors[i]);
		if (blend)
		{
			MTLBlendFactor const src = blend_factor(material.src_blend_mode()), dest = blend_factor(material.dest_blend_mode());
			attachment.blendingEnabled = YES;
			attachment.sourceRGBBlendFactor = src;
			attachment.destinationRGBBlendFactor = dest;
			attachment.sourceAlphaBlendFactor = material.preserve_destination_alpha() ? MTLBlendFactorZero : src;
			attachment.destinationAlphaBlendFactor = material.preserve_destination_alpha() ? MTLBlendFactorOne : dest;
		}
	}

	// the vertex: the buffer's elements at the shader locations, and for every attribute the shader reads that it lacks a constant
	MTLVertexDescriptor *vertex = [MTLVertexDescriptor vertexDescriptor];
	bool provided[shader::ATTRIBUTE_LOCATIONS] = {};
	for (VertexElement const &element : traits.elements())
	{
		int const location = shader::attribute_location(element);
		MTLVertexFormat format;
		switch (element.type)
		{
		case VertexElement::Type::FLOAT2: format = MTLVertexFormatFloat2; break;
		case VertexElement::Type::FLOAT3: format = MTLVertexFormatFloat3; break;
		case VertexElement::Type::FLOAT4: format = MTLVertexFormatFloat4; break;
		// four unsigned bytes read as integers (a shader's uvec4: the joints), as Vulkan's R8G8B8A8_UINT
		case VertexElement::Type::UBYTE4: format = MTLVertexFormatUChar4; break;
		default:
			throw std::logic_error("Metal backend: vertex element type " + std::to_string(static_cast<unsigned>(element.type)) + " is not implemented yet");
		}
		vertex.attributes[location].format = format;
		vertex.attributes[location].offset = element.offset;
		vertex.attributes[location].bufferIndex = MetalRenderer::VERTEX_BUFFER;
		provided[location] = true;
	}
	vertex.layouts[MetalRenderer::VERTEX_BUFFER].stride = traits.size();
	vertex.layouts[MetalRenderer::VERTEX_BUFFER].stepFunction = MTLVertexStepFunctionPerVertex;
	for (MTLVertexAttribute *attribute in program.vertex.vertexAttributes)
	{
		NSUInteger const location = attribute.attributeIndex;
		if (!attribute.active || (location < shader::ATTRIBUTE_LOCATIONS && provided[location]))
			continue;
		bool const integer = is_integer(attribute.attributeType);
		vertex.attributes[location].format = default_format(attribute.attributeType);
		vertex.attributes[location].offset = 0;
		vertex.attributes[location].bufferIndex = integer ? DEFAULT_INTS_BUFFER : DEFAULT_FLOATS_BUFFER;
		MTLVertexBufferLayoutDescriptor *layout = vertex.layouts[integer ? DEFAULT_INTS_BUFFER : DEFAULT_FLOATS_BUFFER];
		layout.stride = 16;
		layout.stepFunction = MTLVertexStepFunctionConstant;
		layout.stepRate = 0;
	}
	descriptor.vertexDescriptor = vertex;

	NSError *error = nil;
	id<MTLRenderPipelineState> state = [context->impl().device newRenderPipelineStateWithDescriptor:descriptor error:&error];
	if (!state)
		throw std::runtime_error(std::string("Metal: a render pipeline cannot be made: ") + [error.localizedDescription UTF8String]);
	pipelines[key] = state;
	return state;
}

void MetalRenderer::do_draw(DrawCall const &call)
{
	Impl &impl = *impl_;
	auto *vertex_buffer = dynamic_cast<MetalVertexBuffer *>(call.vertex_buffer().get());
	if (!vertex_buffer || vertex_buffer->context() != impl.context)
		throw std::invalid_argument("a Metal draw call needs a MetalVertexBuffer of its renderer");
	if (!call.material())
		throw std::logic_error("a Metal draw call needs a material");
	Material const &material = *call.material();
	if (material.stencil_write())
		throw std::logic_error("Metal backend: stencil writes are not implemented yet");
	VertexTraits const &traits = *vertex_buffer->vertex_traits();
	MetalIndexBuffer *index_buffer = nullptr;
	if (call.index_buffer())
	{
		index_buffer = dynamic_cast<MetalIndexBuffer *>(call.index_buffer().get());
		if (!index_buffer || index_buffer->context() != impl.context)
			throw std::invalid_argument("a Metal indexed draw call needs a MetalIndexBuffer of its renderer");
	}

	// The shaders: the material's own, or the built-in ones the vertex's make-up picks, as the Vulkan renderer does
	shader::Shaders const shaders = shader::shaders(material.shader_program(pass_type()).get(), traits);
	std::shared_ptr<MetalProgram> const program = impl.program(*this, shaders.vertex, shaders.fragment);
	id<MTLRenderCommandEncoder> encoder = impl.encoder;
	[encoder setRenderPipelineState:impl.pipeline(*program, traits, *impl.target, material)];

	// The faces drawn are the front ones (CullMode::NONE's are CullMode::CCW's, as on Vulkan: gl_FrontFacing says them either way)
	[encoder setFrontFacingWinding:material.cull_mode() == Material::CullMode::CW ? MTLWindingClockwise : MTLWindingCounterClockwise];
	[encoder setCullMode:material.cull_mode() == Material::CullMode::NONE ? MTLCullModeNone : MTLCullModeBack];
	[encoder setDepthStencilState:material.zwrite_enabled() ? impl.depth_write : impl.depth_read];

	// the uniform blocks, each at the index the map says; the arena is this frame's
	MetalArena &arena = impl.arenas[impl.frame_serial % 3];
	id<MTLDevice> device = impl.context->impl().device;
	struct Block { id<MTLBuffer> buffer; size_t offset; };
	auto const from_arena = [&](void const *data, size_t size, size_t block_size)
	{
		MetalArena::Piece const piece = arena.allocate(device, block_size);
		std::memset(piece.pointer, 0, block_size);
		if (size)
			std::memcpy(piece.pointer, data, size);
		return Block{piece.buffer, piece.offset};
	};
	Block const zeros{impl.zeros, 0};
	rove::matrix<4,4> const &world_view_projection = call.shader_world_view_projection(), &world = call.shader_world();
	float constants[64];
	std::memcpy(constants, &world_view_projection.ij[0][0], 16 * sizeof(float));
	std::memcpy(constants + 16, &world.ij[0][0], 16 * sizeof(float));
	std::memcpy(constants + 32, &call.shader_previous_world_view_projection().ij[0][0], 16 * sizeof(float));
	std::memcpy(constants + 48, &call.shader_previous_world().ij[0][0], 16 * sizeof(float));
	Block const draw_constants = from_arena(constants, sizeof(constants), sizeof(constants));
	std::vector<unsigned char> const &parameters = material.parameters();
	Block const material_parameters = parameters.empty() ? zeros
		: from_arena(parameters.data(), parameters.size(), Material::PARAMETERS_SIZE);
	std::vector<rove::matrix<4,4>> const *const joint_matrices = call.joint_matrices();
	Block joints = zeros, previous_joints = zeros;
	if (joint_matrices)
	{
		if (joint_matrices->size() > MAX_JOINT_MATRICES)
			throw std::length_error(std::to_string(joint_matrices->size()) + " joint matrices, at most "
				+ std::to_string(MAX_JOINT_MATRICES));
		size_t const bytes = joint_matrices->size() * sizeof(rove::matrix<4,4>);
		joints = from_arena(joint_matrices->data(), bytes, MAX_JOINT_MATRICES * sizeof(rove::matrix<4,4>));
		if (program->reads(shader::BINDING_PREVIOUS_JOINT_MATRICES))
		{
			previous_joints = from_arena(call.previous_joint_matrices()->data(), bytes, MAX_JOINT_MATRICES * sizeof(rove::matrix<4,4>));
			++impl.previous_block_writes;
		}
	}

	// the push constants, one buffer
	shader::PushConstants const push = shader::push_constants(material, traits, call.is_uber(), joint_matrices != nullptr,
		call.camera()->eye_point(), call.is_instanced(), !call.has_previous());
	if (program->vertex_stage.push)
		[encoder setVertexBytes:&push length:sizeof(push) atIndex:PUSH_BUFFER];
	if (program->fragment_stage.push)
		[encoder setFragmentBytes:&push length:sizeof(push) atIndex:PUSH_BUFFER];

	// the textures, each with the sampler state of its binding
	auto const bind_textures = [&](MetalProgram::Stage const &stage, bool vertex_stage)
	{
		for (MetalProgram::Sampler const &sampler : stage.samplers)
		{
			int const binding = sampler.texture;
			TexturePtr texture;
			bool global = false;
			if (binding >= 0 && binding < 4)
				texture = static_cast<size_t>(binding) < material.diffuse_textures_count() ? material.diffuse_texture(binding) : nullptr;
			else if (binding == 4)
				texture = material.texture_blending_weights_texture();
			else if (binding == 5)
				texture = material.normal_map_texture();
			else if (binding == 10)
				texture = material.metallic_roughness_texture();
			else if (binding == 11)
				texture = material.emissive_texture();
			else if (binding >= 12 && binding < 12 + static_cast<int>(GLOBAL_TEXTURES_COUNT))
			{
				texture = impl.global_textures[binding - 12];
				global = true;
			}
			else
				throw std::logic_error("a texture at binding " + std::to_string(binding) + ", which has none");
			if (!texture)
			{
				if (!impl.white_texture)
				{
					impl.white_texture = create_texture(1, 1, PixelFormat::A8R8G8B8);
					auto texels = impl.white_texture->write(Contents::DISCARD);
					*texels.as<uint32_t>().data() = 0xFFFFFFFFu;
					texels.unmap();
				}
				texture = impl.white_texture;
			}
			auto *metal_texture = dynamic_cast<MetalTexture *>(texture.get());
			if (!metal_texture || metal_texture->context() != impl.context)
				throw std::invalid_argument("a Metal draw needs textures of its renderer");
			metal_texture->used();
			// a depth is read by the nearest texel; a shadow texture compared, then filtered; 32-bit floats are not filtered
			bool const compare = global && impl.global_compare[binding - 12];
			bool const nearest = metal_texture->is_depth() || metal_texture->pixel_format() == PixelFormat::R32G32B32A32F
				|| metal_texture->pixel_format() == PixelFormat::R32F;
			id<MTLSamplerState> state = compare ? impl.compare
				: global || metal_texture->is_depth() ? (nearest ? impl.global_nearest : impl.global_linear)
				: (nearest ? impl.material_nearest : impl.material_linear);
			if (vertex_stage)
			{
				[encoder setVertexTexture:metal_texture->impl().texture atIndex:binding];
				[encoder setVertexSamplerState:state atIndex:sampler.sampler];
			}
			else
			{
				[encoder setFragmentTexture:metal_texture->impl().texture atIndex:binding];
				[encoder setFragmentSamplerState:state atIndex:sampler.sampler];
			}
		}
	};
	bind_textures(program->vertex_stage, true);
	bind_textures(program->fragment_stage, false);

	// the vertex buffer, and the constants for what it lacks
	vertex_buffer->store().used();
	[encoder setVertexBuffer:vertex_buffer->store().impl().buffer offset:0 atIndex:VERTEX_BUFFER];
	[encoder setVertexBuffer:impl.default_floats offset:0 atIndex:DEFAULT_FLOATS_BUFFER];
	[encoder setVertexBuffer:impl.default_ints offset:0 atIndex:DEFAULT_INTS_BUFFER];
	if (index_buffer)
		index_buffer->store().used();

	auto const bind_blocks = [&](Block const &instances, Block const &previous_instances)
	{
		for (bool vertex_stage : {true, false})
		{
			MetalProgram::Stage const &stage = vertex_stage ? program->vertex_stage : program->fragment_stage;
			for (int index : stage.ubos)
			{
				Block block;
				switch (index)
				{
				case shader::BINDING_JOINT_MATRICES:          block = joints; break;
				case shader::BINDING_INSTANCES:               block = instances; break;
				case shader::BINDING_DRAW_CONSTANTS:          block = draw_constants; break;
				case shader::BINDING_FRAME_CONSTANTS:         block = {impl.frame_constants, 0}; break;
				case shader::BINDING_MATERIAL_PARAMETERS:     block = material_parameters; break;
				case shader::BINDING_PREVIOUS_INSTANCES:      block = previous_instances; break;
				case shader::BINDING_PREVIOUS_JOINT_MATRICES: block = previous_joints; break;
				default:
					throw std::logic_error("a uniform block at binding " + std::to_string(index) + ", which has none");
				}
				if (vertex_stage)
					[encoder setVertexBuffer:block.buffer offset:block.offset atIndex:index];
				else
					[encoder setFragmentBuffer:block.buffer offset:block.offset atIndex:index];
			}
		}
	};

	// one draw, or the instances in batches of MAX_INSTANCES, each batch's matrices in the block at binding 7, the frame before's at 21
	size_t const instances = call.instance_count();
	for (size_t first = 0; first < instances; first += MAX_INSTANCES)
	{
		size_t const batch = std::min(MAX_INSTANCES, instances - first);
		Block batch_instances = zeros, batch_previous = zeros;
		if (call.is_instanced())
		{
			size_t const bytes = batch * sizeof(rove::matrix<4,4>);
			batch_instances = from_arena(call.visible_instances().data() + first, bytes, MAX_INSTANCES * sizeof(rove::matrix<4,4>));
			if (program->reads(shader::BINDING_PREVIOUS_INSTANCES))
			{
				batch_previous = from_arena(call.previous_visible_instances().data() + first, bytes,
					MAX_INSTANCES * sizeof(rove::matrix<4,4>));
				++impl.previous_block_writes;
			}
		}
		bind_blocks(batch_instances, batch_previous);
		if (index_buffer)
		{
			[encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle indexCount:call.triangles_count() * 3
				indexType:index_buffer->index_size() == 2 ? MTLIndexTypeUInt16 : MTLIndexTypeUInt32
				indexBuffer:index_buffer->store().impl().buffer
				indexBufferOffset:call.first_index_offset() * index_buffer->index_size()
				instanceCount:batch];
		}
		else
		{
			[encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:call.minimal_vertex_index()
				vertexCount:call.triangles_count() * 3 instanceCount:batch];
		}
	}
}

}
