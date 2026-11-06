// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include "python_bindings.h"

#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <nanobind/nanobind.h>
#include <nanobind/ndarray.h>
#include <nanobind/stl/shared_ptr.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/vector.h>

#include "pixel.h"
#include "render_target.h"
#include "texture.h"

namespace nb = nanobind;
using namespace nb::literals;

namespace {

size_t texture_total_size(edge::Texture const &texture)
{
    int const bits = edge::bits_per_pixel(texture.pixel_format());
    if (bits <= 0 || (bits % 8) != 0)
    {
        throw std::runtime_error("unsupported texture pixel format for raw upload");
    }

    size_t const bytes_per_pixel = static_cast<size_t>(bits / 8);
    size_t const width = static_cast<size_t>(texture.width());
    size_t const height = static_cast<size_t>(texture.height());

    if (width > 0 && height > (std::numeric_limits<size_t>::max() / width))
    {
        throw std::runtime_error("texture dimensions are too large");
    }

    size_t const pixel_count = width * height;
    if (bytes_per_pixel > 0 && pixel_count > (std::numeric_limits<size_t>::max() / bytes_per_pixel))
    {
        throw std::runtime_error("texture byte size is too large");
    }

    return pixel_count * bytes_per_pixel;
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

void upload_bytes_to_texture(edge::TexturePtr const &texture, nb::bytes const &data,
    size_t offset, bool discard)
{
    size_t const total_size = texture_total_size(*texture);
    size_t const data_size = data.size();
    validate_upload_range(total_size, offset, data_size, "texture");

    auto texels = texture->write(discard ? edge::Contents::DISCARD : edge::Contents::KEEP);
    if (data_size > 0)
    {
        std::memcpy(texels.data() + offset, data.c_str(), data_size);
    }
}

// Copy the CPU clone out through read(), which never sends anything to the GPU (write() would: on Vulkan, three waited-on submits).
nb::bytes clone_bytes(edge::Texture const &texture)
{
    size_t const total_size = texture_total_size(texture);
    auto const texels = texture.read();
    return nb::bytes(reinterpret_cast<char const *>(texels.data()), total_size);
}

nb::bytes download_texture_bytes(edge::TexturePtr const &texture)
{
    if (!texture)
    {
        throw std::runtime_error("texture is null");
    }

    {
        // the readback lets other Python threads run; the bytes are made with the GIL
        nb::gil_scoped_release release;
        texture->download();
    }
    return clone_bytes(*texture);
}

nb::bytes read_cached_pixels_bytes(edge::TexturePtr const &texture)
{
    if (!texture)
    {
        throw std::runtime_error("texture is null");
    }

    // No GPU readback — read whatever is already in the CPU-side clone.
    // The caller is responsible for having populated it (either through a
    // prior download() or through Renderer.batch_download_textures()).
    return clone_bytes(*texture);
}

// The CPU copy as a numpy array, a copy: (h, w, 4) of the 8-bit formats' bytes (B, G, R, A, as stored), of R16G16B16A16F's halves
// or R32G32B32A32F's floats; (h, w) of R32F's floats or R8UI's bytes.  ValueError for a format of no such array (block-compressed,
// depth) or an array texture.
nb::object texture_to_numpy(edge::Texture const &texture)
{
    if (texture.usage().array_layers > 0)
        throw nb::value_error("an array texture is not one image: lock() it for its layers");
    edge::PixelFormat const format = texture.pixel_format();
    nb::dlpack::dtype dtype;
    size_t channels = 0;
    if (format == edge::PixelFormat::A8R8G8B8 || format == edge::PixelFormat::X8R8G8B8)
        dtype = nb::dtype<uint8_t>(), channels = 4;
    else if (format == edge::PixelFormat::R16G16B16A16F)
        dtype = {uint8_t(nb::dlpack::dtype_code::Float), 16, 1}, channels = 4;
    else if (format == edge::PixelFormat::R32G32B32A32F)
        dtype = nb::dtype<float>(), channels = 4;
    else if (format == edge::PixelFormat::R32F)
        dtype = nb::dtype<float>(), channels = 1;
    else if (format == edge::PixelFormat::R8UI)
        dtype = nb::dtype<uint8_t>(), channels = 1;
    else
        throw nb::value_error(("a texture of pixel format " + edge::to_string(format) + " has no array of pixels").c_str());

    size_t const height = size_t(texture.height()), width = size_t(texture.width());
    size_t const bytes = height * width * channels * (dtype.bits / 8);
    auto *copy = new std::vector<unsigned char>(bytes);
    try
    {
        std::memcpy(copy->data(), texture.read().data(), bytes);
    }
    catch (...)
    {
        delete copy;
        throw;
    }
    nb::capsule owner(copy, [](void *p) noexcept { delete static_cast<std::vector<unsigned char> *>(p); });
    size_t const shape[3] = {height, width, channels};
    return nb::cast(nb::ndarray<nb::numpy>(copy->data(), channels == 1 ? 2 : 3, shape, owner, nullptr, dtype));
}

}  // namespace

void bind_texture(nb::module_ &m)
{
    nb::class_<edge::Texture>(m, "Texture")
        .def_prop_ro("width", &edge::Texture::width)
        .def_prop_ro("height", &edge::Texture::height)
        .def_prop_ro("pixel_format", &edge::Texture::pixel_format)
        .def_prop_ro("filename", &edge::Texture::filename)
        .def_prop_ro("array_layers", &edge::Texture::array_layers,
            "Layers of an array texture (a target of layers' textures), 6 for a cube's; 0 for a plain one")
        .def_prop_ro("is_cube", &edge::Texture::is_cube, "Whether it is a cube target's texture (a samplerCube)")
        .def_prop_ro("is_depth", &edge::Texture::is_depth, "Whether it is a render target's readable depth")
        .def("upload", &upload_bytes_to_texture,
            "data"_a, "offset"_a = 0, "discard"_a = true,
            "Upload raw pixel bytes into texture memory.")
        .def("download", &download_texture_bytes,
            "Read raw pixel bytes from texture memory (triggers a GPU→CPU readback).")
        .def("to_numpy", &texture_to_numpy,
            "The CPU copy as a numpy array (a copy, no GPU readback: download() first for what the GPU drew): (h, w, 4) bytes "
            "B, G, R, A of the 8-bit formats, float16 or float32 (h, w, 4) of the float ones, (h, w) of R32F and R8UI.")
        .def("read_pixels", &read_cached_pixels_bytes,
            "Return the CPU-side cached pixel bytes WITHOUT triggering a GPU readback. "
            "Used after Renderer.batch_download_textures() has populated the clone; "
            "returns whatever the last download / batch readback left behind. Empty "
            "bytes if the texture has no host-visible clone.")
        .def("save_to_file",
            [](edge::TexturePtr const &self, std::string const &filename) {
                edge::Texture::save_to_file(self, filename);
            },
            "filename"_a);

    nb::class_<edge::RenderTarget>(m, "RenderTarget")
        .def_prop_ro("width", &edge::RenderTarget::width)
        .def_prop_ro("height", &edge::RenderTarget::height)
        .def_prop_ro("color_texture", [](edge::RenderTarget const &self) { return self.color_texture(); },
            "Colour attachment 0 (None for a target of depth alone)")
        .def_prop_ro("color_textures", &edge::RenderTarget::color_textures,
            "The colour attachments in order, what a fragment shader writes at output location 0, 1, ...")
        .def_prop_ro("depth_texture", &edge::RenderTarget::depth_texture)
        .def_prop_ro("stencil_texture", &edge::RenderTarget::stencil_texture)
        .def_prop_ro("layers", &edge::RenderTarget::layers, "Layers of its textures: 0 plain, N an array, 6 a cube")
        .def_prop_ro("is_cube", &edge::RenderTarget::is_cube)
        .def_prop_ro("layer_index", &edge::RenderTarget::layer_index,
            "The layer this target draws into (target.layer(i) is i); -1 for a target itself")
        .def("layer", &edge::RenderTarget::layer, "i"_a,
            "The target that draws into layer (or cube face) i, which begin_pass takes: a target of layers is drawn a layer at a "
            "time. IndexError for a layer it has not, RuntimeError for a layer's own.");

    nb::class_<edge::RenderPassClearState>(m, "RenderPassClearState")
        .def(nb::init<>())
        .def_rw("clear_color", &edge::RenderPassClearState::clear_color)
        .def_rw("clear_color_r", &edge::RenderPassClearState::clear_color_r)
        .def_rw("clear_color_g", &edge::RenderPassClearState::clear_color_g)
        .def_rw("clear_color_b", &edge::RenderPassClearState::clear_color_b)
        .def_rw("clear_color_a", &edge::RenderPassClearState::clear_color_a)
        .def_rw("clear_depth", &edge::RenderPassClearState::clear_depth)
        .def_rw("clear_depth_value", &edge::RenderPassClearState::clear_depth_value)
        .def_rw("clear_stencil", &edge::RenderPassClearState::clear_stencil);
}
