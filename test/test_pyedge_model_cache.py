# SPDX-License-Identifier: MIT
# Copyright (c) 2004-2026 Manvel Avetisian

"""Contract of pyedge.ModelCache: one read per file, a new model per load."""
from pathlib import Path

import pyedge
import pyrove
import pytest


def _asset_path(name: str) -> str:
    return str(Path(__file__).resolve().parent / "data" / name)


@pytest.fixture
def renderer():
    return pyedge.Renderer(backend="null")


def test_each_load_is_a_new_model_of_the_requested_class(renderer):
    cache = pyedge.ModelCache(renderer)
    scheduler = renderer.create_scheduler()

    a = cache.load(pyedge.StaticModelPTBNT, _asset_path("test_gltf_loder_materials.glb"), scheduler)
    b = cache.load(pyedge.StaticModelPTBNT, _asset_path("test_gltf_loder_materials.glb"), scheduler)
    animated = cache.load(pyedge.AnimatedModelPTBNJWT, _asset_path("test_gltf_loder_animation.glb"), scheduler)

    assert type(a) is pyedge.StaticModelPTBNT
    assert type(animated) is pyedge.AnimatedModelPTBNJWT
    assert a is not b
    assert cache.prototypes_count == 2
    assert a.meshes_count == b.meshes_count > 0
    assert a.materials_count == b.materials_count > 1


def test_material_settings_stay_on_their_instance(renderer):
    cache = pyedge.ModelCache(renderer)
    scheduler = renderer.create_scheduler()
    path = _asset_path("test_gltf_loder_materials.glb")
    a = cache.load(pyedge.StaticModelPTBNT, path, scheduler)
    b = cache.load(pyedge.StaticModelPTBNT, path, scheduler)

    a.set_materials_extra_parameter(0, pyrove.vec4(4.0, 0.0, 0.0, 0.0))
    a.set_materials_shader_program(renderer.create_shader_program("vertex.spv", "fragment.spv"))

    for i in range(b.materials_count):
        assert a.material(i).extra_parameter(0).x == pytest.approx(4.0)
        assert b.material(i).extra_parameter(0).x == pytest.approx(0.0)
        assert b.material(i).shader_program is None


def test_animations_pose_their_own_instance(renderer):
    cache = pyedge.ModelCache(renderer)
    scheduler = renderer.create_scheduler()
    path = _asset_path("test_gltf_loder_animation.glb")
    a = cache.load(pyedge.AnimatedModelPTBNJWT, path, scheduler)
    b = cache.load(pyedge.AnimatedModelPTBNJWT, path, scheduler)
    identity = pyrove.mat4()
    identity.identity()

    a.pose(-1000.0)
    b.pose(-1000.0)
    before = b.bounds(identity)
    a.animations[0].pose(1000.0)
    after_a, after_b = a.bounds(identity), b.bounds(identity)

    assert (after_a.lo.x, after_a.lo.y, after_a.lo.z, after_a.hi.x, after_a.hi.y, after_a.hi.z) != (
        before.lo.x, before.lo.y, before.lo.z, before.hi.x, before.hi.y, before.hi.z)
    assert (after_b.lo.x, after_b.lo.y, after_b.lo.z, after_b.hi.x, after_b.hi.y, after_b.hi.z) == (
        before.lo.x, before.lo.y, before.lo.z, before.hi.x, before.hi.y, before.hi.z)


def test_refusals(renderer):
    cache = pyedge.ModelCache(renderer)
    scheduler = renderer.create_scheduler()

    with pytest.raises(TypeError):
        cache.load(pyedge.ModelBuilder, _asset_path("cone.glb"), scheduler)
    with pytest.raises(ValueError, match="another renderer"):
        cache.load(pyedge.StaticModelPTBNT, _asset_path("cone.glb"),
                   pyedge.Renderer(backend="null").create_scheduler())
    with pytest.raises(RuntimeError):
        cache.load(pyedge.StaticModelPTBNT, _asset_path("no_such_model.glb"), scheduler)
    assert cache.prototypes_count == 0


def test_clear_rereads_and_keeps_instances(renderer):
    cache = pyedge.ModelCache(renderer)
    scheduler = renderer.create_scheduler()
    a = cache.load(pyedge.StaticModelPTBNT, _asset_path("cone.glb"), scheduler)
    cache.clear()
    assert cache.prototypes_count == 0
    identity = pyrove.mat4()
    identity.identity()
    assert a.bounds(identity).hi.x > a.bounds(identity).lo.x
    cache.load(pyedge.StaticModelPTBNT, _asset_path("cone.glb"), scheduler)
    assert cache.prototypes_count == 1
