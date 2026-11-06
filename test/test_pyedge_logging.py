# SPDX-License-Identifier: MIT
# Copyright (c) 2004-2026 Manvel Avetisian

"""pyedge logs through one native logger (edge/src/log.h). Loading pyedge's
binding (the first name of it a program uses) points it at $EDGE_LOG; without
it nothing is written to disk and warnings and errors go to stderr.
init_logging() moves it."""

from pathlib import Path

import base64
import importlib
import json
import os
import struct
import shutil
import subprocess
import sys
import tempfile
import uuid

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

_ASSET = Path(__file__).resolve().parent / "data" / "test_gltf_loder_plane.glb"


def _unique_asset(directory: Path) -> Path:
    # The load line names the file, so a uniquely named copy is a token
    # this test alone put in the log.
    assert _ASSET.exists(), _ASSET
    copy = directory / f"plane_{uuid.uuid4().hex}.glb"
    shutil.copy(_ASSET, copy)
    return copy


def _untextured_gltf(directory: Path) -> tuple[Path, str]:
    """One triangle without TEXCOORD_0 in a uniquely named file: loaded as a PTBNT model, check() warns that the texture
    coordinates were never given, naming the file."""
    token = f"untextured_{uuid.uuid4().hex}"
    positions = struct.pack("<9f", 0, 0, 0, 1, 0, 0, 0, 1, 0)
    normals = struct.pack("<9f", 0, 0, 1, 0, 0, 1, 0, 0, 1)
    buffer = positions + normals
    gltf = {
        "asset": {"version": "2.0"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"mesh": 0, "name": token}],
        "meshes": [{"name": token, "primitives": [{"attributes": {"POSITION": 0, "NORMAL": 1}}]}],
        "buffers": [{"byteLength": len(buffer),
                     "uri": "data:application/octet-stream;base64," + base64.b64encode(buffer).decode()}],
        "bufferViews": [{"buffer": 0, "byteOffset": 0, "byteLength": 36},
                        {"buffer": 0, "byteOffset": 36, "byteLength": 36}],
        "accessors": [{"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3",
                       "min": [0, 0, 0], "max": [1, 1, 0]},
                      {"bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC3"}],
    }
    path = directory / "triangle.gltf"
    path.write_text(json.dumps(gltf))
    return path, token


def _load(asset: Path) -> None:
    renderer = pyedge.Renderer(backend="null")
    scheduler = renderer.create_scheduler()
    model = pyedge.StaticModelPTBNT(renderer, scheduler)
    model.load_from_gltf(str(asset))
    model.check()


@pytest.fixture
def detached_after():
    yield
    pyedge.init_logging(None)


def test_init_logging_writes_a_model_load_to_the_file(tmp_path, detached_after):
    log_file = tmp_path / "sub" / "edge.log"  # the directory does not exist yet
    pyedge.init_logging(str(log_file), level="info")
    asset = _unique_asset(tmp_path)
    _load(asset)
    text = log_file.read_text()
    assert f"glTF primitive(s) from {asset}" in text
    assert "gltf_reader.cc:" in text
    assert " info]" in text
    assert "vertices: count = " not in text  # debug, off at info


def test_debug_level_adds_the_mesh_lines(tmp_path, detached_after):
    log_file = tmp_path / "edge.log"
    pyedge.init_logging(str(log_file), level="debug")
    _load(_unique_asset(tmp_path))
    pyedge.init_logging(str(log_file), level="info")  # re-init flushes what the debug logger had
    text = log_file.read_text()
    assert "vertices: count = " in text
    assert "bounds = (" in text
    assert " debug]" in text


def test_init_logging_refuses_an_unknown_level(tmp_path):
    for level in ("loud", "", "off", "INFO"):
        with pytest.raises(ValueError):
            pyedge.init_logging(str(tmp_path / "edge.log"), level=level)
    assert not (tmp_path / "edge.log").exists()


def test_init_logging_refuses_a_file_it_cannot_create(tmp_path):
    (tmp_path / "not_a_dir").write_text("x")
    with pytest.raises(RuntimeError, match="not_a_dir"):
        pyedge.init_logging(str(tmp_path / "not_a_dir" / "edge.log"))


def test_a_check_warning_is_in_the_file(tmp_path, detached_after):
    log_file = tmp_path / "edge.log"
    pyedge.init_logging(str(log_file))
    asset, token = _untextured_gltf(tmp_path)
    _load(asset)
    text = log_file.read_text()
    assert f"mesh '{token}': vertex components not initialized" in text
    assert " warning]" in text


def test_init_logging_none_stops_writing_to_the_previous_file(tmp_path):
    log_file = tmp_path / "edge.log"
    pyedge.init_logging(str(log_file))
    pyedge.init_logging(None)
    asset = _unique_asset(tmp_path)
    _load(asset)
    assert asset.name not in log_file.read_text()


def _run_load(asset: Path, env: dict, check: bool = True, cwd: Path | None = None) -> subprocess.CompletedProcess:
    script = (
        "import pyedge\n"
        'r = pyedge.Renderer(backend="null")\n'
        "s = r.create_scheduler()\n"
        "m = pyedge.StaticModelPTBNT(r, s)\n"
        f"m.load_from_gltf({str(asset)!r})\n"
        "m.check()\n"
    )
    inherited = {k: v for k, v in os.environ.items() if k != "EDGE_LOG"}
    return subprocess.run([sys.executable, "-c", script], cwd=cwd or tempfile.gettempdir(), capture_output=True,
                          text=True, env={**inherited, **env}, check=check)


def _files_under(*roots: Path) -> dict[Path, tuple[int, int]]:
    """Every file below the roots with its size and mtime: a new file or an appended one shows."""
    return {p: (p.stat().st_size, p.stat().st_mtime_ns) for root in roots if root.exists()
            for p in root.rglob("*") if p.is_file()}


def test_without_edge_log_nothing_is_written(tmp_path):
    asset, token = _untextured_gltf(tmp_path)
    workdir = tmp_path / "cwd"
    workdir.mkdir()
    edge_dir = Path(__file__).resolve().parents[1]
    roots = (workdir, edge_dir / "pyedge", edge_dir / "logs")
    before = _files_under(*roots)
    result = _run_load(asset, {}, cwd=workdir)
    written = {p for p, stamp in _files_under(*roots).items() if before.get(p) != stamp}
    assert written == set(), written
    assert token in result.stderr, result.stderr  # the warning still reaches stderr
    assert " info]" not in result.stderr, result.stderr  # the info lines do not


def test_edge_log_is_where_the_log_goes(tmp_path):
    asset = _unique_asset(tmp_path)
    log_file = tmp_path / "elsewhere" / "custom.log"  # the directory does not exist yet
    _run_load(asset, {"EDGE_LOG": str(log_file)})
    assert f"glTF primitive(s) from {asset}" in log_file.read_text()


def test_an_edge_log_that_cannot_be_created_fails_the_first_use_of_the_binding(tmp_path):
    (tmp_path / "not_a_dir").write_text("x")
    result = _run_load(_unique_asset(tmp_path), {"EDGE_LOG": str(tmp_path / "not_a_dir" / "edge.log")},
                       check=False)
    assert result.returncode != 0
    assert "not_a_dir" in result.stderr, result.stderr


def test_an_empty_edge_log_fails_the_import(tmp_path):
    result = _run_load(_unique_asset(tmp_path), {"EDGE_LOG": ""}, check=False)
    assert result.returncode != 0
    assert "EDGE_LOG" in result.stderr, result.stderr


def test_info_lines_stay_off_stderr(tmp_path):
    asset = _unique_asset(tmp_path)
    result = _run_load(asset, {"EDGE_LOG": str(tmp_path / "edge.log")})
    assert asset.name not in result.stderr, result.stderr


def test_warnings_reach_stderr_as_well(tmp_path):
    asset, token = _untextured_gltf(tmp_path)
    log_file = tmp_path / "edge.log"
    result = _run_load(asset, {"EDGE_LOG": str(log_file)})
    assert f"mesh '{token}': vertex components not initialized" in result.stderr, result.stderr
    assert token in log_file.read_text()
