// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

// GltfReader's images, textures and materials (gltf_reader.h).

#include "gltf_reader.h"

#include <cmath>
#include <exception>
#include <fstream>
#include <variant>

#include "log.h"
#include "material.h"
#include "texture_io.h"

namespace edge
{

std::optional<std::size_t> GltfReader::resolve_texture_image_index(std::size_t texture_index) const
{
	if (texture_index >= asset_.textures.size())
	{
		return std::nullopt;
	}

	fastgltf::Texture const &texture = asset_.textures[texture_index];
	if (texture.ddsImageIndex)       // MSFT_texture_dds: block-compressed, mips included
	{
		return static_cast<std::size_t>(*texture.ddsImageIndex);
	}
	if (texture.imageIndex)
	{
		return static_cast<std::size_t>(*texture.imageIndex);
	}
	if (texture.basisuImageIndex)
	{
		return static_cast<std::size_t>(*texture.basisuImageIndex);
	}
	if (texture.webpImageIndex)
	{
		return static_cast<std::size_t>(*texture.webpImageIndex);
	}

	return std::nullopt;
}

fastgltf::span<const std::byte> GltfReader::image_encoded_data(std::size_t image_index)
{
	fastgltf::Image const &image = asset_.images[image_index];
	return std::visit(fastgltf::visitor {
		[&](fastgltf::sources::Array const &array) -> fastgltf::span<const std::byte>
		{
			return fastgltf::span<const std::byte>(array.bytes.data(), array.bytes.size_bytes());
		},
		[&](fastgltf::sources::Vector const &vector) -> fastgltf::span<const std::byte>
		{
			return fastgltf::span<const std::byte>(vector.bytes.data(), vector.bytes.size());
		},
		[&](fastgltf::sources::ByteView const &byte_view) -> fastgltf::span<const std::byte>
		{
			return byte_view.bytes;
		},
		[&](fastgltf::sources::BufferView const &buffer_view) -> fastgltf::span<const std::byte>
		{
			return buffer_view_data(buffer_view.bufferViewIndex);
		},
		[&](auto const &) -> fastgltf::span<const std::byte>
		{
			throw load_error("glTF '" + filename_ + "' image " + std::to_string(image_index) + ": a source edge cannot read");
		}
	}, image.data);
}

std::vector<std::byte> GltfReader::image_bytes(std::size_t image_index, std::string &source)
{
	if (auto const *uri = std::get_if<fastgltf::sources::URI>(&asset_.images[image_index].data))
	{
		if (!uri->uri.isLocalPath() || uri->uri.fspath().empty())
		{
			throw std::runtime_error("its URI '" + std::string(uri->uri.string()) + "' is not a file");
		}
		std::filesystem::path path = uri->uri.fspath();
		if (path.is_relative())
		{
			path = basepath_ / path;
		}
		path = path.lexically_normal();
		EDGE_LOG_INFO("glTF image[{}]: file {}", image_index, path.string());
		std::ifstream in(path, std::ios::binary | std::ios::ate);
		if (!in)
		{
			throw std::runtime_error("texture file " + path.string() + " cannot be read");
		}
		std::vector<std::byte> all(static_cast<std::size_t>(std::streamsize(in.tellg())));
		in.seekg(0);
		if (!in.read(reinterpret_cast<char *>(all.data()), std::streamsize(all.size())))
		{
			throw std::runtime_error("short read of " + path.string());
		}
		source = path.string();
		return all;
	}
	fastgltf::span<const std::byte> const bytes = image_encoded_data(image_index);
	EDGE_LOG_INFO("glTF image[{}]: embedded ({} bytes)", image_index, bytes.size());
	source = "image " + std::to_string(image_index) + " of " + filename_;
	return std::vector<std::byte>(bytes.begin(), bytes.end());
}

std::int64_t GltfReader::load_image_texture(std::size_t image_index, bool srgb)
{
	check_index(image_index, asset_.images.size(), "image");
	auto const key = std::make_pair(image_index, srgb);
	auto const cached = image_texture_cache_.find(key);
	if (cached != image_texture_cache_.end())
	{
		return cached->second;
	}

	TextureDecoding how;
	how.compress = options_.compress_textures;
	how.srgb = srgb && options_.srgb_color;
	how.mipmaps = options_.mipmaps;
	how.keep_cpu_copy = options_.keep_cpu_copy;

	std::string const context = "glTF '" + filename_ + "' image " + std::to_string(image_index);
	SerializedTexture texture;
	try
	{
		std::string source;
		std::vector<std::byte> const bytes = image_bytes(image_index, source);
		texture = decode_texture(bytes.data(), bytes.size(), how);
		if (std::holds_alternative<fastgltf::sources::URI>(asset_.images[image_index].data))
		{
			texture.filename = source;
		}
	}
	catch (load_error const &)
	{
		throw;
	}
	catch (std::exception const &e)
	{
		throw load_error(context + ": " + e.what());
	}
	std::int64_t const index = std::int64_t(model_.textures.size());
	model_.textures.push_back(std::move(texture));
	return image_texture_cache_[key] = index;
}

std::int64_t GltfReader::load_occlusion(std::size_t material_index, std::optional<std::size_t> metallic_roughness, std::size_t occlusion)
{
	std::string const context = "glTF '" + filename_ + "' material " + std::to_string(material_index) + " '" +
		std::string(asset_.materials[material_index].name) + "'";
	auto const image_of = [&](std::size_t texture_index, char const *what) {
		check_index(texture_index, asset_.textures.size(), "texture");
		std::optional<std::size_t> const image = resolve_texture_image_index(texture_index);
		if (!image)
		{
			throw load_error(context + ": its " + what + " texture " + std::to_string(texture_index) + " has no image");
		}
		check_index(*image, asset_.images.size(), "image");
		return *image;
	};
	std::size_t const occlusion_image = image_of(occlusion, "occlusion");
	std::optional<std::size_t> const metallic_roughness_image = metallic_roughness ? std::optional<std::size_t>(image_of(*metallic_roughness,
		"metallic-roughness")) : std::nullopt;
	if (metallic_roughness_image == occlusion_image)
	{
		return load_image_texture(occlusion_image, false);      // glTF's ORM: packed already
	}
	auto const key = std::make_pair(metallic_roughness_image ? std::int64_t(*metallic_roughness_image) : std::int64_t(-1), occlusion_image);
	auto const cached = occlusion_texture_cache_.find(key);
	if (cached != occlusion_texture_cache_.end())
	{
		return cached->second;
	}

	TextureDecoding how;
	how.compress = options_.compress_textures;
	how.srgb = false;
	how.mipmaps = options_.mipmaps;
	how.keep_cpu_copy = options_.keep_cpu_copy;
	SerializedTexture texture;
	std::string occlusion_source, metallic_roughness_source;
	try
	{
		std::vector<std::byte> const occlusion_bytes = image_bytes(occlusion_image, occlusion_source);
		std::vector<std::byte> metallic_roughness_bytes;
		if (metallic_roughness_image)
		{
			metallic_roughness_bytes = image_bytes(*metallic_roughness_image, metallic_roughness_source);
		}
		texture = pack_occlusion(metallic_roughness_image ? metallic_roughness_bytes.data() : nullptr, metallic_roughness_bytes.size(),
			occlusion_bytes.data(), occlusion_bytes.size(), how);
	}
	catch (std::exception const &e)
	{
		throw load_error(context + ": its occlusion (" + (occlusion_source.empty() ? "image " + std::to_string(occlusion_image) : occlusion_source) +
			") cannot be packed into the red of its metallic-roughness texture: " + e.what() +
			" (GltfOptions::occlusion = false, edge_convert --no-occlusion, leaves occlusion out)");
	}
	texture.filename = occlusion_source + (metallic_roughness_image ? " in " + metallic_roughness_source : std::string());
	EDGE_LOG_INFO("{}: occlusion of {} packed into a texture of {} x {}", context, texture.filename, texture.width, texture.height);
	std::int64_t const index = std::int64_t(model_.textures.size());
	model_.textures.push_back(std::move(texture));
	return occlusion_texture_cache_[key] = index;
}

std::int64_t GltfReader::load_texture(std::size_t texture_index, bool srgb)
{
	check_index(texture_index, asset_.textures.size(), "texture");
	std::optional<std::size_t> const image_index = resolve_texture_image_index(texture_index);
	if (!image_index)
	{
		throw load_error("glTF '" + filename_ + "' texture " + std::to_string(texture_index) + " has no image");
	}
	return load_image_texture(*image_index, srgb);
}

void GltfReader::build_materials()
{
	gltf_materials_.assign(asset_.materials.size(), -1);
	skin_cache_.assign(asset_.skins.size(), PreparedSkin());
	skin_prepared_.assign(asset_.skins.size(), false);
	image_texture_cache_.clear();
	occlusion_texture_cache_.clear();

	EDGE_LOG_INFO("glTF '{}': {} material(s), {} texture(s), {} image(s)", filename_,
		asset_.materials.size(), asset_.textures.size(), asset_.images.size());

	for (std::size_t material_index = 0; material_index < asset_.materials.size(); ++material_index)
	{
		fastgltf::Material const &gltf_material = asset_.materials[material_index];

		// a new Material's settings (SerializedMaterial's), then the glTF's
		SerializedMaterial material;
		material.name = gltf_material.name.empty() ? ("gltf_material_" + std::to_string(material_index)) : std::string(gltf_material.name.c_str());
		material.cull_mode = static_cast<int>(gltf_material.doubleSided ? Material::CullMode::NONE : Material::CullMode::CCW);

		{
			auto const &cf = gltf_material.pbrData.baseColorFactor;
			EDGE_LOG_INFO("glTF material[{}] '{}': alpha={} doubleSided={} baseColorFactor=({},{},{},{}) "
				"baseColorTex={} normalTex={}",
				material_index, material.name,
				gltf_material.alphaMode == fastgltf::AlphaMode::Blend ? "blend" :
				gltf_material.alphaMode == fastgltf::AlphaMode::Mask ? "mask" : "opaque",
				gltf_material.doubleSided, cf[0], cf[1], cf[2], cf[3],
				gltf_material.pbrData.baseColorTexture ? "yes" : "no",
				gltf_material.normalTexture ? "yes" : "no");
			material.base_color_factor = rove::vec<4>(static_cast<float>(cf[0]), static_cast<float>(cf[1]), static_cast<float>(cf[2]),
				static_cast<float>(cf[3]));
		}

		material.metallic_factor = static_cast<float>(gltf_material.pbrData.metallicFactor);
		material.roughness_factor = static_cast<float>(gltf_material.pbrData.roughnessFactor);
		{
			float const strength = static_cast<float>(gltf_material.emissiveStrength);
			auto const &e = gltf_material.emissiveFactor;
			material.emissive_factor = rove::vec<3>(static_cast<float>(e[0]) * strength, static_cast<float>(e[1]) * strength,
				static_cast<float>(e[2]) * strength);
		}
		if (gltf_material.alphaMode == fastgltf::AlphaMode::Mask)
		{
			material.alpha_cutoff = static_cast<float>(gltf_material.alphaCutoff);
		}
		std::optional<std::size_t> const metallic_roughness = gltf_material.pbrData.metallicRoughnessTexture
			? std::optional<std::size_t>(gltf_material.pbrData.metallicRoughnessTexture->textureIndex) : std::nullopt;
		if (gltf_material.occlusionTexture && options_.occlusion)
		{
			float const strength = static_cast<float>(gltf_material.occlusionTexture->strength);
			if (!(strength >= 0.0f && strength <= 1.0f))
			{
				throw load_error("glTF '" + filename_ + "' material " + std::to_string(material_index) + " '" + material.name +
					"': an occlusion strength of " + std::to_string(strength) + ", not 0 to 1 (GltfOptions::occlusion = false, " +
					"edge_convert --no-occlusion, leaves occlusion out)");
			}
			material.occlusion = true;
			material.occlusion_strength = strength;
			material.metallic_roughness = load_occlusion(material_index, metallic_roughness,
				static_cast<std::size_t>(gltf_material.occlusionTexture->textureIndex));
		}
		else if (metallic_roughness)
		{
			material.metallic_roughness = load_texture(*metallic_roughness, false);
		}
		if (gltf_material.emissiveTexture)
		{
			material.emissive = load_texture(static_cast<std::size_t>(gltf_material.emissiveTexture->textureIndex), true);
		}

		if (gltf_material.alphaMode == fastgltf::AlphaMode::Blend)
		{
			material.alpha_blend_enabled = true;
			material.src_blend_mode = static_cast<int>(Material::BlendMode::SRC_ALPHA);
			material.dest_blend_mode = static_cast<int>(Material::BlendMode::INV_SRC_ALPHA);
		}

		if (gltf_material.pbrData.baseColorTexture)
		{
			material.diffuse_textures.push_back(load_texture(static_cast<std::size_t>(gltf_material.pbrData.baseColorTexture->textureIndex), true));
		}
		else
		{
			EDGE_LOG_INFO("glTF material[{}] '{}': no base color texture", material_index, material.name);
		}

		if (gltf_material.normalTexture)
		{
			material.normal_map = load_texture(static_cast<std::size_t>(gltf_material.normalTexture->textureIndex), false);
			material.normal_scale = static_cast<float>(gltf_material.normalTexture->scale);
		}

		gltf_materials_[material_index] = std::int64_t(model_.materials.size());
		model_.materials.push_back(std::move(material));
	}
}

std::int64_t GltfReader::default_material()
{
	if (default_material_ < 0)
	{
		SerializedMaterial material;
		material.name = "gltf_default";
		default_material_ = std::int64_t(model_.materials.size());
		model_.materials.push_back(std::move(material));
		EDGE_LOG_INFO("glTF '{}': a primitive without a material has glTF's default one", filename_);
	}
	return default_material_;
}

std::int64_t GltfReader::material_for_primitive(fastgltf::Primitive const &primitive)
{
	if (primitive.materialIndex)
	{
		std::size_t const material_index = static_cast<std::size_t>(*primitive.materialIndex);
		check_index(material_index, gltf_materials_.size(), "material");
		return gltf_materials_[material_index];
	}
	return default_material();
}

bool GltfReader::UvSet::operator==(UvSet const &other) const
{
	return index == other.index && offset[0] == other.offset[0] && offset[1] == other.offset[1] && scale[0] == other.scale[0] &&
		scale[1] == other.scale[1] && rotation == other.rotation;
}

bool GltfReader::UvSet::moves() const
{
	return offset[0] != 0 || offset[1] != 0 || scale[0] != 1 || scale[1] != 1 || rotation != 0;
}

void GltfReader::UvSet::apply(float &u, float &v) const
{
	float const c = std::cos(rotation), s = std::sin(rotation);
	float const su = scale[0] * u, sv = scale[1] * v;
	u = offset[0] + c * su + s * sv;
	v = offset[1] - s * su + c * sv;
}

GltfReader::UvSet GltfReader::material_uv_set(std::optional<std::size_t> const &material_index) const
{
	UvSet result;
	if (!material_index)
	{
		return result;
	}
	fastgltf::Material const &material = asset_.materials.at(*material_index);
	auto read = [&](fastgltf::TextureInfo const &info, char const *slot)
	{
		UvSet set;
		set.textured = true;
		set.index = info.texCoordIndex;
		if (info.transform)
		{
			fastgltf::TextureTransform const &t = *info.transform;
			if (t.texCoordIndex)
				set.index = *t.texCoordIndex;
			set.offset[0] = static_cast<float>(t.uvOffset[0]);
			set.offset[1] = static_cast<float>(t.uvOffset[1]);
			set.scale[0] = static_cast<float>(t.uvScale[0]);
			set.scale[1] = static_cast<float>(t.uvScale[1]);
			set.rotation = static_cast<float>(t.rotation);
		}
		if (result.textured && !(set == result))
		{
			throw load_error("glTF '" + filename_ + "': material " + std::to_string(*material_index) + " '" + std::string(material.name) +
				"': its " + slot + " reads other UVs (TEXCOORD_" + std::to_string(set.index) + ", moved or not) than its other textures (TEXCOORD_" +
				std::to_string(result.index) + "), and a mesh has one set of them" +
				(std::string(slot) == "occlusion texture" ? " (GltfOptions::occlusion = false, edge_convert --no-occlusion, leaves occlusion out)" : ""));
		}
		result = set;
	};
	if (material.pbrData.baseColorTexture)
		read(*material.pbrData.baseColorTexture, "base colour texture");
	if (material.pbrData.metallicRoughnessTexture)
		read(*material.pbrData.metallicRoughnessTexture, "metallic-roughness texture");
	if (material.normalTexture)
		read(*material.normalTexture, "normal map");
	if (material.emissiveTexture)
		read(*material.emissiveTexture, "emissive texture");
	if (material.occlusionTexture && options_.occlusion)
		read(*material.occlusionTexture, "occlusion texture");
	return result;
}

}
