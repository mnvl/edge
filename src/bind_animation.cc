// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include "python_bindings.h"

#include <nanobind/nanobind.h>
#include <nanobind/stl/shared_ptr.h>
#include <nanobind/stl/string.h>

#include "animation.h"

namespace nb = nanobind;
using namespace nb::literals;

void bind_animation(nb::module_ &m)
{
    nb::class_<edge::Animation>(m, "Animation")
        .def_prop_ro("name", [](edge::Animation const &self) { return self.name; })
        .def_prop_ro("duration", [](edge::Animation const &self) { return self.duration(); })
        .def("pose", &edge::Animation::pose, "time"_a, "The clip at time (seconds), and its model's joints rebuilt")
        .def("set_time", pyedge::deprecated("Animation.set_time(t) is Animation.pose(t)", &edge::Animation::pose), "time"_a);
}
