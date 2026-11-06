# SPDX-License-Identifier: MIT
# Copyright (c) 2004-2026 Manvel Avetisian

"""Regression tests for bugs found during ModelBuilder rendering debugging.

These tests verify fixes for:
- Joint matrix not initialized to identity (rove::matrix<4,4> has no-op default ctor)
- nanobind shared_ptr control block mismatch causing weak_ptr expiry
- Multi-mesh collider bounds collapsing to origin
- End-to-end GPU rendering producing only background clear color
"""
from pathlib import Path

import importlib
import math



def _import_required(module_name: str):
    try:
        return importlib.import_module(module_name)
    except (ModuleNotFoundError, ImportError) as exc:
        raise RuntimeError(
            f"Required module '{module_name}' is not importable. "
            "Install pyrove and build bindings first:\n"
            "  pip install -e rove\n"
            "  cmake --build build --target pyedge"
        ) from exc


pyedge = _import_required("pyedge")
pyrove = _import_required("pyrove")


def _build_quad_model(renderer, scheduler, *, with_material=False):
    """Helper: build a single-quad ModelBuilder with PTBNT vertices."""
    import numpy as np

    model = pyedge.ModelBuilder(renderer, scheduler)
    material = None
    if with_material:
        material = renderer.create_material()
        material.name = "white"
    mesh = model.add_mesh_builder("quad", "quad")
    positions = np.array(
        [[-1, -1, 0], [1, -1, 0], [1, 1, 0], [-1, 1, 0]], dtype=np.float32,
    )
    normals = np.tile(np.array([0, 0, 1], dtype=np.float32), (4, 1))
    tangents = np.tile(np.array([1, 0, 0], dtype=np.float32), (4, 1))
    binormals = np.tile(np.array([0, 1, 0], dtype=np.float32), (4, 1))
    texcoords = np.array([[0, 0], [1, 0], [1, 1], [0, 1]], dtype=np.float32)
    indices = np.array([0, 1, 2, 0, 2, 3], dtype=np.uint32)
    mesh.set_vertices_numpy(
        positions, texcoords=texcoords, normals=normals,
        tangents=tangents, binormals=binormals,
    )
    mesh.set_indices_numpy(indices)
    mesh.set_default_triangles(material)
    model.attach_mesh_to_joint(mesh)
    return model


def test_joint_matrix_is_identity_after_attach(tmp_path: Path):
    """Regression: attach_mesh_to_joint must set joint matrix to identity.

    rove::matrix<4,4> default constructor leaves values uninitialized.
    If the joint matrix is not explicitly set to identity, the garbage values
    get serialized, and on reload the transform collapses all geometry to the
    origin (invisible rendering, zero-extent collider bounds).
    """
    renderer = pyedge.Renderer(backend="null")
    scheduler = renderer.create_scheduler()

    model = _build_quad_model(renderer, scheduler)
    edge_file = tmp_path / "joint_identity.edge"
    model.save_to_file(str(edge_file))

    loaded = pyedge.StaticModelPTBNT(renderer, scheduler)
    loaded.load_from_file(str(edge_file))
    loaded.check()

    world = pyrove.mat4()
    world.identity()

    octree = loaded.build_octree(world)
    bounds = octree.bounds

    # The quad spans [-1,1] in X and Y, 0 in Z.  Collider bounds must be
    # non-degenerate (not collapsed to origin).
    extent_x = bounds.hi.x - bounds.lo.x
    extent_y = bounds.hi.y - bounds.lo.y
    assert extent_x > 1.0, f"collider X extent too small: {extent_x}"
    assert extent_y > 1.0, f"collider Y extent too small: {extent_y}"
    assert octree.size == 2


def test_joint_weak_ptr_resolves_after_roundtrip(tmp_path: Path):
    """Regression: weak_ptr in joints must resolve meshes after save/load.

    nanobind creates independent shared_ptr control blocks when Python passes
    a shared_ptr back to C++.  If attach_mesh_to_joint stores the Python
    shared_ptr's weak_ptr instead of looking up the mesh in the internal
    vector, the weak_ptr expires immediately and draw() silently skips the
    mesh.  Verify by checking that collider has triangles (which requires
    the joint->mesh weak_ptr to lock successfully).
    """
    renderer = pyedge.Renderer(backend="null")
    scheduler = renderer.create_scheduler()

    model = _build_quad_model(renderer, scheduler)
    edge_file = tmp_path / "weakptr_resolve.edge"
    model.save_to_file(str(edge_file))

    loaded = pyedge.StaticModelPTBNT(renderer, scheduler)
    loaded.load_from_file(str(edge_file))
    loaded.check()

    world = pyrove.mat4()
    world.identity()

    octree = loaded.build_octree(world)
    # If weak_ptr failed, no triangles would be collected.
    assert octree.size == 2, (
        f"expected 2 triangles but got {octree.size} "
        "(joint->mesh weak_ptr may have expired)"
    )


def test_multi_mesh_collider_bounds_non_degenerate(tmp_path: Path):
    """Multiple meshes each attached to their own joint should all have
    identity joint matrices and produce correct combined collider bounds."""
    import numpy as np

    renderer = pyedge.Renderer(backend="null")
    scheduler = renderer.create_scheduler()

    model = pyedge.ModelBuilder(renderer, scheduler)
    for i, offset in enumerate([(0, 0), (10, 0), (0, 10)]):
        mesh = model.add_mesh_builder(f"quad_{i}", f"quad_{i}")
        positions = np.array(
            [
                [offset[0] - 1, offset[1] - 1, 0],
                [offset[0] + 1, offset[1] - 1, 0],
                [offset[0] + 1, offset[1] + 1, 0],
                [offset[0] - 1, offset[1] + 1, 0],
            ],
            dtype=np.float32,
        )
        normals = np.tile(np.array([0, 0, 1], dtype=np.float32), (4, 1))
        tangents = np.tile(np.array([1, 0, 0], dtype=np.float32), (4, 1))
        binormals = np.tile(np.array([0, 1, 0], dtype=np.float32), (4, 1))
        texcoords = np.array([[0, 0], [1, 0], [1, 1], [0, 1]], dtype=np.float32)
        indices = np.array([0, 1, 2, 0, 2, 3], dtype=np.uint32)
        mesh.set_vertices_numpy(
            positions, texcoords=texcoords, normals=normals,
            tangents=tangents, binormals=binormals,
        )
        mesh.set_indices_numpy(indices)
        mesh.set_default_triangles()
        model.attach_mesh_to_joint(mesh)

    edge_file = tmp_path / "multi_mesh.edge"
    model.save_to_file(str(edge_file))

    loaded = pyedge.StaticModelPTBNT(renderer, scheduler)
    loaded.load_from_file(str(edge_file))
    loaded.check()
    assert loaded.meshes_count == 3
    assert loaded.joints_count == 3

    world = pyrove.mat4()
    world.identity()

    octree = loaded.build_octree(world)
    bounds = octree.bounds
    assert octree.size == 6
    # Bounds should span from (-1,-1) to (11,11) in XY.
    assert bounds.lo.x < 0, f"lo.x should be negative: {bounds.lo.x}"
    assert bounds.hi.x > 9, f"hi.x should be > 9: {bounds.hi.x}"
    assert bounds.hi.y > 9, f"hi.y should be > 9: {bounds.hi.y}"


def test_model_builder_renders_visible_pixels(tmp_path: Path):
    """Regression: ModelBuilder geometry must produce visible pixels after
    save/load, not just the background clear color.

    This was the end-to-end symptom of the joint matrix bug: the viewer
    showed only the blue clear color because all geometry was transformed
    to the origin by the garbage joint matrix.
    """
    renderer = pyedge.create_renderer(64, 64, backend="vulkan")
    assert renderer.backend == "vulkan"
    scheduler = renderer.create_scheduler()

    model = _build_quad_model(renderer, scheduler, with_material=True)
    edge_file = tmp_path / "render_test.edge"
    model.save_to_file(str(edge_file))

    # Free builder objects before loading — they hold duplicate GPU resources.
    del model

    loaded = pyedge.StaticModelPTBNT(renderer, scheduler)
    loaded.load_from_file(str(edge_file))
    loaded.check()
    loaded.always_visible = True

    camera = pyedge.Camera()
    camera.set_perspective_projection(math.pi / 3.0, 1.0, 0.1, 100.0)
    camera.look_at(
        pyrove.vec3(0.0, 0.0, 3.0),
        pyrove.vec3(0.0, 0.0, 0.0),
        pyrove.vec3(0.0, 1.0, 0.0),
    )

    world = pyrove.mat4()
    world.identity()

    rt = renderer.create_render_target(64, 64)
    clear_state = pyedge.RenderPassClearState()
    clear_state.clear_color_r = 0.0
    clear_state.clear_color_g = 0.0
    clear_state.clear_color_b = 1.0
    clear_state.clear_color_a = 1.0

    renderer.begin_frame()
    loaded.draw(camera, world)
    renderer.begin_pass(rt, clear_state)
    scheduler.flush()
    renderer.end_pass()
    renderer.finish_frame()

    pixels = rt.color_texture.download()
    pixel_data = bytes(pixels)
    unique = set()
    for i in range(0, len(pixel_data), 4):
        unique.add(pixel_data[i:i + 4])

    # Explicit cleanup: GPU resources must be freed before the renderer.
    del loaded, rt, clear_state, camera
    del scheduler
    del renderer

    assert len(unique) > 1, (
        "only one unique pixel color found — geometry did not render "
        "(likely joint matrix or weak_ptr regression)"
    )
