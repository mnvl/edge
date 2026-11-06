// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include "shader_dirs.h"

#include <cstdlib>
#include <stdexcept>
#include <string>
#include <system_error>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif !defined(__EMSCRIPTEN__)
#include <dlfcn.h>
#if defined(__linux__)
#include <sys/auxv.h>
#endif
#if defined(__APPLE__)
#include <cstdint>
#include <mach-o/dyld.h>
#endif
#endif

// Where the build puts each set and `cmake --install` would (src/CMakeLists.txt defines them for this file alone)
#ifndef EDGE_VK_SHADER_BUILD_DIR
#define EDGE_VK_SHADER_BUILD_DIR ""
#endif
#ifndef EDGE_GLES_SHADER_BUILD_DIR
#define EDGE_GLES_SHADER_BUILD_DIR ""
#endif
#ifndef EDGE_METAL_SHADER_BUILD_DIR
#define EDGE_METAL_SHADER_BUILD_DIR ""
#endif
#ifndef EDGE_VIEW_SHADER_BUILD_DIR
#define EDGE_VIEW_SHADER_BUILD_DIR ""
#endif
#ifndef EDGE_SHADER_INSTALL_DIR
#define EDGE_SHADER_INSTALL_DIR ""
#endif
#ifndef EDGE_VIEW_SHADER_INSTALL_DIR
#define EDGE_VIEW_SHADER_INSTALL_DIR ""
#endif

namespace edge
{

namespace
{

// (an address in edge's code, whose binary is asked for)
char const anchor = 0;

std::vector<std::filesystem::path> const EDGE_RELATIVE = {"shaders/edge", "../share/edge/shaders"};

/// The binary that holds anchor, as the system names it; empty where it cannot say.
std::filesystem::path binary_of_edge()
{
#if defined(_WIN32)
	HMODULE module = nullptr;
	if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			reinterpret_cast<LPCWSTR>(&anchor), &module))
		return {};
	std::wstring name(MAX_PATH, L'\0');
	for (;;)
	{
		DWORD const size = GetModuleFileNameW(module, name.data(), static_cast<DWORD>(name.size()));
		if (size == 0)
			return {};
		if (size < name.size())
		{
			name.resize(size);
			return std::filesystem::path(name);
		}
		name.resize(name.size() * 2);       // (truncated: a longer path)
	}
#elif defined(__EMSCRIPTEN__)
	return {};
#else
	Dl_info info{};
	if (dladdr(&anchor, &info) == 0 || info.dli_fname == nullptr || *info.dli_fname == '\0')
		return {};
#if defined(__linux__)
	// The executable is named as it was run (argv[0]: maybe relative to where it started, maybe without a directory); the kernel
	// knows its path.  It is the object that holds the program headers the kernel loaded.
	Dl_info main_info{};
	unsigned long const phdr = getauxval(AT_PHDR);
	if (phdr != 0 && dladdr(reinterpret_cast<void const *>(phdr), &main_info) != 0 && main_info.dli_fbase == info.dli_fbase)
	{
		std::error_code error;
		std::filesystem::path const exe = std::filesystem::read_symlink("/proc/self/exe", error);
		if (!error)
			return exe;
	}
#elif defined(__APPLE__)
	if (info.dli_fbase == static_cast<void const *>(_dyld_get_image_header(0)))
	{
		uint32_t size = 0;
		_NSGetExecutablePath(nullptr, &size);
		std::string exe(size, '\0');
		if (_NSGetExecutablePath(exe.data(), &size) == 0)
			return std::filesystem::path(exe.c_str());
	}
#endif
	return std::filesystem::path(info.dli_fname);
#endif
}

}

ShaderLookup vulkan_shader_lookup()
{
	return {"edge's SPIR-V", "EDGE_VK_SHADER_DIR", "vertex.spv", EDGE_RELATIVE, EDGE_VK_SHADER_BUILD_DIR, EDGE_SHADER_INSTALL_DIR};
}

ShaderLookup gles_shader_lookup()
{
	return {"edge's GLSL ES shaders", "EDGE_GLES_SHADER_DIR", "vertex.es.glsl", EDGE_RELATIVE, EDGE_GLES_SHADER_BUILD_DIR,
		EDGE_SHADER_INSTALL_DIR};
}

ShaderLookup metal_shader_lookup()
{
	return {"edge's Metal shaders", "EDGE_METAL_SHADER_DIR", "vertex.msl.map", EDGE_RELATIVE, EDGE_METAL_SHADER_BUILD_DIR,
		EDGE_SHADER_INSTALL_DIR};
}

ShaderLookup view_shader_lookup()
{
	return {"edge's view shaders", "EDGE_VIEW_SHADER_DIR", "lit.frag.spv", {"view_shaders", "../share/edge/view_shaders"},
		EDGE_VIEW_SHADER_BUILD_DIR, EDGE_VIEW_SHADER_INSTALL_DIR};
}

std::filesystem::path find_shader_dir(ShaderLookup const &lookup, char const *variable_value, std::filesystem::path const &binary_dir)
{
	if (variable_value != nullptr)
	{
		if (*variable_value == '\0')
			throw std::runtime_error(lookup.variable + " is set but empty; set it to the directory of " + lookup.what + " or unset it");
		return variable_value;
	}

	std::string tried = "$" + lookup.variable + " (unset)";
	auto const holds = [&](std::filesystem::path const &dir, char const *origin) {
		tried += ", " + dir.string() + " (" + origin + ")";
		std::error_code error;
		return std::filesystem::is_regular_file(dir / lookup.file, error);
	};
	if (!binary_dir.empty())
	{
		for (std::filesystem::path const &relative : lookup.relative)
		{
			std::filesystem::path const dir = (binary_dir / relative).lexically_normal();
			if (holds(dir, "beside the binary"))
				return dir;
		}
	}
	else
		tried += ", not beside the binary (its directory is not known)";
	if (!lookup.build_dir.empty() && holds(lookup.build_dir, "the build's"))
		return lookup.build_dir;
	if (!lookup.install_dir.empty() && holds(lookup.install_dir, "the install's"))
		return lookup.install_dir;
	throw std::runtime_error(lookup.what + " are not found: no " + lookup.file + " in " + tried + "; set " + lookup.variable +
		" to their directory");
}

std::filesystem::path edge_binary_dir()
{
	// (the binary does not move while it runs)
	static std::filesystem::path const dir = [] {
		std::filesystem::path binary = binary_of_edge();
		if (binary.empty())
			return std::filesystem::path();
		std::error_code error;
		std::filesystem::path const canonical = std::filesystem::weakly_canonical(std::filesystem::absolute(binary, error), error);
		return (error ? binary : canonical).parent_path();
	}();
	return dir;
}

namespace
{

std::filesystem::path shader_dir(ShaderLookup const &lookup)
{
	return find_shader_dir(lookup, std::getenv(lookup.variable.c_str()), edge_binary_dir());
}

}

std::string vulkan_shader_dir()
{
	return shader_dir(vulkan_shader_lookup()).string();
}

std::filesystem::path gles_shader_dir()
{
	return shader_dir(gles_shader_lookup());
}

std::filesystem::path metal_shader_dir()
{
	return shader_dir(metal_shader_lookup());
}

std::filesystem::path view_shader_dir()
{
	return shader_dir(view_shader_lookup());
}

}
