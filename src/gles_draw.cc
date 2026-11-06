// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>

#include "camera.h"
#include "gles_buffer.h"
#include "gles_renderer.h"
#include "material.h"
#include "shader_program.h"
#include "vertex_traits.h"
#include "draw_call.h"
#include "shader_interface.h"

namespace edge
{

namespace
{

GLenum blend_factor(Material::BlendMode mode)
{
	switch (mode)
	{
	case Material::BlendMode::ZERO:          return GL_ZERO;
	case Material::BlendMode::ONE:           return GL_ONE;
	case Material::BlendMode::SRC_ALPHA:     return GL_SRC_ALPHA;
	case Material::BlendMode::INV_SRC_ALPHA: return GL_ONE_MINUS_SRC_ALPHA;
	case Material::BlendMode::DEST_ALPHA:    return GL_DST_ALPHA;
	case Material::BlendMode::INV_DEST_ALPHA:return GL_ONE_MINUS_DST_ALPHA;
	}
	throw std::logic_error("blend mode " + std::to_string(static_cast<int>(mode)));
}

}

void GLESRenderer::do_draw(DrawCall const &call)
{

	auto *vertex_buffer = dynamic_cast<GLESVertexBuffer *>(call.vertex_buffer().get());
	if (!vertex_buffer)
		throw std::logic_error("a GLES draw call needs a GLESVertexBuffer");
	if (!call.material())
		throw std::logic_error("a GLES draw call needs a material");
	Material const &material = *call.material();
	if (material.stencil_write())
		throw std::logic_error("GLES backend: stencil writes are not implemented yet");
	VertexTraits const &traits = *vertex_buffer->vertex_traits();

	// The shaders: the material's own, or the built-in ones the vertex's make-up picks, as the Vulkan renderer does
	shader::Shaders const shaders = shader::shaders(material.shader_program(pass_type()).get(), traits);
	auto const program = this->program(shaders.vertex, shaders.fragment);

	// The material's state.  The image is drawn upside down (see GLESRenderer), which turns a face that runs counter-clockwise on the
	// screen into one that runs clockwise in GL's window.  The front faces are set whether or not they are culled: gl_FrontFacing
	// says them (CullMode::NONE's are CullMode::CCW's, as on Vulkan).
	glFrontFace(material.cull_mode() == Material::CullMode::CW ? GL_CCW : GL_CW);
	if (material.cull_mode() == Material::CullMode::NONE)
	{
		glDisable(GL_CULL_FACE);
	}
	else
	{
		glEnable(GL_CULL_FACE);
		glCullFace(GL_BACK);
	}
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);
	glDepthMask(material.zwrite_enabled() ? GL_TRUE : GL_FALSE);
	if (material.alpha_blend_enabled())
	{
		GLenum const src = blend_factor(material.src_blend_mode());
		GLenum const dest = blend_factor(material.dest_blend_mode());
		glEnable(GL_BLEND);
		if (material.preserve_destination_alpha())
			glBlendFuncSeparate(src, dest, GL_ZERO, GL_ONE);
		else
			glBlendFuncSeparate(src, dest, src, dest);
	}
	else
	{
		glDisable(GL_BLEND);
	}

	glUseProgram(program->id);
	rove::matrix<4,4> const &world_view_projection = call.shader_world_view_projection(), &world = call.shader_world();
	bind_draw_constants(world_view_projection, world, call.shader_previous_world_view_projection(), call.shader_previous_world());
	if (program->reads_material_parameters)
	{
		// the whole block, zeros past what the material sets (and all of it for one without)
		unsigned char block[Material::PARAMETERS_SIZE] = {};
		std::vector<unsigned char> const &parameters = material.parameters();
		std::copy(parameters.begin(), parameters.end(), block);
		glBindBuffer(GL_UNIFORM_BUFFER, material_parameters_buffer_);
		glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(block), block);
	}
	std::vector<rove::matrix<4,4>> const *const joint_matrices = call.joint_matrices();
	if (joint_matrices)
	{
		if (joint_matrices->size() > MAX_JOINT_MATRICES)
			throw std::length_error(std::to_string(joint_matrices->size()) + " joint matrices, at most "
				+ std::to_string(MAX_JOINT_MATRICES));
		glBindBuffer(GL_UNIFORM_BUFFER, joint_matrices_buffer_);
		glBufferSubData(GL_UNIFORM_BUFFER, 0, static_cast<GLsizeiptr>(joint_matrices->size() * sizeof(rove::matrix<4,4>)),
			joint_matrices->data());
		if (program->reads_previous_joint_matrices)
		{
			glBindBuffer(GL_UNIFORM_BUFFER, previous_joint_matrices_buffer_);
			glBufferSubData(GL_UNIFORM_BUFFER, 0, static_cast<GLsizeiptr>(joint_matrices->size() * sizeof(rove::matrix<4,4>)),
				call.previous_joint_matrices()->data());
			++previous_block_writes_;
		}
	}

	// the push constants, each member a uniform
	shader::PushConstants const push = shader::push_constants(material, traits, call.is_uber(), joint_matrices != nullptr,
		call.camera()->eye_point(), call.is_instanced(), !call.has_previous());
	if (program->extra_parameters >= 0)
		glUniform4fv(program->extra_parameters, Material::EXTRA_PARAMETERS_COUNT, &push.extra_parameters[0][0]);
	if (program->camera_eye >= 0)
		glUniform4fv(program->camera_eye, 1, push.camera_eye);
	if (program->push_base_color_factor >= 0)
		glUniform4fv(program->push_base_color_factor, 1, push.base_color_factor);
	if (program->material_pbr >= 0)
		glUniform4fv(program->material_pbr, 1, push.material_pbr);
	if (program->emissive_factor >= 0)
		glUniform3fv(program->emissive_factor, 1, push.emissive_factor);
	if (program->flags >= 0)
		glUniform1ui(program->flags, push.flags);
	for (auto const &sampler : program->samplers)
		bind_texture(material, sampler.binding, sampler.unit);
	if (program->world_view_projection >= 0)
		glUniformMatrix4fv(program->world_view_projection, 1, GL_FALSE, &world_view_projection.ij[0][0]);
	if (program->world >= 0)
		glUniformMatrix4fv(program->world, 1, GL_FALSE, &world.ij[0][0]);
	if (program->base_color_factor >= 0)
	{
		rove::vec<4> const &color = material.base_color_factor();
		glUniform4f(program->base_color_factor, color.x, color.y, color.z, color.w);
	}

	glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer->get());
	bool enabled[shader::ATTRIBUTE_LOCATIONS] = {};
	for (VertexElement const &element : traits.elements())
	{
		int const location = shader::attribute_location(element);
		auto const *offset = reinterpret_cast<void const *>(static_cast<uintptr_t>(element.offset));
		auto const stride = static_cast<GLsizei>(traits.size());
		glEnableVertexAttribArray(location);
		switch (element.type)
		{
		case VertexElement::Type::FLOAT2: glVertexAttribPointer(location, 2, GL_FLOAT, GL_FALSE, stride, offset); break;
		case VertexElement::Type::FLOAT3: glVertexAttribPointer(location, 3, GL_FLOAT, GL_FALSE, stride, offset); break;
		case VertexElement::Type::FLOAT4: glVertexAttribPointer(location, 4, GL_FLOAT, GL_FALSE, stride, offset); break;
		// four unsigned bytes read as integers (a shader's uvec4: the joints), as Vulkan's R8G8B8A8_UINT
		case VertexElement::Type::UBYTE4: glVertexAttribIPointer(location, 4, GL_UNSIGNED_BYTE, stride, offset); break;
		default:
			throw std::logic_error("GLES backend: vertex element type " + std::to_string(static_cast<unsigned>(element.type)) + " is not implemented yet");
		}
		enabled[location] = true;
	}
	for (int location = 0; location < shader::ATTRIBUTE_LOCATIONS; ++location)
		if (!enabled[location])
			glDisableVertexAttribArray(location);

	GLESIndexBuffer *index_buffer = nullptr;
	if (call.index_buffer())
	{
		index_buffer = dynamic_cast<GLESIndexBuffer *>(call.index_buffer().get());
		if (!index_buffer)
			throw std::logic_error("a GLES indexed draw call needs a GLESIndexBuffer");
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, index_buffer->get());
	}
	// one draw, or the instances in batches of MAX_INSTANCES, each batch's matrices in the block at binding 7, the frame before's at 21
	size_t const instances = call.instance_count();
	for (size_t first = 0; first < instances; first += MAX_INSTANCES)
	{
		GLsizei const batch = static_cast<GLsizei>(std::min(MAX_INSTANCES, instances - first));
		if (call.is_instanced())
		{
			glBindBuffer(GL_UNIFORM_BUFFER, instances_buffer_);
			glBufferSubData(GL_UNIFORM_BUFFER, 0, static_cast<GLsizeiptr>(batch * sizeof(rove::matrix<4,4>)),
				call.visible_instances().data() + first);
			if (program->reads_previous_instances)
			{
				glBindBuffer(GL_UNIFORM_BUFFER, previous_instances_buffer_);
				glBufferSubData(GL_UNIFORM_BUFFER, 0, static_cast<GLsizeiptr>(batch * sizeof(rove::matrix<4,4>)),
					call.previous_visible_instances().data() + first);
				++previous_block_writes_;
			}
		}
		if (index_buffer)
		{
			glDrawElementsInstanced(GL_TRIANGLES, static_cast<GLsizei>(call.triangles_count() * 3), index_buffer->index_type(),
				reinterpret_cast<void const *>(static_cast<uintptr_t>(call.first_index_offset() * index_buffer->index_size())),
				batch);
		}
		else
		{
			glDrawArraysInstanced(GL_TRIANGLES, static_cast<GLint>(call.minimal_vertex_index()),
				static_cast<GLsizei>(call.triangles_count() * 3), batch);
		}
	}
	check_gl_often("a draw call");
}

}
