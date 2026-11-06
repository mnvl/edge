// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

// SPIR-V (what glslangValidator makes of edge's shaders) to GLSL ES 3.00 and a map of what the OpenGL ES renderer has to bind:
//
//     shader_cross in.spv out.glsl out.map
//
// or, with --msl, to Metal Shading Language and the map of what the Metal renderer binds (the end of this comment):
//
//     shader_cross --msl in.spv out.metal out.map
//
// The GLSL is the shader as SPIRV-Cross writes it for a GL that has y up and clip z from -w to w: the vertex stage's gl_Position is
// turned into that (y flipped, so that the row 0 of a render target is its top as in Vulkan, whose viewport edge flips; z to -w..w).
//
// The map has a line for each thing a shader brings that GL names differently from Vulkan's (set 0, binding N):
//
//     stage vertex|fragment
//     ubo <block name> <binding>            a uniform block: glUniformBlockBinding(program, block, binding)
//     sampler <uniform name> <binding>      a sampler: the texture of that binding, in a unit the renderer picks
//     push <uniform name>                   a member of the push constants, a uniform of its own ("pc.camera_eye")
//
// Vertex inputs and fragment outputs keep their locations (the GLSL has them).  Both have only what the stage uses.
//
// --msl: clip z is Metal's own (0..w, as Vulkan's) and y is not flipped, so the vertex stage is written as the SPIR-V has it.  The map:
//
//     stage vertex|fragment
//     ubo <block name> <buffer index>               set 0 binding N is buffer(N), N below kMslFirstVertexBuffer or from kMslFirstHighUbo
//                                                   to kMslLastHighUbo (the material parameters and the previous frame's blocks: 20 to 22)
//     sampler <name> <texture index> <sampler index>   set 0 binding N is texture(N); the sampler states of the stage are numbered from 0
//                                                       in the order of the bindings (Metal has 16 a stage; the bindings go to 19)
//     push <buffer index>                           the push constants, one buffer: kMslPushBuffer in every shader
//
// The indices are an explicit table (MSLResourceBinding for each resource the stage uses), not SPIRV-Cross's defaults; the vertex
// inputs are [[attribute(location)]], and the renderer puts the vertex buffers from kMslFirstVertexBuffer up.  Another descriptor set,
// a uniform buffer past the range, storage buffers and storage images are errors.

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

#include <spirv_glsl.hpp>
#include <spirv_msl.hpp>

namespace
{

constexpr uint32_t kMslFirstVertexBuffer = 16;     // the uniform buffers are buffer(0..15) and (20..27), the vertex buffers 16..19, the
constexpr uint32_t kMslFirstHighUbo = 20;          // zero-filled attributes 28 and 29, the push constants:
constexpr uint32_t kMslLastHighUbo = 27;
constexpr uint32_t kMslPushBuffer = 30;            // (src/metal_renderer.h says the same)

std::vector<uint32_t> read_words(std::string const &path)
{
	std::ifstream in(path, std::ios::binary);
	if (!in)
		throw std::runtime_error("cannot read " + path);
	std::vector<char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	if (bytes.empty() || bytes.size() % 4)
		throw std::runtime_error(path + " is not SPIR-V (its size is not a multiple of 4)");
	std::vector<uint32_t> words(bytes.size() / 4);
	std::copy(bytes.begin(), bytes.end(), reinterpret_cast<char *>(words.data()));
	return words;
}

// the Metal form: the MSL, and the map in map_text
std::string translate_msl(std::string const &path, std::string &map_text)
{
	spirv_cross::CompilerMSL compiler(read_words(path));
	bool const vertex = compiler.get_execution_model() == spv::ExecutionModelVertex;
	if (!vertex && compiler.get_execution_model() != spv::ExecutionModelFragment)
		throw std::runtime_error(path + ": only vertex and fragment shaders");
	spirv_cross::CompilerMSL::Options options;
	options.set_msl_version(2, 4);                       // iOS 15 and macOS 12 and later
	options.platform = spirv_cross::CompilerMSL::Options::macOS;
	options.enable_decoration_binding = false;
	compiler.set_msl_options(options);
	std::unordered_set<spirv_cross::VariableID> const active = compiler.get_active_interface_variables();
	compiler.set_enabled_interface_variables(active);

	spirv_cross::ShaderResources const resources = compiler.get_shader_resources(active);
	if (!resources.storage_buffers.empty() || !resources.storage_images.empty() || !resources.subpass_inputs.empty())
		throw std::runtime_error(path + ": storage buffers, storage images and subpass inputs are not supported");

	auto const stage = compiler.get_execution_model();
	auto bind = [&](spirv_cross::Resource const &r, uint32_t buffer, uint32_t texture, uint32_t sampler)
	{
		uint32_t const set = compiler.get_decoration(r.id, spv::DecorationDescriptorSet);
		if (set != 0)
			throw std::runtime_error(path + ": " + r.name + " is in descriptor set " + std::to_string(set) + ": only set 0 is supported");
		spirv_cross::MSLResourceBinding b;
		b.stage = stage;
		b.desc_set = 0;
		b.binding = compiler.get_decoration(r.id, spv::DecorationBinding);
		b.msl_buffer = buffer;
		b.msl_texture = texture;
		b.msl_sampler = sampler;
		compiler.add_msl_resource_binding(b);
	};

	std::ostringstream map;
	map << "stage " << (vertex ? "vertex" : "fragment") << "\n";
	for (auto const &ubo : resources.uniform_buffers)
	{
		uint32_t const binding = compiler.get_decoration(ubo.id, spv::DecorationBinding);
		if (binding >= kMslFirstVertexBuffer && (binding < kMslFirstHighUbo || binding > kMslLastHighUbo))
			throw std::runtime_error(path + ": uniform buffer " + ubo.name + " at binding " + std::to_string(binding)
				+ ": must be below " + std::to_string(kMslFirstVertexBuffer) + " or from " + std::to_string(kMslFirstHighUbo) + " to "
				+ std::to_string(kMslLastHighUbo) + " (the vertex buffers start at " + std::to_string(kMslFirstVertexBuffer) + ")");
		bind(ubo, binding, 0, 0);
		map << "ubo " << compiler.get_name(ubo.base_type_id) << " " << binding << "\n";
	}
	std::vector<spirv_cross::Resource> samplers(resources.sampled_images.begin(), resources.sampled_images.end());
	std::sort(samplers.begin(), samplers.end(), [&](auto const &x, auto const &y)
		{ return compiler.get_decoration(x.id, spv::DecorationBinding) < compiler.get_decoration(y.id, spv::DecorationBinding); });
	if (samplers.size() > 16)
		throw std::runtime_error(path + ": " + std::to_string(samplers.size()) + " samplers: Metal has 16 sampler states a stage");
	uint32_t next_sampler = 0;
	for (auto const &sampler : samplers)
	{
		uint32_t const binding = compiler.get_decoration(sampler.id, spv::DecorationBinding);
		bind(sampler, 0, binding, next_sampler);
		map << "sampler " << sampler.name << " " << binding << " " << next_sampler << "\n";
		++next_sampler;
	}
	if (!resources.push_constant_buffers.empty())
	{
		spirv_cross::MSLResourceBinding b;
		b.stage = stage;
		b.desc_set = spirv_cross::kPushConstDescSet;
		b.binding = spirv_cross::kPushConstBinding;
		b.msl_buffer = kMslPushBuffer;
		compiler.add_msl_resource_binding(b);
		map << "push " << kMslPushBuffer << "\n";
	}
	std::string msl = compiler.compile();
	map_text = map.str();
	return msl;
}

}

int main(int argc, char **argv)
{
	bool const msl = argc == 5 && std::string(argv[1]) == "--msl";
	if (argc != 4 && !msl)
	{
		std::cerr << "usage: shader_cross in.spv out.glsl out.map\n       shader_cross --msl in.spv out.metal out.map\n";
		return 2;
	}
	if (msl)
		++argv;                 // the arguments are the same three from here
	try
	{
		if (msl)
		{
			std::string map_text;
			std::string const source = translate_msl(argv[1], map_text);
			std::ofstream(argv[2], std::ios::binary) << source;
			std::ofstream(argv[3], std::ios::binary) << map_text;
			return 0;
		}
		spirv_cross::CompilerGLSL compiler(read_words(argv[1]));
		bool const vertex = compiler.get_execution_model() == spv::ExecutionModelVertex;
		if (!vertex && compiler.get_execution_model() != spv::ExecutionModelFragment)
			throw std::runtime_error(std::string(argv[1]) + ": only vertex and fragment shaders");

		spirv_cross::CompilerGLSL::Options options;
		options.es = true;
		options.version = 300;
		options.vertex.flip_vert_y = vertex;
		options.vertex.fixup_clipspace = vertex;
		options.vertex.support_nonzero_base_instance = false;          // every draw's instances count from 0: gl_InstanceID, no uniform
		options.fragment.default_float_precision = spirv_cross::CompilerGLSL::Options::Highp;
		options.fragment.default_int_precision = spirv_cross::CompilerGLSL::Options::Highp;
		compiler.set_common_options(options);
		// what the stage uses, and nothing it only declares: GL wants a uniform block of one name alike in both stages of a program, so
		// a block one stage declares from an include and never reads (edge.glsl's draw constants in a fragment shader) would have to
		// match the other stage's own declaration of it
		std::unordered_set<spirv_cross::VariableID> const active = compiler.get_active_interface_variables();
		compiler.set_enabled_interface_variables(active);
		std::string const glsl = compiler.compile();

		std::ostringstream map;
		map << "stage " << (vertex ? "vertex" : "fragment") << "\n";
		spirv_cross::ShaderResources const resources = compiler.get_shader_resources(active);
		for (auto const &ubo : resources.uniform_buffers)
			map << "ubo " << compiler.get_name(ubo.base_type_id) << " "
				<< compiler.get_decoration(ubo.id, spv::DecorationBinding) << "\n";
		for (auto const &sampler : resources.sampled_images)
			map << "sampler " << sampler.name << " " << compiler.get_decoration(sampler.id, spv::DecorationBinding) << "\n";
		for (auto const &block : resources.push_constant_buffers)
		{
			spirv_cross::SPIRType const &type = compiler.get_type(block.base_type_id);
			for (uint32_t i = 0; i < type.member_types.size(); ++i)
				map << "push " << block.name << "." << compiler.get_member_name(block.base_type_id, i) << "\n";
		}
		if (!resources.storage_buffers.empty() || !resources.storage_images.empty() || !resources.subpass_inputs.empty())
			throw std::runtime_error(std::string(argv[1]) + ": storage buffers, storage images and subpass inputs have no GLES 3.0 form");

		std::ofstream(argv[2], std::ios::binary) << glsl;
		std::ofstream(argv[3], std::ios::binary) << map.str();
	}
	catch (std::exception const &e)
	{
		std::cerr << "shader_cross: " << e.what() << "\n";
		return 1;
	}
	return 0;
}
