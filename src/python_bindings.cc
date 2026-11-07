// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include "python_bindings.h"

#include <nanobind/nanobind.h>

#include <rove/vec.h>

namespace nb = nanobind;

NB_MODULE(_pyedge, m)
{
    m.doc() = "Python bindings for edge";

    // Reuse pyrove math classes (vec/matrix/obb/etc.) in signatures.
    nb::module_::import_("pyrove");

    bind_camera(m);
    bind_texture(m);
    bind_shader_program(m);
    bind_material(m);
    bind_mesh_builder(m);
    bind_draw_call(m);
    bind_animation(m);
    bind_model(m);
    bind_renderer(m);
    // (pyedge's import calls this: a TypeError here says the two modules do not share rove's types)
    m.def("_takes_a_pyrove_vector", [](rove::vec<3> const &) {});
    bind_log(m);
    bind_lod(m);
}
