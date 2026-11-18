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

    a = cache.load(pyedge.StaticModelPTBNT, _asset_path("test_gltf_loder_materials.glb"))
    b = cache.load(pyedge.StaticModelPTBNT, _asset_path("test_gltf_loder_materials.glb"))
    animated = cache.load(pyedge.AnimatedModelPTBNJWT, _asset_path("test_gltf_loder_animation.glb"))

    assert type(a) is pyedge.StaticModelPTBNT
    assert type(animated) is pyedge.AnimatedModelPTBNJWT
    assert a is not b
    assert cache.prototypes_count == 2
    assert a.meshes_count == b.meshes_count > 0
    assert a.materials_count == b.materials_count > 1


def test_material_settings_stay_on_their_instance(renderer):
    cache = pyedge.ModelCache(renderer)
    path = _asset_path("test_gltf_loder_materials.glb")
    a = cache.load(pyedge.StaticModelPTBNT, path)
    b = cache.load(pyedge.StaticModelPTBNT, path)

    a.set_materials_extra_parameter(0, pyrove.vec4(4.0, 0.0, 0.0, 0.0))
    a.set_materials_shader_program(renderer.create_shader_program("vertex.spv", "fragment.spv"))

    for i in range(b.materials_count):
        assert a.material(i).extra_parameter(0).x == pytest.approx(4.0)
        assert b.material(i).extra_parameter(0).x == pytest.approx(0.0)
        assert b.material(i).shader_program is None


def test_animations_pose_their_own_instance(renderer):
    cache = pyedge.ModelCache(renderer)
    path = _asset_path("test_gltf_loder_animation.glb")
    a = cache.load(pyedge.AnimatedModelPTBNJWT, path)
    b = cache.load(pyedge.AnimatedModelPTBNJWT, path)
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

    with pytest.raises(TypeError):
        cache.load(pyedge.ModelBuilder, _asset_path("cone.glb"))
    with pytest.raises(RuntimeError):
        cache.load(pyedge.StaticModelPTBNT, _asset_path("no_such_model.glb"))
    assert cache.prototypes_count == 0


def test_clear_rereads_and_keeps_instances(renderer):
    cache = pyedge.ModelCache(renderer)
    a = cache.load(pyedge.StaticModelPTBNT, _asset_path("cone.glb"))
    cache.clear()
    assert cache.prototypes_count == 0
    identity = pyrove.mat4()
    identity.identity()
    assert a.bounds(identity).hi.x > a.bounds(identity).lo.x
    b = cache.load(pyedge.StaticModelPTBNT, _asset_path("cone.glb"))
    assert cache.prototypes_count == 1
    assert b.bounds(identity).hi.x > b.bounds(identity).lo.x


def test_a_model_is_cached_while_python_holds_an_instance_of_it(renderer):
    """The cache holds its prototypes weakly; the instances Python holds
    keep theirs alive, whatever Python's wrapper of them owns."""
    import gc

    cache = pyedge.ModelCache(renderer)
    a = cache.load(pyedge.StaticModelPTBNT, _asset_path("cone.glb"))
    b = cache.load(pyedge.StaticModelPTBNT, _asset_path("cone.glb"))
    del a
    gc.collect()
    assert cache.prototypes_count == 1
    del b
    gc.collect()
    assert cache.prototypes_count == 0
    assert cache.sweep() == 1
    assert cache.sweep() == 0
    c = cache.load(pyedge.StaticModelPTBNT, _asset_path("cone.glb"))
    assert cache.prototypes_count == 1 and c is not None


def test_a_cache_keeping_its_models_keeps_one_no_instance_holds(renderer):
    """keep_models=True (edge::ModelCache::Keep::ALWAYS): a file is read once for the cache's life; sweep() lets go of the
    models nothing else holds.  The default lets a model go with its last instance."""
    import gc

    path = _asset_path("cone.glb")
    keeping = pyedge.ModelCache(renderer, keep_models=True)
    assert keeping.keep_models is True and pyedge.ModelCache(renderer).keep_models is False
    a = keeping.load(pyedge.StaticModelPTBNT, path)
    del a
    gc.collect()
    assert keeping.prototypes_count == 1
    b = keeping.load(pyedge.StaticModelPTBNT, path)
    assert keeping.sweep() == 0
    del b
    gc.collect()
    assert keeping.sweep() == 1
    assert keeping.prototypes_count == 0

    default = pyedge.ModelCache(renderer)
    c = default.load(pyedge.StaticModelPTBNT, path)
    del c
    gc.collect()
    assert default.prototypes_count == 0


@pytest.mark.parametrize("keep_models", [False, True])
def test_a_cache_that_keeps_no_cpu_copy_loads_the_same_geometry(renderer, keep_models):
    identity = pyrove.mat4()
    identity.identity()
    path = _asset_path("test_gltf_loder_directions.glb")
    kept = pyedge.ModelCache(renderer, keep_models=keep_models).load(pyedge.StaticModelPTBNT, path)
    lean = pyedge.ModelCache(renderer, keep_cpu_copy=False, keep_models=keep_models).load(pyedge.StaticModelPTBNT, path)
    a, b = kept.bounds(identity), lean.bounds(identity)
    assert (a.lo.x, a.lo.y, a.lo.z, a.hi.x, a.hi.y, a.hi.z) == (b.lo.x, b.lo.y, b.lo.z, b.hi.x, b.hi.y, b.hi.z)
    assert lean.meshes_count == kept.meshes_count > 0
