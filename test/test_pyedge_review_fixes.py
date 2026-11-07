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


def _an_installed_package(tmp_path):
    package = tmp_path / "installed" / "pyedge"
    package.mkdir(parents=True)
    shutil.copy(Path(pyedge.__file__), package / "__init__.py")
    shutil.copy(Path(pyedge._bindings.__file__), package)
    return package


def _which_binding(tmp_path, binding_dir):
    env = {**os.environ, "EDGE_PYTHON_BINDINGS_DIR": str(binding_dir), "ROVE_PYTHON_BINDINGS_DIR": str(Path(pyrove._bindings.__file__).parent),
           "PYTHONPATH": os.pathsep.join([str(tmp_path / "installed")] + os.environ.get("PYTHONPATH", "").split(os.pathsep))}
    code = "import pyedge; print(pyedge._bindings.__file__)"
    return subprocess.run([sys.executable, "-W", "always", "-c", code], capture_output=True, text=True, env=env, cwd=tmp_path)


def test_a_variable_that_names_nothing_leaves_an_installed_package_its_own_module(tmp_path):
    package = _an_installed_package(tmp_path)
    (tmp_path / "empty").mkdir()
    for nothing in (tmp_path / "empty", tmp_path / "missing"):
        result = _which_binding(tmp_path, nothing)
        assert result.returncode == 0, result.stderr
        assert Path(result.stdout.strip()).parent == package
        assert "EDGE_PYTHON_BINDINGS_DIR" in result.stderr


def test_a_variable_that_names_a_module_picks_it(tmp_path):
    _an_installed_package(tmp_path)
    elsewhere = tmp_path / "elsewhere"
    elsewhere.mkdir()
    module = Path(pyedge._bindings.__file__)
    shutil.copy(module, elsewhere)
    result = _which_binding(tmp_path, elsewhere)
    assert result.returncode == 0, result.stderr
    assert Path(result.stdout.strip()).parent == elsewhere


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


# --- the pyrove pyedge is released with -------------------------------------------------------------------------------------------

ROOT = Path(__file__).resolve().parents[1]


def test_pyedge_pins_exactly_the_pyrove_it_builds_against():
    import re
    pyproject = (ROOT / "pyproject.toml").read_text(encoding="utf-8")
    dependencies = re.search(r"^dependencies = \[(.*)\]$", pyproject, flags=re.M).group(1)
    pyrove_requirements = re.findall(r'"(pyrove[^"]*)"', dependencies)
    assert len(pyrove_requirements) == 1
    tag = re.search(r'^set\(EDGE_ROVE_GIT_TAG "v([^"]+)"', (ROOT / "CMakeLists.txt").read_text(encoding="utf-8"), flags=re.M).group(1)
    assert pyrove_requirements[0] == f"pyrove=={tag}"


@pytest.mark.parametrize("pin", ['"pyrove==9.9.9"', '"pyrove>=3,<4"', '"pyrove"'])
def test_the_pin_check_refuses_a_pin_that_is_not_the_tag(tmp_path, pin):
    shutil.copy(ROOT / "CMakeLists.txt", tmp_path)
    (tmp_path / ".github" / "workflows").mkdir(parents=True)
    for workflow in (ROOT / ".github" / "workflows").glob("*.yml"):
        shutil.copy(workflow, tmp_path / ".github" / "workflows")
    (tmp_path / "pyproject.toml").write_text(f"[project]\ndependencies = [{pin}, \"numpy\"]\n", encoding="utf-8")
    result = subprocess.run(["sh", str(ROOT / "test" / "check_rove_pin.sh"), str(tmp_path)], capture_output=True, text=True)
    assert result.returncode != 0
    assert "pyproject.toml" in result.stderr


def test_the_pin_check_passes_on_the_repository():
    result = subprocess.run(["sh", str(ROOT / "test" / "check_rove_pin.sh"), str(ROOT)], capture_output=True, text=True)
    assert result.returncode == 0, result.stderr


def test_every_place_that_says_the_version_says_the_same():
    import json
    import re
    in_cmake = re.search(r"PROJECT\(EDGE VERSION ([0-9.]+)", (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")).group(1)
    in_pyproject = re.search(r'^version = "(.*)"$', (ROOT / "pyproject.toml").read_text(encoding="utf-8"), flags=re.M).group(1)
    in_port = json.loads((ROOT / "ports" / "edge" / "vcpkg.json").read_text(encoding="utf-8"))["version"]
    in_changelog = re.search(r"^## \[([0-9.]+)\]", (ROOT / "CHANGELOG.md").read_text(encoding="utf-8"), flags=re.M).group(1)
    assert {in_pyproject, in_port, in_changelog, pyedge.__version__} == {in_cmake}


# --- help() -----------------------------------------------------------------------------------------------------------------------

def test_help_of_the_package_lists_every_function_and_overload():
    import pydoc
    text = pydoc.render_doc(pyedge, renderer=pydoc.plaintext)
    # (in the description, under its heading: pydoc's DATA section prints a function's repr, which is no signature to read)
    assert "Functions:" in pyedge.__doc__
    assert "    create_renderer(width: int = 800, height: int = 600, backend: str = 'auto'" in pyedge.__doc__
    assert "Functions:" in text
    for name in dir(pyedge):
        if hasattr(getattr(pyedge, name), "__nb_signature__"):
            assert f"    {name}(" in pyedge.__doc__, name
    assert "_pyedge." not in pyedge.__doc__
    assert "pyrove_bind." not in pyedge.__doc__
    assert "\\" not in pyedge.__doc__
    assert pyedge.__doc__.startswith("edge's Python bindings")


def test_importing_and_reading_the_description_need_no_binding(tmp_path):
    # (only help() and the like read __doc__, and where the binding cannot load they still get the description)
    package = tmp_path / "pyedge"
    package.mkdir()
    shutil.copy(Path(pyedge.__file__), package / "__init__.py")
    (tmp_path / "empty").mkdir()
    code = (
        "import sys, pyedge\n"
        "assert '_pyedge' not in sys.modules\n"
        "print(pyedge.__doc__.split(':')[0])\n"
    )
    env = {**os.environ, "PYTHONPATH": str(tmp_path), "EDGE_PYTHON_BINDINGS_DIR": str(tmp_path / "empty")}
    result = subprocess.run([sys.executable, "-c", code], capture_output=True, text=True, env=env, cwd=tmp_path)
    assert result.returncode == 0, result.stderr
    assert result.stdout.startswith("edge's Python bindings")
