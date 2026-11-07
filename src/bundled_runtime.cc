// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include "bundled_runtime.h"

#include <cstdlib>
#include <stdexcept>
#include <string>

namespace edge
{

namespace
{

void set_variable(char const *name, char const *value)
{
#if defined(_WIN32)
	_putenv_s(name, value);
#else
	setenv(name, value, 1);
#endif
}

}

std::filesystem::path use_bundled_runtime(std::filesystem::path const &dir)
{
	std::filesystem::path const shaders = dir / "shaders" / "edge";
	if (!std::filesystem::is_regular_file(shaders / "vertex.spv"))
		throw std::runtime_error(shaders.string() + ": edge's shaders are not there");
#if defined(__APPLE__)
	std::filesystem::path const driver = dir / "vulkan" / "MoltenVK_icd.json";
	if (!std::filesystem::is_regular_file(driver))
		throw std::runtime_error(driver.string() + ": the Vulkan driver, MoltenVK, is not there");
	set_variable("VK_DRIVER_FILES", driver.string().c_str());
#endif
	set_variable("EDGE_VK_SHADER_DIR", shaders.string().c_str());
	return shaders;
}

}
