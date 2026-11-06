// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

#include <cstddef>
#include <vector>

#include "model_atlas.h"

namespace edge
{

/// atlas_materials() with AtlasOptions::Mode::Xatlas, of the materials that are @p packable (validated, with their textures 8 bits a texel;
/// atlas_detail::validate(), find_packable()).
std::size_t atlas_with_xatlas(SerializedModel &model, AtlasOptions const &options, std::vector<bool> packable);

}
