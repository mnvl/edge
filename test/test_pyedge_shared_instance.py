# SPDX-License-Identifier: MIT
# Copyright (c) 2004-2026 Manvel Avetisian

"""Several renderers in one Python process (the process's one Vulkan instance under them, vulkan_instance.h): on every GPU, picked by
UUID, made and let go in mixed order, the first let go while another is in a frame; each draws the colour it is asked for."""
import gc

import numpy as np

import pyedge


def frame_into(renderer, rgb):
    """A frame clearing a target to `rgb`, left open in its pass; the target"""
    target = renderer.create_render_target(8, 8, pyedge.PIXEL_FORMAT_A8R8G8B8)
    clear = pyedge.RenderPassClearState()
    clear.clear_color_r, clear.clear_color_g, clear.clear_color_b = (c / 255.0 for c in rgb)
    clear.clear_color_a = 1.0
    renderer.begin_frame()
    renderer.begin_pass(target, clear)
    return target


def finished(renderer, target, rgb):
    """Ends the frame; whether every pixel is `rgb` (A8R8G8B8: bytes b, g, r, a)"""
    renderer.end_pass()
    renderer.finish_frame()
    pixels = np.frombuffer(target.color_texture.download(), np.uint8).reshape(8, 8, 4)
    return bool(np.all(np.abs(pixels[:, :, :3].astype(int) - np.array(rgb[::-1])) <= 1))


def draws(renderer, rgb):
    return finished(renderer, frame_into(renderer, rgb), rgb)


def test_renderers_on_every_gpu_made_and_let_go_in_mixed_order_each_draw_their_colour():
    uuids = [d["uuid"] for d in pyedge.enumerate_physical_devices()]
    assert uuids
    live = {}
    live["a"] = pyedge.create_renderer(32, 32, backend="vulkan", device_uuid=uuids[0])
    live["b"] = pyedge.create_renderer(32, 32, backend="vulkan", device_uuid=uuids[-1])
    live["c"] = pyedge.create_renderer(32, 32, backend="vulkan", device_uuid=uuids[0])
    assert live["a"].device_uuid == uuids[0] and live["b"].device_uuid == uuids[-1]
    colours = {"a": (200, 10, 10), "b": (10, 200, 10), "c": (10, 10, 200), "d": (120, 60, 30)}
    for name, renderer in live.items():
        assert draws(renderer, colours[name]), name

    # the first let go while the second is in a frame
    target = frame_into(live["b"], colours["b"])
    del live["a"]
    gc.collect()
    assert finished(live["b"], target, colours["b"])
    live["d"] = pyedge.create_renderer(32, 32, backend="vulkan", device_uuid=uuids[-1])
    del live["b"]
    gc.collect()
    for name, renderer in live.items():
        assert draws(renderer, colours[name]), name
