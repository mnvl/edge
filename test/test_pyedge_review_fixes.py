# SPDX-License-Identifier: MIT
# Copyright (c) 2004-2026 Manvel Avetisian

"""The same mistake is the same error on every backend; what pyedge needs of pyrove and the system it says at import."""
import os
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

import pyedge
import pyrove

BACKENDS = ["null", "auto"]


@pytest.mark.parametrize("backend", BACKENDS)
@pytest.mark.parametrize("width,height", [(0, 0), (-1, 5), (5, 0)])
def test_a_renderer_of_no_size_is_refused(backend, width, height):
    with pytest.raises(ValueError):
        pyedge.create_renderer(width, height, backend=backend)


@pytest.mark.parametrize("backend", BACKENDS)
def test_resizing_to_no_size_is_refused(backend):
    renderer = pyedge.create_renderer(8, 8, backend=backend)
    with pytest.raises(ValueError):
        renderer.resize(0, 0)


@pytest.mark.parametrize("backend", BACKENDS)
def test_a_render_target_past_what_a_gpu_has_is_refused(backend):
    renderer = pyedge.create_renderer(8, 8, backend=backend)
    with pytest.raises(ValueError):
        renderer.create_render_target(100000, 100000)


def test_frames_in_flight_on_the_null_backend_is_the_same_error():
    with pytest.raises(ValueError):
        pyedge.create_renderer(8, 8, backend="null", frames_in_flight=5)


def test_a_texture_of_no_size_is_refused():
    renderer = pyedge.create_renderer(8, 8, backend="null")
    with pytest.raises(ValueError):
        renderer.create_texture(0, 0)


@pytest.mark.parametrize("value", ["-5", "", "0", "abc"])
def test_a_capacity_that_is_not_a_number_is_refused(monkeypatch, value):
    monkeypatch.setenv("EDGE_VK_JOINT_MATRICES", value)
    with pytest.raises(ValueError, match="EDGE_VK_JOINT_MATRICES"):
        pyedge.create_renderer(8, 8, backend="vulkan")


def test_a_download_inside_a_frame_is_refused_on_the_null_backend_too():
    renderer = pyedge.create_renderer(8, 8, backend="null")
    target = renderer.create_render_target(8, 8)
    texture = target.color_texture
    renderer.begin_frame()
    with pytest.raises(RuntimeError):
        texture.download()
    renderer.finish_frame()


# --- what pyedge needs ------------------------------------------------------------------------------------------------------------

def test_pyedge_and_pyrove_pass_types_to_each_other_here():
    pyedge._bindings._takes_a_pyrove_vector(pyrove.vec3(1, 2, 3))


def test_a_pyrove_it_cannot_talk_to_is_an_import_error_that_says_so():
    # (what a wheel of another compiler looks like from here: its vector is not this binding's)
    code = (
        "import pyedge, pyrove\n"
        "class Fake: vec3 = staticmethod(lambda *a: object())\n"
        "try:\n"
        "    pyedge._check_pyrove_works(Fake, pyedge._bindings)\n"
        "except ImportError as e:\n"
        "    assert 'no-binary' in str(e), str(e)\n"
        "    print('refused')\n"
    )
    result = subprocess.run([sys.executable, "-c", code], capture_output=True, text=True)
    assert result.stdout.strip() == "refused", result.stderr


def test_hasattr_answers_when_the_binding_cannot_load():
    code = (
        "import pyedge\n"
        "def broken(): raise ImportError('libvulkan.so.1: cannot open shared object file')\n"
        "pyedge._loaded_binding = broken\n"
        "assert not hasattr(pyedge, 'create_renderer')\n"
        "try:\n"
        "    pyedge.create_renderer\n"
        "except AttributeError as e:\n"
        "    assert 'libvulkan' in str(e), str(e)\n"
        "    print('said')\n"
    )
    result = subprocess.run([sys.executable, "-c", code], capture_output=True, text=True)
    assert result.stdout.strip() == "said", result.stderr


# --- the module, as a wheel has it ------------------------------------------------------------------------------------------------

@pytest.mark.skipif(sys.platform != "linux" or shutil.which("nm") is None, reason="reads an ELF file's symbols")
def test_the_module_exports_its_entry_and_none_of_what_it_links():
    module = Path(pyedge._bindings.__file__)
    symbols = subprocess.run(["nm", "-D", "--defined-only", str(module)], capture_output=True, text=True, check=True).stdout
    for bundled in ("stbi_", "meshopt_", "astcenc_", "xatlas", "spdlog", "simdjson", "fastgltf"):
        assert bundled not in symbols, f"{bundled} is exported"
    assert "PyInit__pyedge" in symbols


def test_the_summary_names_only_what_the_wheel_has():
    pyproject = (Path(__file__).resolve().parents[1] / "pyproject.toml").read_text(encoding="utf-8")
    summary = next(line for line in pyproject.splitlines() if line.startswith("description"))
    assert "OpenGL" not in summary and "Metal" not in summary
    assert "Vulkan" in summary


def test_the_readme_says_what_it_needs_and_has_no_relative_links():
    import re
    readme = (Path(__file__).resolve().parents[1] / "README.md").read_text(encoding="utf-8")
    assert "libvulkan" in readme
    relative = [link for link in re.findall(r"\]\(([^)]+)\)", readme) if not re.match(r"https?://|#", link)]
    assert relative == []


def test_the_licence_notices_hold_the_text_of_what_is_linked():
    notices = (Path(__file__).resolve().parents[1] / "THIRD_PARTY_NOTICES.md").read_text(encoding="utf-8")
    for project in ("fastgltf", "simdjson", "cereal", "stb", "spdlog", "fmt", "meshoptimizer", "bc7enc_rdo", "astc-encoder", "xatlas", "nanobind"):
        assert f"## {project}" in notices, project
    assert "Permission is hereby granted" in notices
    assert "Apache License" in notices
