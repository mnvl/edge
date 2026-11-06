# SPDX-License-Identifier: MIT
# Copyright (c) 2004-2026 Manvel Avetisian

"""Contract: reading a texture's CPU clone does no GPU work.

``Renderer.batch_download_textures`` copies every camera texture of a frame
to its CPU clone in one submit, and ``Texture.read_pixels`` is documented as
returning that clone with no GPU readback.  It used to do worse than a
readback: the binding released the clone through the texture's non-const
``unlock()``, which on Vulkan is ``upload_to_gpu()`` -- three single-time
submits, each waiting for the queue to go idle.  On a GPU shared by a dozen
simulators and a trainer, every read of a lidar depth texture cost several
milliseconds of waiting (20 ms for four 16-row lidar depth textures).

``Renderer.single_time_submit_count`` counts those submits, so the contract
is checked by what the renderer did, not by how long it took.
"""

import numpy as np
import pytest

import pyedge


assert "vulkan" in pyedge.available_renderer_backends(), (
    f"pyedge built without the Vulkan backend: {pyedge.available_renderer_backends()}"
)

SIZE = 32


@pytest.fixture(scope="module")
def renderer():
    return pyedge.Renderer(64, 64, backend="vulkan")


def _clear(renderer, target, r, g, b):
    state = pyedge.RenderPassClearState()
    state.clear_color_r = r
    state.clear_color_g = g
    state.clear_color_b = b
    state.clear_color_a = 1.0
    renderer.begin_frame()
    renderer.begin_pass(target, state)
    renderer.end_pass()
    renderer.finish_frame()


def _bgra(texture):
    return np.frombuffer(texture.read_pixels(), dtype=np.uint8).reshape(SIZE, SIZE, 4)


def test_read_pixels_submits_nothing(renderer):
    target = renderer.create_render_target(SIZE, SIZE, pyedge.PIXEL_FORMAT_A8R8G8B8)
    _clear(renderer, target, 1.0, 0.0, 0.0)
    renderer.batch_download_textures([target.color_texture])

    before = renderer.single_time_submit_count
    for _ in range(3):
        pixels = _bgra(target.color_texture)
    assert renderer.single_time_submit_count == before, (
        "read_pixels went to the GPU; it must only copy the CPU clone")
    # A8R8G8B8 is [B, G, R, A] in memory.
    assert (pixels[..., 2] == 255).all() and (pixels[..., 0] == 0).all()


def test_read_pixels_returns_the_latest_download_not_a_stale_one(renderer):
    target = renderer.create_render_target(SIZE, SIZE, pyedge.PIXEL_FORMAT_A8R8G8B8)
    _clear(renderer, target, 1.0, 0.0, 0.0)
    renderer.batch_download_textures([target.color_texture])
    assert (_bgra(target.color_texture)[..., 2] == 255).all()

    _clear(renderer, target, 0.0, 0.0, 1.0)
    renderer.batch_download_textures([target.color_texture])
    pixels = _bgra(target.color_texture)
    assert (pixels[..., 0] == 255).all() and (pixels[..., 2] == 0).all()


def test_a_batch_download_is_one_submit_however_many_textures(renderer):
    targets = [renderer.create_render_target(SIZE, SIZE, pyedge.PIXEL_FORMAT_A8R8G8B8)
               for _ in range(4)]
    for target in targets:
        _clear(renderer, target, 0.0, 1.0, 0.0)
    before = renderer.single_time_submit_count
    renderer.batch_download_textures([t.color_texture for t in targets])
    assert renderer.single_time_submit_count == before + 1


def test_an_upload_still_reaches_the_gpu(renderer):
    """The fix must not take the upload path down with it: writing pixels
    is the one texture call that is supposed to submit."""
    texture = renderer.create_texture(SIZE, SIZE, pyedge.PIXEL_FORMAT_A8R8G8B8)
    before = renderer.single_time_submit_count
    texture.upload(bytes(SIZE * SIZE * 4))
    assert renderer.single_time_submit_count > before


def test_the_count_is_refused_on_a_backend_that_has_none():
    renderer = pyedge.Renderer(64, 64, backend="null")
    with pytest.raises(RuntimeError, match="Vulkan"):
        renderer.single_time_submit_count
