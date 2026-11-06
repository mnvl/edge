# SPDX-License-Identifier: MIT
# Copyright (c) 2004-2026 Manvel Avetisian

"""Model.bounds(world) is the box of what draw(world) puts on screen.

The oracle is the renderer: the model is drawn orthographically along each
world axis, and the silhouette's pixel rectangle is compared with the
bounds' eight corners projected through the same camera.  Both directions
are checked -- nothing drawn outside the box (the bounds enclose the mesh,
to the pixel) and every face of the box touched by the silhouette (the
bounds are the mesh's, not something larger such as its joints' origins).
The second is looser: a tip thinner than a pixel -- a finger, a strand of
hair -- falls between pixel centres and is not rasterised, so the drawn
silhouette can stop short of a vertex the bounds rightly include.  It is
allowed 1% of the model's size; joint origins missed by 8-55 cm on a person.

A simulation sizes characters and places animated props from these bounds.
"""
from pathlib import Path

import numpy as np
import pytest

import pyedge
import pyrove

EDGE_TEST = Path(__file__).resolve().parent / "data"
SIZE = 768
CLEAR = (255, 0, 255)  # magenta: no texture in these assets is exactly it


@pytest.fixture(scope="module")
def renderer():
    renderer = pyedge.create_renderer(SIZE, SIZE, backend="vulkan")
    assert renderer.backend == "vulkan"
    return renderer


def _identity():
    m = pyrove.mat4()
    m.identity()
    return m


def _corners(b) -> np.ndarray:
    lo = np.array([b.lo.x, b.lo.y, b.lo.z])
    hi = np.array([b.hi.x, b.hi.y, b.hi.z])
    return np.array([[(lo, hi)[i >> k & 1][k] for k in range(3)] for i in range(8)])


def _silhouette_vs_bounds(renderer, scheduler, model, world, reference_size: float):
    """For each view axis: (silhouette pixel rect, projected bounds rect)."""
    bounds = model.bounds(world)
    corners = _corners(bounds)
    centre = corners.mean(axis=0)
    extent = 2.5 * reference_size
    pixel = extent / SIZE

    target = renderer.create_render_target(SIZE, SIZE)
    clear = pyedge.RenderPassClearState()
    clear.clear_color_r, clear.clear_color_g, clear.clear_color_b = (c / 255.0 for c in CLEAR)
    clear.clear_color_a = 1.0

    results = []
    for axis, up in ((0, (0.0, 0.0, 1.0)), (1, (0.0, 0.0, 1.0)), (2, (0.0, 1.0, 0.0))):
        eye = centre.copy()
        eye[axis] += 10.0 * extent
        camera = pyedge.Camera()
        camera.set_orthographic_projection(extent, extent, 0.1 * extent, 30.0 * extent)
        camera.look_at(pyrove.vec3(*eye), pyrove.vec3(*centre), pyrove.vec3(*up))

        renderer.begin_frame()
        model.draw(camera, world)
        renderer.begin_pass(target, clear)
        scheduler.flush()
        renderer.end_pass()
        renderer.finish_frame()
        bgrx = np.frombuffer(target.color_texture.download(), np.uint8).reshape(SIZE, SIZE, 4)
        drawn = np.any(bgrx[..., 2::-1] != np.array(CLEAR, np.uint8), axis=-1)
        rows, cols = np.nonzero(drawn)
        assert rows.size > 0, f"view along axis {axis}: nothing drawn"
        assert rows.min() > 0 and cols.min() > 0 and rows.max() < SIZE - 1 and cols.max() < SIZE - 1, \
            f"view along axis {axis}: silhouette touches the image border, widen the view"
        silhouette = (cols.min(), rows.min(), cols.max() + 1, rows.max() + 1)

        view_proj = camera.projection_matrix.to_numpy() @ camera.view_matrix.to_numpy()
        clip = view_proj @ np.c_[corners, np.ones(8)].T
        ndc = clip[:2] / clip[3]
        px = (ndc[0] + 1.0) * 0.5 * SIZE
        py = (1.0 - ndc[1]) * 0.5 * SIZE
        projected = (px.min(), py.min(), px.max(), py.max())
        results.append((axis, silhouette, projected, pixel))
    return bounds, results


def _assert_bounds_are_silhouette(renderer, scheduler, model, world, reference_size, what):
    bounds, results = _silhouette_vs_bounds(renderer, scheduler, model, world, reference_size)
    for axis, silhouette, projected, pixel in results:
        # silhouette - projected, per edge (x0, y0, x1, y1): drawn beyond the
        # box is negative on the low edges and positive on the high ones.
        diff = np.subtract(silhouette, projected)
        outside = np.concatenate([-diff[:2], diff[2:]])
        slack = max(1.5, 0.01 * reference_size / pixel)
        assert np.all(outside <= 1.5) and np.all(-outside <= slack), (
            f"{what}, view along axis {axis}: drawn pixels span {silhouette}, "
            f"get_bounds projects to {tuple(round(v, 1) for v in projected)} "
            f"(drawn beyond the box by {np.round(outside, 1)} px, {pixel * 100:.2f} cm each, "
            f"allowed 1.5 outside and {slack:.1f} inside); "
            f"bounds lo=({bounds.lo.x:.3f}, {bounds.lo.y:.3f}, {bounds.lo.z:.3f}) "
            f"hi=({bounds.hi.x:.3f}, {bounds.hi.y:.3f}, {bounds.hi.z:.3f})")


def _reference_size(renderer, path: Path) -> float:
    """The mesh's size in its bind pose, only to frame the views."""
    static = pyedge.StaticModelPTBNT(renderer, renderer.create_scheduler())
    static.load_from_gltf(str(path))
    size = static.bounds(_identity()).diagonal()
    return max(size.x, size.y, size.z)


@pytest.mark.parametrize("asset, animation, time", [
    (EDGE_TEST / "test_figure.glb", None, None),  # no animation applied
    (EDGE_TEST / "test_figure.glb", "Walk", 0.37),
    (EDGE_TEST / "test_figure.glb", "Dance", 1.1),
    (EDGE_TEST / "test_figure.glb", "Walk", 0.9),
    (EDGE_TEST / "test_figure.glb", "Idle", 2.0),
], ids=lambda v: v.name if isinstance(v, Path) else str(v))
def test_animated_model_bounds_are_the_drawn_skinned_mesh(renderer, asset, animation, time):
    path = asset
    assert path.exists(), path
    scheduler = renderer.create_scheduler()
    model = pyedge.AnimatedModelPTBNJWT(renderer, scheduler)
    model.load_from_gltf(str(path))
    model.always_visible = True  # culling is not under test; the draw is the oracle
    if animation is not None:
        by_name = {a.name: a for a in model.animations}
        by_name[animation].pose(time)

    world = pyrove.mat4()
    world.identity()
    world.rotate(pyrove.vec3(1.0, 0.0, 0.0), np.radians(90.0))  # a Z-up world
    world.translate(0.3, -0.2, 0.1)
    _assert_bounds_are_silhouette(renderer, scheduler, model, world, _reference_size(renderer, path),
                                  f"{asset.name} {animation}@{time}")


@pytest.mark.parametrize("asset", [
    EDGE_TEST / "cone.glb",
    EDGE_TEST / "test_figure.glb",
    EDGE_TEST / "cylinder_30_degrees.glb",  # rotated and mirrored by its node
], ids=lambda v: v.name)
def test_static_model_bounds_are_the_drawn_mesh(renderer, asset):
    path = asset
    assert path.exists(), path
    scheduler = renderer.create_scheduler()
    model = pyedge.StaticModelPTBNT(renderer, scheduler)
    model.load_from_gltf(str(path))
    model.always_visible = True
    world = pyrove.mat4()
    world.identity()
    world.rotate(pyrove.vec3(0.0, 0.0, 1.0), np.radians(30.0))
    _assert_bounds_are_silhouette(renderer, scheduler, model, world, _reference_size(renderer, path), asset.name)
