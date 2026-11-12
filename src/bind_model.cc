// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include "python_bindings.h"

#include <memory>
#include <stdexcept>
#include <string>

#include <nanobind/nanobind.h>
#include <nanobind/stl/shared_ptr.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/vector.h>

#include "animated_model.h"
#include "joint.h"
#include "load_model.h"
#include "material.h"
#include "mesh.h"
#include "model_builder.h"
#include "model_cache.h"
#if defined(EDGE_PYEDGE_ASSET_TOOLS)
#include "model_convert.h"
#endif
#include "scheduler.h"
#include "shader_program.h"
#include "static_model.h"

namespace nb = nanobind;
using namespace nb::literals;

namespace {

template <typename ModelType>
std::shared_ptr<ModelType> create_render_model(edge::RendererPtr const &renderer,
    edge::SchedulerPtr const &scheduler, std::string const &model_name)
{
    if (!renderer)
    {
        throw std::runtime_error(model_name + " requires a valid renderer");
    }

    edge::SchedulerPtr model_scheduler = scheduler;
    if (!model_scheduler)
    {
        model_scheduler = std::make_shared<edge::Scheduler>(renderer);
    }
    return std::make_shared<ModelType>(renderer, model_scheduler);
}

// cache.load<M>(filename) with other Python threads running meanwhile; the Python object is made after, with the GIL
template <typename M>
nb::object load_letting_python_run(edge::ModelCache &cache, std::string const &filename, edge::SchedulerPtr const &scheduler)
{
    std::shared_ptr<M> model;
    {
        nb::gil_scoped_release release;
        model = cache.load<M>(filename, scheduler);
    }
    return nb::cast(model);
}

template <typename ModelType>
void bind_render_model(nb::module_ &m, char const *model_name)
{
    nb::class_<ModelType>(m, model_name)
        .def(nb::new_([model_name](edge::RendererPtr const &renderer, edge::SchedulerPtr const &scheduler) {
                return create_render_model<ModelType>(renderer, scheduler, model_name);
            }),
            "renderer"_a, "scheduler"_a = edge::SchedulerPtr())
        .def("load_from_gltf", [](ModelType &self, std::string const &filename) { self.load_from_gltf(filename); },
            "filename"_a, nb::call_guard<nb::gil_scoped_release>())
        .def("save_to_file", &ModelType::save_to_file, "filename"_a)
        .def("load_from_file", &ModelType::load_from_file, "filename"_a, "keep_cpu_copy"_a = true,
            nb::call_guard<nb::gil_scoped_release>())
        .def("clear", &ModelType::clear)
        .def("pose", &ModelType::pose, "time"_a, "Every clip at time (seconds), and the joints rebuilt")
        .def("set_animation_time", pyedge::deprecated<ModelType>("Model.set_animation_time(t) is Model.pose(t)", &ModelType::pose), "time"_a)
        .def_prop_rw("always_visible", &ModelType::always_visible, &ModelType::set_always_visible,
            "Whether the model and its meshes are drawn whatever the camera sees")
        .def("set_always_visible", &ModelType::set_always_visible, "always_visible"_a)
        .def("draw", [](ModelType &model, edge::CameraPtr const &camera, rove::matrix<4,4> const &world_matrix, unsigned lod) { model.draw(camera, world_matrix, lod); },
            "camera"_a, "world_matrix"_a, "lod"_a = 0u)
        .def("draw_instances", [](ModelType &model, edge::CameraPtr const &camera, std::vector<rove::matrix<4,4>> const &world_matrices,
                unsigned lod) { model.draw_instances(camera, world_matrices, lod); },
            "camera"_a, "world_matrices"_a, "lod"_a = 0u,
            "The model once for each world matrix, in one instanced draw for each triangle group, each instance culled on its own.")
        .def("reset_previous", &ModelType::reset_previous,
            "Forgets where the model was drawn the frames before (motion vectors): from its next draw on, it was where it is")
        .def("build_octree", &ModelType::build_octree, "world_matrix"_a, nb::call_guard<nb::gil_scoped_release>())
        .def("bounds", static_cast<rove::aabb<3> (ModelType::*)(rove::matrix<4,4> const &) const>(&ModelType::bounds), "world_matrix"_a)
        .def("get_bounds", pyedge::deprecated<ModelType>("Model.get_bounds(w) is Model.bounds(w)",
            static_cast<rove::aabb<3> (ModelType::*)(rove::matrix<4,4> const &) const>(&ModelType::bounds)), "world_matrix"_a)
        .def("check", &ModelType::check)
        .def_prop_ro("lod_count", &ModelType::lod_count,
            "Levels of detail: 1 for a model made without any, else the most any mesh has (draw() takes 0 to lod_count - 1).")
        .def("lod_error", &ModelType::lod_error, "lod"_a,
            "How far level lod strays from level 0, in the model's units: the largest error of its meshes' levels (0 for level 0).")
        .def_prop_ro("joints_count", [](ModelType const &self) {
            return self.joints().size();
        })
        .def_prop_ro("meshes_count", [](ModelType const &self) {
            return self.meshes().size();
        })
        .def_prop_ro("animations", [](ModelType &self) {
            return self.animations();
        })
        .def_prop_ro("animations_count", [](ModelType const &self) {
            return self.animations().size();
        })
        .def_prop_ro("animation_duration", &ModelType::animation_duration)
        .def_prop_ro("materials_count", [](ModelType const &self) {
            return self.materials().size();
        })
        .def("material", [](ModelType &self, size_t index) {
            if (index >= self.materials().size())
            {
                throw std::out_of_range("material index is out of range");
            }
            return self.materials()[index];
        }, "index"_a)
        .def("get_material", pyedge::deprecated<ModelType>("Model.get_material(i) is Model.material(i)", [](ModelType &self, size_t index) {
            if (index >= self.materials().size())
            {
                throw std::out_of_range("material index is out of range");
            }
            return self.materials()[index];
        }), "index"_a)
        .def("set_materials_shader_program", &ModelType::set_materials_shader_program, "program"_a,
            "The shader program of every material")
        .def("set_all_material_shader_program", pyedge::deprecated<ModelType>(
            "Model.set_all_material_shader_program(p) is Model.set_materials_shader_program(p)", &ModelType::set_materials_shader_program),
            "program"_a)
        .def("clear_materials_shader_program", &ModelType::clear_materials_shader_program)
        .def("clear_all_material_shader_program", pyedge::deprecated<ModelType>(
            "Model.clear_all_material_shader_program() is Model.clear_materials_shader_program()",
            &ModelType::clear_materials_shader_program))
        .def("set_materials_extra_parameter", &ModelType::set_materials_extra_parameter, "index"_a, "value"_a,
            "Extra parameter index of every material")
        .def("set_all_material_extra_parameter", pyedge::deprecated<ModelType>(
            "Model.set_all_material_extra_parameter(i, v) is Model.set_materials_extra_parameter(i, v)",
            &ModelType::set_materials_extra_parameter),
            "index"_a, "value"_a)
        .def("joint", [](ModelType &self, size_t index) {
            if (index >= self.joints().size())
                throw std::out_of_range("joint index is out of range");
            return self.joints()[index];
        }, "index"_a)
        .def("get_joint", pyedge::deprecated<ModelType>("Model.get_joint(i) is Model.joint(i)", [](ModelType &self, size_t index) {
            if (index >= self.joints().size())
                throw std::out_of_range("joint index is out of range");
            return self.joints()[index];
        }), "index"_a);
}

}  // namespace

void bind_model(nb::module_ &m)
{
    nb::class_<edge::Joint>(m, "Joint")
        .def_prop_ro("index", [](edge::Joint const &self) { return self.index; })
        .def_prop_ro("id", [](edge::Joint const &self) { return self.id; })
        .def_prop_ro("name", [](edge::Joint const &self) { return self.name; })
        .def_prop_ro("mesh", [](edge::Joint const &self) { return self.mesh.lock(); })
        .def_prop_ro("parent", [](edge::Joint const &self) { return self.parent.lock(); },
            "The parent joint, or None for a root.")
        .def_prop_ro("matrix", [](edge::Joint const &self) { return self.matrix; },
            "A copy of the joint's model-space matrix as the last pose()\n"
            "(or the load) left it: where draw() puts a mesh hanging off this joint.");

    nb::class_<edge::Mesh>(m, "Mesh")
        .def_prop_ro("id", [](edge::Mesh const &self) { return self.id(); })
        .def_prop_ro("name", [](edge::Mesh const &self) { return self.name(); })
        .def_prop_ro("materials", [](edge::Mesh const &self) {
            std::vector<edge::MaterialPtr> result;
            for (auto const &tri : self.triangles())
                if (tri.material_ptr)
                    result.push_back(tri.material_ptr);
            return result;
        });


    nb::class_<edge::ModelBuilder>(m, "ModelBuilder")
        .def(nb::new_([](edge::RendererPtr const &renderer, edge::SchedulerPtr const &scheduler) {
                return create_render_model<edge::ModelBuilder>(renderer, scheduler, "ModelBuilder");
            }),
            "renderer"_a, "scheduler"_a = edge::SchedulerPtr())
        .def("load_from_gltf", [](edge::ModelBuilder &self, std::string const &filename) { self.load_from_gltf(filename); },
            nb::call_guard<nb::gil_scoped_release>(),
            "filename"_a)
        .def("save_to_file", &edge::ModelBuilder::save_to_file, "filename"_a)
        .def("load_from_file", &edge::ModelBuilder::load_from_file, "filename"_a, "keep_cpu_copy"_a = true,
            nb::call_guard<nb::gil_scoped_release>())
        .def("clear", &edge::ModelBuilder::clear)
        .def("add_mesh_builder", &edge::ModelBuilder::add_mesh_builder,
            "id"_a = std::string(), "name"_a = std::string())
        .def("attach_mesh_to_joint", [](edge::ModelBuilder &self, edge::MeshBuilderPtr const &mesh,
            std::string const &joint_name) {
            self.attach_mesh_to_joint(mesh, joint_name);
        }, "mesh"_a, "joint_name"_a = std::string())
        .def("mesh_by_id", &edge::ModelBuilder::mesh_by_id, "id"_a)
        .def("mesh_by_name", &edge::ModelBuilder::mesh_by_name, "name"_a)
        .def("get_mesh_by_id", pyedge::deprecated<edge::ModelBuilder>("ModelBuilder.get_mesh_by_id(i) is ModelBuilder.mesh_by_id(i)",
            &edge::ModelBuilder::mesh_by_id), "id"_a)
        .def("get_mesh_by_name", pyedge::deprecated<edge::ModelBuilder>("ModelBuilder.get_mesh_by_name(n) is ModelBuilder.mesh_by_name(n)",
            &edge::ModelBuilder::mesh_by_name), "name"_a)
        .def("pose", &edge::ModelBuilder::pose, "time"_a, "Every clip at time (seconds), and the joints rebuilt")
        .def("set_animation_time", pyedge::deprecated<edge::ModelBuilder>("ModelBuilder.set_animation_time(t) is ModelBuilder.pose(t)", &edge::ModelBuilder::pose), "time"_a)
        .def_prop_rw("always_visible", &edge::ModelBuilder::always_visible, &edge::ModelBuilder::set_always_visible,
            "Whether the model and its meshes are drawn whatever the camera sees")
        .def("set_always_visible", &edge::ModelBuilder::set_always_visible, "always_visible"_a)
        .def("draw", [](edge::ModelBuilder &model, edge::CameraPtr const &camera, rove::matrix<4,4> const &world_matrix, unsigned lod) { model.draw(camera, world_matrix, lod); },
            "camera"_a, "world_matrix"_a, "lod"_a = 0u)
        .def("build_octree", &edge::ModelBuilder::build_octree, "world_matrix"_a, nb::call_guard<nb::gil_scoped_release>())
        .def("bounds", static_cast<rove::aabb<3> (edge::ModelBuilder::*)(rove::matrix<4,4> const &) const>(&edge::ModelBuilder::bounds), "world_matrix"_a)
        .def("get_bounds", pyedge::deprecated<edge::ModelBuilder>("ModelBuilder.get_bounds(w) is ModelBuilder.bounds(w)",
            static_cast<rove::aabb<3> (edge::ModelBuilder::*)(rove::matrix<4,4> const &) const>(&edge::ModelBuilder::bounds)), "world_matrix"_a)
        .def("check", &edge::ModelBuilder::check)
        .def_prop_ro("joints_count", [](edge::ModelBuilder const &self) {
            return self.joints().size();
        })
        .def_prop_ro("meshes_count", [](edge::ModelBuilder const &self) {
            return self.meshes().size();
        })
        .def_prop_ro("animations", [](edge::ModelBuilder &self) {
            return self.animations();
        })
        .def_prop_ro("animations_count", [](edge::ModelBuilder const &self) {
            return self.animations().size();
        })
        .def_prop_ro("animation_duration", &edge::ModelBuilder::animation_duration)
        .def_prop_ro("materials_count", [](edge::ModelBuilder const &self) {
            return self.materials().size();
        })
        .def("material", [](edge::ModelBuilder &self, size_t index) {
            if (index >= self.materials().size())
            {
                throw std::out_of_range("material index is out of range");
            }
            return self.materials()[index];
        }, "index"_a)
        .def("get_material", pyedge::deprecated<edge::ModelBuilder>("ModelBuilder.get_material(i) is ModelBuilder.material(i)", [](edge::ModelBuilder &self, size_t index) {
            if (index >= self.materials().size())
            {
                throw std::out_of_range("material index is out of range");
            }
            return self.materials()[index];
        }), "index"_a)
        .def("set_materials_shader_program", &edge::ModelBuilder::set_materials_shader_program, "program"_a,
            "The shader program of every material")
        .def("set_all_material_shader_program", pyedge::deprecated<edge::ModelBuilder>(
            "ModelBuilder.set_all_material_shader_program(p) is ModelBuilder.set_materials_shader_program(p)",
            &edge::ModelBuilder::set_materials_shader_program),
            "program"_a)
        .def("clear_materials_shader_program", &edge::ModelBuilder::clear_materials_shader_program)
        .def("clear_all_material_shader_program", pyedge::deprecated<edge::ModelBuilder>(
            "ModelBuilder.clear_all_material_shader_program() is ModelBuilder.clear_materials_shader_program()",
            &edge::ModelBuilder::clear_materials_shader_program))
        .def("set_materials_extra_parameter", &edge::ModelBuilder::set_materials_extra_parameter, "index"_a, "value"_a,
            "Extra parameter index of every material")
        .def("set_all_material_extra_parameter", pyedge::deprecated<edge::ModelBuilder>(
            "ModelBuilder.set_all_material_extra_parameter(i, v) is ModelBuilder.set_materials_extra_parameter(i, v)",
            &edge::ModelBuilder::set_materials_extra_parameter), "index"_a, "value"_a);

    bind_render_model<edge::StaticModelPNT>(m, "StaticModelPNT");
    bind_render_model<edge::StaticModelPTBNT>(m, "StaticModelPTBNT");
    bind_render_model<edge::AnimatedModelPNJWT>(m, "AnimatedModelPNJWT");
    bind_render_model<edge::AnimatedModelPTBNJWT>(m, "AnimatedModelPTBNJWT");
    bind_render_model<edge::StaticModelPTBNTC>(m, "StaticModelPTBNTC");
    bind_render_model<edge::AnimatedModelPTBNJWTC>(m, "AnimatedModelPTBNJWTC");

    m.def("load_model", [](edge::RendererPtr const &renderer, edge::SchedulerPtr const &scheduler, std::string const &filename,
            bool keep_cpu_copy) -> nb::object {
            edge::GltfOptions options;
            options.keep_cpu_copy = keep_cpu_copy;
            edge::ModelPtr const model = edge::load_model(renderer, scheduler ? scheduler : std::make_shared<edge::Scheduler>(renderer),
                filename, options);
            // (each its class: nanobind hands a model out by the type it is bound as)
            if (auto typed = std::dynamic_pointer_cast<edge::StaticModelPTBNT>(model))
                return nb::cast(typed);
            if (auto typed = std::dynamic_pointer_cast<edge::StaticModelPTBNTC>(model))
                return nb::cast(typed);
            if (auto typed = std::dynamic_pointer_cast<edge::StaticModelPNT>(model))
                return nb::cast(typed);
            if (auto typed = std::dynamic_pointer_cast<edge::AnimatedModelPTBNJWT>(model))
                return nb::cast(typed);
            if (auto typed = std::dynamic_pointer_cast<edge::AnimatedModelPTBNJWTC>(model))
                return nb::cast(typed);
            if (auto typed = std::dynamic_pointer_cast<edge::AnimatedModelPNJWT>(model))
                return nb::cast(typed);
            throw std::logic_error("load_model: a model of a class pyedge does not bind");
        }, "renderer"_a, "scheduler"_a = edge::SchedulerPtr(), "filename"_a, "keep_cpu_copy"_a = true,
        "A model of the class its file is: a .edge file's meshes name it, a glTF's contents decide it as edge_convert does\n"
        "(StaticModelPTBNT, StaticModelPTBNTC, AnimatedModelPTBNJWT, AnimatedModelPTBNJWTC, ...).  See edge/src/load_model.h.");

#if defined(EDGE_PYEDGE_ASSET_TOOLS)
    m.def("convert_gltf_to_edge",
        [](std::string const &gltf_file, std::string const &edge_file, std::vector<float> const &lods,
            std::size_t min_triangles, bool lock_border) {
            edge::LodOptions options;
            options.ratios = lods;
            options.min_triangles = min_triangles;
            options.lock_border = lock_border;
            return edge::convert_gltf_to_edge(gltf_file, edge_file, edge::GltfOptions(), options);
        },
        "gltf_file"_a, "edge_file"_a, "lods"_a = std::vector<float>(), "min_triangles"_a = 300, "lock_border"_a = false,
        "edge_convert as a function: writes edge_file from gltf_file with the default GltfOptions.  lods gives every mesh of at least\n"
        "min_triangles a coarser version for each ratio (falling, each in (0, 1)) of its triangles.  Returns the triangles of the whole\n"
        "model at each level, level 0 first.");
#endif

    nb::class_<edge::ModelCache>(m, "ModelCache",
        "Reads each model file once per renderer; every load() is a new model\n"
        "sharing the file's GPU buffers and textures, with its own joints,\n"
        "animations and materials.  See edge/src/model_cache.h.")
        .def(nb::init<edge::RendererPtr const &>(), "renderer"_a)
        .def("sweep", &edge::ModelCache::sweep,
            "Forget the models no instance holds any more (their memory went with their last instance); returns how many.")
        .def("load", [](edge::ModelCache &self, nb::handle cls, std::string const &filename,
                edge::SchedulerPtr const &scheduler) -> nb::object {
                if (cls.is(nb::type<edge::StaticModelPNT>()))
                    return load_letting_python_run<edge::StaticModelPNT>(self, filename, scheduler);
                if (cls.is(nb::type<edge::StaticModelPTBNT>()))
                    return load_letting_python_run<edge::StaticModelPTBNT>(self, filename, scheduler);
                if (cls.is(nb::type<edge::AnimatedModelPNJWT>()))
                    return load_letting_python_run<edge::AnimatedModelPNJWT>(self, filename, scheduler);
                if (cls.is(nb::type<edge::AnimatedModelPTBNJWT>()))
                    return load_letting_python_run<edge::AnimatedModelPTBNJWT>(self, filename, scheduler);
                if (cls.is(nb::type<edge::StaticModelPTBNTC>()))
                    return load_letting_python_run<edge::StaticModelPTBNTC>(self, filename, scheduler);
                if (cls.is(nb::type<edge::AnimatedModelPTBNJWTC>()))
                    return load_letting_python_run<edge::AnimatedModelPTBNJWTC>(self, filename, scheduler);
                throw nb::type_error("ModelCache.load: cls must be one of StaticModelPNT, StaticModelPTBNT, StaticModelPTBNTC, "
                    "AnimatedModelPNJWT, AnimatedModelPTBNJWT, AnimatedModelPTBNJWTC");
            }, "cls"_a, "filename"_a, "scheduler"_a,
            "A new model of class cls holding filename, drawing through scheduler.")
        .def("clear", &edge::ModelCache::clear)
        .def_prop_ro("renderer", &edge::ModelCache::renderer)
        .def_prop_ro("prototypes_count", &edge::ModelCache::prototypes_count);
}
