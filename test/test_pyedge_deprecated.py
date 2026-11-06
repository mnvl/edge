# SPDX-License-Identifier: MIT
# Copyright (c) 2004-2026 Manvel Avetisian

"""The names pyedge had before 2026-11 that are gone (get_x, and a set_x that is a verb now) and the arguments that do nothing now
(writeonly) still work, and each says what it is now (DeprecationWarning); set_x(v) and x = v both stay."""

import pytest

pyedge = pytest.importorskip("pyedge")
pyrove = pytest.importorskip("pyrove")


def _renderer():
    return pyedge.create_renderer(backend="null")


def test_a_getter_that_is_a_property_now_warns_and_answers_as_the_property():
    camera = pyedge.Camera()
    with pytest.deprecated_call(match=r"Camera\.get_eye_point\(\) is Camera\.eye_point"):
        eye = camera.get_eye_point()
    assert (eye.x, eye.y, eye.z) == (camera.eye_point.x, camera.eye_point.y, camera.eye_point.z)


def test_a_setter_is_set_x_and_the_property_both_without_a_warning():
    renderer = _renderer()
    material = renderer.create_material()
    import warnings
    with warnings.catch_warnings():
        warnings.simplefilter("error", DeprecationWarning)
        material.set_cull_mode(pyedge.CullMode.NONE)
        assert material.cull_mode == pyedge.CullMode.NONE
        material.cull_mode = pyedge.CullMode.CW
        assert material.cull_mode == pyedge.CullMode.CW


def test_a_setter_that_is_a_verb_now_warns_and_does_what_the_verb_does():
    camera = pyedge.Camera()
    eye, at, up = pyrove.vec3(0, 0, -5), pyrove.vec3(0, 0, 0), pyrove.vec3(0, 1, 0)
    with pytest.deprecated_call(match=r"Camera\.set_eye_lookat_up\(\) is Camera\.look_at\(\)"):
        camera.set_eye_lookat_up(eye, at, up)
    old = camera.view_matrix.to_numpy()
    camera.look_at(eye, at, up)
    assert (camera.view_matrix.to_numpy() == old).all()


def test_a_warning_made_an_error_stops_the_call():
    camera = pyedge.Camera()
    import warnings
    with warnings.catch_warnings():
        warnings.simplefilter("error", DeprecationWarning)
        with pytest.raises(DeprecationWarning):
            camera.get_view_matrix()


def test_flags_that_do_nothing_warn_and_are_ignored():
    renderer = _renderer()
    with pytest.deprecated_call(match=r"create_index_buffer: writeonly does nothing"):
        indices = renderer.create_index_buffer(12, writeonly=True)
    assert indices.size == 12
    with pytest.deprecated_call(match=r"create_texture: dynamic and writeonly do nothing"):
        texture = renderer.create_texture(4, 4, dynamic=True)
    assert texture.width == 4


def test_a_dynamic_buffer_is_made_without_a_warning():
    renderer = _renderer()
    import warnings
    with warnings.catch_warnings():
        warnings.simplefilter("error", DeprecationWarning)
        assert renderer.create_index_buffer(12, dynamic=True).size == 12
        assert renderer.create_texture(4, 4).width == 4
