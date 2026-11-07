# SPDX-License-Identifier: MIT
# Copyright (c) 2004-2026 Manvel Avetisian

"""create_renderer(joint_matrices=n): how many joint matrices a Vulkan renderer's frames have room for.

A skinned draw takes as many as its skeleton has joints, 64 bytes each.  The default, 1024 draws of 256 joints, is 16 MB; a process
that draws more asks for more, and one past what it asked for in a frame is an error, never a draw with another's joints.
"""
from pathlib import Path

import pytest

import pyedge
import pyrove

ASSET = Path(__file__).resolve().parent / "data" / "test_gltf_loder_animation.glb"


def _identity():
    m = pyrove.mat4()
    m.identity()
    return m


def _draw_twice_in_a_frame(renderer):
    """The skinned model drawn twice in one frame: at least two skinned draws, two joint matrices."""
    scheduler = renderer.create_scheduler()
    model = pyedge.AnimatedModelPTBNJWT(renderer, scheduler)
    model.load_from_gltf(str(ASSET))
    camera = pyedge.Camera()
    camera.set_orthographic_projection(10.0, 10.0, 0.1, 100.0)
    camera.look_at(pyrove.vec3(0.0, 0.0, 20.0), pyrove.vec3(0.0, 0.0, 0.0), pyrove.vec3(0.0, 1.0, 0.0))
    target = renderer.create_render_target(64, 64)
    renderer.begin_frame()
    model.draw(camera, _identity())
    model.draw(camera, _identity())
    renderer.begin_pass(target, pyedge.RenderPassClearState())
    scheduler.flush()
    renderer.end_pass()
    renderer.finish_frame()


@pytest.fixture(autouse=True)
def _no_capacity_in_the_environment(monkeypatch):
    monkeypatch.delenv("EDGE_VK_JOINT_MATRICES", raising=False)
    monkeypatch.delenv("EDGE_VK_JOINT_MATRIX_BLOCKS", raising=False)


def test_too_few_for_the_frame_are_refused_by_name():
    renderer = pyedge.create_renderer(64, 64, backend="vulkan", joint_matrices=1)
    with pytest.raises(RuntimeError, match="joint_matrices"):
        _draw_twice_in_a_frame(renderer)


def test_enough_draw_the_frame():
    _draw_twice_in_a_frame(pyedge.create_renderer(64, 64, backend="vulkan", joint_matrices=4096))


def test_the_class_takes_it_as_create_renderer_does():
    renderer = pyedge.Renderer(64, 64, backend="vulkan", joint_matrices=1)
    with pytest.raises(RuntimeError, match="joint_matrices"):
        _draw_twice_in_a_frame(renderer)


def test_the_environment_overrides_it(monkeypatch):
    monkeypatch.setenv("EDGE_VK_JOINT_MATRICES", "4096")
    _draw_twice_in_a_frame(pyedge.create_renderer(64, 64, backend="vulkan", joint_matrices=1))


@pytest.mark.parametrize("matrices", [0, -1])
def test_none_is_refused(matrices):
    with pytest.raises(ValueError, match="joint_matrices"):
        pyedge.create_renderer(64, 64, backend="vulkan", joint_matrices=matrices)


def test_the_null_renderer_has_none_to_size():
    with pytest.raises(ValueError, match="joint_matrices"):
        pyedge.create_renderer(64, 64, backend="null", joint_matrices=64)
