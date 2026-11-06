// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include "python_bindings.h"

#include <nanobind/nanobind.h>
#include <nanobind/stl/shared_ptr.h>
#include <nanobind/stl/string.h>

#include "shader_program.h"

namespace nb = nanobind;

void bind_shader_program(nb::module_ &m)
{
    nb::class_<edge::ShaderProgram>(m, "ShaderProgram")
        .def_prop_ro("vertex_shader_filename", &edge::ShaderProgram::vertex_shader_filename)
        .def_prop_ro("fragment_shader_filename", &edge::ShaderProgram::fragment_shader_filename);
}
