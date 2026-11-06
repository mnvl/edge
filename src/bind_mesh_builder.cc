// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include "python_bindings.h"

#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include <nanobind/nanobind.h>
#include <nanobind/ndarray.h>
#include <nanobind/stl/shared_ptr.h>
#include <nanobind/stl/string.h>

#include "material.h"
#include "model_builder.h"
#include "vertex.h"
#include "vertex_traits.h"

namespace nb = nanobind;
using namespace nb::literals;

namespace {

void validate_mesh_builder_array_size(char const *name, size_t expected, size_t actual)
{
    if (actual != expected)
    {
        throw std::runtime_error(std::string(name) + " row count does not match positions row count");
    }
}

void mesh_builder_set_vertices_numpy(edge::MeshBuilder &self,
    nb::ndarray<float, nb::shape<-1, 3>, nb::c_contig> positions,
    nb::object const &texcoords_obj,
    nb::object const &normals_obj,
    nb::object const &tangents_obj,
    nb::object const &binormals_obj)
{
    size_t const vertex_count = positions.shape(0);
    std::vector<edge::GenericVertex> vertices(vertex_count);
    if (!vertices.empty())
    {
        std::memset(vertices.data(), 0, sizeof(vertices[0]) * vertices.size());
    }

    edge::GenericVertex::format_type format = edge::GenericVertex::FORMAT_POSITION;
    for (size_t i = 0; i < vertex_count; ++i)
    {
        vertices[i].position.set(positions(i, 0), positions(i, 1), positions(i, 2));
    }

    if (!texcoords_obj.is_none())
    {
        auto texcoords = nb::cast<nb::ndarray<float, nb::shape<-1, 2>, nb::c_contig>>(texcoords_obj);
        validate_mesh_builder_array_size("texcoords", vertex_count, texcoords.shape(0));
        for (size_t i = 0; i < vertex_count; ++i)
        {
            vertices[i].texcoords.set(texcoords(i, 0), texcoords(i, 1));
        }
        format |= edge::GenericVertex::FORMAT_TEXCOORDS;
    }

    if (!normals_obj.is_none())
    {
        auto normals = nb::cast<nb::ndarray<float, nb::shape<-1, 3>, nb::c_contig>>(normals_obj);
        validate_mesh_builder_array_size("normals", vertex_count, normals.shape(0));
        for (size_t i = 0; i < vertex_count; ++i)
        {
            vertices[i].normal.set(normals(i, 0), normals(i, 1), normals(i, 2));
        }
        format |= edge::GenericVertex::FORMAT_NORMAL;
    }

    if (!tangents_obj.is_none())
    {
        auto tangents = nb::cast<nb::ndarray<float, nb::shape<-1, 3>, nb::c_contig>>(tangents_obj);
        validate_mesh_builder_array_size("tangents", vertex_count, tangents.shape(0));
        for (size_t i = 0; i < vertex_count; ++i)
        {
            vertices[i].tangent.set(tangents(i, 0), tangents(i, 1), tangents(i, 2));
        }
        format |= edge::GenericVertex::FORMAT_TANGENT;
    }

    if (!binormals_obj.is_none())
    {
        auto binormals = nb::cast<nb::ndarray<float, nb::shape<-1, 3>, nb::c_contig>>(binormals_obj);
        validate_mesh_builder_array_size("binormals", vertex_count, binormals.shape(0));
        for (size_t i = 0; i < vertex_count; ++i)
        {
            vertices[i].binormal.set(binormals(i, 0), binormals(i, 1), binormals(i, 2));
        }
        format |= edge::GenericVertex::FORMAT_BINORMAL;
    }

    self.set_vertices(vertices.empty() ? nullptr : vertices.data(), format, vertices.size());
}

template <typename IndexType>
void mesh_builder_set_indices_numpy_1d(edge::MeshBuilder &self,
    nb::ndarray<IndexType, nb::shape<-1>, nb::c_contig> indices)
{
    std::vector<unsigned> converted(indices.shape(0));
    for (size_t i = 0; i < converted.size(); ++i)
    {
        std::uint64_t const value = static_cast<std::uint64_t>(indices(i));
        if (value > static_cast<std::uint64_t>(std::numeric_limits<edge::MeshBuilder::index_type>::max()))
        {
            throw std::runtime_error("mesh indices exceed 16-bit range");
        }
        converted[i] = static_cast<unsigned>(value);
    }

    self.set_indices(converted.empty() ? nullptr : converted.data(), converted.size());
}

template <typename IndexType>
void mesh_builder_set_indices_numpy_2d(edge::MeshBuilder &self,
    nb::ndarray<IndexType, nb::shape<-1, 3>, nb::c_contig> triangles)
{
    std::vector<unsigned> converted(triangles.shape(0) * 3);
    size_t offset = 0;
    for (size_t i = 0; i < triangles.shape(0); ++i)
    {
        for (size_t j = 0; j < 3; ++j)
        {
            std::uint64_t const value = static_cast<std::uint64_t>(triangles(i, j));
            if (value > static_cast<std::uint64_t>(std::numeric_limits<edge::MeshBuilder::index_type>::max()))
            {
                throw std::runtime_error("mesh indices exceed 16-bit range");
            }
            converted[offset++] = static_cast<unsigned>(value);
        }
    }

    self.set_indices(converted.empty() ? nullptr : converted.data(), converted.size());
}

}  // namespace

void bind_mesh_builder(nb::module_ &m)
{
    auto default_triangles = [](edge::MeshBuilder &self, edge::MaterialPtr const &material_ptr) {
            if (self.index_buffer().size() % 3 != 0)
            {
                throw std::runtime_error("mesh index buffer size must be divisible by 3");
            }
            self.clear_triangles();
            if (!self.index_buffer().empty())
            {
                self.add_triangles(material_ptr, 0, self.index_buffer().size() / 3);
            }
        };

    nb::class_<edge::MeshBuilder>(m, "MeshBuilder")
        .def_prop_rw("id",
            [](edge::MeshBuilder const &self) { return self.id(); },
            [](edge::MeshBuilder &self, std::string const &value) { self.set_id(value); })
        .def_prop_rw("name",
            [](edge::MeshBuilder const &self) { return self.name(); },
            [](edge::MeshBuilder &self, std::string const &value) { self.set_name(value); })
        .def("set_id", [](edge::MeshBuilder &self, std::string const &value) { self.set_id(value); }, "id"_a)
        .def("set_name", [](edge::MeshBuilder &self, std::string const &value) { self.set_name(value); }, "name"_a)
        .def_prop_ro("vertices_count", [](edge::MeshBuilder const &self) {
            return self.vertex_buffer().size();
        })
        .def_prop_ro("indices_count", [](edge::MeshBuilder const &self) {
            return self.index_buffer().size();
        })
        .def_prop_ro("triangles_count", [](edge::MeshBuilder const &self) {
            return self.triangles().size();
        })
        .def("clear_geometry", &edge::MeshBuilder::clear_geometry)
        .def("clear_triangles", &edge::MeshBuilder::clear_triangles)
        .def("add_triangles", [](edge::MeshBuilder &self,
            size_t first_index_offset,
            size_t triangles_count,
            edge::MaterialPtr const &material_ptr) {
            self.add_triangles(material_ptr, first_index_offset, triangles_count);
        }, "first_index_offset"_a, "triangles_count"_a, "material"_a = edge::MaterialPtr())
        .def("set_default_triangles", default_triangles, "material"_a = edge::MaterialPtr(),
            "One group of all the triangles, of material")
        .def("set_vertices_numpy", &mesh_builder_set_vertices_numpy, "positions"_a, "texcoords"_a = nb::none(), "normals"_a = nb::none(),
            "tangents"_a = nb::none(), "binormals"_a = nb::none())
        .def("set_indices_numpy", &mesh_builder_set_indices_numpy_1d<std::uint16_t>, "indices"_a)
        .def("set_indices_numpy", &mesh_builder_set_indices_numpy_1d<std::uint32_t>, "indices"_a)
        .def("set_indices_numpy", &mesh_builder_set_indices_numpy_2d<std::uint16_t>, "triangles"_a)
        .def("set_indices_numpy", &mesh_builder_set_indices_numpy_2d<std::uint32_t>, "triangles"_a);
}
