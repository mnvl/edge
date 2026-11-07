// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include "serialized_model.h"

#include <charconv>
#include <cstddef>
#include <cstring>
#include <string>

#include "pixel.h"
#include "renderer.h"
#include "texture.h"

namespace edge {

namespace
{

/// A GenericVertex component: its format bit, where it is in the vertex and how long.
struct VertexComponent
{
	GenericVertex::format_type bit;
	std::size_t offset;
	std::size_t size;
};

#define EDGE_VERTEX_COMPONENT(bit, member) VertexComponent{GenericVertex::bit, offsetof(GenericVertex, member), sizeof(GenericVertex::member)}
/// In the order of their bits, which is the order a packed vertex has them in.
std::array<VertexComponent, 9> const VERTEX_COMPONENTS = {
	EDGE_VERTEX_COMPONENT(FORMAT_POSITION, position),
	EDGE_VERTEX_COMPONENT(FORMAT_TANGENT, tangent),
	EDGE_VERTEX_COMPONENT(FORMAT_BINORMAL, binormal),
	EDGE_VERTEX_COMPONENT(FORMAT_NORMAL, normal),
	EDGE_VERTEX_COMPONENT(FORMAT_JOINT_INDICES, joints),
	EDGE_VERTEX_COMPONENT(FORMAT_JOINT_WEIGHTS, weights),
	EDGE_VERTEX_COMPONENT(FORMAT_TEXCOORDS, texcoords),
	EDGE_VERTEX_COMPONENT(FORMAT_TEXTURE_BLEND_WEIGHTS, texture_blend_weights),
	EDGE_VERTEX_COMPONENT(FORMAT_COLOR, color),
};
#undef EDGE_VERTEX_COMPONENT

std::size_t packed_vertex_size(GenericVertex::format_type format)
{
	std::size_t size = 0;
	for (VertexComponent const &component : VERTEX_COMPONENTS)
	{
		if (format & component.bit)
		{
			size += component.size;
		}
	}
	return size;
}

/// The bytes of a texture's CPU copy: level 0, or every level the texture was given.
size_t texels_size(int width, int height, PixelFormat pixel_format, int provided_mip_levels)
{
	return Texture::levels_size(width, height, pixel_format, std::max(1, provided_mip_levels));
}

}

GenericVertex::format_type serialized_vertex_format(std::string const &serialization_type)
{
	GenericVertex::format_type known = 0;
	for (VertexComponent const &component : VERTEX_COMPONENTS)
	{
		known |= component.bit;
	}
	std::size_t const colon = serialization_type.rfind(':');
	char const *const end = serialization_type.data() + serialization_type.size();
	GenericVertex::format_type format = 0;
	auto const parsed = colon == std::string::npos ? std::from_chars_result{nullptr, std::errc::invalid_argument}
		: std::from_chars(serialization_type.data() + colon + 1, end, format);
	if (parsed.ec != std::errc() || parsed.ptr != end || format == 0 || (format & ~known) != 0)
	{
		throw std::runtime_error("mesh type '" + serialization_type + "' names no vertex format");
	}
	return format;
}

std::vector<std::uint8_t> pack_vertices(std::vector<GenericVertex> const &vertices, GenericVertex::format_type format)
{
	std::vector<std::uint8_t> bytes(vertices.size() * packed_vertex_size(format));
	std::uint8_t *out = bytes.data();
	for (GenericVertex const &vertex : vertices)
	{
		for (VertexComponent const &component : VERTEX_COMPONENTS)
		{
			if (format & component.bit)
			{
				std::memcpy(out, reinterpret_cast<std::uint8_t const *>(&vertex) + component.offset, component.size);
				out += component.size;
			}
		}
	}
	return bytes;
}

std::vector<GenericVertex> unpack_vertices(std::vector<std::uint8_t> const &bytes, GenericVertex::format_type format)
{
	std::size_t const size = packed_vertex_size(format);
	if (bytes.size() % size != 0)
	{
		throw std::runtime_error(std::to_string(bytes.size()) + " bytes of vertices of " + std::to_string(size) + " bytes each");
	}
	std::vector<GenericVertex> vertices(bytes.size() / size);
	std::memset(static_cast<void *>(vertices.data()), 0, vertices.size() * sizeof(GenericVertex));
	std::uint8_t const *in = bytes.data();
	for (GenericVertex &vertex : vertices)
	{
		for (VertexComponent const &component : VERTEX_COMPONENTS)
		{
			if (format & component.bit)
			{
				std::memcpy(reinterpret_cast<std::uint8_t *>(&vertex) + component.offset, in, component.size);
				in += component.size;
			}
		}
	}
	return vertices;
}

SerializedTexture export_texture(TexturePtr const &texture)
{
	SerializedTexture result;
	result.filename = texture->filename();
	result.width = texture->width();
	result.height = texture->height();
	result.pixel_format = texture->pixel_format();
	TextureUsage const &usage = texture->usage();
	result.srgb = usage.srgb;
	result.mipmaps = usage.mipmaps;
	result.provided_mip_levels = usage.provided_mip_levels;

	Mapping<Texture const> texels;
	try
	{
		texture->download();
		texels = std::as_const(*texture).read();
	}
	catch (std::logic_error const &e)
	{
		throw std::runtime_error("texture '" + result.filename + "' has no CPU copy to save (load its model with the CPU copy kept): " + e.what());
	}
	result.pixels.resize(texels_size(result.width, result.height, result.pixel_format, result.provided_mip_levels));
	std::memcpy(result.pixels.data(), texels.data(), result.pixels.size());
	return result;
}

TexturePtr import_texture(SerializedTexture const &serialized, RendererPtr const &renderer_ptr, bool keep_cpu_copy)
{
	if (serialized.width <= 0 || serialized.height <= 0)
	{
		throw std::runtime_error("texture '" + serialized.filename + "' is " + std::to_string(serialized.width) + "x" + std::to_string(serialized.height));
	}
	// (before anything adds up that many levels: a count from a file is any number)
	int full_levels = 1;
	for (int size = std::max(serialized.width, serialized.height); size > 1; size /= 2)
		++full_levels;
	if (serialized.provided_mip_levels < 0 || serialized.provided_mip_levels > full_levels)
	{
		throw std::runtime_error("texture '" + serialized.filename + "' has " + std::to_string(serialized.provided_mip_levels) + " mip levels for " +
			std::to_string(serialized.width) + "x" + std::to_string(serialized.height));
	}
	size_t const need = texels_size(serialized.width, serialized.height, serialized.pixel_format, serialized.provided_mip_levels);
	if (serialized.pixels.size() != need)
	{
		throw std::runtime_error("texture '" + serialized.filename + "' has " + std::to_string(serialized.pixels.size()) + " bytes of texels, " +
			std::to_string(need) + " needed");
	}
	TextureUsage usage;
	usage.srgb = serialized.srgb;
	usage.mipmaps = serialized.mipmaps;
	usage.keep_cpu_copy = keep_cpu_copy;
	usage.provided_mip_levels = serialized.provided_mip_levels;
	TexturePtr texture = renderer_ptr->create_texture(serialized.width, serialized.height, serialized.pixel_format, usage);
	auto texels = texture->write(Contents::DISCARD);
	std::memcpy(texels.data(), serialized.pixels.data(), need);
	texels.unmap();
	texture->set_filename(serialized.filename);
	return texture;
}

std::int64_t TextureTable::add(TexturePtr const &texture)
{
	if (!texture)
	{
		return -1;
	}
	auto const [at, added] = index_.emplace(texture.get(), std::int64_t(textures_.size()));
	if (added)
	{
		textures_.push_back(export_texture(texture));
	}
	return at->second;
}

SerializedMaterial export_material(MaterialPtr const &material_ptr, TextureTable &textures)
{
	SerializedMaterial result;
	result.name = material_ptr->name();
	result.cull_mode = static_cast<int>(material_ptr->cull_mode());
	result.zwrite_enabled = material_ptr->zwrite_enabled();
	result.alpha_blend_enabled = material_ptr->alpha_blend_enabled();
	result.preserve_destination_alpha = material_ptr->preserve_destination_alpha();
	result.src_blend_mode = static_cast<int>(material_ptr->src_blend_mode());
	result.dest_blend_mode = static_cast<int>(material_ptr->dest_blend_mode());
	for (std::size_t i = 0; i < Material::EXTRA_PARAMETERS_COUNT; ++i)
	{
		result.extra_parameters[i] = material_ptr->extra_parameter(i);
	}
	result.base_color_factor = material_ptr->base_color_factor();
	result.metallic_factor = material_ptr->metallic_factor();
	result.roughness_factor = material_ptr->roughness_factor();
	result.emissive_factor = material_ptr->emissive_factor();
	result.alpha_cutoff = material_ptr->alpha_cutoff();
	result.normal_scale = material_ptr->normal_scale();
	result.occlusion = material_ptr->occlusion();
	result.occlusion_strength = material_ptr->occlusion_strength();
	for (std::size_t i = 0; i < material_ptr->diffuse_textures_count(); ++i)
	{
		result.diffuse_textures.push_back(textures.add(material_ptr->diffuse_texture(i)));
	}
	result.texture_blending_weights = textures.add(material_ptr->texture_blending_weights_texture());
	result.normal_map = textures.add(material_ptr->normal_map_texture());
	result.metallic_roughness = textures.add(material_ptr->metallic_roughness_texture());
	result.emissive = textures.add(material_ptr->emissive_texture());
	return result;
}

MaterialPtr import_material(SerializedMaterial const &serialized, std::vector<TexturePtr> const &textures, RendererPtr const &renderer_ptr)
{
	auto const texture = [&](std::int64_t index) -> TexturePtr {
		if (index < 0)
		{
			return TexturePtr();
		}
		if (size_t(index) >= textures.size())
		{
			throw std::runtime_error("material '" + serialized.name + "' uses texture " + std::to_string(index) + " of " + std::to_string(textures.size()));
		}
		return textures[size_t(index)];
	};

	// (before they reach a pipeline: an enum from a file is any number)
	if (serialized.cull_mode < 0 || serialized.cull_mode > static_cast<int>(Material::CullMode::NONE))
		throw std::runtime_error("material '" + serialized.name + "' has cull mode " + std::to_string(serialized.cull_mode));
	int const last_blend_mode = static_cast<int>(Material::BlendMode::INV_DEST_ALPHA);
	if (serialized.src_blend_mode < 0 || serialized.src_blend_mode > last_blend_mode ||
		serialized.dest_blend_mode < 0 || serialized.dest_blend_mode > last_blend_mode)
	{
		throw std::runtime_error("material '" + serialized.name + "' has blend modes " + std::to_string(serialized.src_blend_mode) + " and " +
			std::to_string(serialized.dest_blend_mode));
	}

	MaterialPtr material_ptr = renderer_ptr->create_material();
	material_ptr->set_name(serialized.name);
	material_ptr->set_cull_mode(static_cast<Material::CullMode>(serialized.cull_mode));
	material_ptr->set_zwrite_enabled(serialized.zwrite_enabled);
	material_ptr->set_alpha_blend_enabled(serialized.alpha_blend_enabled);
	material_ptr->set_preserve_destination_alpha(serialized.preserve_destination_alpha);
	material_ptr->set_src_blend_mode(static_cast<Material::BlendMode>(serialized.src_blend_mode));
	material_ptr->set_dest_blend_mode(static_cast<Material::BlendMode>(serialized.dest_blend_mode));
	for (std::size_t i = 0; i < Material::EXTRA_PARAMETERS_COUNT; ++i)
	{
		material_ptr->set_extra_parameter(i, serialized.extra_parameters[i]);
	}
	material_ptr->set_base_color_factor(serialized.base_color_factor);
	material_ptr->set_metallic_factor(serialized.metallic_factor);
	material_ptr->set_roughness_factor(serialized.roughness_factor);
	material_ptr->set_emissive_factor(serialized.emissive_factor);
	material_ptr->set_alpha_cutoff(serialized.alpha_cutoff);
	material_ptr->set_normal_scale(serialized.normal_scale);
	if (serialized.occlusion && serialized.metallic_roughness < 0)
	{
		throw std::runtime_error("material '" + serialized.name + "' has occlusion and no metallic-roughness texture to hold it");
	}
	material_ptr->set_occlusion(serialized.occlusion);
	try
	{
		material_ptr->set_occlusion_strength(serialized.occlusion_strength);      // (the material says what a strength may be)
	}
	catch (std::invalid_argument const &e)
	{
		throw std::runtime_error(e.what());
	}
	for (std::size_t i = 0; i < serialized.diffuse_textures.size(); ++i)
	{
		material_ptr->set_diffuse_texture(texture(serialized.diffuse_textures[i]), i);
	}
	material_ptr->set_texture_blending_weights_texture(texture(serialized.texture_blending_weights));
	material_ptr->set_normal_map_texture(texture(serialized.normal_map));
	material_ptr->set_metallic_roughness_texture(texture(serialized.metallic_roughness));
	material_ptr->set_emissive_texture(texture(serialized.emissive));
	return material_ptr;
}

}  // namespace edge
