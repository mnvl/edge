// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include "python_bindings.h"

#include <nanobind/nanobind.h>
#include <nanobind/stl/shared_ptr.h>

#include "camera.h"

namespace nb = nanobind;
using namespace nb::literals;

void bind_camera(nb::module_ &m)
{
    using edge::Camera;
    auto view_matrix = [](Camera const &self) { return self.view_matrix(); };
    auto set_view_matrix = [](Camera &self, rove::matrix<4,4> const &matrix) { self.set_view_matrix(matrix); };
    auto inverted_view_matrix = [](Camera const &self) { return self.inverted_view_matrix(); };
    auto billboard_matrix = [](Camera const &self) { return self.billboard_matrix(); };
    auto projection_matrix = [](Camera const &self) { return self.projection_matrix(); };
    auto inverted_projection_matrix = [](Camera const &self) { return self.inverted_projection_matrix(); };
    auto eye_point = [](Camera const &self) { return self.eye_point(); };
    auto up_dir = [](Camera const &self) { return self.up_dir(); };
    auto view_dir = [](Camera const &self) { return self.view_dir(); };
    auto cross_dir = [](Camera const &self) { return self.cross_dir(); };

    nb::class_<Camera>(m, "Camera")
        .def(nb::new_([]() { return std::make_shared<Camera>(); }))
        .def_prop_rw("view_matrix", view_matrix, set_view_matrix,
            "The view: world to view space.  Set, the camera's eye and axes are its; RuntimeError for a matrix without an inverse")
        .def_prop_ro("inverted_view_matrix", inverted_view_matrix)
        .def_prop_ro("billboard_matrix", billboard_matrix)
        .def_prop_ro("projection_matrix", projection_matrix)
        .def_prop_ro("inverted_projection_matrix", inverted_projection_matrix)
        .def_prop_ro("eye_point", eye_point)
        .def_prop_ro("up_dir", up_dir)
        .def_prop_ro("view_dir", view_dir)
        .def_prop_ro("cross_dir", cross_dir)
        .def("set_perspective_projection", &Camera::set_perspective_projection, "fov"_a, "aspect"_a, "z_near"_a, "z_far"_a)
        .def("set_orthographic_projection", &Camera::set_orthographic_projection, "width"_a, "height"_a, "z_near"_a, "z_far"_a)
        .def("look_at", &Camera::look_at, "eye"_a, "lookat"_a, "up"_a)
        .def("set_view_matrix", set_view_matrix, "matrix"_a)
        .def("set_cube_face", &Camera::set_cube_face, "eye"_a, "face"_a, "z_near"_a, "z_far"_a,
            "The camera that draws face 0 to 5 (+x, -x, +y, -y, +z, -z) of a cube target from eye: a square right angle, turned so that "
            "a shader samples the cube in world direction d by edge_cube_direction(d) (edge_cube.glsl). IndexError for another face.")
        .def("cut", &Camera::cut,
            "A cut: the draws committed through the camera from now to the end of the frame have no motion of the camera's "
            "(their previous view-projection is the one it has); what they see keeps its own")
        .def_prop_ro("cuts", &Camera::cuts, "How many times cut() was called")
        // the names before 2026-11
        .def("get_view_matrix", pyedge::deprecated("Camera.get_view_matrix() is Camera.view_matrix", view_matrix))
        .def("get_inverted_view_matrix", pyedge::deprecated("Camera.get_inverted_view_matrix() is Camera.inverted_view_matrix",
            inverted_view_matrix))
        .def("get_billboard_matrix", pyedge::deprecated("Camera.get_billboard_matrix() is Camera.billboard_matrix", billboard_matrix))
        .def("get_projection_matrix", pyedge::deprecated("Camera.get_projection_matrix() is Camera.projection_matrix", projection_matrix))
        .def("get_inverted_projection_matrix", pyedge::deprecated(
            "Camera.get_inverted_projection_matrix() is Camera.inverted_projection_matrix", inverted_projection_matrix))
        .def("get_eye_point", pyedge::deprecated("Camera.get_eye_point() is Camera.eye_point", eye_point))
        .def("get_up_dir", pyedge::deprecated("Camera.get_up_dir() is Camera.up_dir", up_dir))
        .def("get_view_dir", pyedge::deprecated("Camera.get_view_dir() is Camera.view_dir", view_dir))
        .def("get_cross_dir", pyedge::deprecated("Camera.get_cross_dir() is Camera.cross_dir", cross_dir))
        .def("set_eye_lookat_up", pyedge::deprecated("Camera.set_eye_lookat_up() is Camera.look_at()", &Camera::look_at),
            "eye"_a, "lookat"_a, "up"_a);
}
