# SPDX-License-Identifier: MIT
# Copyright (c) 2004-2026 Manvel Avetisian

"""pyedge reaches a draw's previous frame (motion vectors): the frame a commit is of, what a call was placed by the frame before, what is
given in place of it, a camera's cut, a model forgetting where it was.

The oracle is the GPU's pixels where it can be: test_motion.vert/.frag ($EDGE_TEST_SHADER_DIR, set by ctest) write a pixel's motion
since the frame before in the target's texture coordinates (u right, v down) into red and green, and 1 into alpha."""
import os
import struct
from pathlib import Path

import numpy as np
import pytest

import pyedge
import pyrove

DATA = Path(__file__).resolve().parent / "data"
SIZE = 32


def shader(name):
    directory = os.environ.get("EDGE_TEST_SHADER_DIR")
    assert directory, "EDGE_TEST_SHADER_DIR names the compiled test shaders (ctest sets it)"
    path = Path(directory) / f"{name}.spv"
    assert path.is_file(), f"no test shader {path}"
    return str(path)


def moved(x, y, z=0.0):
    m = pyrove.mat4()
    m.translation(x, y, z)
    return m


def look_at(camera, x, y):
    camera.look_at(pyrove.vec3(x, y, 1), pyrove.vec3(x, y, 0), pyrove.vec3(0, 1, 0))


def assert_matrix(got, want):
    np.testing.assert_allclose(got.to_numpy(), want.to_numpy(), atol=1e-6)


class Scene:
    """A square of half-width 0.25 in z = 0, an orthographic camera over [-1, 1]^2 from +z, a float target, the motion shaders"""

    def __init__(self, renderer):
        self.renderer = renderer
        e = 0.25
        data = b"".join(struct.pack("<14f", x, y, 0.0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0) for x, y in [(-e, -e), (e, -e), (e, e), (-e, e)])
        self.vb = renderer.create_vertex_buffer(pyedge.VERTEX_FORMAT_PTBNT, len(data))
        self.vb.upload(data)
        self.ib = renderer.create_index_buffer(12)
        self.ib.upload(struct.pack("<6H", 0, 1, 2, 0, 2, 3))
        self.camera = pyedge.Camera()
        self.camera.set_orthographic_projection(2.0, 2.0, 0.0, 2.0)
        look_at(self.camera, 0, 0)
        self.target = renderer.create_render_target(SIZE, SIZE, pyedge.PIXEL_FORMAT_R32G32B32A32F)
        self.program = renderer.create_shader_program(shader("test_motion.vert"), shader("test_motion.frag"))
        self.material = renderer.create_material()
        self.material.cull_mode = pyedge.CullMode.NONE
        self.material.set_shader_program(self.program)
        self.scheduler = renderer.create_scheduler()

    def call(self, world):
        dc = self.renderer.create_draw_call()
        dc.vertex_buffer = self.vb
        dc.index_buffer = self.ib
        dc.material = self.material
        dc.camera = self.camera
        dc.triangles_count = 2
        dc.set_world_matrix(world)
        dc.set_always_visible(True)
        return dc

    def render(self, calls=(), draw=None):
        """A frame of `calls`, committed in it, and of what `draw` queues on the scheduler; the target's pixels"""
        clear = pyedge.RenderPassClearState()
        clear.clear_color_r = clear.clear_color_g = clear.clear_color_b = clear.clear_color_a = 0.0
        self.renderer.begin_frame()
        for dc in calls:
            dc.commit()
            self.scheduler.add(dc)
        if draw:
            draw()
        self.renderer.begin_pass(self.target, clear)
        self.scheduler.flush()
        self.renderer.end_pass()
        self.renderer.finish_frame()
        return np.frombuffer(self.target.color_texture.download(), np.float32).reshape(SIZE, SIZE, 4)

    @staticmethod
    def at(image, x, y):
        """The pixel where the camera sees (x, y) of [-1, 1]^2, +y up"""
        return image[min(SIZE - 1, int((1 - y) / 2 * SIZE)), min(SIZE - 1, int((x + 1) / 2 * SIZE))]


@pytest.fixture(scope="module")
def renderer():
    r = pyedge.create_renderer(64, 64, backend="vulkan")
    assert r.backend == "vulkan"
    return r


def test_the_frame_index_counts_the_frames_ended():
    for backend in ("null", "vulkan"):
        r = pyedge.create_renderer(16, 16, backend=backend)
        assert r.frame_index == 0
        r.begin_frame()
        assert r.frame_index == 0
        r.finish_frame()
        assert r.frame_index == 1           # between frames: the next one's
        r.begin_frame()
        r.submit_frame()
        assert r.frame_index == 2


def test_a_square_moving_under_a_moving_camera_writes_its_motion(renderer):
    scene = Scene(renderer)
    dc = scene.call(moved(0, 0))
    image = scene.render([dc])
    np.testing.assert_allclose(Scene.at(image, 0, 0), (0, 0, 0, 1), atol=1e-5)       # seen the first time: no motion
    dc.set_world_matrix(moved(0.25, 0.125))
    look_at(scene.camera, -0.125, 0)
    image = scene.render([dc])
    # on the screen from (0, 0) to (0.375, 0.125): half that in texture coordinates, v down
    np.testing.assert_allclose(Scene.at(image, 0.375, 0.125), (0.1875, -0.0625, 0, 1), atol=1e-5)
    # cut to (0.5, 0.25), the square moved on by (0, -0.25): its own motion only
    dc.set_world_matrix(moved(0.25, -0.125))
    look_at(scene.camera, 0.5, 0.25)
    cuts = scene.camera.cuts
    scene.camera.cut()
    assert scene.camera.cuts == cuts + 1
    image = scene.render([dc])
    np.testing.assert_allclose(Scene.at(image, -0.25, -0.375), (0, 0.125, 0, 1), atol=1e-5)


def test_what_is_given_stands_in_for_what_a_call_remembers_and_reset_forgets(renderer):
    scene = Scene(renderer)
    dc = scene.call(moved(0.25, 0))
    scene.render([dc])
    dc.set_world_matrix(moved(0.5, 0))
    dc.commit()
    assert_matrix(dc.previous_world, moved(0.25, 0))
    dc.set_previous_world_matrix(moved(-0.5, 0))
    dc.commit()
    assert_matrix(dc.previous_world, moved(-0.5, 0))
    assert dc.has_previous
    dc.reset_previous()
    dc.commit()
    assert not dc.has_previous
    assert_matrix(dc.previous_world, moved(0.5, 0))
    vp = scene.camera.projection_matrix * scene.camera.view_matrix
    assert_matrix(dc.previous_world_view_projection, vp * moved(0.5, 0))

    instanced = scene.call(moved(0, 0))
    instanced.set_instances([moved(-0.5, 0), moved(0.5, 0)])
    with pytest.raises(ValueError):
        instanced.set_previous_instances([])
    with pytest.raises(ValueError):
        instanced.set_previous_instances(np.zeros((0, 4, 4), np.float32))
    instanced.set_previous_instances(np.stack([moved(-0.5, 0.25).to_numpy()]).astype(np.float32))
    image = scene.render([instanced])
    # the first from (-0.5, 0.25) to (-0.5, 0): texture (0, 0.125); the second new: none
    np.testing.assert_allclose(Scene.at(image, -0.5, 0), (0, 0.125, 0, 1), atol=1e-5)
    np.testing.assert_allclose(Scene.at(image, 0.5, 0), (0, 0, 0, 1), atol=1e-5)

    plain = scene.call(moved(0, 0))
    plain.set_previous_instances([moved(1, 0)])
    with pytest.raises(RuntimeError):                 # previous instances of a call that has none: std::logic_error
        plain.commit()


def test_a_model_drawn_once_a_frame_has_its_motion_until_reset(renderer):
    scene = Scene(renderer)
    model = pyedge.StaticModelPTBNT(renderer, scene.scheduler)
    model.load_from_gltf(str(DATA / "test_gltf_loader_pbr.glb"))      # a quad of x, z in -1..1 at y 0
    model.set_materials_shader_program(scene.program)
    model.always_visible = True
    for i in range(model.materials_count):
        model.material(i).cull_mode = pyedge.CullMode.NONE
    # turned to face the camera and made a quarter as large: x, z to x, y
    upright = pyrove.mat4.from_numpy(np.array([[0.25, 0, 0, 0], [0, 0, 0.25, 0], [0, -0.25, 0, 0], [0, 0, 0, 1]], np.float32))

    def at(x, y):
        return moved(x, y) * upright

    image = scene.render(draw=lambda: model.draw(scene.camera, at(0, 0)))
    np.testing.assert_allclose(Scene.at(image, 0, 0), (0, 0, 0, 1), atol=1e-5)
    image = scene.render(draw=lambda: model.draw(scene.camera, at(0.25, 0)))
    np.testing.assert_allclose(Scene.at(image, 0.25, 0), (0.125, 0, 0, 1), atol=1e-5)
    model.reset_previous()
    image = scene.render(draw=lambda: model.draw(scene.camera, at(0.5, 0)))
    np.testing.assert_allclose(Scene.at(image, 0.5, 0), (0, 0, 0, 1), atol=1e-5)
