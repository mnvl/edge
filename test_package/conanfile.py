# SPDX-License-Identifier: MIT
# Copyright (c) 2004-2026 Manvel Avetisian

import os

from conan import ConanFile
from conan.tools.build import can_run
from conan.tools.cmake import CMake, cmake_layout


class EdgeTestConan(ConanFile):
    settings = "os", "arch", "compiler", "build_type"
    generators = "CMakeDeps", "CMakeToolchain", "VirtualBuildEnv"

    def requirements(self):
        self.requires(self.tested_reference_str)

    def build_requirements(self):
        # glslangValidator for edge_add_shaders, as a game that compiles its own shaders needs it
        self.tool_requires("glslang/1.4.313.0")

    def layout(self):
        cmake_layout(self)

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def test(self):
        if can_run(self):
            self.run(os.path.join(self.cpp.build.bindir, "edge_example"), env="conanrun")
