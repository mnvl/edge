# SPDX-License-Identifier: MIT
# Copyright (c) 2004-2026 Manvel Avetisian

"""Levels of detail through the bindings: convert_gltf_to_edge makes them, a loaded model says how many it has and how far each strays
from level 0, and draw() takes a level."""
from pathlib import Path

import pytest

import pyedge

needs_convert = pytest.mark.skipif(not hasattr(pyedge, "convert_gltf_to_edge"),
                                   reason="pyedge is built without edge's asset tools (EDGE_ASSET_TOOLS)")

TEST_DIR = Path(__file__).resolve().parent / "data"
SKINNED = TEST_DIR / "test_figure.glb"   # 53,632 triangles, one skin
STATIC = TEST_DIR / "cylinder_30_degrees.glb"  # 128 triangles: under the default minimum for levels


def _renderer():
    renderer = pyedge.Renderer(backend="null")
    return renderer, renderer.create_scheduler()


@needs_convert
def test_convert_reports_the_triangles_of_every_level(tmp_path):
    levels = pyedge.convert_gltf_to_edge(str(SKINNED), str(tmp_path / "m.edge"), lods=[0.5, 0.2])
    assert len(levels) == 3
    assert levels[0] == 53632
    assert levels[0] > levels[1] > levels[2] > 0
    assert levels[1] == pytest.approx(0.5 * levels[0], rel=0.1)


@needs_convert
def test_a_model_knows_its_levels_and_their_error(tmp_path):
    edge_file = tmp_path / "m.edge"
    pyedge.convert_gltf_to_edge(str(SKINNED), str(edge_file), lods=[0.5, 0.2, 0.06])
    renderer, scheduler = _renderer()
    model = pyedge.AnimatedModelPTBNJWT(renderer, scheduler)
    model.load_from_file(str(edge_file))

    assert model.lod_count == 4
    assert model.lod_error(0) == 0.0
    errors = [model.lod_error(level) for level in range(4)]
    assert errors == sorted(errors)
    assert errors[1] > 0.0
    assert errors[3] > errors[1]


@needs_convert
def test_a_model_made_without_levels_has_one(tmp_path):
    edge_file = tmp_path / "m.edge"
    pyedge.convert_gltf_to_edge(str(STATIC), str(edge_file))
    renderer, scheduler = _renderer()
    model = pyedge.StaticModelPTBNT(renderer, scheduler)
    model.load_from_file(str(edge_file))
    assert model.lod_count == 1
    assert model.lod_error(0) == 0.0


def test_a_gltf_loaded_directly_has_one_level():
    renderer, scheduler = _renderer()
    model = pyedge.AnimatedModelPTBNJWT(renderer, scheduler)
    model.load_from_gltf(str(SKINNED))
    assert model.lod_count == 1


@needs_convert
def test_a_missing_gltf_is_an_error(tmp_path):
    with pytest.raises(Exception, match="missing.glb"):
        pyedge.convert_gltf_to_edge(str(tmp_path / "missing.glb"), str(tmp_path / "m.edge"))


@needs_convert
def test_a_ratio_outside_zero_to_one_is_refused(tmp_path):
    with pytest.raises(Exception):
        pyedge.convert_gltf_to_edge(str(SKINNED), str(tmp_path / "m.edge"), lods=[1.5])


# ---- the level of detail by pixel error (pyedge.lod_level, pyedge.pixel_pitch: src/lod_choice.h)


def test_a_pixel_spans_what_the_camera_makes_of_it():
    import math

    camera = pyedge.Camera()
    camera.set_perspective_projection(1.0, 16 / 9, 0.1, 100.0)
    assert pyedge.pixel_pitch(camera.projection_matrix, 720) == pytest.approx(2 * math.tan(0.5) / 720, rel=1e-5)
    camera.set_orthographic_projection(20.0, 10.0, 0.1, 100.0)
    assert pyedge.pixel_pitch(camera.projection_matrix, 500) == pytest.approx(0.02, rel=1e-5)
    with pytest.raises(ValueError):
        pyedge.pixel_pitch(camera.projection_matrix, 0)


def test_the_coarsest_level_within_the_tolerance_is_chosen():
    errors = (0.0, 0.25, 1.0, 4.0)
    assert [pyedge.lod_level(errors, span, 1.0) for span in (0.0, 0.125, 0.25, 0.5, 1.0, 1000.0)] == [0, 0, 1, 1, 2, 3]
    assert pyedge.lod_level(errors, 0.5, 2.0) == 2
    assert pyedge.lod_level(errors, 1000.0, 0.0) == 0                  # zero tolerance: level 0 everywhere
    assert pyedge.lod_level([0.0], 1000.0, 1.0) == 0


@pytest.mark.parametrize("errors, span, tolerance", [
    ((), 1.0, 1.0), ((0.5, 1.0), 1.0, 1.0), ((0.0, 1.0, 0.5), 1.0, 1.0), ((0.0, float("nan")), 1.0, 1.0),
    ((0.0, 1.0), -1.0, 1.0), ((0.0, 1.0), float("inf"), 1.0), ((0.0, 1.0), 1.0, -1.0), ((0.0, 1.0), 1.0, float("nan")),
])
def test_what_is_not_a_ladder_or_not_a_span_is_a_value_error(errors, span, tolerance):
    with pytest.raises(ValueError):
        pyedge.lod_level(errors, span, tolerance)


@needs_convert
def test_a_models_levels_scaled_as_placed_are_chosen_where_each_error_fits(tmp_path):
    edge_file = tmp_path / "m.edge"
    pyedge.convert_gltf_to_edge(str(SKINNED), str(edge_file), lods=[0.5, 0.2, 0.06])
    renderer, scheduler = _renderer()
    model = pyedge.AnimatedModelPTBNJWT(renderer, scheduler)
    model.load_from_file(str(edge_file))
    errors = [model.lod_error(level) for level in range(model.lod_count)]
    for scale in (1.0, 3.0):
        scaled = [e * scale for e in errors]
        for level, error in enumerate(scaled):
            assert pyedge.lod_level(scaled, error, 1.0) == level
            assert pyedge.lod_level(scaled, error * 0.999, 1.0) == max(level - 1, 0) or scaled[level - 1] == scaled[level]
