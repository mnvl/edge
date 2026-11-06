// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace edge
{

/// Where a set of edge's compiled shaders is looked for, in this order:
///
/// 1. $<variable>, when set (set but empty is refused): taken as it is, whether or not it holds the shaders (a draw then fails
///    naming the file);
/// 2. each of @p relative under the directory of the binary edge is linked into (edge_binary_dir()): `shaders/edge`, the bundle
///    edge_bundle_runtime makes beside a game's binary and a wheel's pyedge package; `../share/edge/shaders`, an installed edge
///    (bin/ or lib/ beside share/);
/// 3. the directory the build put them in (not a wheel's: its build is gone when it runs), then the one `cmake --install` puts them
///    in (where edge was built with EDGE_INSTALL).
///
/// The first of 2 and 3 that holds @p file is taken.
struct ShaderLookup
{
	std::string what;                                   // what the shaders are, for the message: "edge's SPIR-V"
	std::string variable;                               // the environment variable that names their directory
	std::string file;                                   // a file every complete directory of them has: "vertex.spv"
	std::vector<std::filesystem::path> relative;        // under the binary's directory
	std::filesystem::path build_dir;                    // where the build put them (empty: it made none, or it is a wheel's)
	std::filesystem::path install_dir;                  // where `cmake --install` puts them (empty: no install rules)
};

/// The lookups of edge's shaders: its SPIR-V (the Vulkan renderer's), their GLSL ES translations (a GLESRenderer's shader_dir) and
/// their Metal ones (a MetalRenderer's), each with its variable ($EDGE_VK_SHADER_DIR, $EDGE_GLES_SHADER_DIR, $EDGE_METAL_SHADER_DIR),
/// and the view shaders (tools/shaders: edge_shot's light, and a Python model viewer's; $EDGE_VIEW_SHADER_DIR, `view_shaders` beside
/// the binary or `../share/edge/view_shaders`).  A set this build does not make has no build directory.
ShaderLookup vulkan_shader_lookup();
ShaderLookup gles_shader_lookup();
ShaderLookup metal_shader_lookup();
ShaderLookup view_shader_lookup();

/// @p lookup done with @p variable_value as its variable's value (nullptr: unset) and @p binary_dir as the binary's directory
/// (empty: unknown, the relative places are not tried).  std::runtime_error naming the variable when it is set but empty, and
/// naming the file and every place tried when none holds it.
std::filesystem::path find_shader_dir(ShaderLookup const &lookup, char const *variable_value, std::filesystem::path const &binary_dir);

/// The directory of the binary edge's code is in: the executable, or the shared library it is linked into (pyedge's module), found
/// from an address of edge's (dladdr, GetModuleHandleEx).  Empty where that cannot be told (Emscripten).
std::filesystem::path edge_binary_dir();

/// The lookups done here, with the environment and edge_binary_dir().  vulkan_shader_dir(): the directory edge's own SPIR-V is read
/// from ($EDGE_VK_SHADER_DIR when set, set but empty refused).
std::string vulkan_shader_dir();
std::filesystem::path gles_shader_dir();
std::filesystem::path metal_shader_dir();
std::filesystem::path view_shader_dir();

}
