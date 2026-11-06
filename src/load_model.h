// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

#include <string>

#include "forward.h"
#include "gltf_loader.h"

namespace edge
{

/// A model of the type its file is, for what opens any model without knowing it (pyedge, the viewer): a .edge file's meshes name
/// their type (StaticModelPTBNT, StaticModelPTBNTC, StaticModelPNT, AnimatedModelPTBNJWT, AnimatedModelPTBNJWTC, AnimatedModelPNJWT),
/// a glTF's contents decide it as the converter does (a skin: AnimatedModelPTBNJWT; vertex colors: StaticModelPTBNTC; neither:
/// StaticModelPTBNT), loaded with @p options (a .edge file takes their keep_cpu_copy).  std::invalid_argument for a file that is neither
/// .edge nor .glb or .gltf; std::runtime_error for one that cannot be read, of no meshes, of meshes of two types or of one nothing loads.
ModelPtr load_model(RendererPtr const &renderer, SchedulerPtr const &scheduler, std::string const &filename,
	GltfOptions const &options = GltfOptions());

}
