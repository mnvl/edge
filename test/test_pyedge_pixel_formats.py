# SPDX-License-Identifier: MIT
# Copyright (c) 2004-2026 Manvel Avetisian

"""Contract tests for the R32F pixel format.

R32F exists so a render target that carries one number per pixel costs one
number per pixel to read back.  A lidar camera writes a single metric depth;
in an RGBA32F target three quarters of every readback would be thrown away,
which across many parallel simulators is gigabytes a second on a step that
is already memory-bandwidth bound.

The tests are about the *format plumbing*: the byte size the engine computes,
the Vulkan format it resolves to, and whether float data survives a round trip
through GPU memory unchanged.  Which channel a caller then reads is the
caller's to test.
"""
from __future__ import annotations

import numpy as np
import pytest

import pyedge

# R32F is a Vulkan format; the "null" backend allocates no device objects and
# would exercise none of this.  Fail rather than skip -- a build without Vulkan
# is not a build where these guarantees hold.
assert "vulkan" in pyedge.available_renderer_backends(), (
    f"pyedge built without the Vulkan backend: {pyedge.available_renderer_backends()}")

WIDTH, HEIGHT = 64, 32


@pytest.fixture(scope="module")
def renderer():
    r = pyedge.create_renderer(64, 64, backend="vulkan", offscreen=True)
    assert r.backend == "vulkan", f"got the {r.backend!r} backend, not vulkan"
    yield r


class TestTheFormatIsRegistered:

    def test_it_is_exposed_to_python(self):
        assert hasattr(pyedge, "PIXEL_FORMAT_R32F")

    def test_it_is_distinct_from_every_other_format(self):
        codes = {name: getattr(pyedge, name) for name in dir(pyedge)
                 if name.startswith("PIXEL_FORMAT_")}
        # DEFAULT/DEFAULT_ALPHA are deliberate aliases of two real formats.
        real = {k: v for k, v in codes.items() if not k.endswith(("_DEFAULT", "_DEFAULT_ALPHA"))}
        assert len(set(real.values())) == len(real), (
            f"two pixel format constants share a code: {sorted(real.items(), key=lambda kv: kv[1])}")


def _written_texture(renderer, pixel_format: int, bytes_per_pixel: int):
    """A texture that has been written once, so it can be read back.

    A freshly created texture is in VK_IMAGE_LAYOUT_UNDEFINED and download()
    refuses to transition out of it -- true of every format, not just this
    one.  Uploading is the cheapest way to give it a defined layout.
    """
    texture = renderer.create_texture(WIDTH, HEIGHT, pixel_format)
    texture.upload(bytes(WIDTH * HEIGHT * bytes_per_pixel))
    return texture


class TestItCostsOneFloatPerPixel:
    """The whole reason the format was added."""

    def test_a_texture_reads_back_four_bytes_per_pixel(self, renderer):
        texture = _written_texture(renderer, pyedge.PIXEL_FORMAT_R32F, 4)
        assert len(texture.download()) == WIDTH * HEIGHT * 4

    def test_that_is_a_quarter_of_the_rgba32f_it_replaces(self, renderer):
        one = _written_texture(renderer, pyedge.PIXEL_FORMAT_R32F, 4)
        four = _written_texture(renderer, pyedge.PIXEL_FORMAT_R32G32B32A32F, 16)
        assert len(four.download()) == 4 * len(one.download())


class TestFloatsSurviveTheRoundTrip:
    """A depth map is useless if the format quantizes it on the way through."""

    def test_values_come_back_bit_exact(self, renderer):
        texture = renderer.create_texture(WIDTH, HEIGHT, pyedge.PIXEL_FORMAT_R32F)
        # Values a lidar range actually produces, including the 1000.0 miss
        # marker and a couple that no lower-precision format would preserve.
        rng = np.random.default_rng(0)
        sent = rng.uniform(0.05, 60.0, (HEIGHT, WIDTH)).astype(np.float32)
        sent[0, 0] = 1000.0          # SENSOR_MISS_DEPTH
        sent[0, 1] = 0.0
        sent[0, 2] = 12.345678       # not representable in fp16
        sent[0, 3] = 1e-6

        texture.upload(sent.tobytes())
        got = np.frombuffer(texture.download(), dtype=np.float32).reshape(HEIGHT, WIDTH)

        np.testing.assert_array_equal(got, sent)

    def test_a_render_target_can_be_built_on_it(self, renderer):
        """It has to work as a colour attachment, not just as a sampled texture."""
        target = renderer.create_render_target(WIDTH, HEIGHT, pyedge.PIXEL_FORMAT_R32F)
        assert target.width == WIDTH and target.height == HEIGHT
        assert target.color_texture.pixel_format == pyedge.PIXEL_FORMAT_R32F
        assert len(target.color_texture.download()) == WIDTH * HEIGHT * 4
