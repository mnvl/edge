# SPDX-License-Identifier: MIT
# Copyright (c) 2004-2026 Manvel Avetisian

"""pyedge reaches what edge's renderer has: frames submitted without waiting and kept in flight, the frame constants and global
textures every shader reads, a material's own parameters, instanced draws, blended draws in order, debug lines; and its long calls
(finishing a frame, downloads, loading models, building octrees) let other Python threads run.

The oracle is the GPU's pixels, drawn with the test shaders the C++ tests draw with ($EDGE_TEST_SHADER_DIR, set by ctest)."""
import os
import struct
import threading
import time
from pathlib import Path

import numpy as np
import pytest

import pyedge
import pyrove

DATA = Path(__file__).resolve().parent / "data"
SIZE = 16


def shader(name):
    directory = os.environ.get("EDGE_TEST_SHADER_DIR")
    assert directory, "EDGE_TEST_SHADER_DIR names the compiled test shaders (ctest sets it)"
    path = Path(directory) / f"{name}.spv"
    assert path.is_file(), f"no test shader {path}"
    return str(path)


def identity():
    m = pyrove.mat4()
    m.identity()
    return m


def moved(x, y, z=0.0):
    m = pyrove.mat4()
    m.translation(x, y, z)
    return m


class Quad:
    """A quad of half-width `extent` in z = 0 seen from +z by an orthographic camera over [-1, 1]^2, drawn into a float target"""

    def __init__(self, renderer, extent=1.0, size=SIZE, vertex_format=pyedge.VERTEX_FORMAT_PT):
        self.renderer = renderer
        corners = [(-extent, -extent, 0, 0), (extent, -extent, 1, 0), (extent, extent, 1, 1), (-extent, extent, 0, 1)]
        if vertex_format == pyedge.VERTEX_FORMAT_PT:
            data = b"".join(struct.pack("<5f", x, y, 0.0, u, v) for x, y, u, v in corners)
        else:                                   # PTBNT: position, tangent, binormal, normal, texcoords
            data = b"".join(struct.pack("<14f", x, y, 0.0, 1, 0, 0, 0, 1, 0, 0, 0, 1, u, v) for x, y, u, v in corners)
        self.vb = renderer.create_vertex_buffer(vertex_format, len(data))
        self.vb.upload(data)
        self.ib = renderer.create_index_buffer(12)
        self.ib.upload(struct.pack("<6H", 0, 1, 2, 0, 2, 3))
        self.camera = pyedge.Camera()
        self.camera.set_orthographic_projection(2.0, 2.0, 0.0, 2.0)
        self.camera.look_at(pyrove.vec3(0, 0, 1), pyrove.vec3(0, 0, 0), pyrove.vec3(0, 1, 0))
        self.target = renderer.create_render_target(size, size, pyedge.PIXEL_FORMAT_R32G32B32A32F)
        self.size = size

    def call(self, material, world=None):
        dc = self.renderer.create_draw_call()
        dc.vertex_buffer = self.vb
        dc.index_buffer = self.ib
        dc.material = material
        dc.camera = self.camera
        dc.triangles_count = 2
        dc.set_world_matrix(world if world is not None else identity())
        dc.set_always_visible(True)
        return dc

    def render(self, calls, submit=False):
        clear = pyedge.RenderPassClearState()
        clear.clear_color_r = clear.clear_color_g = clear.clear_color_b = clear.clear_color_a = -1.0
        scheduler = self.renderer.create_scheduler()
        for dc in calls:
            dc.commit()
            scheduler.add(dc)
        self.renderer.begin_frame()
        self.renderer.begin_pass(self.target, clear)
        scheduler.flush()
        self.renderer.end_pass()
        (self.renderer.submit_frame if submit else self.renderer.finish_frame)()
        return self.pixels()

    def pixels(self):
        return np.frombuffer(self.target.color_texture.download(), np.float32).reshape(self.size, self.size, 4)

    def at(self, image, x, y):
        """The pixel under (x, y) of [-1, 1]^2, +y up"""
        col = min(self.size - 1, int((x + 1) / 2 * self.size))
        row = min(self.size - 1, int((1 - y) / 2 * self.size))
        return image[row, col]


def material_with(renderer, fragment):
    material = renderer.create_material()
    material.cull_mode = pyedge.CullMode.NONE
    material.set_shader_program(renderer.create_shader_program(shader("test_frame.vert"), shader(fragment)))
    return material


@pytest.fixture(scope="module")
def renderer():
    r = pyedge.create_renderer(64, 64, backend="vulkan")
    assert r.backend == "vulkan"
    return r


# --- frame constants and global textures ---

def test_frame_constants_reach_every_shader(renderer):
    quad = Quad(renderer)
    material = material_with(renderer, "test_frame_constants.frag")
    renderer.set_frame_constants(np.array([0.25, 0.5, 0.75, 1.0, 0.125, 0, 0, 0], np.float32))
    np.testing.assert_allclose(quad.at(quad.render([quad.call(material)]), 0, 0), (0.375, 0.5, 0.75, 1.0))
    renderer.set_frame_constants(struct.pack("<8f", 0.1, 0.2, 0.3, 0.4, 0, 0, 0, 0))             # bytes as well
    np.testing.assert_allclose(quad.at(quad.render([quad.call(material)]), 0, 0), (0.1, 0.2, 0.3, 0.4), rtol=1e-6)


def test_frame_constants_are_refused_inside_a_frame_and_past_their_size(renderer):
    assert pyedge.FRAME_CONSTANTS_SIZE == 16384
    with pytest.raises(ValueError):
        renderer.set_frame_constants(bytes(pyedge.FRAME_CONSTANTS_SIZE + 1))
    renderer.set_frame_constants(bytes(pyedge.FRAME_CONSTANTS_SIZE))
    renderer.begin_frame()
    try:
        with pytest.raises(RuntimeError):
            renderer.set_frame_constants(bytes(16))
    finally:
        renderer.finish_frame()
    with pytest.raises(TypeError):
        renderer.set_frame_constants(np.zeros((2, 2), np.float32))           # one flat block of bytes, not a shape


def solid(renderer, rgba):
    """A 4 x 4 texture of one colour (A8R8G8B8: bytes b, g, r, a)"""
    t = renderer.create_texture(4, 4, pyedge.PIXEL_FORMAT_A8R8G8B8)
    r, g, b, a = (int(round(c * 255)) for c in rgba)
    t.upload(bytes([b, g, r, a]) * 16)
    return t


def test_global_textures_are_sampled_in_their_slots(renderer):
    quad = Quad(renderer)
    material = material_with(renderer, "test_global_texture.frag")
    renderer.set_global_texture(2, solid(renderer, (1.0, 0.0, 0.0, 1.0)))
    renderer.set_global_texture(5, solid(renderer, (0.0, 0.0, 1.0, 1.0)))
    for slot, want in ((2, (1, 0, 0, 1)), (5, (0, 0, 1, 1))):
        material.set_extra_parameter(0, pyrove.vec4(slot, 0, 0, 0))
        np.testing.assert_allclose(quad.at(quad.render([quad.call(material)]), 0, 0), want, atol=1 / 255)
    renderer.set_global_texture(5, None)                                         # none: white
    np.testing.assert_allclose(quad.at(quad.render([quad.call(material)]), 0, 0), (1, 1, 1, 1), atol=1 / 255)
    assert pyedge.GLOBAL_TEXTURES_COUNT == 8
    with pytest.raises(IndexError):
        renderer.set_global_texture(8, None)


def test_a_shadow_texture_is_a_readable_depth_and_nothing_else(renderer):
    target = renderer.create_render_target(8, 8, pyedge.PIXEL_FORMAT_X8R8G8B8, readable_depth=True)
    renderer.set_global_shadow_texture(0, target.depth_texture)
    with pytest.raises(ValueError):
        renderer.set_global_shadow_texture(0, target.color_texture)
    with pytest.raises(ValueError):
        renderer.set_global_shadow_texture(0, None)
    renderer.set_global_texture(0, None)


# --- material parameters ---

def test_a_materials_parameters_are_its_own_block(renderer):
    quad = Quad(renderer)
    material = material_with(renderer, "test_material_parameters.frag")
    block = np.arange(64 * 4, dtype=np.float32) / 256
    material.parameters = block
    for k in (0, 7, 63):
        material.set_extra_parameter(0, pyrove.vec4(k, 0, 0, 0))
        np.testing.assert_allclose(quad.at(quad.render([quad.call(material)]), 0, 0), block[4 * k:4 * k + 4])
    assert bytes(material.parameters) == block.tobytes()
    material.parameters = struct.pack("<4f", 1, 2, 3, 4)              # shorter: zeros past it
    material.set_extra_parameter(0, pyrove.vec4(1, 0, 0, 0))
    np.testing.assert_allclose(quad.at(quad.render([quad.call(material)]), 0, 0), (0, 0, 0, 0))
    material.parameters = b""
    assert len(material.parameters) == 0
    assert pyedge.MATERIAL_PARAMETERS_SIZE == 1024
    with pytest.raises(ValueError):
        material.parameters = bytes(1025)


# --- instancing ---

def test_a_draw_given_instances_is_drawn_at_each(renderer):
    quad = Quad(renderer, extent=0.2, vertex_format=pyedge.VERTEX_FORMAT_PTBNT)
    material = renderer.create_material()
    material.cull_mode = pyedge.CullMode.NONE
    places = [(-0.6, 0.6), (0.6, -0.6), (0.6, 0.6)]
    for instances in ([moved(x, y) for x, y in places],
                      np.stack([moved(x, y).to_numpy() for x, y in places]).astype(np.float32)):
        dc = quad.call(material)
        dc.set_instances(instances)
        image = quad.render([dc])
        for x, y in places:
            assert quad.at(image, x, y)[0] > 0, (x, y)
        assert quad.at(image, -0.6, -0.6)[0] == -1.0                  # nowhere else
        assert quad.at(image, 0, 0)[0] == -1.0                        # and not at the draw's own place
    with pytest.raises(ValueError):
        quad.call(material).set_instances([])
    with pytest.raises(TypeError):
        quad.call(material).set_instances(np.zeros((3, 4, 3), np.float32))
    with pytest.raises(ValueError):
        quad.call(material).set_instances(np.zeros((0, 4, 4), np.float32))


# --- blended draws in order ---

def test_a_scheduler_sorts_blended_draws_when_told(renderer):
    s = renderer.create_scheduler()
    assert s.sort_blended is False
    s.sort_blended = True
    assert s.sort_blended is True


# --- debug lines ---

def test_debug_lines_draw_what_was_collected_and_start_afresh(renderer):
    quad = Quad(renderer, size=32)
    lines = pyedge.DebugLines(renderer, 0.1)
    lines.line(pyrove.vec3(-1, 0, 0), pyrove.vec3(1, 0, 0), pyrove.vec4(1, 0, 0, 1))
    lines.box(pyrove.aabb3(pyrove.vec3(-0.5, -0.5, -0.1), pyrove.vec3(0.5, 0.5, 0.1)), pyrove.vec4(0, 1, 0, 1))
    lines.sphere(pyrove.vec3(0, 0, 0), 0.8, pyrove.vec4(0, 0, 1, 1), 16)
    lines.axes(identity(), 0.3)
    assert lines.segments > 0
    scheduler = renderer.create_scheduler()
    clear = pyedge.RenderPassClearState()
    for draw_again in (True, False):
        if draw_again:
            lines.draw(scheduler, quad.camera)
            assert lines.segments == 0
        renderer.begin_frame()
        renderer.begin_pass(quad.target, clear)
        scheduler.flush()
        renderer.end_pass()
        renderer.finish_frame()
        image = quad.pixels()
        drawn = quad.at(image, -0.9, 0.0)
        if draw_again:
            np.testing.assert_allclose(drawn, (1, 0, 0, 1), atol=1e-3)           # the red line
            np.testing.assert_allclose(quad.at(image, -0.5, -0.25)[:3], (0, 1, 0), atol=1e-3)     # the green box's side
        else:
            assert drawn[0] == 0                                                   # nothing collected: nothing drawn
    with pytest.raises(ValueError):
        pyedge.DebugLines(renderer, 0.0)
    with pytest.raises(ValueError):
        lines.sphere(pyrove.vec3(0, 0, 0), 1.0, pyrove.vec4(1, 1, 1, 1), 2)


# --- frames in flight ---

def test_submitted_frames_are_downloaded_as_drawn():
    r = pyedge.create_renderer(64, 64, backend="vulkan", frames_in_flight=3)
    assert r.frames_in_flight == 3
    quad = Quad(r)
    material = material_with(r, "test_frame_constants.frag")
    targets = [r.create_render_target(SIZE, SIZE, pyedge.PIXEL_FORMAT_R32G32B32A32F) for _ in range(3)]
    serial = r.frame_serial
    for k, target in enumerate(targets):                     # three frames in flight, each into its own target
        r.set_frame_constants(np.array([k / 4, 0, 0, 1, 0, 0, 0, 0], np.float32))
        quad.target = target
        scheduler = r.create_scheduler()
        dc = quad.call(material)
        dc.commit()
        scheduler.add(dc)
        r.begin_frame()
        r.begin_pass(target, pyedge.RenderPassClearState())
        scheduler.flush()
        r.end_pass()
        r.submit_frame()
    assert r.frame_serial == serial + 3
    r.batch_download_textures([t.color_texture for t in targets])
    for k, target in enumerate(targets):
        quad.target = target
        assert quad.at(quad.pixels(), 0, 0)[0] == pytest.approx(k / 4)
    r.wait_frames()


def test_frames_in_flight_are_one_to_four_and_the_null_renderer_has_one():
    for bad in (0, 5):
        with pytest.raises(ValueError):
            pyedge.create_renderer(64, 64, backend="vulkan", frames_in_flight=bad)
    null = pyedge.create_renderer(64, 64, backend="null")
    assert null.frames_in_flight == 1
    null.begin_frame()
    null.submit_frame()                                     # as finish_frame where the renderer cannot keep frames in flight
    with pytest.raises(ValueError):
        pyedge.create_renderer(64, 64, backend="null", frames_in_flight=2)


def test_wait_frames_is_refused_inside_a_frame():
    r = pyedge.create_renderer(64, 64, backend="vulkan", frames_in_flight=2)
    r.begin_frame()
    with pytest.raises(RuntimeError):
        r.wait_frames()
    r.submit_frame()
    r.wait_frames()


# --- the GIL ---

def _stall_during(call):
    """(how long the call took in a thread of its own, the longest the main thread could not wake meanwhile)"""
    done = threading.Event()
    took = {}

    def worker():
        start = time.perf_counter()
        call()
        took["s"] = time.perf_counter() - start
        done.set()

    t = threading.Thread(target=worker)
    ticks = [time.perf_counter()]
    t.start()
    while not done.is_set():
        time.sleep(0.001)
        ticks.append(time.perf_counter())
    t.join()
    return took["s"], float(np.diff(ticks).max())


def _assert_runs_alongside(call, what):
    took, stall = _stall_during(call)
    assert took > 0.05, f"{what} took {took:.3f} s: too short to tell"
    assert stall < took / 3, f"{what}: the main thread could not run for {stall:.3f} s of {took:.3f} s"


def _heavy_frame(r, draws):
    quad = Quad(r, size=2048)
    material = material_with(r, "test_frame_constants.frag")
    scheduler = r.create_scheduler()
    for _ in range(draws):
        dc = quad.call(material)
        dc.commit()
        scheduler.add(dc)
    r.begin_frame()
    r.begin_pass(quad.target, pyedge.RenderPassClearState())
    scheduler.flush()
    r.end_pass()
    return quad


def test_finishing_a_frame_and_downloading_let_python_run(renderer):
    draws = 200
    while True:                                           # enough fill that the GPU's part is long enough to tell
        quad = _heavy_frame(renderer, draws)
        start = time.perf_counter()
        renderer.finish_frame()
        if time.perf_counter() - start > 0.15 or draws > 50000:
            break
        draws *= 2
    quad = _heavy_frame(renderer, draws)
    _assert_runs_alongside(renderer.finish_frame, "finish_frame")
    targets = [renderer.create_render_target(2048, 2048, pyedge.PIXEL_FORMAT_R32G32B32A32F) for _ in range(4)]
    _assert_runs_alongside(lambda: renderer.batch_download_textures([t.color_texture for t in targets]),
                           "batch_download_textures")
    _assert_runs_alongside(lambda: [t.color_texture.download() for t in targets], "Texture.download")


def test_submit_frame_does_not_wait_for_the_gpu():
    r = pyedge.create_renderer(64, 64, backend="vulkan", frames_in_flight=2)
    draws = 200
    while True:
        _heavy_frame(r, draws)
        start = time.perf_counter()
        r.finish_frame()
        finished = time.perf_counter() - start
        if finished > 0.15 or draws > 50000:
            break
        draws *= 2
    kept = _heavy_frame(r, draws)          # (what the open frame draws with, kept: one freed meanwhile makes the submit wait for it)
    start = time.perf_counter()
    r.submit_frame()
    submitted = time.perf_counter() - start
    r.wait_frames()
    del kept
    assert submitted < finished / 4, f"submit_frame took {submitted:.3f} s of a frame the GPU draws in {finished:.3f} s"


def test_loading_a_model_lets_python_run(renderer):
    scheduler = renderer.create_scheduler()

    models = []                  # kept: a model's destruction, with the GIL, may wait for the GPU, which is not what is timed here

    def load():
        for _ in range(4):
            models.append(pyedge.AnimatedModelPTBNJWT(renderer, scheduler))
            models[-1].load_from_gltf(str(DATA / "test_figure.glb"))

    took, stall = _stall_during(load)
    del models
    assert took > 0.2, f"loading took {took:.3f} s: too short to tell"
    assert stall < 0.05, f"loading: the main thread could not run for {stall:.3f} s"


def test_a_caches_first_load_lets_python_run(renderer):
    scheduler = renderer.create_scheduler()

    kept = []                    # (as above: destruction is not what is timed)

    def load():
        for _ in range(4):                       # a cache of its own each time: each load reads the file
            cache = pyedge.ModelCache(renderer)
            kept.append((cache, cache.load(pyedge.AnimatedModelPTBNJWT, str(DATA / "test_figure.glb"), scheduler)))

    took, stall = _stall_during(load)
    del kept
    assert took > 0.2, f"loading took {took:.3f} s: too short to tell"
    assert stall < 0.05, f"ModelCache.load: the main thread could not run for {stall:.3f} s"
    model = pyedge.ModelCache(renderer).load(pyedge.AnimatedModelPTBNJWT, str(DATA / "test_figure.glb"), scheduler)
    assert isinstance(model, pyedge.AnimatedModelPTBNJWT)


def test_building_an_octree_lets_python_run(renderer):
    n = 700
    model = pyedge.ModelBuilder(renderer, renderer.create_scheduler())
    mesh = model.add_mesh_builder("ground", "ground")
    ys, xs = np.mgrid[0:n + 1, 0:n + 1].astype(np.float32)
    positions = np.stack([xs.ravel(), ys.ravel(), np.zeros(xs.size, np.float32)], axis=1)
    k = (np.arange(n)[:, None] * (n + 1) + np.arange(n)[None, :]).ravel().astype(np.uint32)
    triangles = np.concatenate([np.stack([k, k + 1, k + n + 2], 1), np.stack([k, k + n + 2, k + n + 1], 1)])
    mesh.set_vertices_numpy(positions)
    mesh.set_indices_numpy(triangles)
    mesh.set_default_triangles()
    model.attach_mesh_to_joint(mesh)
    out = {}
    _assert_runs_alongside(lambda: out.setdefault("tree", model.build_octree(identity())), "build_octree")
    assert out["tree"].size == 2 * n * n
