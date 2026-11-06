// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include <string>

#include <stdexcept>

#include "log.h"
#include "material.h"

namespace edge
{

Material::Material(RendererPtr const &renderer_ptr):
	renderer_(renderer_ptr),
	cull_mode_(CullMode::NONE),
	zwrite_enabled_(true),
	alpha_blend_enabled_(false),
	preserve_destination_alpha_(false),
	src_blend_mode_(BlendMode::ONE),
	dest_blend_mode_(BlendMode::ZERO),
	base_color_factor_(1.0f, 1.0f, 1.0f, 1.0f)
{
	if (!renderer_)
	{
		throw std::invalid_argument("a material without a renderer");
	}
	for (size_t i = 0; i < EXTRA_PARAMETERS_COUNT; ++i)
	{
		extra_parameters_[i] = rove::vec<4>(0.0f, 0.0f, 0.0f, 0.0f);
	}
}

Material::~Material() = default;

void Material::set_occlusion_strength(float strength)
{
	if (!(strength >= 0.0f && strength <= 1.0f))
	{
		throw std::invalid_argument("material '" + name_ + "': an occlusion strength of " + std::to_string(strength) + ", not 0 to 1");
	}
	occlusion_strength_ = strength;
}

MaterialPtr Material::clone() const
{
	MaterialPtr copy = std::make_shared<Material>(renderer_);
	copy->name_ = name_;
	copy->cull_mode_ = cull_mode_;
	copy->zwrite_enabled_ = zwrite_enabled_;
	copy->stencil_write_ = stencil_write_;
	copy->alpha_blend_enabled_ = alpha_blend_enabled_;
	copy->preserve_destination_alpha_ = preserve_destination_alpha_;
	copy->src_blend_mode_ = src_blend_mode_;
	copy->dest_blend_mode_ = dest_blend_mode_;
	copy->shader_program_ = shader_program_;
	copy->pass_shader_programs_ = pass_shader_programs_;
	copy->extra_parameters_ = extra_parameters_;
	copy->parameters_ = parameters_;
	copy->base_color_factor_ = base_color_factor_;
	copy->diffuse_textures_ = diffuse_textures_;
	copy->texture_blending_weights_texture_ = texture_blending_weights_texture_;
	copy->normal_map_texture_ = normal_map_texture_;
	copy->metallic_roughness_texture_ = metallic_roughness_texture_;
	copy->emissive_texture_ = emissive_texture_;
	copy->metallic_factor_ = metallic_factor_;
	copy->roughness_factor_ = roughness_factor_;
	copy->emissive_factor_ = emissive_factor_;
	copy->alpha_cutoff_ = alpha_cutoff_;
	copy->normal_scale_ = normal_scale_;
	copy->occlusion_ = occlusion_;
	copy->occlusion_strength_ = occlusion_strength_;
	return copy;
}

void Material::set_parameters(void const *data, size_t size)
{
	if (size > PARAMETERS_SIZE)
		throw std::invalid_argument("material parameters of " + std::to_string(size) + " bytes, at most " + std::to_string(PARAMETERS_SIZE));
	if (size > 0 && !data)
		throw std::invalid_argument("material parameters of " + std::to_string(size) + " bytes from nowhere");
	unsigned char const *bytes = static_cast<unsigned char const *>(data);
	parameters_.assign(bytes, bytes + size);
}

rove::vec<4> const &Material::extra_parameter(size_t index) const
{
	if (index >= EXTRA_PARAMETERS_COUNT)
	{
		throw std::out_of_range("material extra parameter index is out of range");
	}

	return extra_parameters_[index];
}

void Material::set_extra_parameter(size_t index, rove::vec<4> const &value)
{
	if (index >= EXTRA_PARAMETERS_COUNT)
	{
		throw std::out_of_range("material extra parameter index is out of range");
	}

	extra_parameters_[index] = value;
}

ShaderProgramPtr const &Material::shader_program(uint32_t pass_type) const
{
	if (pass_type == 0)
	{
		return shader_program_;
	}
	auto it = pass_shader_programs_.find(pass_type);
	if (it != pass_shader_programs_.end())
	{
		return it->second;
	}
	return shader_program_;
}

void Material::set_shader_program(uint32_t pass_type, ShaderProgramPtr const &shader_program)
{
	if (pass_type == 0)
	{
		shader_program_ = shader_program;
	}
	else
	{
		pass_shader_programs_[pass_type] = shader_program;
	}
}

void Material::clear_shader_program(uint32_t pass_type)
{
	if (pass_type == 0)
	{
		shader_program_ = ShaderProgramPtr();
	}
	else
	{
		pass_shader_programs_.erase(pass_type);
	}
}

void Material::set_diffuse_texture(TexturePtr const &texture, size_t index) {
	if (diffuse_textures_.size() <= index) diffuse_textures_.resize(index + 1);
	diffuse_textures_[index] = texture;
}

void Material::set_diffuse_texture(std::string const &filename, size_t index)
{
	if (diffuse_textures_.size() <= index) diffuse_textures_.resize(index + 1);

	diffuse_textures_[index] = Texture::load_from_file(renderer_, filename);
}

void Material::set_texture_blending_weights_texture(std::string const &filename)
{
	texture_blending_weights_texture_ = Texture::load_from_file(renderer_, filename);
}

void Material::set_texture_blending_weights_texture(TexturePtr const &texture)
{
	texture_blending_weights_texture_ = texture;
}

void Material::set_normal_map_texture(std::string const &filename)
{
	normal_map_texture_ = Texture::load_from_file(renderer_, filename);
}

void Material::set_normal_map_texture(TexturePtr const &texture)
{
	normal_map_texture_ = texture;
}

}
