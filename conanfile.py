# SPDX-License-Identifier: MIT
# Copyright (c) 2004-2026 Manvel Avetisian

"""Conan 2 recipe of edge: `conan create .` builds and tests the package from this checkout.

The dependencies come from Conan (EDGE_SYSTEM_DEPS), but fastgltf, which edge patches, and the asset tools' encoders (bc7enc_rdo,
astc-encoder, xatlas), which no package manager has: CMake fetches those at build time, so the build needs network access and git.
"""
import os
import re

from conan import ConanFile
from conan.errors import ConanInvalidConfiguration
from conan.tools.build import check_min_cppstd
from conan.tools.cmake import CMake, CMakeDeps, CMakeToolchain, cmake_layout
from conan.tools.files import copy, load


class EdgeConan(ConanFile):
    name = "edge"
    description = "A small C++/Python renderer for games and tools: Vulkan, OpenGL ES and Metal backends, glTF and its own model format"
    license = "MIT"
    url = "https://github.com/mnvl/edge"
    homepage = "https://github.com/mnvl/edge"
    topics = ("graphics", "rendering", "vulkan", "gltf", "game-development")
    package_type = "static-library"
    settings = "os", "arch", "compiler", "build_type"
    options = {
        "fPIC": [True, False],
        "asset_tools": [True, False],   # edge::asset, edge_convert: making .edge models (meshoptimizer and the encoders)
        "with_imgui": [True, False],    # edge::imgui: Dear ImGui drawn through edge
        "with_sdl": [True, False],      # edge::sdl: an SDL3 window's Vulkan surface
    }
    default_options = {"fPIC": True, "asset_tools": True, "with_imgui": False, "with_sdl": False}
    exports_sources = ("CMakeLists.txt", "LICENSE.md", "THIRD_PARTY.md", "THIRD_PARTY_NOTICES.md", "src/*", "include/*", "etc/cmake/*", "etc/patches/*",
                       "tools/*")

    def set_version(self):
        text = load(self, os.path.join(self.recipe_folder, "CMakeLists.txt"))
        self.version = re.search(r"PROJECT\(EDGE VERSION ([0-9.]+)\)", text).group(1)

    def config_options(self):
        if self.settings.os == "Windows":
            del self.options.fPIC

    def requirements(self):
        # (transitive_headers: edge's public headers include them)
        self.requires("rove/[>=3 <4]", transitive_headers=True)
        self.requires("cereal/1.3.2")    # (inside edge: no public header includes it)
        self.requires("stb/cci.20240531")
        if self.settings.os != "Macos":
            self.requires("vulkan-headers/1.3.268.0", transitive_headers=True)
            self.requires("vulkan-loader/1.3.268.0")
        if self.options.asset_tools:
            self.requires("meshoptimizer/0.22")
        if self.options.with_imgui:
            self.requires("imgui/1.92.9b", transitive_headers=True)
        if self.options.with_sdl:
            self.requires("sdl/[>=3 <4]", transitive_headers=True)

    def build_requirements(self):
        # glslangValidator, which compiles edge's shaders (and, with the package's CMake helpers, a game's)
        self.tool_requires("glslang/1.4.313.0")

    def validate(self):
        check_min_cppstd(self, 20)
        if self.settings.os == "Windows":
            raise ConanInvalidConfiguration("edge's recipe is made for Linux and macOS")

    def layout(self):
        cmake_layout(self)

    def generate(self):
        tc = CMakeToolchain(self)
        tc.cache_variables["EDGE_SYSTEM_DEPS"] = True
        tc.cache_variables["EDGE_INSTALL"] = True
        tc.cache_variables["EDGE_TESTS"] = False
        tc.cache_variables["EDGE_PYTHON"] = False
        tc.cache_variables["EDGE_GLES"] = False
        tc.cache_variables["EDGE_ASSET_TOOLS"] = bool(self.options.asset_tools)
        tc.cache_variables["EDGE_IMGUI"] = bool(self.options.with_imgui)
        tc.cache_variables["EDGE_SDL"] = bool(self.options.with_sdl)
        tc.cache_variables["EDGE_SHOT"] = False
        tc.generate()
        CMakeDeps(self).generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        copy(self, "LICENSE.md", self.source_folder, os.path.join(self.package_folder, "licenses"))
        CMake(self).install()

    def package_info(self):
        # edge's own CMake package (lib/cmake/edge/edgeConfig.cmake), not one Conan generates: it has the targets edge::edge,
        # edge::asset, edge::imgui, edge::sdl, edge::convert and the helpers edge_add_shaders, edge_bundle_runtime and
        # edge_convert_models.  It finds rove, Vulkan, meshoptimizer, imgui and SDL3 through the files CMakeDeps
        # generates for them.
        self.cpp_info.set_property("cmake_find_mode", "none")
        self.cpp_info.builddirs = [os.path.join("lib", "cmake", "edge")]
        self.cpp_info.includedirs = ["include"]
        self.cpp_info.libdirs = ["lib"]
        self.cpp_info.libs = []
        self.cpp_info.bindirs = ["bin"]
