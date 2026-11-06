# SPDX-License-Identifier: MIT
# Copyright (c) 2004-2026 Manvel Avetisian

"""Texture.to_numpy(): the CPU copy of a texture as an array of the shape and type its pixel format holds -- (h, w, 4) bytes in the
B, G, R, A order they are stored in, floats of 16 or 32 bits, (h, w) for one channel -- a copy, not a GPU readback."""
from __future__ import annotations

import struct
from pathlib import Path

import numpy as np
import pytest

import pyedge


@pytest.fixture
def renderer():
    return pyedge.create_renderer(16, 16, backend="null")


def test_pyedge_eight_bit_texels_are_bgra_bytes(renderer):
    texture = renderer.create_texture(3, 2, pyedge.PIXEL_FORMAT_A8R8G8B8)
    texels = bytes(range(3 * 2 * 4))
    texture.upload(texels)
    array = texture.to_numpy()
    assert array.dtype == np.uint8 and array.shape == (2, 3, 4)
    assert array.tobytes() == texels
    assert array[1, 2].tolist() == [20, 21, 22, 23]          # row 1, column 2: B, G, R, A as stored
    array[0, 0, 0] = 99                                      # a copy: writing it writes nothing back
    assert texture.to_numpy()[0, 0, 0] == 0


def test_pyedge_float_and_one_channel_texels_keep_their_type(renderer):
    r32 = renderer.create_texture(2, 2, pyedge.PIXEL_FORMAT_R32F)
    r32.upload(struct.pack("<4f", 0.5, -1.0, 2.25, 1e30))
    a = r32.to_numpy()
    assert a.dtype == np.float32 and a.shape == (2, 2)
    assert a.tolist() == [[0.5, -1.0], [2.25, pytest.approx(1e30)]]
    rgba32 = renderer.create_texture(1, 1, pyedge.PIXEL_FORMAT_R32G32B32A32F)
    rgba32.upload(struct.pack("<4f", 1, 2, 3, 4))
    assert rgba32.to_numpy().shape == (1, 1, 4) and rgba32.to_numpy().dtype == np.float32
    half = renderer.create_texture(1, 1, pyedge.PIXEL_FORMAT_R16G16B16A16F)
    half.upload(np.array([0.5, 1.0, -2.0, 0.25], np.float16).tobytes())
    assert half.to_numpy().dtype == np.float16
    assert half.to_numpy()[0, 0].tolist() == [0.5, 1.0, -2.0, 0.25]
    labels = renderer.create_texture(2, 1, pyedge.PIXEL_FORMAT_R8UI)
    labels.upload(bytes([7, 200]))
    assert labels.to_numpy().dtype == np.uint8 and labels.to_numpy().tolist() == [[7, 200]]


def test_pyedge_what_a_gpu_drew_is_read_back_first(tmp_path):
    renderer = pyedge.create_renderer(8, 8, backend="vulkan")
    target = renderer.create_render_target(8, 8, pyedge.PIXEL_FORMAT_R32G32B32A32F)
    clear = pyedge.RenderPassClearState()
    clear.clear_color = True
    clear.clear_color_r, clear.clear_color_g, clear.clear_color_b, clear.clear_color_a = 0.25, 0.5, 0.75, 1.0
    renderer.begin_frame()
    renderer.begin_pass(target, clear)
    renderer.end_pass()
    renderer.finish_frame()
    target.color_texture.download()
    array = target.color_texture.to_numpy()
    assert array.shape == (8, 8, 4)
    assert np.allclose(array, [0.25, 0.5, 0.75, 1.0])


def test_pyedge_a_shader_program_is_loaded_by_its_name():
    renderer = pyedge.create_renderer(8, 8, backend="null")
    shaders = Path(__file__).resolve().parent / "no_shaders_here"
    with pytest.raises(RuntimeError, match="test.vert.spv"):
        renderer.load_shader_program(str(shaders), "test")
