// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

#include "image_compress.h"
#include "serialized_model.h"

namespace edge
{

/// Makes the textures of @p model what a game wants of them: every texture of 8 bits a texel becomes BC7 (or ASTC, @p codec) with its whole
/// mip chain (SerializedTexture::provided_mip_levels), as a DDS would bring.  Colour (diffuse, emissive) is judged and averaged as an eye
/// would, as sRGB when the texture is; normal maps are averaged as vectors; everything else (metallic-roughness, blend weights) is numbers.
/// ASTC's block is the use's: colour 6x6 (8x8 for a texture of 128 or less on a side), metallic-roughness 8x8, normal maps and blend
/// weights 4x4.  A texture the materials use in two of these ways becomes two, each once however many materials use it.  A texture that is
/// block-compressed already (a DDS) is left as it is.
///
/// std::runtime_error for a texture no material uses, one of a pixel format other than A8R8G8B8, X8R8G8B8 or a block-compressed one, one
/// whose texels are not as many as its size needs, or, for ASTC, one block-compressed otherwise; the model is then as it was.
void bake_textures(SerializedModel &model, TextureCodec codec = TextureCodec::BC7);

/// Takes out the textures no material uses, the others keeping their order and the materials their places in them.
void remove_unused_textures(SerializedModel &model);

/// The model without light of its own: no material has an emissive texture or colour any more, and the emissive textures nothing else uses
/// are gone.
void drop_emissive(SerializedModel &model);

}
