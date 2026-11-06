# SPDX-License-Identifier: MIT
# Copyright (c) 2004-2026 Manvel Avetisian

from pathlib import Path

import importlib

import numpy as np
import pytest


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


def _asset_path(name: str) -> str:
    return str(Path(__file__).resolve().parent / "data" / name)


def test_render_models_load_gltf_assets():
    renderer = pyedge.Renderer(backend="null")
    scheduler = renderer.create_scheduler()

    plane = pyedge.StaticModelPTBNT(renderer, scheduler)
    plane.load_from_gltf(_asset_path("test_gltf_loder_plane.glb"))
    plane.check()

    assert plane.joints_count >= 1
    assert plane.meshes_count >= 1
    assert plane.materials_count >= 1
    first_material = plane.material(0)
    first_material.set_extra_parameter(0, pyrove.vec4(3.0, 0.0, 0.0, 0.0))
    plane.set_materials_extra_parameter(0, pyrove.vec4(5.0, 0.0, 0.0, 0.0))
    plane.set_materials_shader_program(renderer.create_shader_program("vertex.spv", "fragment.spv"))
    assert first_material.extra_parameter(0).x == pytest.approx(5.0)
    assert first_material.shader_program is not None
    plane.clear_materials_shader_program()
    assert first_material.shader_program is None

    animated = pyedge.AnimatedModelPTBNJWT(renderer, scheduler)
    animated.load_from_gltf(_asset_path("test_gltf_loder_animation.glb"))
    animated.check()
    assert animated.animations_count >= 1
    animated.animations[0].pose(0.25)


def test_model_builder_numpy_geometry_and_binary_roundtrip(tmp_path: Path):

    renderer = pyedge.Renderer(backend="null")
    scheduler = renderer.create_scheduler()

    model = pyedge.ModelBuilder(renderer, scheduler)
    mesh = model.add_mesh_builder("quad", "quad")

    positions = np.array(
        [
            [-1.0, -1.0, 0.0],
            [1.0, -1.0, 0.0],
            [1.0, 1.0, 0.0],
            [-1.0, 1.0, 0.0],
        ],
        dtype=np.float32,
    )
    texcoords = np.array(
        [
            [0.0, 0.0],
            [1.0, 0.0],
            [1.0, 1.0],
            [0.0, 1.0],
        ],
        dtype=np.float32,
    )
    normals = np.array([[0.0, 0.0, 1.0]] * 4, dtype=np.float32)
    tangents = np.array([[1.0, 0.0, 0.0]] * 4, dtype=np.float32)
    binormals = np.array([[0.0, 1.0, 0.0]] * 4, dtype=np.float32)
    triangles = np.array([[0, 1, 2], [0, 2, 3]], dtype=np.uint16)

    mesh.set_vertices_numpy(
        positions,
        texcoords=texcoords,
        normals=normals,
        tangents=tangents,
        binormals=binormals,
    )
    mesh.set_indices_numpy(triangles)
    mesh.set_default_triangles()
    model.attach_mesh_to_joint(mesh, "quad_joint")

    out_file = tmp_path / "quad_model_builder.bin"
    model.save_to_file(str(out_file))

    assert out_file.exists()
    assert out_file.stat().st_size > 0

    loaded_model = pyedge.ModelBuilder(renderer, scheduler)
    loaded_model.load_from_file(str(out_file))

    assert loaded_model.meshes_count == 1
    assert loaded_model.joints_count == 1

    loaded_mesh = loaded_model.mesh_by_name("quad")
    assert loaded_mesh.vertices_count == 4
    assert loaded_mesh.indices_count == 6
    assert loaded_mesh.triangles_count == 1


def test_model_builder_to_static_model_cross_load(tmp_path: Path):

    renderer = pyedge.Renderer(backend="null")
    scheduler = renderer.create_scheduler()

    # Build a two-mesh model with ModelBuilder.
    model = pyedge.ModelBuilder(renderer, scheduler)

    mesh_a = model.add_mesh_builder("floor", "floor")
    positions_a = np.array(
        [[-5, 0, -5], [5, 0, -5], [5, 0, 5], [-5, 0, 5]], dtype=np.float32,
    )
    texcoords_a = np.array([[0, 0], [1, 0], [1, 1], [0, 1]], dtype=np.float32)
    normals_a = np.array([[0, 1, 0]] * 4, dtype=np.float32)
    tangents_a = np.array([[1, 0, 0]] * 4, dtype=np.float32)
    binormals_a = np.array([[0, 0, 1]] * 4, dtype=np.float32)
    tris_a = np.array([[0, 1, 2], [0, 2, 3]], dtype=np.uint16)
    mesh_a.set_vertices_numpy(
        positions_a, texcoords=texcoords_a, normals=normals_a,
        tangents=tangents_a, binormals=binormals_a,
    )
    mesh_a.set_indices_numpy(tris_a)
    mesh_a.set_default_triangles()
    model.attach_mesh_to_joint(mesh_a, "floor_joint")

    mesh_b = model.add_mesh_builder("wall", "wall")
    positions_b = np.array(
        [[-5, 0, 0], [5, 0, 0], [5, 3, 0], [-5, 3, 0]], dtype=np.float32,
    )
    texcoords_b = np.array([[0, 0], [1, 0], [1, 1], [0, 1]], dtype=np.float32)
    normals_b = np.array([[0, 0, 1]] * 4, dtype=np.float32)
    tangents_b = np.array([[1, 0, 0]] * 4, dtype=np.float32)
    binormals_b = np.array([[0, 1, 0]] * 4, dtype=np.float32)
    tris_b = np.array([[0, 1, 2], [0, 2, 3]], dtype=np.uint16)
    mesh_b.set_vertices_numpy(
        positions_b, texcoords=texcoords_b, normals=normals_b,
        tangents=tangents_b, binormals=binormals_b,
    )
    mesh_b.set_indices_numpy(tris_b)
    mesh_b.set_default_triangles()
    model.attach_mesh_to_joint(mesh_b, "wall_joint")

    edge_file = tmp_path / "builder_to_static.edge"
    model.save_to_file(str(edge_file))
    assert edge_file.stat().st_size > 0

    # Load the same file with StaticModelPTBNT.
    static_model = pyedge.StaticModelPTBNT(renderer, scheduler)
    static_model.load_from_file(str(edge_file))
    static_model.check()

    assert static_model.meshes_count == 2
    assert static_model.joints_count == 2


def test_static_model_octree_build_and_query():
    renderer = pyedge.Renderer(backend="null")
    scheduler = renderer.create_scheduler()

    model = pyedge.StaticModelPTBNT(renderer, scheduler)
    model.load_from_gltf(_asset_path("test_gltf_loder_plane.glb"))

    world = pyrove.mat4()
    world.identity()

    octree = model.build_octree(world)
    assert octree.size > 0

    bounds = octree.bounds
    hits_aabb = octree.query_aabb(bounds)
    assert len(hits_aabb) > 0

    items = octree.query_aabb_items(bounds)
    assert len(items) > 0
    triangle, mesh_id = items[0]
    assert type(triangle).__name__ == "triangle3"

    sphere = pyrove.sphere3(bounds.centre(), 10.0)
    hits_sphere = octree.query_sphere(sphere)
    assert len(hits_sphere) > 0


def test_load_model_gives_the_class_the_file_is(tmp_path):
    """A .edge file's meshes name the model's class, a glTF's contents decide it: what opens any model need not know it."""
    renderer = pyedge.Renderer(backend="null")
    scheduler = renderer.create_scheduler()
    assert type(pyedge.load_model(renderer, scheduler, _asset_path("test_gltf_loder_plane.glb"))) is pyedge.StaticModelPTBNT
    coloured = pyedge.load_model(renderer, scheduler, _asset_path("test_vertex_colors.glb"))
    assert type(coloured) is pyedge.StaticModelPTBNTC
    assert type(pyedge.load_model(renderer, scheduler, _asset_path("test_skinned_groups.glb"))) is pyedge.AnimatedModelPTBNJWT

    # coloured files, static and skinned, open as what they are
    coloured.save_to_file(str(tmp_path / "c.edge"))
    assert type(pyedge.load_model(renderer, scheduler, str(tmp_path / "c.edge"))) is pyedge.StaticModelPTBNTC
    skinned = pyedge.AnimatedModelPTBNJWTC(renderer, scheduler)
    skinned.load_from_gltf(_asset_path("test_skinned_colors.glb"))
    skinned.save_to_file(str(tmp_path / "s.edge"))
    again = pyedge.load_model(renderer, scheduler, str(tmp_path / "s.edge"))
    assert type(again) is pyedge.AnimatedModelPTBNJWTC
    assert again.meshes_count == skinned.meshes_count

    with pytest.raises(ValueError):
        pyedge.load_model(renderer, scheduler, _asset_path("test_white.png"))
    with pytest.raises(RuntimeError):
        pyedge.load_model(renderer, scheduler, str(tmp_path / "not_there.edge"))


def test_a_model_drawn_as_instances_draws_each_instance_in_view():
    renderer = pyedge.Renderer(backend="null")
    scheduler = renderer.create_scheduler()
    cone = pyedge.StaticModelPTBNT(renderer, scheduler)
    cone.load_from_gltf(_asset_path("cone.glb"))
    camera = pyedge.Camera()
    camera.set_orthographic_projection(10.0, 10.0, 0.1, 20.0)
    camera.look_at(pyrove.vec3(0, 0, 10), pyrove.vec3(0, 0, 0), pyrove.vec3(0, 1, 0))

    def at(x):
        m = pyrove.mat4()
        m.identity()
        m.translate(x, 0.0, 0.0)
        return m

    def flush():
        renderer.begin_frame()
        renderer.begin_pass(renderer.default_render_target)
        scheduler.flush()
        renderer.end_pass()
        renderer.finish_frame()

    cone.draw(camera, at(0.0))
    flush()
    one = scheduler.executed_triangles_count
    assert one > 0
    # two in view and one far out of it: one draw call, the triangles of two cones drawn and of three offered
    cone.draw_instances(camera, [at(-2.0), at(2.0), at(100.0)])
    flush()
    assert scheduler.visible_calls_count == 1
    assert scheduler.executed_triangles_count - one == 2 * one
    assert scheduler.offered_triangles_count - one == 3 * one
    cone.draw_instances(camera, [])
    flush()
    assert scheduler.visible_calls_count == 0
