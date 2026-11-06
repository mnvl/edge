// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

#include <filesystem>

namespace edge
{

/// Points edge and the Vulkan loader at what edge_bundle_runtime (etc/cmake/EdgeRuntime.cmake) put in @p dir: edge's shaders in
/// dir/shaders/edge ($EDGE_VK_SHADER_DIR) and, on macOS, the driver in dir/vulkan ($VK_DRIVER_FILES, its manifest).  Before a
/// renderer is made: the loader reads its variable when Vulkan starts.  Returns the shaders' directory, which a GLESRenderer is given.
/// std::runtime_error naming what is not there, the environment as it was.
std::filesystem::path use_bundled_runtime(std::filesystem::path const &dir);

}
