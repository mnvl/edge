# SPDX-License-Identifier: MIT
# Copyright (c) 2004-2026 Manvel Avetisian

"""Model.build_octree(world) is the geometry draw(world) puts on screen, at the current pose.

The oracle is the renderer.  The octree's triangles, projected through the
camera that drew the model and filled, must cover exactly the pixels the
model was drawn on -- up to the one-pixel band along the silhouette, where
two coverage rules may disagree.  And rays from the eye through a sample of
pixels, cast into the octree as a simulated lidar casts them, must hit where
the model was drawn and miss where it was not.  A simulation casts its lidar
into these octrees (an animated character per pose), so an
octree of any other pose -- the bind pose, say -- is a wrong collider.

(Rays are sampled, not cast for every pixel: rove's segment query costs
some 300 us a ray on a 60k-triangle mesh.)
"""
from pathlib import Path

import numpy as np
import pytest
from PIL import Image, ImageDraw

import pyedge
import pyrove

EDGE_TEST = Path(__file__).resolve().parent / "data"
SIZE = 320
CLEAR = (255, 0, 255)


@pytest.fixture(scope="module")
def renderer():
    renderer = pyedge.create_renderer(SIZE, SIZE, backend="vulkan")
    assert renderer.backend == "vulkan"
    return renderer


def _box(b) -> tuple[np.ndarray, np.ndarray]:
    return np.array([b.lo.x, b.lo.y, b.lo.z]), np.array([b.hi.x, b.hi.y, b.hi.z])


def _grow(mask: np.ndarray, pixels: int) -> np.ndarray:
    grown = mask.copy()
    for _ in range(pixels):
        for axis in (0, 1):
            for shift in (-1, 1):
                grown |= np.roll(grown, shift, axis)
    return grown


def _band(mask: np.ndarray) -> np.ndarray:
    """Pixels within one pixel of the mask's boundary."""
    grown, shrunk = mask.copy(), mask.copy()
    for axis in (0, 1):
        for shift in (-1, 1):
            moved = np.roll(mask, shift, axis)
            grown |= moved
            shrunk &= moved
    return grown & ~shrunk


def _drawn_and_filled(renderer, scheduler, model, world, view_dir) -> tuple[np.ndarray, np.ndarray]:
    lo, hi = _box(model.bounds(world))
    centre, radius = (lo + hi) / 2, np.linalg.norm(hi - lo) / 2
    eye = centre + np.asarray(view_dir, float) / np.linalg.norm(view_dir) * 3.0 * radius
    camera = pyedge.Camera()
    camera.set_perspective_projection(np.radians(50.0), 1.0, 0.5 * radius, 6.0 * radius)
    camera.look_at(pyrove.vec3(*eye), pyrove.vec3(*centre), pyrove.vec3(0.0, 0.0, 1.0))

    target = renderer.create_render_target(SIZE, SIZE)
    clear = pyedge.RenderPassClearState()
    clear.clear_color_r, clear.clear_color_g, clear.clear_color_b = (c / 255.0 for c in CLEAR)
    clear.clear_color_a = 1.0
    renderer.begin_frame()
    model.draw(camera, world)
    renderer.begin_pass(target, clear)
    scheduler.flush()
    renderer.end_pass()
    renderer.finish_frame()
    bgrx = np.frombuffer(target.color_texture.download(), np.uint8).reshape(SIZE, SIZE, 4)
    drawn = np.any(bgrx[..., 2::-1] != np.array(CLEAR, np.uint8), axis=-1)

    view_projection = camera.projection_matrix.to_numpy() @ camera.view_matrix.to_numpy()
    octree = model.build_octree(world)

    # The octree's triangles, projected and filled.
    triangles = np.array([[(v.x, v.y, v.z) for v in (tri.A, tri.B, tri.C)]
                          for tri, _ in octree.query_aabb_items(octree.bounds)])
    clip = np.c_[triangles.reshape(-1, 3), np.ones(len(triangles) * 3)] @ view_projection.T
    assert np.all(clip[:, 3] > 0), "a triangle reaches behind the camera"
    px = np.stack([(clip[:, 0] / clip[:, 3] + 1) / 2 * SIZE, (1 - clip[:, 1] / clip[:, 3]) / 2 * SIZE], -1)
    # Filled at 4x and read at each pixel's centre, as the GPU samples it:
    # PIL fills the pixels an edge touches, which thin slivers would inflate.
    canvas = Image.new("1", (4 * SIZE, 4 * SIZE), 0)
    draw = ImageDraw.Draw(canvas)
    for tri in px.reshape(-1, 3, 2) * 4:
        draw.polygon([tuple(p) for p in tri], fill=1)
    filled = np.asarray(canvas, bool)[2::4, 2::4]

    # Rays from the eye through the centres of sampled pixels away from the
    # silhouette's edge: half on the drawing, half off it but near it.
    band = _band(drawn)
    rng = np.random.default_rng(0)
    on = np.argwhere(drawn & ~band)
    off = np.argwhere(_grow(drawn, 12) & ~drawn & ~band)
    picks = np.concatenate([on[rng.choice(len(on), 150, replace=False)],
                            off[rng.choice(len(off), 150, replace=False)]])
    ndc = np.stack([(picks[:, 1] + 0.5) / SIZE * 2 - 1, 1 - (picks[:, 0] + 0.5) / SIZE * 2,
                    np.full(len(picks), 0.5), np.ones(len(picks))], -1)
    points = ndc @ np.linalg.inv(view_projection).T
    directions = points[:, :3] / points[:, 3:] - eye
    directions /= np.linalg.norm(directions, axis=1, keepdims=True)
    hits = octree.raycast_batch(pyrove.vec3(*eye), directions.astype(np.float32), float(10.0 * radius))
    np.testing.assert_array_equal(np.isfinite(hits), drawn[picks[:, 0], picks[:, 1]],
                                  err_msg="rays into the octree and the drawing disagree")
    return drawn, filled


def _assert_collider_is_the_drawing(renderer, scheduler, model, world, what):
    for view_dir in ((1.0, -2.0, 0.5), (-2.0, 0.3, 1.0)):
        drawn, filled = _drawn_and_filled(renderer, scheduler, model, world, view_dir)
        assert drawn.sum() > 1000, f"{what}: the model hardly shows ({drawn.sum()} px)"
        band = _band(drawn)
        wrong = (drawn != filled) & ~band
        assert wrong.sum() <= 0.005 * drawn.sum(), (
            f"{what}, view {view_dir}: {wrong.sum()} pixels where the octree and the drawing disagree "
            f"({(filled & ~drawn & ~band).sum()} octree only, {(drawn & ~filled & ~band).sum()} drawing only) "
            f"of {drawn.sum()} drawn")


def _world():
    m = pyrove.mat4()
    m.identity()
    m.rotate(pyrove.vec3(1.0, 0.0, 0.0), np.radians(90.0))  # a Z-up world
    m.translate(0.3, -0.2, 0.1)
    return m


@pytest.mark.parametrize("animation, time", [
    (None, None),
    ("Walk", 0.37),
    ("Dance", 1.1),
    ("Idle", 2.0),
])
def test_animated_octree_is_the_drawn_pose(renderer, animation, time):
    scheduler = renderer.create_scheduler()
    model = pyedge.AnimatedModelPTBNJWT(renderer, scheduler)
    model.load_from_gltf(str(EDGE_TEST / "test_figure.glb"))
    model.always_visible = True
    if animation is not None:
        {a.name: a for a in model.animations}[animation].pose(time)
    _assert_collider_is_the_drawing(renderer, scheduler, model, _world(), f"figure {animation}@{time}")


@pytest.mark.parametrize("asset", ["cone.glb", "cylinder_30_degrees.glb", "test_figure.glb"])
def test_static_octree_is_the_drawn_mesh(renderer, asset):
    scheduler = renderer.create_scheduler()
    model = pyedge.StaticModelPTBNT(renderer, scheduler)
    model.load_from_gltf(str(EDGE_TEST / asset))
    model.always_visible = True
    _assert_collider_is_the_drawing(renderer, scheduler, model, _world(), asset)


def test_animated_octree_follows_the_pose(renderer):
    scheduler = renderer.create_scheduler()
    model = pyedge.AnimatedModelPTBNJWT(renderer, scheduler)
    model.load_from_gltf(str(EDGE_TEST / "test_figure.glb"))
    walk = {a.name: a for a in model.animations}["Walk"]
    world = _world()
    walk.pose(0.1)
    a = _box(model.build_octree(world).bounds)
    walk.pose(0.6)
    b = _box(model.build_octree(world).bounds)
    assert not np.allclose(np.r_[a], np.r_[b], atol=1e-3), "the octree did not move with the animation"
