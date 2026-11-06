# SPDX-License-Identifier: MIT
# Copyright (c) 2004-2026 Manvel Avetisian

"""Contract: edge's own SPIR-V (vertex.spv, fragment.spv, ...) is read from
$EDGE_VK_SHADER_DIR when it is set, else from shaders/edge beside the module (a
wheel's package), ../share/edge/shaders beside it (an install), else the
directory the build put it in; the view shaders likewise
($EDGE_VIEW_SHADER_DIR, view_shaders beside the module).

``vulkan_shader_dir()`` says which directory that is; a draw proves the
renderer reads from it, and from nowhere else.
"""
import os
import shutil
import struct
import subprocess
import sys
from pathlib import Path

import pytest

import pyedge
import pyrove


assert "vulkan" in pyedge.available_renderer_backends(), (
    f"pyedge built without the Vulkan backend: {pyedge.available_renderer_backends()}"
)


@pytest.fixture
def built_dir(monkeypatch):
    monkeypatch.delenv("EDGE_VK_SHADER_DIR", raising=False)
    return Path(pyedge.vulkan_shader_dir())


def _draw_a_triangle():
    """One triangle with a default material (edge's vertex.spv and fragment.spv)
    on a fresh renderer, so no pipeline is cached from before."""
    renderer = pyedge.Renderer(32, 32, backend="vulkan")
    camera = pyedge.Camera()
    view = pyrove.mat4()
    view.identity()
    camera.view_matrix = view
    world = pyrove.mat4()
    world.identity()

    vertex_buffer = renderer.create_vertex_buffer(
        pyedge.VERTEX_FORMAT_PT, pyedge.vertex_stride(pyedge.VERTEX_FORMAT_PT) * 3)
    vertex_buffer.upload(struct.pack("<15f", -1.0, -1.0, 0.5, 0.0, 0.0,
                                     1.0, -1.0, 0.5, 1.0, 0.0,
                                     0.0, 1.0, 0.5, 0.5, 1.0))
    index_buffer = renderer.create_index_buffer(6)
    index_buffer.upload(struct.pack("<3H", 0, 1, 2))

    draw_call = renderer.create_draw_call()
    draw_call.set_always_visible(True)
    draw_call.set_world_matrix(world)
    draw_call.camera = camera
    draw_call.material = renderer.create_material()
    draw_call.vertex_buffer = vertex_buffer
    draw_call.index_buffer = index_buffer
    draw_call.triangles_count = 1
    draw_call.commit()

    scheduler = renderer.create_scheduler()
    scheduler.add(draw_call)
    renderer.begin_frame()
    renderer.begin_pass(renderer.default_render_target)
    scheduler.flush()
    renderer.end_pass()
    renderer.finish_frame()


def test_unset_it_is_the_build_s_directory_and_holds_the_shaders(built_dir):
    assert built_dir.is_absolute(), built_dir
    for name in ("vertex.spv", "fragment.spv"):
        assert (built_dir / name).is_file(), built_dir / name


def test_set_it_is_the_named_directory(monkeypatch, tmp_path):
    monkeypatch.setenv("EDGE_VK_SHADER_DIR", str(tmp_path))
    assert pyedge.vulkan_shader_dir() == str(tmp_path)


def test_set_but_empty_is_refused(monkeypatch):
    """Not taken for 'unset': an empty variable is a mistake, and the build's
    directory would hide it on the machine that has one."""
    monkeypatch.setenv("EDGE_VK_SHADER_DIR", "")
    with pytest.raises(RuntimeError, match="EDGE_VK_SHADER_DIR"):
        pyedge.vulkan_shader_dir()


def test_a_draw_reads_the_shaders_from_the_named_directory(built_dir, monkeypatch, tmp_path):
    moved = tmp_path / "shaders"
    shutil.copytree(built_dir, moved)
    monkeypatch.setenv("EDGE_VK_SHADER_DIR", str(moved))
    _draw_a_triangle()


def test_a_draw_does_not_fall_back_to_the_build_s_directory(built_dir, monkeypatch, tmp_path):
    monkeypatch.setenv("EDGE_VK_SHADER_DIR", str(tmp_path))
    with pytest.raises(RuntimeError, match=str(tmp_path / "vertex.spv")):
        _draw_a_triangle()


def test_a_draw_with_it_set_but_empty_is_refused(built_dir, monkeypatch):
    monkeypatch.setenv("EDGE_VK_SHADER_DIR", "")
    with pytest.raises(RuntimeError, match="EDGE_VK_SHADER_DIR"):
        _draw_a_triangle()


def test_unset_the_view_shaders_are_the_build_s_and_set_the_named_directory(monkeypatch, tmp_path):
    monkeypatch.delenv("EDGE_VIEW_SHADER_DIR", raising=False)
    assert (Path(pyedge.view_shader_dir()) / "lit.frag.spv").is_file()
    monkeypatch.setenv("EDGE_VIEW_SHADER_DIR", str(tmp_path))
    assert pyedge.view_shader_dir() == str(tmp_path)


def test_a_module_with_its_shaders_beside_it_reads_those(built_dir, monkeypatch, tmp_path):
    """The layout of a wheel's package: the module, shaders/edge and view_shaders beside it.  A copy of the module there finds them
    (from where the module is, not from the build), and draws with them."""
    monkeypatch.delenv("EDGE_VIEW_SHADER_DIR", raising=False)
    package = tmp_path / "package"
    package.mkdir()
    module = Path(pyedge._bindings.__file__)
    shutil.copy2(module, package / module.name)
    shutil.copytree(built_dir, package / "shaders" / "edge")
    shutil.copytree(Path(pyedge.view_shader_dir()), package / "view_shaders")
    script = (
        "import sys\n"
        f"sys.path.insert(0, {str(Path(__file__).parent)!r})\n"
        "import pyedge\n"
        f"assert pyedge._bindings.__file__ == {str(package / module.name)!r}, pyedge._bindings.__file__\n"
        "print(pyedge.vulkan_shader_dir())\n"
        "print(pyedge.view_shader_dir())\n"
        "from test_pyedge_shader_dir import _draw_a_triangle\n"
        "_draw_a_triangle()\n"
    )
    env = {k: v for k, v in os.environ.items() if k not in ("EDGE_VK_SHADER_DIR", "EDGE_VIEW_SHADER_DIR")}
    env.update(EDGE_PYTHON_BINDINGS_DIR=str(package), ROVE_PYTHON_BINDINGS_DIR=str(Path(pyrove.pyrove_bind.__file__).parent))
    result = subprocess.run([sys.executable, "-c", script], cwd=tmp_path, env=env, capture_output=True, text=True)
    assert result.returncode == 0, result.stderr
    assert result.stdout.splitlines() == [str(package / "shaders" / "edge"), str(package / "view_shaders")]
    # without them it is the build's again (the one the module was built in, an installed module's too): not the copy's directory
    shutil.rmtree(package / "shaders")
    result = subprocess.run([sys.executable, "-c", script], cwd=tmp_path, env=env, capture_output=True, text=True)
    assert result.returncode == 0, result.stderr
    fallback = Path(result.stdout.splitlines()[0])
    assert fallback != package / "shaders" / "edge" and (fallback / "vertex.spv").is_file(), fallback
