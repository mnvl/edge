// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include "python_bindings.h"

#include <string>
#include <variant>
#include <vector>

#include <nanobind/nanobind.h>
#include <nanobind/ndarray.h>
#include <nanobind/stl/optional.h>
#include <nanobind/stl/shared_ptr.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/variant.h>
#include <nanobind/stl/vector.h>

#include "material.h"
#include "shader_program.h"

namespace nb = nanobind;
using namespace nb::literals;

void bind_material(nb::module_ &m)
{
    m.attr("MATERIAL_PARAMETERS_SIZE") = nb::int_(edge::Material::PARAMETERS_SIZE);

    auto cull_mode = nb::enum_<edge::Material::CullMode>(m, "CullMode")
        .value("CW", edge::Material::CullMode::CW)
        .value("CCW", edge::Material::CullMode::CCW)
        .value("NONE", edge::Material::CullMode::NONE);
    // the names before 2026-11, kept for the scripts that use them
    for (char const *name : {"CW", "CCW", "NONE"})
        cull_mode.attr((std::string("CULL_") + name).c_str()) = cull_mode.attr(name);

    auto blend_mode = nb::enum_<edge::Material::BlendMode>(m, "BlendMode")
        .value("ZERO", edge::Material::BlendMode::ZERO)
        .value("ONE", edge::Material::BlendMode::ONE)
        .value("SRC_ALPHA", edge::Material::BlendMode::SRC_ALPHA)
        .value("INV_SRC_ALPHA", edge::Material::BlendMode::INV_SRC_ALPHA)
        .value("DEST_ALPHA", edge::Material::BlendMode::DEST_ALPHA)
        .value("INV_DEST_ALPHA", edge::Material::BlendMode::INV_DEST_ALPHA);
    for (char const *name : {"ZERO", "ONE", "SRC_ALPHA", "INV_SRC_ALPHA", "DEST_ALPHA", "INV_DEST_ALPHA"})
        blend_mode.attr((std::string("BLEND_") + name).c_str()) = blend_mode.attr(name);

    using edge::Material;
    using edge::ShaderProgramPtr;
    using edge::TexturePtr;
    auto parameters = [](Material const &self) {
        std::vector<unsigned char> const &p = self.parameters();
        return nb::bytes(reinterpret_cast<char const *>(p.data()), p.size());
    };
    auto set_parameters = [](Material &self, nb::handle data) {
        if (nb::isinstance<nb::bytes>(data))
        {
            nb::bytes const bytes = nb::borrow<nb::bytes>(data);
            self.set_parameters(bytes.size() ? bytes.c_str() : nullptr, bytes.size());
            return;
        }
        auto const array = nb::cast<nb::ndarray<nb::ro, nb::ndim<1>, nb::c_contig, nb::device::cpu>>(data);
        self.set_parameters(array.nbytes() ? array.data() : nullptr, array.nbytes());
    };
    // a texture, or the file of one (str)
    using TextureOrFile = std::variant<TexturePtr, std::string>;
    auto set_normal_map_texture = [](Material &self, TextureOrFile const &t) {
        std::visit([&](auto const &v) { self.set_normal_map_texture(v); }, t);
    };
    auto set_texture_blending_weights_texture = [](Material &self, TextureOrFile const &t) {
        std::visit([&](auto const &v) { self.set_texture_blending_weights_texture(v); }, t);
    };

    nb::class_<Material>(m, "Material")
        .def_prop_rw("name", &Material::name, &Material::set_name)
        .def_prop_ro("diffuse_textures_count", &Material::diffuse_textures_count)
        .def("diffuse_texture", nb::overload_cast<size_t>(&Material::diffuse_texture, nb::const_), "index"_a = 0)
        .def("set_diffuse_texture", nb::overload_cast<TexturePtr const &, size_t>(&Material::set_diffuse_texture), "texture"_a, "index"_a = 0)
        .def("set_diffuse_texture", nb::overload_cast<std::string const &, size_t>(&Material::set_diffuse_texture), "filename"_a,
            "index"_a = 0)
        .def_prop_rw("cull_mode", &Material::cull_mode,
            &Material::set_cull_mode)
        .def_prop_rw("zwrite_enabled", &Material::zwrite_enabled,
            &Material::set_zwrite_enabled)
        .def_prop_rw("stencil_write", &Material::stencil_write,
            &Material::set_stencil_write)
        .def_prop_rw("alpha_blend_enabled", &Material::alpha_blend_enabled,
            &Material::set_alpha_blend_enabled)
        .def_prop_rw("preserve_destination_alpha", &Material::preserve_destination_alpha,
            &Material::set_preserve_destination_alpha)
        .def_prop_rw("src_blend_mode", &Material::src_blend_mode,
            &Material::set_src_blend_mode)
        .def_prop_rw("dest_blend_mode", &Material::dest_blend_mode,
            &Material::set_dest_blend_mode)
        // shader_program: the default; pass_shader_program(pass_type) and set_shader_program(pass_type, program): a pass's
        .def_prop_rw("shader_program", nb::overload_cast<>(&Material::shader_program, nb::const_),
            nb::overload_cast<ShaderProgramPtr const &>(&Material::set_shader_program), nb::arg("program").none())
        .def("set_shader_program", nb::overload_cast<ShaderProgramPtr const &>(&Material::set_shader_program), "program"_a.none())
        .def("set_shader_program", nb::overload_cast<uint32_t, ShaderProgramPtr const &>(&Material::set_shader_program),
            "pass_type"_a, "program"_a.none())
        .def("pass_shader_program", nb::overload_cast<uint32_t>(&Material::shader_program, nb::const_), "pass_type"_a,
            "The pass's own program, or the default where it has none")
        .def("clear_shader_program", [](Material &self) { self.set_shader_program(ShaderProgramPtr()); })
        .def("clear_shader_program", &Material::clear_shader_program, "pass_type"_a)
        .def_prop_rw("parameters", parameters, set_parameters,
            "The material's own uniform block, read by a shader of its own at EDGE_BINDING_MATERIAL_PARAMETERS (20): bytes, set from "
            "bytes or a 1-D array up to MATERIAL_PARAMETERS_SIZE (ValueError past it), zeros past what it sets; empty clears it")
        .def("extra_parameter", nb::overload_cast<size_t>(&Material::extra_parameter, nb::const_), "index"_a)
        .def("set_extra_parameter", &Material::set_extra_parameter, "index"_a, "value"_a)
        .def_prop_rw("texture_blending_weights_texture", &Material::texture_blending_weights_texture,
            set_texture_blending_weights_texture, "The texture of the blend weights, or None; set from a texture or a file")
        .def_prop_rw("normal_map_texture", &Material::normal_map_texture, set_normal_map_texture,
            "The normal map, or None; set from a texture or a file")
        .def_prop_rw("occlusion", &Material::occlusion, &Material::set_occlusion,
            "Whether the metallic-roughness texture's red is ambient occlusion (glTF's ORM; glTF loading packs occlusionTexture there)")
        .def_prop_rw("occlusion_strength", &Material::occlusion_strength,
            &Material::set_occlusion_strength, "glTF's occlusionTexture.strength, 0 to 1 (ValueError otherwise)")
        .def_prop_ro("metallic_roughness_texture", &Material::metallic_roughness_texture,
            "The metallic-roughness texture (glTF's ORM: occlusion red where the material has it, roughness green, metallic blue), "
            "or None")
        // set_x(v) as well as x = v
        .def("set_name", &Material::set_name, "name"_a)
        .def("set_stencil_write", &Material::set_stencil_write, "value"_a.none())
        .def("set_occlusion", &Material::set_occlusion, "occlusion"_a)
        .def("set_occlusion_strength", &Material::set_occlusion_strength, "strength"_a)
        .def("set_cull_mode", &Material::set_cull_mode, "mode"_a)
        .def("set_zwrite_enabled", &Material::set_zwrite_enabled, "enabled"_a)
        .def("set_alpha_blend_enabled", &Material::set_alpha_blend_enabled, "enabled"_a)
        .def("set_preserve_destination_alpha", &Material::set_preserve_destination_alpha, "preserve"_a)
        .def("set_src_blend_mode", &Material::set_src_blend_mode, "mode"_a)
        .def("set_dest_blend_mode", &Material::set_dest_blend_mode, "mode"_a)
        .def("set_parameters", set_parameters, "data"_a)
        .def("set_texture_blending_weights_texture", set_texture_blending_weights_texture, "texture"_a)
        .def("set_normal_map_texture", set_normal_map_texture, "texture"_a)
        // the names before 2026-11
        .def("get_diffuse_textures_count", pyedge::deprecated("Material.get_diffuse_textures_count() is Material.diffuse_textures_count",
            &Material::diffuse_textures_count))
        .def("get_diffuse_texture", pyedge::deprecated("Material.get_diffuse_texture(i) is Material.diffuse_texture(i)",
            nb::overload_cast<size_t>(&Material::diffuse_texture, nb::const_)), "index"_a = 0)
        .def("get_shader_program", pyedge::deprecated("Material.get_shader_program() is Material.shader_program",
            nb::overload_cast<>(&Material::shader_program, nb::const_)))
        .def("get_shader_program", pyedge::deprecated("Material.get_shader_program(t) is Material.pass_shader_program(t)",
            nb::overload_cast<uint32_t>(&Material::shader_program, nb::const_)), "pass_type"_a)
        .def("get_parameters", pyedge::deprecated("Material.get_parameters() is Material.parameters", parameters))
        .def("get_extra_parameter", pyedge::deprecated("Material.get_extra_parameter(i) is Material.extra_parameter(i)",
            nb::overload_cast<size_t>(&Material::extra_parameter, nb::const_)), "index"_a);
}
