# SPDX-License-Identifier: MIT
# Copyright (c) 2004-2026 Manvel Avetisian

"""pyedge reaches a material's occlusion: glTF's occlusionTexture packed into the red of the metallic-roughness texture (glTF's ORM),
whether a material has it and how strong; what cannot be packed is refused, naming the file.  The assets are made by
data/make_test_occlusion.py."""
from pathlib import Path

import numpy as np
import pytest

import pyedge

DATA = Path(__file__).resolve().parent / "data"


@pytest.fixture(scope="module")
def renderer():
    return pyedge.create_renderer(16, 16, backend="null")


def materials(model):
    return {m.name: m for m in (model.material(i) for i in range(model.materials_count))}


def test_a_material_has_occlusion_and_its_strength(renderer):
    model = pyedge.StaticModelPTBNT(renderer)
    model.load_from_gltf(str(DATA / "test_occlusion.gltf"))
    found = materials(model)
    assert (found["orm"].occlusion, found["orm"].occlusion_strength) == (True, 0.5)
    assert (found["separate"].occlusion, found["separate"].occlusion_strength) == (True, 1.0)
    assert (found["occlusion_only"].occlusion, found["occlusion_only"].occlusion_strength) == (True, 0.25)
    assert not found["plain"].occlusion
    assert found["occlusion_only"].metallic_roughness_texture is not None
    packed = found["separate"].metallic_roughness_texture
    assert (packed.width, packed.height) == (4, 4)


def test_occlusion_is_set_and_its_strength_is_0_to_1(renderer):
    material = renderer.create_material()
    assert not material.occlusion and material.occlusion_strength == 1.0
    material.occlusion = True
    material.occlusion_strength = 0.75
    assert material.occlusion and material.occlusion_strength == 0.75
    for bad in (-0.25, 1.25, float("nan")):
        with pytest.raises(ValueError):
            material.occlusion_strength = bad
    assert material.occlusion_strength == 0.75


def test_occlusion_that_cannot_be_packed_is_refused_naming_the_file(renderer):
    for name in ("test_occlusion_uv1.gltf", "test_occlusion_dds.gltf", "test_occlusion_strength.gltf"):
        model = pyedge.StaticModelPTBNT(renderer)
        with pytest.raises(RuntimeError, match=name.replace(".", r"\.") + ".*occlusion"):
            model.load_from_gltf(str(DATA / name))


@pytest.mark.skipif(not hasattr(pyedge, "convert_gltf_to_edge"), reason="pyedge is built without edge's asset tools (EDGE_ASSET_TOOLS)")
def test_converted_occlusion_survives_the_file(renderer, tmp_path):
    pyedge.convert_gltf_to_edge(str(DATA / "test_occlusion.gltf"), str(tmp_path / "o.edge"))
    model = pyedge.StaticModelPTBNT(renderer)
    model.load_from_file(str(tmp_path / "o.edge"))
    found = materials(model)
    assert (found["orm"].occlusion, found["orm"].occlusion_strength) == (True, 0.5)
    assert not found["plain"].occlusion
    assert np.isclose(found["occlusion_only"].occlusion_strength, 0.25)
