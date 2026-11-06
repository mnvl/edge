# SPDX-License-Identifier: MIT
# Copyright (c) 2004-2026 Manvel Avetisian

"""pyedge's GPU timer, the renderer's (gpu_timer.h): a frame's pass durations come once the GPU has drawn it, asked for by the frame's
number, never before it is ended (with frames in flight, not before it is waited for); the null renderer has no timer and says so."""
import math
import struct

import pytest

import pyedge


def empty_frame(renderer, target, passes):
    renderer.begin_frame()
    frame = renderer.frame_serial
    for _ in range(passes):
        renderer.begin_pass(target)
        renderer.end_pass()
    renderer.finish_frame()
    return frame


def square(renderer):
    """A white square over all of a default camera's view (it looks down +z from the origin), blended over what is there"""
    corners = [(-50, -50), (50, -50), (50, 50), (-50, -50), (50, 50), (-50, 50)]
    data = b"".join(struct.pack("<5f", x, y, 5.0, 0.0, 0.0) for x, y in corners)
    vb = renderer.create_vertex_buffer(pyedge.VERTEX_FORMAT_PT, len(data))
    vb.upload(data)
    material = renderer.create_material()
    material.cull_mode = pyedge.CullMode.NONE
    material.alpha_blend_enabled = True
    dc = renderer.create_draw_call()
    dc.vertex_buffer = vb
    dc.material = material
    dc.camera = pyedge.Camera()
    dc.triangles_count = 2
    dc.set_always_visible(True)
    dc.commit()
    return dc, vb, material


def test_the_null_renderer_has_no_gpu_timer_and_says_so():
    r = pyedge.create_renderer(64, 64, backend="null")
    assert r.has_gpu_timer is False
    with pytest.raises(RuntimeError, match="null renderer"):
        r.gpu_trace_enabled = True
    assert r.gpu_trace_enabled is False
    r.gpu_trace_enabled = False
    target = r.create_render_target(8, 8)
    assert empty_frame(r, target, 2) == 1                  # frames are numbered on every renderer
    assert empty_frame(r, target, 2) == 2
    assert r.pass_times_us(1) is None
    assert r.last_pass_times_us == []
    assert r.last_pass_times_frame == 0


def test_tracing_is_switched_between_frames_only():
    for backend in ("null", "vulkan"):
        r = pyedge.create_renderer(64, 64, backend=backend)
        r.begin_frame()
        with pytest.raises(RuntimeError, match="between frames"):
            r.gpu_trace_enabled = True
        r.finish_frame()
        assert r.gpu_trace_enabled is False


def test_a_frame_in_flight_has_its_times_once_it_is_waited_for():
    r = pyedge.create_renderer(64, 64, backend="vulkan", frames_in_flight=3)
    assert r.has_gpu_timer is True
    target = r.create_render_target(64, 64)
    untimed = empty_frame(r, target, 1)
    r.gpu_trace_enabled = True
    assert r.gpu_trace_enabled is True
    frames = []
    for passes in (1, 3):
        r.begin_frame()
        frames.append(r.frame_serial)
        assert r.pass_times_us(frames[-1]) is None     # not ended
        for _ in range(passes):
            r.begin_pass(target)
            r.end_pass()
        r.submit_frame()
    assert r.pass_times_us(frames[0]) is None          # submitted, not waited for
    assert r.pass_times_us(frames[1]) is None
    assert r.last_pass_times_frame == 0
    r.wait_frames()
    for frame, passes in zip(frames, (1, 3)):
        times = r.pass_times_us(frame)
        assert times is not None and len(times) == passes
        assert all(math.isfinite(t) and t >= 0 for t in times)
        assert r.is_gpu_time_disjoint(frame) is False
    assert r.last_pass_times_frame == frames[1]
    assert r.last_pass_times_us == r.pass_times_us(frames[1])
    assert r.pass_times_us(untimed) is None
    r.gpu_trace_enabled = False
    assert r.pass_times_us(frames[1]) is None
    assert r.last_pass_times_us == []


def test_frames_in_flight_when_tracing_is_turned_off_never_get_their_times():
    """Turning tracing off drops the frames still to be read back (two in flight), though it is on again when they are"""
    r = pyedge.create_renderer(64, 64, backend="vulkan", frames_in_flight=3)
    target = r.create_render_target(64, 64)

    def frame():
        r.begin_frame()
        serial = r.frame_serial
        r.begin_pass(target)
        r.end_pass()
        r.submit_frame()
        return serial

    r.gpu_trace_enabled = True
    dropped = [frame(), frame()]
    r.gpu_trace_enabled = False
    r.gpu_trace_enabled = True
    timed = [frame() for _ in range(4)]
    r.wait_frames()
    assert [r.pass_times_us(serial) for serial in dropped] == [None, None]
    assert all(r.pass_times_us(serial) is not None for serial in timed)
    assert r.last_pass_times_frame == timed[-1]


def test_a_pass_that_draws_more_takes_measurably_longer():
    r = pyedge.create_renderer(64, 64, backend="vulkan")
    target = r.create_render_target(1024, 1024)
    dc, vb, material = square(r)
    r.gpu_trace_enabled = True
    empty, drawn = [], []
    for _ in range(5):
        r.begin_frame()
        frame = r.frame_serial
        r.begin_pass(target)
        r.end_pass()
        r.begin_pass(target)
        for _ in range(200):
            dc.execute()
        r.end_pass()
        r.finish_frame()
        times = r.pass_times_us(frame)
        assert times is not None and len(times) == 2
        empty.append(times[0])
        drawn.append(times[1])
    empty.sort()
    drawn.sort()
    assert drawn[2] > 10 * empty[2] and drawn[2] > empty[2] + 200, (empty, drawn)
