// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include "bundled_runtime.h"

#include <cstdlib>
#include <stdexcept>
#include <string>

namespace edge
{

std::filesystem::path use_bundled_runtime(std::filesystem::path const &dir)
{
	std::filesystem::path const shaders = dir / "shaders" / "edge";
	if (!std::filesystem::is_regular_file(shaders / "vertex.spv"))
		throw std::runtime_error(shaders.string() + ": edge's shaders are not there");
#if defined(__APPLE__)
	std::filesystem::path const driver = dir / "vulkan" / "MoltenVK_icd.json";
	if (!std::filesystem::is_regular_file(driver))
		throw std::runtime_error(driver.string() + ": the Vulkan driver, MoltenVK, is not there");
	setenv("VK_DRIVER_FILES", driver.c_str(), 1);
#endif
	setenv("EDGE_VK_SHADER_DIR", shaders.c_str(), 1);
	return shaders;
}

}
