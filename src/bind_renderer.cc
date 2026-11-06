// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include "python_bindings.h"

#include <memory>
#include <optional>
#include <stdexcept>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

#include <nanobind/nanobind.h>
#include <nanobind/ndarray.h>
#include <nanobind/stl/optional.h>
#include <nanobind/stl/shared_ptr.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/vector.h>

#include <rove/matrix.h>

#include "draw_call.h"
#include "index.h"
#include "material.h"
#include "null_renderer.h"
#include "pixel.h"
#include "render_target.h"
#include "scheduler.h"
#include "shader_dirs.h"
#include "shader_program.h"
#include "texture.h"
#include "uber_draw_call.h"
#include "vertex.h"
#include "vertex_traits.h"
#if defined(EDGE_USE_VULKAN)
#include "vulkan_renderer.h"
#include "vulkan_device_info.h"
#include "vulkan_pipeline_builder.h"
#endif

namespace nb = nanobind;
using namespace nb::literals;

namespace {

edge::VertexTraits const *resolve_vertex_traits(unsigned format)
{
    switch (format)
    {
    case edge::VERTEX_P::FORMAT:
        return edge::VertexTraits::of<edge::VERTEX_P>();
    case edge::VERTEX_PT::FORMAT:
        return edge::VertexTraits::of<edge::VERTEX_PT>();
    case edge::VERTEX_PNT::FORMAT:
        return edge::VertexTraits::of<edge::VERTEX_PNT>();
    case edge::VERTEX_PNTB::FORMAT:
        return edge::VertexTraits::of<edge::VERTEX_PNTB>();
    case edge::VERTEX_PTBNT::FORMAT:
        return edge::VertexTraits::of<edge::VERTEX_PTBNT>();
    case edge::VERTEX_PTBNTC::FORMAT:
        return edge::VertexTraits::of<edge::VERTEX_PTBNTC>();
    case edge::VERTEX_PNJWT::FORMAT:
        return edge::VertexTraits::of<edge::VERTEX_PNJWT>();
    case edge::VERTEX_PTBNJWT::FORMAT:
        return edge::VertexTraits::of<edge::VERTEX_PTBNJWT>();
    case edge::VERTEX_PTBNJWTC::FORMAT:
        return edge::VertexTraits::of<edge::VERTEX_PTBNJWTC>();
    default:
        throw std::runtime_error("unsupported vertex format");
    }
}

size_t resolve_vertex_stride(unsigned format)
{
    switch (format)
    {
    case edge::VERTEX_P::FORMAT:
        return sizeof(edge::VERTEX_P);
    case edge::VERTEX_PT::FORMAT:
        return sizeof(edge::VERTEX_PT);
    case edge::VERTEX_PNT::FORMAT:
        return sizeof(edge::VERTEX_PNT);
    case edge::VERTEX_PNTB::FORMAT:
        return sizeof(edge::VERTEX_PNTB);
    case edge::VERTEX_PTBNT::FORMAT:
        return sizeof(edge::VERTEX_PTBNT);
    case edge::VERTEX_PTBNTC::FORMAT:
        return sizeof(edge::VERTEX_PTBNTC);
    case edge::VERTEX_PNJWT::FORMAT:
        return sizeof(edge::VERTEX_PNJWT);
    case edge::VERTEX_PTBNJWT::FORMAT:
        return sizeof(edge::VERTEX_PTBNJWT);
    case edge::VERTEX_PTBNJWTC::FORMAT:
        return sizeof(edge::VERTEX_PTBNJWTC);
    default:
        throw std::runtime_error("unsupported vertex format");
    }
}

#if defined(EDGE_USE_VULKAN)
// The renderer as Vulkan's, for what only Vulkan keeps; RuntimeError naming `what` for another
edge::VulkanRenderer &vulkan_of(edge::Renderer &renderer, char const *what)
{
    if (auto *vk = dynamic_cast<edge::VulkanRenderer *>(&renderer))
        return *vk;
    throw std::runtime_error(std::string(what) + " is only kept by the Vulkan backend");
}
#endif

std::shared_ptr<edge::Renderer> create_null_renderer(int width, int height)
{
    auto renderer = std::make_shared<edge::NullRenderer>(width, height);
    return renderer;
}

#if defined(EDGE_USE_VULKAN)
std::shared_ptr<edge::Renderer> create_vulkan_renderer(int width, int height, std::string const &device_uuid,
    std::optional<int> joint_matrices, std::optional<int> frames_in_flight)
{
    auto renderer = std::make_shared<edge::VulkanRenderer>(width, height, device_uuid, std::vector<std::string>(),
        frames_in_flight.value_or(1), joint_matrices.value_or(edge::VulkanRenderer::DEFAULT_JOINT_MATRICES));
    return renderer;
}
#endif

nb::list enumerate_physical_devices()
{
#if defined(EDGE_USE_VULKAN)
    nb::list out;
    std::vector<edge::PhysicalDeviceInfo> const devices = edge::enumerate_vulkan_physical_devices();
    for (auto const &device : devices)
    {
        nb::dict entry;
        entry["name"] = device.name;
        entry["uuid"] = device.uuid;
        entry["pci_bus"] = device.pci_bus;
        entry["discrete"] = device.discrete;
        out.append(entry);
    }
    return out;
#else
    return nb::list();
#endif
}

std::string renderer_device_uuid(edge::Renderer const &renderer)
{
#if defined(EDGE_USE_VULKAN)
    auto const *vulkan = dynamic_cast<edge::VulkanRenderer const *>(&renderer);
    if (vulkan != nullptr)
        return vulkan->device_uuid();
#endif
    return std::string();
}

std::vector<std::string> available_renderer_backends()
{
    std::vector<std::string> backends;
    backends.push_back("auto");
#if defined(EDGE_USE_VULKAN)
    backends.push_back("vulkan");
#endif
    backends.push_back("null");
    return backends;
}

std::string resolve_renderer_backend(edge::Renderer const &renderer)
{
#if defined(EDGE_USE_VULKAN)
    if (dynamic_cast<edge::VulkanRenderer const *>(&renderer) != nullptr)
        return "vulkan";
#endif
    if (dynamic_cast<edge::NullRenderer const *>(&renderer) != nullptr)
        return "null";
    return "unknown";
}

std::shared_ptr<edge::Renderer> create_renderer(int width, int height, std::string const &backend,
    bool offscreen, std::string const &device_uuid, std::optional<int> joint_matrices, std::optional<int> frames_in_flight)
{
    if (!offscreen)
        throw nb::value_error("a pyedge renderer draws offscreen, into render targets: there is no window to draw into");

    if (backend == "null")
    {
        if (!device_uuid.empty())
            throw std::runtime_error("the null backend has no device to select by uuid");
        if (joint_matrices)
            throw std::runtime_error("the null backend has no joint_matrices to size");
        if (frames_in_flight)
            throw std::runtime_error("the null backend keeps no frames in flight (it has one)");
        return create_null_renderer(width, height);
    }

    // auto: the GPU this build has; a GPU that cannot be had is an error, not the null renderer, which draws nothing
    if (backend == "vulkan" || backend == "auto")
    {
#if defined(EDGE_USE_VULKAN)
        return create_vulkan_renderer(width, height, device_uuid, joint_matrices, frames_in_flight);
#else
        throw std::runtime_error("pyedge is built without Vulkan: backend '" + backend + "' is not there");
#endif
    }

    throw std::runtime_error("unsupported renderer backend: " + backend);
}

}  // namespace

void bind_renderer(nb::module_ &m)
{
    // The formats are enums that are numbers too: a function that takes one takes its number, and pyedge.PIXEL_FORMAT_* and
    // INDEX_FORMAT_* (the names before 2026-11) are those numbers.
    auto pixel_format = nb::enum_<edge::PixelFormat>(m, "PixelFormat", nb::is_arithmetic());
    for (auto [name, format] : std::initializer_list<std::pair<char const *, edge::PixelFormat>>{
             {"X8R8G8B8", edge::PixelFormat::X8R8G8B8}, {"A8R8G8B8", edge::PixelFormat::A8R8G8B8},
             {"R16G16B16A16F", edge::PixelFormat::R16G16B16A16F}, {"R32G32B32A32F", edge::PixelFormat::R32G32B32A32F},
             {"R32F", edge::PixelFormat::R32F}, {"R8UI", edge::PixelFormat::R8UI}, {"BC1", edge::PixelFormat::BC1},
             {"BC3", edge::PixelFormat::BC3}, {"BC5", edge::PixelFormat::BC5}, {"BC7", edge::PixelFormat::BC7},
             {"ASTC_4x4", edge::PixelFormat::ASTC_4x4}, {"ASTC_6x6", edge::PixelFormat::ASTC_6x6},
             {"ASTC_8x8", edge::PixelFormat::ASTC_8x8}})
    {
        pixel_format.value(name, format);
        m.attr((std::string("PIXEL_FORMAT_") + name).c_str()) = nb::int_(static_cast<unsigned>(format));
    }
    m.attr("PIXEL_FORMAT_DEFAULT") = nb::int_(static_cast<unsigned>(edge::DEFAULT_PIXEL_FORMAT));
    m.attr("PIXEL_FORMAT_DEFAULT_ALPHA") = nb::int_(static_cast<unsigned>(edge::DEFAULT_ALPHA_PIXEL_FORMAT));

    nb::enum_<edge::IndexFormat>(m, "IndexFormat", nb::is_arithmetic())
        .value("U16", edge::IndexFormat::U16)
        .value("U32", edge::IndexFormat::U32);
    m.attr("INDEX_FORMAT_U16") = nb::int_(static_cast<unsigned>(edge::IndexFormat::U16));
    m.attr("INDEX_FORMAT_U32") = nb::int_(static_cast<unsigned>(edge::IndexFormat::U32));

    m.attr("FRAME_CONSTANTS_SIZE") = nb::int_(edge::Renderer::FRAME_CONSTANTS_SIZE);
    m.attr("GLOBAL_TEXTURES_COUNT") = nb::int_(edge::Renderer::GLOBAL_TEXTURES_COUNT);
    m.attr("MAX_INSTANCES") = nb::int_(edge::Renderer::MAX_INSTANCES);

    // VERTEX_FORMAT_P: position-only (3 floats, 12 bytes).
    m.attr("VERTEX_FORMAT_P")      = nb::int_(edge::VERTEX_P::FORMAT);
    m.attr("VERTEX_FORMAT_PT")     = nb::int_(edge::VERTEX_PT::FORMAT);
    m.attr("VERTEX_FORMAT_PNT")    = nb::int_(edge::VERTEX_PNT::FORMAT);
    m.attr("VERTEX_FORMAT_PNTB")   = nb::int_(edge::VERTEX_PNTB::FORMAT);
    m.attr("VERTEX_FORMAT_PTBNT")  = nb::int_(edge::VERTEX_PTBNT::FORMAT);
    m.attr("VERTEX_FORMAT_PTBNTC") = nb::int_(edge::VERTEX_PTBNTC::FORMAT);
    m.attr("VERTEX_FORMAT_PNJWT")  = nb::int_(edge::VERTEX_PNJWT::FORMAT);
    m.attr("VERTEX_FORMAT_PTBNJWT") = nb::int_(edge::VERTEX_PTBNJWT::FORMAT);
    m.attr("VERTEX_FORMAT_PTBNJWTC") = nb::int_(edge::VERTEX_PTBNJWTC::FORMAT);

    m.def("vertex_stride", &resolve_vertex_stride, "vertex_format"_a,
        "Return vertex size in bytes for a known VERTEX_FORMAT_* value.");
    m.def("available_renderer_backends", &available_renderer_backends,
        "Return list of renderer backends supported by this build.");
    m.def("enumerate_physical_devices", &enumerate_physical_devices,
        "List the Vulkan physical devices on this machine as dicts with keys "
        "'name', 'uuid', 'pci_bus' and 'discrete'. The uuid is the key to pass "
        "to create_renderer(device_uuid=...); for NVIDIA devices it is the same "
        "value nvidia-smi --query-gpu=uuid reports.");
#if defined(EDGE_USE_VULKAN)
    m.def("vulkan_shader_dir", &edge::vulkan_shader_dir,
        "The directory edge's own SPIR-V (vertex.spv, fragment.spv, ...) is read from: "
        "$EDGE_VK_SHADER_DIR when set, else the first that holds it of shaders/edge beside this module (a wheel's), "
        "../share/edge/shaders beside it (an install's), the build's directory and the install's.  "
        "RuntimeError naming every place tried when none does.");
#endif
    m.def("view_shader_dir", [] { return edge::view_shader_dir().string(); },
        "The directory of edge's view shaders (lit.frag.spv: edge_shot's light, for a model viewer): $EDGE_VIEW_SHADER_DIR when set, "
        "else the first that holds them of view_shaders beside this module, ../share/edge/view_shaders beside it, the build's "
        "directory and the install's.  RuntimeError naming every place tried when none does.");
    m.def("create_renderer", &create_renderer,
        "width"_a = 800, "height"_a = 600, "backend"_a = std::string("auto"), "offscreen"_a = true,
        "device_uuid"_a = std::string(), "joint_matrices"_a = nb::none(), "frames_in_flight"_a = nb::none(),
        "Create renderer using backend: auto (the GPU: vulkan) | vulkan | null; a GPU that cannot be had is an error. "
        "The renderer draws offscreen (offscreen=False is a ValueError). "
        "device_uuid picks which GPU to run on (see enumerate_physical_devices); "
        "empty takes the first suitable one, which is PCI bus order. "
        "joint_matrices (Vulkan): how many joint matrices a frame's skinned draws have room for, 64 bytes of host-visible "
        "memory each; None keeps the default (1024 draws of 256 joints, 16 MB); $EDGE_VK_JOINT_MATRICES, when set, is taken instead. "
        "frames_in_flight (Vulkan, 1 to 4, ValueError otherwise; None is 1): how many frames submit_frame() lets the GPU draw while "
        "the host builds the next.");

    auto frame_constants_of_bytes = [](edge::Renderer &self, nb::bytes const &data) { self.set_frame_constants(data.c_str(), data.size()); };
    auto frame_constants_of_array = [](edge::Renderer &self, nb::ndarray<nb::ro, nb::ndim<1>, nb::c_contig, nb::device::cpu> const &data) {
        self.set_frame_constants(data.data(), data.nbytes());
    };

    nb::class_<edge::Renderer>(m, "Renderer")
        .def(nb::new_(&create_renderer),
            "width"_a = 800, "height"_a = 600, "backend"_a = std::string("null"),
            "offscreen"_a = true, "device_uuid"_a = std::string(), "joint_matrices"_a = nb::none(),
            "frames_in_flight"_a = nb::none())
        .def_prop_ro("device_uuid", &renderer_device_uuid,
            "UUID of the GPU this renderer is actually running on; empty for "
            "the null backend.")
        .def_prop_ro("backend", &resolve_renderer_backend)
        .def_prop_ro("screen_width", &edge::Renderer::screen_width)
        .def_prop_ro("screen_height", &edge::Renderer::screen_height)
        .def("resize", &edge::Renderer::resize, "width"_a, "height"_a)
        // the frame's calls that wait for the GPU let other Python threads run (none may use this renderer meanwhile: README, Threads)
        .def("begin_frame", &edge::Renderer::begin_frame, nb::call_guard<nb::gil_scoped_release>(),
            "Begins a frame, waiting first for the oldest frame in flight when as many as the renderer keeps are in flight")
        .def("finish_frame", &edge::Renderer::finish_frame, nb::call_guard<nb::gil_scoped_release>(),
            "Ends the frame and waits until the GPU has drawn it")
        .def("submit_frame", &edge::Renderer::submit_frame, nb::call_guard<nb::gil_scoped_release>(),
            "Ends the frame and hands it to the GPU without waiting (Vulkan; as finish_frame on the null backend): the host builds "
            "the next frame meanwhile, and a download still sees what it drew")
        .def_prop_ro("frames_in_flight", [](edge::Renderer const &self) -> int {
#if defined(EDGE_USE_VULKAN)
                if (auto const *vk = dynamic_cast<edge::VulkanRenderer const *>(&self))
                    return vk->frames_in_flight();
#endif
                (void)self;
                return 1;
            }, "How many frames submit_frame() keeps in flight: 1 but where the renderer was made with more")
        .def_prop_ro("frame_index", &edge::Renderer::frame_index,
            "The frame a draw call committed now is drawn in, from 0: the open one, or between frames the next; the frames ended "
            "before it (what a draw call's previous frame is)")
        .def_prop_ro("frame_serial", &edge::Renderer::frame_serial,
            "Frames begun since the renderer was made: the number of the frame open, or of the last one (0 before the first)")
        .def("wait_frames", [](edge::Renderer &self) {
#if defined(EDGE_USE_VULKAN)
                edge::VulkanRenderer &vk = vulkan_of(self, "wait_frames");
                nb::gil_scoped_release release;
                vk.wait_frames();
#else
                (void)self;
                throw std::runtime_error("wait_frames: Vulkan backend not compiled into this edge build");
#endif
            }, "Waits for every frame submitted (Vulkan only); RuntimeError inside a frame")
        .def("set_frame_constants", frame_constants_of_bytes, "data"_a)
        .def("set_frame_constants", frame_constants_of_array, "data"_a,
            "The bytes every shader reads at binding 9 (std140 is the caller's business): bytes or a 1-D array, up to "
            "FRAME_CONSTANTS_SIZE (ValueError past it). Between frames only (RuntimeError inside one); they stay until the next call.")
        .def("set_global_texture", &edge::Renderer::set_global_texture, "slot"_a, "texture"_a.none(),
            "Global texture slot (binding 12 + slot, below GLOBAL_TEXTURES_COUNT: IndexError otherwise) for every draw; None is "
            "white. Between frames only (RuntimeError inside one).")
        .def("set_global_shadow_texture", &edge::Renderer::set_global_shadow_texture, "slot"_a, "depth"_a.none(),
            "A render target's readable depth in a global slot, sampled with a comparison (a sampler2DShadow); ValueError for "
            "anything but a readable depth of this renderer")
        .def_prop_ro("default_render_target", &edge::Renderer::default_render_target)
        .def("get_default_render_target", pyedge::deprecated("Renderer.get_default_render_target() is Renderer.default_render_target",
            &edge::Renderer::default_render_target))
        .def("create_render_target",
            static_cast<edge::RenderTargetPtr (edge::Renderer::*)(int, int, edge::PixelFormat, bool)>(&edge::Renderer::create_render_target),
            "width"_a, "height"_a,
            "pixel_format"_a = edge::DEFAULT_PIXEL_FORMAT, "readable_depth"_a = false)
        .def("create_render_target",
            [](edge::Renderer &self, int width, int height, std::vector<edge::PixelFormat> const &color_formats, bool readable_depth, int layers,
                bool cube) {
                edge::RenderTargetFormat format;
                format.color_formats = color_formats;
                format.readable_depth = readable_depth;
                format.layers = layers;
                format.cube = cube;
                return self.create_render_target(width, height, format);
            },
            "width"_a, "height"_a, "color_formats"_a, "readable_depth"_a = false, "layers"_a = 0, "cube"_a = false,
            "A target of these colour attachments (a pixel format each, fragment output i to attachment i; [] with readable_depth: depth "
            "alone), of layers (an array of each, drawn a layer at a time: target.layer(i)) or a cube's six faces. ValueError for what "
            "cannot be made, saying why (more colours than max_color_attachments, layers past the device's, a cube not square).")
        .def_prop_ro("max_color_attachments", &edge::Renderer::max_color_attachments,
            "The colour attachments a target can have here: the device's, at least 4 (the null backend's are 4)")
        .def("batch_download_textures",
            [](edge::Renderer &self, std::vector<edge::TexturePtr> const &textures) {
                nb::gil_scoped_release release;
                self.batch_download_textures(textures);
            },
            "textures"_a,
            "Download multiple textures GPU→CPU in a single submit + wait. "
            "Equivalent to calling Texture.download() on each one, but coalesced "
            "into one command buffer; on NVIDIA Vulkan this avoids paying a full "
            "vkQueueWaitIdle pipeline drain per texture. Must be called outside "
            "an active frame/pass.")
        .def_prop_ro("has_gpu_timer", &edge::Renderer::has_gpu_timer,
            "Whether the renderer can time its passes on the GPU (Vulkan where its queue writes timestamps; never the null backend)")
        .def_prop_rw("gpu_trace_enabled", &edge::Renderer::gpu_trace_enabled,
            &edge::Renderer::set_gpu_trace_enabled,
            "Set, every pass of the frames begun from now on is timed: its duration comes once the GPU has drawn the frame "
            "(pass_times_us(frame)).  Turning it off drops the durations.  RuntimeError inside a frame, and turned on "
            "where the renderer has no GPU timer (has_gpu_timer), saying why.")
        .def("set_gpu_trace_enabled", &edge::Renderer::set_gpu_trace_enabled, "enabled"_a)
        .def_prop_ro("single_time_submit_count",
            [](edge::Renderer const &self) -> uint64_t {
#if defined(EDGE_USE_VULKAN)
                auto const *vk = dynamic_cast<edge::VulkanRenderer const *>(&self);
                if (vk == nullptr)
                {
                    throw std::runtime_error(
                        "single_time_submit_count is only kept by the Vulkan backend");
                }
                return vk->single_time_submit_count();
#else
                (void)self;
                throw std::runtime_error("Vulkan backend not compiled into this edge build");
#endif
            },
            "Waited-on single-time submits (uploads, downloads, layout transitions) "
            "this renderer has made (Vulkan only). Frames are not counted.")
        .def_prop_ro("material_descriptor_set_count",
            [](edge::Renderer const &self) -> size_t {
#if defined(EDGE_USE_VULKAN)
                auto const *vk = dynamic_cast<edge::VulkanRenderer const *>(&self);
                if (vk == nullptr)
                {
                    throw std::runtime_error(
                        "material_descriptor_set_count is only kept by the Vulkan backend");
                }
                return vk->material_descriptor_set_count();
#else
                (void)self;
                throw std::runtime_error("Vulkan backend not compiled into this edge build");
#endif
            },
            "Descriptor sets the renderer holds for materials (Vulkan only): "
            "one per material it has drawn that is still alive.  A dead "
            "material's set is freed at the next begin_frame.")
        .def_prop_ro("last_pass_times_us", &edge::Renderer::last_pass_times_us,
            "Per-pass GPU times in microseconds, in begin_pass order, of the latest timed frame read back "
            "(last_pass_times_frame names it: with frames in flight an earlier frame than the last ended). Empty while "
            "tracing is off, and before a timed frame is read back.")
        .def("get_last_pass_times_us", pyedge::deprecated("Renderer.get_last_pass_times_us() is Renderer.last_pass_times_us",
            &edge::Renderer::last_pass_times_us))
        .def_prop_ro("last_pass_times_frame", &edge::Renderer::last_pass_times_frame,
            "The frame (frame_serial) last_pass_times_us is of; 0 for none")
        .def("get_pass_times_us", pyedge::deprecated("Renderer.get_pass_times_us(f) is Renderer.pass_times_us(f)",
            &edge::Renderer::pass_times_us), "frame"_a)
        .def("pass_times_us", &edge::Renderer::pass_times_us, "frame"_a,
            "The per-pass GPU times (microseconds, begin_pass order) of frame `frame` (as frame_serial numbers it) once the "
            "GPU has drawn it and they are read back: after finish_frame, or with frames in flight once the frame is waited "
            "for (wait_frames, or the begin_frame that reuses its share), while it is one of the last 16 read back.  None "
            "before, after, for a frame begun untimed, and for a disjoint one (is_gpu_time_disjoint).")
        .def("is_gpu_time_disjoint", &edge::Renderer::is_gpu_time_disjoint, "frame"_a,
            "Whether frame `frame` was timed and the GPU could not measure it (OpenGL ES's disjoint): its times never come")
        .def("begin_pass",
            [](edge::Renderer &self, edge::RenderTargetPtr const &target) {
                self.begin_pass(target, edge::RenderPassClearState());
            },
            "target"_a)
        .def("begin_pass",
            [](edge::Renderer &self, edge::RenderTargetPtr const &target,
                edge::RenderPassClearState const &clear_state) {
                self.begin_pass(target, clear_state);
            },
            "target"_a, "clear_state"_a)
        .def("begin_pass",
            [](edge::Renderer &self, edge::RenderTargetPtr const &target,
                edge::RenderPassClearState const &clear_state, uint32_t pass_type) {
                self.begin_pass(target, clear_state, pass_type);
            },
            "target"_a, "clear_state"_a, "pass_type"_a)
        .def("end_pass", &edge::Renderer::end_pass)
        .def("create_texture",
            [](edge::Renderer &self, int width, int height, edge::PixelFormat pixel_format, std::optional<bool> dynamic,
                std::optional<bool> writeonly) {
                if (dynamic || writeonly)
                    pyedge::warn_deprecated("create_texture: dynamic and writeonly do nothing; leave them out");
                return self.create_texture(width, height, pixel_format);
            },
            "width"_a, "height"_a,
            "pixel_format"_a = edge::DEFAULT_PIXEL_FORMAT,
            "dynamic"_a = nb::none(), "writeonly"_a = nb::none())
        .def("load_texture", [](edge::Renderer &self, std::string const &filename, bool compress) {
            return edge::Texture::load_from_file(self.shared_from_this(), filename, compress);
        }, "filename"_a, "compress"_a = true)
        .def("create_material", &edge::Renderer::create_material)
        .def("create_shader_program", &edge::Renderer::create_shader_program,
            "vertex_shader_filename"_a, "fragment_shader_filename"_a)
        .def("load_shader_program",
            [](edge::Renderer &self, std::string const &dir, std::string const &name) { return self.load_shader_program(dir, name); },
            "dir"_a, "name"_a, "The program of <dir>/<name>.vert.spv and .frag.spv; RuntimeError naming a file that is not there.")
        .def("create_vertex_buffer",
            [](edge::Renderer &self, unsigned vertex_format, size_t size, bool dynamic, std::optional<bool> writeonly) {
                if (writeonly)
                    pyedge::warn_deprecated("create_vertex_buffer: writeonly does nothing; leave it out");
                edge::VertexTraits const *traits = resolve_vertex_traits(vertex_format);
                return self.create_vertex_buffer(traits, size, dynamic ? edge::BufferUsage::DYNAMIC : edge::BufferUsage::STATIC);
            },
            "vertex_format"_a, "size"_a, "dynamic"_a = false, "writeonly"_a = nb::none(),
            "A buffer of size bytes; dynamic: one rewritten every frame or so.")
        .def("create_index_buffer",
            [](edge::Renderer &self, size_t size, edge::IndexFormat index_format, bool dynamic, std::optional<bool> writeonly) {
                if (writeonly)
                    pyedge::warn_deprecated("create_index_buffer: writeonly does nothing; leave it out");
                return self.create_index_buffer(index_format, size, dynamic ? edge::BufferUsage::DYNAMIC : edge::BufferUsage::STATIC);
            },
            "size"_a, "index_format"_a = edge::IndexFormat::U16,
            "dynamic"_a = false, "writeonly"_a = nb::none(),
            "A buffer of size bytes; dynamic: one rewritten every frame or so.")
        .def("create_draw_call", &edge::Renderer::create_draw_call)
        .def("create_scheduler", [](edge::Renderer &self) {
            return std::make_shared<edge::Scheduler>(self.shared_from_this());
        })
        .def("create_uber_draw_call", &edge::Renderer::create_uber_draw_call);
}
