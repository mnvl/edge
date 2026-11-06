# SPDX-License-Identifier: MIT
# Copyright (c) 2004-2026 Manvel Avetisian

"""pyedge's render targets beyond one colour and a depth: several colour attachments read back in one batch (a sensor's colour and
its range from one pass), targets of layers drawn a layer at a time and sampled as an array, cubes drawn face by face with
Camera.set_cube_face and sampled with edge_cube.glsl, and what the renderer refuses, as Python's exceptions.

The oracle is the GPU's pixels, drawn with the test shaders the C++ tests draw with ($EDGE_TEST_SHADER_DIR, set by ctest)."""
import math
import os
import struct
from pathlib import Path

import numpy as np
import pytest

import pyedge
import pyrove

SIZE = 16


def shader(name):
    directory = os.environ.get("EDGE_TEST_SHADER_DIR")
    assert directory, "EDGE_TEST_SHADER_DIR names the compiled test shaders (ctest sets it)"
    path = Path(directory) / f"{name}.spv"
    assert path.is_file(), f"no test shader {path}"
    return str(path)


@pytest.fixture(scope="module")
def renderer():
    r = pyedge.create_renderer(64, 64, backend="vulkan")
    assert r.backend == "vulkan"
    return r


@pytest.fixture(scope="module")
def null():
    return pyedge.create_renderer(64, 64, backend="null")


def material(renderer, fragment, vertex="test_frame.vert", **parameters):
    m = renderer.create_material()
    m.cull_mode = pyedge.CullMode.NONE
    m.set_shader_program(renderer.create_shader_program(shader(vertex), shader(fragment)))
    for index, value in parameters.items():
        m.set_extra_parameter(int(index[1:]), pyrove.vec4(*value))
    return m


def flat_camera():
    camera = pyedge.Camera()
    camera.set_orthographic_projection(2.0, 2.0, 0.0, 2.0)
    camera.look_at(pyrove.vec3(0, 0, 1), pyrove.vec3(0, 0, 0), pyrove.vec3(0, 1, 0))
    return camera


def draw_call(renderer, material, camera, positions, indices):
    data = b"".join(struct.pack("<5f", x, y, z, 0.5, 0.5) for x, y, z in positions)
    vb = renderer.create_vertex_buffer(pyedge.VERTEX_FORMAT_PT, len(data))
    vb.upload(data)
    ib = renderer.create_index_buffer(2 * len(indices))
    ib.upload(struct.pack(f"<{len(indices)}H", *indices))
    dc = renderer.create_draw_call()
    dc.vertex_buffer = vb
    dc.index_buffer = ib
    dc.material = material
    dc.camera = camera
    dc.triangles_count = len(indices) // 3
    world = pyrove.mat4()
    world.identity()
    dc.set_world_matrix(world)
    dc.set_always_visible(True)
    dc.commit()
    return dc


def cover(renderer, material, z=0.0):
    """A quad over the whole of flat_camera()'s view, at z"""
    return draw_call(renderer, material, flat_camera(), [(-1, -1, z), (1, -1, z), (1, 1, z), (-1, 1, z)], [0, 1, 2, 0, 2, 3])


def box(renderer, material, camera, half):
    corners = [(half if i & 1 else -half, half if i & 2 else -half, half if i & 4 else -half) for i in range(8)]
    faces = [0, 1, 3, 0, 3, 2, 4, 5, 7, 4, 7, 6, 0, 1, 5, 0, 5, 4, 2, 3, 7, 2, 7, 6, 0, 2, 6, 0, 6, 4, 1, 3, 7, 1, 7, 5]
    return draw_call(renderer, material, camera, corners, faces)


def frame(renderer, passes):
    """One frame of passes, each (target, draw call)"""
    renderer.begin_frame()
    for target, dc in passes:
        renderer.begin_pass(target)
        dc.execute()
        renderer.end_pass()
    renderer.finish_frame()


def rgba(texture):
    return np.frombuffer(texture.download(), np.float32).reshape(texture.height, texture.width, 4)


# --- several colours ---

def test_each_colour_attachment_holds_its_own_output_read_back_in_one_batch(renderer):
    formats = [pyedge.PIXEL_FORMAT_R32G32B32A32F, pyedge.PIXEL_FORMAT_A8R8G8B8, pyedge.PIXEL_FORMAT_R32F]
    target = renderer.create_render_target(SIZE, SIZE, color_formats=formats)
    assert len(target.color_textures) == 3
    assert [t.pixel_format for t in target.color_textures] == formats
    assert target.color_texture.pixel_format == formats[0]
    frame(renderer, [(target, cover(renderer, material(renderer, "test_mrt.frag", p0=(0.75, SIZE, SIZE, 0.25))))])
    renderer.batch_download_textures(target.color_textures)
    first, second, third = (t.to_numpy() for t in target.color_textures)
    x, y = np.meshgrid(np.arange(SIZE) + 0.5, np.arange(SIZE) + 0.5)
    np.testing.assert_array_equal(first[..., 0], x)
    np.testing.assert_array_equal(first[..., 1], y)
    np.testing.assert_array_equal(first[..., 2], 0.75)
    np.testing.assert_allclose(second[..., 2], x / SIZE * 255, atol=1)          # B, G, R, A bytes: red is x / SIZE
    np.testing.assert_allclose(second[..., 1], y / SIZE * 255, atol=1)
    np.testing.assert_array_equal(third, 1000 * x + y)


def test_a_target_of_depth_alone_has_no_colour_and_keeps_its_depth(renderer):
    target = renderer.create_render_target(SIZE, SIZE, color_formats=[], readable_depth=True)
    assert target.color_textures == []
    assert target.color_texture is None
    assert target.depth_texture.is_depth and target.depth_texture.array_layers == 0
    reader = material(renderer, "test_depth.frag")
    reader.set_diffuse_texture(target.depth_texture)
    copy = renderer.create_render_target(SIZE, SIZE, pyedge.PIXEL_FORMAT_R32G32B32A32F)
    frame(renderer, [(target, cover(renderer, material(renderer, "test_no_output.frag"), z=0.5)), (copy, cover(renderer, reader))])
    np.testing.assert_allclose(rgba(copy.color_texture)[..., 0], 0.25, rtol=1e-4)


def test_what_cannot_be_made_is_refused(renderer, null):
    most = renderer.max_color_attachments
    assert most >= 4
    assert null.max_color_attachments == 4
    assert len(renderer.create_render_target(4, 4, color_formats=[pyedge.PIXEL_FORMAT_A8R8G8B8] * most).color_textures) == most
    with pytest.raises(ValueError, match=str(most)):
        renderer.create_render_target(4, 4, color_formats=[pyedge.PIXEL_FORMAT_A8R8G8B8] * (most + 1))
    with pytest.raises(ValueError):
        renderer.create_render_target(4, 4, color_formats=[])                     # nothing kept
    with pytest.raises(ValueError):
        renderer.create_render_target(4, 4, color_formats=[pyedge.PIXEL_FORMAT_A8R8G8B8], layers=-1)
    with pytest.raises(ValueError, match="layers"):
        renderer.create_render_target(4, 4, color_formats=[pyedge.PIXEL_FORMAT_A8R8G8B8], layers=1 << 20)
    with pytest.raises(ValueError):
        renderer.create_render_target(8, 4, color_formats=[pyedge.PIXEL_FORMAT_A8R8G8B8], cube=True)


# --- layers ---

def test_each_layer_is_drawn_by_its_own_pass_and_sampled_as_an_array(renderer):
    target = renderer.create_render_target(SIZE, SIZE, color_formats=[pyedge.PIXEL_FORMAT_R32G32B32A32F, pyedge.PIXEL_FORMAT_R32F],
                                           readable_depth=True, layers=3)
    assert target.layers == 3 and not target.is_cube and target.layer_index == -1
    colors = target.color_textures[0]
    assert colors.array_layers == 3 and not colors.is_cube
    with pytest.raises(RuntimeError):
        colors.download()                     # an array is sampled, not read back
    layers = [target.layer(i) for i in range(3)]
    assert [layer.layer_index for layer in layers] == [0, 1, 2]
    with pytest.raises(IndexError):
        target.layer(3)
    with pytest.raises(RuntimeError):
        layers[0].layer(0)
    renderer.begin_frame()
    try:
        with pytest.raises(ValueError):
            renderer.begin_pass(target)          # a layer at a time
    finally:
        renderer.finish_frame()

    painters = [cover(renderer, material(renderer, "test_two_constants.frag", p0=(i, 0.5, 0, 1), p1=(10 * i + 1, 0, 0, 0)))
                for i in range(3)]
    frame(renderer, list(zip(layers, painters)))
    copy = renderer.create_render_target(SIZE, SIZE, pyedge.PIXEL_FORMAT_R32G32B32A32F)
    for i in range(3):
        for texture, want in ((colors, (i, 0.5, 0, 1)), (target.color_textures[1], (10 * i + 1,))):
            reader = material(renderer, "test_fetch_array.frag", p0=(i, 0, 0, 0))
            reader.set_diffuse_texture(texture)
            frame(renderer, [(copy, cover(renderer, reader))])
            np.testing.assert_array_equal(rgba(copy.color_texture)[SIZE // 2, SIZE // 3, :len(want)], want)


# --- cubes ---

def test_a_cube_drawn_face_by_face_samples_as_the_world_seen_from_its_centre(renderer):
    cube = renderer.create_render_target(SIZE, SIZE, color_formats=[pyedge.PIXEL_FORMAT_R16G16B16A16F], cube=True)
    assert cube.is_cube and cube.layers == 6
    assert cube.color_texture.is_cube and cube.color_texture.array_layers == 6
    eye = (0.25, -0.5, 0.125)
    direction = material(renderer, "test_world_direction.frag", vertex="test_world_position.vert", p0=(*eye, 0))
    passes = []
    for face in range(6):
        camera = pyedge.Camera()
        camera.set_cube_face(pyrove.vec3(*eye), face, 0.1, 10.0)
        passes.append((cube.layer(face), box(renderer, direction, camera, 2.0)))
    w, h = 32, 16
    sampler = material(renderer, "test_sample_cube.frag", p0=(w, h, 0, 0))
    sampler.set_diffuse_texture(cube.color_texture)
    seen = renderer.create_render_target(w, h, pyedge.PIXEL_FORMAT_R32G32B32A32F)
    frame(renderer, passes + [(seen, cover(renderer, sampler))])
    got = rgba(seen.color_texture)[..., :3]
    longitude = ((np.arange(w) + 0.5) / w * 2 - 1) * math.pi
    latitude = (0.5 - (np.arange(h) + 0.5) / h) * math.pi
    lon, lat = np.meshgrid(longitude, latitude)
    want = np.stack([np.cos(lat) * np.sin(lon), np.sin(lat), np.cos(lat) * np.cos(lon)], axis=-1)
    agreement = (got * want).sum(-1) / np.linalg.norm(got, axis=-1)
    assert agreement.min() > 0.995, f"worst agreement {agreement.min()}"

    with pytest.raises(IndexError):
        pyedge.Camera().set_cube_face(pyrove.vec3(0, 0, 0), 6, 0.1, 10.0)
