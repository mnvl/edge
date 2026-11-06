// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

#include <array>
#include <cstdint>
#include <unordered_map>
#include <vector>
#include <memory>
#include <optional>
#include <rove/vec.h>
#include "forward.h"
#include "texture.h"
#include "renderer.h"

namespace edge
{

class Material: public std::enable_shared_from_this<Material> {
public:
	Material(Material const &) = delete;
	Material &operator=(Material const &) = delete;
	/// Which faces are drawn: CullMode::CW those whose vertices run clockwise on the screen, CullMode::CCW those that run counter-clockwise (glTF's
	/// front faces: the glTF loader gives a single-sided material CullMode::CCW); the others are culled.  CullMode::NONE draws both.
	/// Which faces are culled: those that run clockwise on the screen (CW), counter-clockwise (CCW), or none.  (The values are those
	/// .edge files keep.)
	enum class CullMode
	{
		CW,
		CCW,
		NONE
	};

	/// What a blend multiplies the source or the destination colour by.
	enum class BlendMode
	{
		ZERO,
		ONE,
		SRC_ALPHA,
		INV_SRC_ALPHA,
		DEST_ALPHA,
		INV_DEST_ALPHA
	};

	/// std::invalid_argument without a renderer.
	explicit Material(RendererPtr const &renderer_ptr);
	virtual ~Material();

	/// A new material with this one's settings, sharing its textures and
	/// shader programs; changing either material leaves the other as it was.
	MaterialPtr clone() const;

	RendererPtr const &renderer() const { return renderer_; }

	enum
	{
		EXTRA_PARAMETERS_COUNT = 4
	};
	/// Bytes the material's own uniform block (parameters) holds at most: 64 vec4s.
	static constexpr size_t PARAMETERS_SIZE = 1024;

	std::string const &name() const { return name_; }
	CullMode cull_mode() const { return cull_mode_; }
	bool zwrite_enabled() const { return zwrite_enabled_; }
	/// The value a fragment of this material writes into the stencil where it passes the depth test (whether or not it writes the depth);
	/// none, the default: the stencil is left as it is.  A target whose depth format has no stencil keeps nothing.
	std::optional<uint8_t> const &stencil_write() const { return stencil_write_; }
	bool alpha_blend_enabled() const { return alpha_blend_enabled_; }
	/// Blending changes the colour channels only: the alpha already in the target (data a later pass reads, say) is left as it is.
	bool preserve_destination_alpha() const { return preserve_destination_alpha_; }
	BlendMode src_blend_mode() const { return src_blend_mode_; }
	BlendMode dest_blend_mode() const { return dest_blend_mode_; }
	ShaderProgramPtr const &shader_program() const { return shader_program_; }

	/// Return the shader program for a specific pass type.
	/// Falls back to the default program (pass type 0) if the
	/// requested pass type has no override.
	ShaderProgramPtr const &shader_program(uint32_t pass_type) const;

	/// Assign a shader program for a specific pass type.
	/// Pass type 0 sets the default program.
	void set_shader_program(uint32_t pass_type, ShaderProgramPtr const &shader_program);

	/// Remove the shader override for a specific pass type.
	/// Pass type 0 clears the default program.
	void clear_shader_program(uint32_t pass_type = 0);
	size_t diffuse_textures_count() const { return diffuse_textures_.size(); }
	TexturePtr diffuse_texture(size_t index = 0) const { return diffuse_textures_.at(index); }
	TexturePtr texture_blending_weights_texture() const { return texture_blending_weights_texture_; }
	TexturePtr normal_map_texture() const {	return normal_map_texture_; }

	// PBR metal/rough (glTF's meaning and defaults).  The uber shader ignores
	// them; a material's own shaders read the textures at bindings 10 (green
	// roughness, blue metallic) and 11, and the factors from the push
	// constants (material_pbr: metallic, roughness, alpha cutoff; and
	// emissive_factor), flags 32 and 64 telling whether the textures are set.
	TexturePtr metallic_roughness_texture() const { return metallic_roughness_texture_; }
	TexturePtr emissive_texture() const { return emissive_texture_; }
	float metallic_factor() const { return metallic_factor_; }
	float roughness_factor() const { return roughness_factor_; }
	/// Emitted radiance, linear RGB, glTF's emissiveFactor times its
	/// KHR_materials_emissive_strength.
	rove::vec<3> const &emissive_factor() const { return emissive_factor_; }
	/// Alpha below which a fragment is discarded (glTF alphaMode MASK); 0 = off.
	float alpha_cutoff() const { return alpha_cutoff_; }
	/// glTF's normalTexture.scale: the normal map's x and y times this (1: as the map has them).
	float normal_scale() const { return normal_scale_; }
	void set_metallic_roughness_texture(TexturePtr const &texture) { metallic_roughness_texture_ = texture; }
	void set_emissive_texture(TexturePtr const &texture) { emissive_texture_ = texture; }
	void set_metallic_factor(float factor) { metallic_factor_ = factor; }
	void set_roughness_factor(float factor) { roughness_factor_ = factor; }
	void set_emissive_factor(rove::vec<3> const &factor) { emissive_factor_ = factor; }
	void set_alpha_cutoff(float cutoff) { alpha_cutoff_ = cutoff; }
	void set_normal_scale(float scale) { normal_scale_ = scale; }
	/// Whether the metallic-roughness texture's red is ambient occlusion (glTF's occlusionTexture, which glTF loading packs there:
	/// glTF's ORM, occlusion red, roughness green, metallic blue), and how much of it the surface takes, glTF's occlusionTexture.strength
	/// from 0 (none) to 1 (all): 1 + strength * (red - 1).  A shader reads both (EDGE_FLAG_OCCLUSION_MAP, EDGE_OCCLUSION_STRENGTH;
	/// edge_material.glsl's surface); the built-in one ignores them.  Drawing a material that has occlusion and no metallic-roughness
	/// texture to hold it is std::logic_error.
	bool occlusion() const { return occlusion_; }
	float occlusion_strength() const { return occlusion_strength_; }
	void set_occlusion(bool occlusion) { occlusion_ = occlusion; }
	/// std::invalid_argument for a strength outside 0 to 1, or not a number.
	void set_occlusion_strength(float strength);
	rove::vec<4> const &extra_parameter(size_t index) const;
	/// The material's own uniform block, which a shader of its own reads at EDGE_BINDING_MATERIAL_PARAMETERS (20), in both stages:
	/// up to PARAMETERS_SIZE bytes (std140 is the caller's business), beyond the four extra parameters' vec4s.  What a block does not
	/// cover reads zeros, all of it for a material without one.  Drawn from the next draw on; a clone takes a copy.  Not kept in a
	/// .edge file.  std::invalid_argument for more than PARAMETERS_SIZE bytes, or for bytes from nowhere; (nullptr, 0) clears it.
	void set_parameters(void const *data, size_t size);
	std::vector<unsigned char> const &parameters() const { return parameters_; }
	rove::vec<4> const &base_color_factor() const { return base_color_factor_; }

	void set_name(std::string const &name) { name_ = name; }
	void set_cull_mode(CullMode cull_mode) { cull_mode_ = cull_mode; }
	void set_zwrite_enabled(bool zwrite_enabled) { zwrite_enabled_ = zwrite_enabled; }
	void set_stencil_write(std::optional<uint8_t> value) { stencil_write_ = value; }
	void set_alpha_blend_enabled(bool alpha_blend_enabled) { alpha_blend_enabled_ = alpha_blend_enabled; }
	void set_preserve_destination_alpha(bool preserve) { preserve_destination_alpha_ = preserve; }
	void set_src_blend_mode(BlendMode src_blend_mode) { src_blend_mode_ = src_blend_mode; }
	void set_dest_blend_mode(BlendMode dest_blend_mode) { dest_blend_mode_ = dest_blend_mode; }
	void set_shader_program(ShaderProgramPtr const &shader_program) { shader_program_ = shader_program; }
	void set_extra_parameter(size_t index, rove::vec<4> const &value);
	void set_base_color_factor(rove::vec<4> const &factor) { base_color_factor_ = factor; }

	/// Diffuse texture @p index (0 is the colour texture, 1 to 3 the texture-blending layers): @p texture, or the one in @p filename.
	void set_diffuse_texture(TexturePtr const &texture, size_t index = 0);
	void set_diffuse_texture(std::string const &filename, size_t index = 0);
	void set_texture_blending_weights_texture(std::string const &filename);
	void set_texture_blending_weights_texture(TexturePtr const &texture);
	void set_normal_map_texture(std::string const &filename);
	void set_normal_map_texture(TexturePtr const &texture);

protected:
	std::string name_;

	RendererPtr renderer_;

	CullMode cull_mode_;
	bool zwrite_enabled_;
	std::optional<uint8_t> stencil_write_;
	bool alpha_blend_enabled_;
	bool preserve_destination_alpha_;
	BlendMode src_blend_mode_, dest_blend_mode_;
	ShaderProgramPtr shader_program_;
	std::unordered_map<uint32_t, ShaderProgramPtr> pass_shader_programs_;
	std::array<rove::vec<4>, EXTRA_PARAMETERS_COUNT> extra_parameters_;
	std::vector<unsigned char> parameters_;

	rove::vec<4> base_color_factor_;

	std::vector<TexturePtr> diffuse_textures_;
	TexturePtr texture_blending_weights_texture_;
	TexturePtr normal_map_texture_;
	TexturePtr metallic_roughness_texture_;
	TexturePtr emissive_texture_;
	float metallic_factor_ = 1.0f;
	float roughness_factor_ = 1.0f;
	rove::vec<3> emissive_factor_ = rove::vec<3>(0.0f, 0.0f, 0.0f);
	float alpha_cutoff_ = 0.0f;
	float normal_scale_ = 1.0f;
	bool occlusion_ = false;
	float occlusion_strength_ = 1.0f;
};

}
