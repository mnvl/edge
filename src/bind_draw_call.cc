// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include "python_bindings.h"

#include <cstring>
#include <stdexcept>
#include <string>

#include <nanobind/nanobind.h>
#include <nanobind/ndarray.h>
#include <nanobind/stl/vector.h>
#include <nanobind/stl/shared_ptr.h>

#include "camera.h"
#include "debug_lines.h"
#include "draw_call.h"
#include "material.h"
#include "index_buffer.h"
#include "scheduler.h"
#include "uber_draw_call.h"
#include "vertex_buffer.h"

namespace nb = nanobind;
using namespace nb::literals;

namespace {

/// (N, 4, 4) float32 matrices, each as mat4.to_numpy() has it: [row][column]
using MatrixArray = nb::ndarray<float, nb::shape<-1, 4, 4>, nb::c_contig, nb::device::cpu>;

std::vector<rove::matrix<4,4>> matrices_of(MatrixArray const &array)
{
    std::vector<rove::matrix<4,4>> matrices(array.shape(0));
    for (size_t n = 0; n < matrices.size(); ++n)
        for (size_t row = 0; row < 4; ++row)
            for (size_t col = 0; col < 4; ++col)
                matrices[n].ij[col][row] = array(n, row, col);
    return matrices;
}

void validate_upload_range(size_t total_size, size_t offset, size_t data_size, char const *resource_name)
{
    if (offset > total_size)
    {
        throw std::runtime_error(std::string(resource_name) + " upload offset is out of range");
    }

    if (data_size > (total_size - offset))
    {
        throw std::runtime_error(std::string(resource_name) + " upload data is too large");
    }
}

void upload_bytes_to_vertex_buffer(edge::VertexBufferPtr const &buffer, nb::bytes const &data,
    size_t offset, bool discard)
{
    size_t const total_size = buffer->size();
    size_t const data_size = data.size();
    validate_upload_range(total_size, offset, data_size, "vertex buffer");

    auto bytes = buffer->write(discard ? edge::Contents::DISCARD : edge::Contents::KEEP);
    if (!bytes.data() && data_size > 0)
    {
        throw std::runtime_error("vertex buffer has no bytes to write");
    }
    if (data_size > 0)
    {
        std::memcpy(bytes.data() + offset, data.c_str(), data_size);
    }
}

void upload_bytes_to_index_buffer(edge::IndexBufferPtr const &buffer, nb::bytes const &data,
    size_t offset, bool discard)
{
    size_t const total_size = buffer->size();
    size_t const data_size = data.size();
    validate_upload_range(total_size, offset, data_size, "index buffer");

    auto bytes = buffer->write(discard ? edge::Contents::DISCARD : edge::Contents::KEEP);
    if (!bytes.data() && data_size > 0)
    {
        throw std::runtime_error("index buffer has no bytes to write");
    }
    if (data_size > 0)
    {
        std::memcpy(bytes.data() + offset, data.c_str(), data_size);
    }
}

}  // namespace

void bind_draw_call(nb::module_ &m)
{
    nb::class_<edge::VertexBuffer>(m, "VertexBuffer")
        .def_prop_ro("size", &edge::VertexBuffer::size)
        .def("upload", &upload_bytes_to_vertex_buffer,
            "data"_a, "offset"_a = 0, "discard"_a = true,
            "Upload raw bytes into the vertex buffer.");

    nb::class_<edge::IndexBuffer>(m, "IndexBuffer")
        .def_prop_ro("size", &edge::IndexBuffer::size)
        .def("upload", &upload_bytes_to_index_buffer,
            "data"_a, "offset"_a = 0, "discard"_a = true,
            "Upload raw bytes into the index buffer.");

    using edge::DrawCall;
    using Matrices = std::vector<rove::matrix<4,4>>;
    auto instances_of_array = [](DrawCall &self, MatrixArray const &instances) { self.set_instances(matrices_of(instances)); };
    auto previous_instances_of_array = [](DrawCall &self, MatrixArray const &instances) {
        self.set_previous_instances(matrices_of(instances));
    };
    // what a call draws and how: a property where it can be read back, set_x(v) where it can be set
    nb::class_<DrawCall>(m, "DrawCall")
        .def_prop_rw("priority", &DrawCall::priority, &DrawCall::set_priority)
        .def_prop_rw("vertex_buffer", &DrawCall::vertex_buffer,
            &DrawCall::set_vertex_buffer)
        .def_prop_rw("index_buffer", &DrawCall::index_buffer,
            &DrawCall::set_index_buffer)
        .def_prop_rw("material", &DrawCall::material,
            &DrawCall::set_material)
        .def_prop_rw("camera", &DrawCall::camera,
            &DrawCall::set_camera)
        .def_prop_rw("minimal_vertex_index", &DrawCall::minimal_vertex_index,
            &DrawCall::set_minimal_vertex_index)
        .def_prop_rw("first_index_offset", &DrawCall::first_index_offset,
            &DrawCall::set_first_index_offset)
        .def_prop_rw("triangles_count", &DrawCall::triangles_count,
            &DrawCall::set_triangles_count)
        .def("set_always_visible", &DrawCall::set_always_visible, "always_visible"_a)
        .def("set_obb", &DrawCall::set_obb, "bounds"_a)
        .def("set_world_matrix", &DrawCall::set_world_matrix, "matrix"_a)
        .def("set_instances", nb::overload_cast<Matrices const &>(&DrawCall::set_instances), "instances"_a)
        .def("set_instances", instances_of_array, "instances"_a,
            "Draws the call once for each instance, instance i at instances[i] * world, each culled on its own at commit(): a list "
            "of mat4 or an (N, 4, 4) float32 array of them as mat4.to_numpy() has them; none at all is ValueError. A vertex shader of "
            "one's own applies edge_instance_matrix() (edge_instance.glsl); the built-in ones do.")
        .def("set_previous_world_matrix", &DrawCall::set_previous_world_matrix, "matrix"_a,
            "The world matrix the call had the frame before, for its commits in this frame, in place of what it remembers")
        .def("set_previous_instances", &DrawCall::set_previous_instances, "instances"_a)
        .def("set_previous_instances", previous_instances_of_array, "instances"_a,
            "The instances the call had the frame before (a list of mat4 or an (N, 4, 4) float32 array), for its commits in this frame; "
            "instance i was at instances[i], one past them is new; none at all is ValueError")
        .def("reset_previous", &DrawCall::reset_previous,
            "From the next commit on, the call was where it is (it draws another thing now); its camera's motion stays")
        .def_prop_ro("previous_world_view_projection", &DrawCall::shader_previous_world_view_projection,
            "The camera's view-projection the frame before times the world matrix then, as commit() worked them out")
        .def_prop_ro("previous_world", &DrawCall::shader_previous_world,
            "The world matrix the frame before, as commit() worked it out")
        .def_prop_ro("has_previous", &DrawCall::has_previous,
            "Whether the previous matrices are known (EDGE_FLAG_PREVIOUS_UNKNOWN in the shader where not): false where they are this "
            "frame's for want of them")
        .def("commit", &DrawCall::commit)
        .def("is_visible", &DrawCall::is_visible)
        .def("execute", &DrawCall::execute)
        // set_x(v) as well as x = v
        .def("set_priority", &DrawCall::set_priority, "priority"_a)
        .def("set_vertex_buffer", &DrawCall::set_vertex_buffer, "vertex_buffer"_a)
        .def("set_index_buffer", &DrawCall::set_index_buffer, "index_buffer"_a)
        .def("set_material", &DrawCall::set_material, "material"_a)
        .def("set_minimal_vertex_index", &DrawCall::set_minimal_vertex_index, "value"_a)
        .def("set_first_index_offset", &DrawCall::set_first_index_offset, "value"_a)
        .def("set_triangles_count", &DrawCall::set_triangles_count, "value"_a)
        .def("set_camera", &DrawCall::set_camera, "camera"_a);

    // UberDrawCall: the built-in shader's draw (texture blending, skinning), with no extra Python-visible methods.
    nb::class_<edge::UberDrawCall, edge::DrawCall>(m, "UberDrawCall");

    nb::class_<edge::Scheduler>(m, "Scheduler")
        .def("add", &edge::Scheduler::add, "draw_call"_a)
        .def("change_draw_calls_camera", &edge::Scheduler::change_draw_calls_camera, "camera"_a)
        .def_prop_rw("sort_blended", &edge::Scheduler::sort_blended,
            &edge::Scheduler::set_sort_blended,
            "Within a priority, the opaque calls first in the order added, then the blended ones farthest first; off by default")
        .def("set_sort_blended", &edge::Scheduler::set_sort_blended, "sort_blended"_a)
        .def("flush", &edge::Scheduler::flush, "clear"_a = true)
        .def("clear", &edge::Scheduler::clear)
        .def_prop_ro("visible_calls_count", &edge::Scheduler::visible_calls_count)
        .def_prop_ro("invisible_calls_count", &edge::Scheduler::invisible_calls_count)
        .def_prop_ro("executed_triangles_count", &edge::Scheduler::executed_triangles_count,
            "Triangles every flush has drawn (an instanced call's for each instance drawn)")
        .def_prop_ro("offered_triangles_count", &edge::Scheduler::offered_triangles_count,
            "Triangles every flush was given, culled or not");

    nb::class_<edge::DebugLines>(m, "DebugLines",
        "Lines in the world for debugging: segments, boxes, spheres and axes of a colour, drawn as strips of a width in world units "
        "that face the camera, unlit, one draw call for each colour; collected between draws, draw() queues them and starts afresh")
        .def(nb::init<edge::RendererPtr const &, float>(), "renderer"_a, "width"_a,
            "ValueError for no renderer or a width that is not more than zero")
        .def("line", &edge::DebugLines::line, "a"_a, "b"_a, "colour"_a)
        .def("box", nb::overload_cast<rove::aabb<3> const &, rove::vec<4> const &>(&edge::DebugLines::box), "bounds"_a, "colour"_a)
        .def("box", nb::overload_cast<rove::obb<3> const &, rove::vec<4> const &>(&edge::DebugLines::box), "bounds"_a, "colour"_a)
        .def("sphere", &edge::DebugLines::sphere, "centre"_a, "radius"_a, "colour"_a, "segments"_a = 32,
            "Three great circles; ValueError for fewer than 3 segments")
        .def("axes", &edge::DebugLines::axes, "frame"_a, "length"_a, "The frame's x, y and z axes, red, green and blue")
        .def_prop_ro("segments", &edge::DebugLines::segments, "Segments collected since the last draw")
        .def("draw", &edge::DebugLines::draw, "scheduler"_a, "camera"_a,
            "Queues one draw call for each colour collected, facing the camera, and starts afresh")
        .def("clear", &edge::DebugLines::clear)
        .def("set_priority", &edge::DebugLines::set_priority, "priority"_a);
}
