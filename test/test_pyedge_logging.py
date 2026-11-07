# SPDX-License-Identifier: MIT
# Copyright (c) 2004-2026 Manvel Avetisian

"""pyedge logs through edge's one native log (edge/src/log.h): its lines go to stderr, warnings and errors by default;
set_log_level() lets more or fewer through. Nothing is written to disk."""
from __future__ import annotations

from pathlib import Path

import base64
import importlib
import json
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


def test_set_log_level_refuses_an_unknown_level():
    for level in ("loud", "", "off", "INFO", "warnings"):
        with pytest.raises(ValueError):
            pyedge.set_log_level(level)


def _run_load(asset: Path, level: str | None = None, before: str = "", cwd: Path | None = None) -> subprocess.CompletedProcess:
    """A model loaded and checked in a process of its own, at `level` (the default when None): its stderr is the log's."""
    script = (
        "import pyedge\n"
        + (f"pyedge.set_log_level({level!r})\n" if level is not None else "")
        + before
        + 'r = pyedge.Renderer(backend="null")\n'
        "s = r.create_scheduler()\n"
        "m = pyedge.StaticModelPTBNT(r, s)\n"
        f"m.load_from_gltf({str(asset)!r})\n"
        "m.check()\n"
    )
    return subprocess.run([sys.executable, "-c", script], cwd=cwd or tempfile.gettempdir(), capture_output=True, text=True,
                          check=True)


def test_by_default_a_warning_reaches_stderr_and_an_info_line_does_not(tmp_path):
    asset, token = _untextured_gltf(tmp_path)
    result = _run_load(asset)
    assert f"mesh '{token}': vertex components not initialized" in result.stderr, result.stderr
    assert "[warning " in result.stderr, result.stderr
    assert "glTF primitive(s) from" not in result.stderr, result.stderr
    assert "[info " not in result.stderr, result.stderr


def test_at_info_the_load_is_on_stderr_with_its_source(tmp_path):
    asset = _unique_asset(tmp_path)
    result = _run_load(asset, "info")
    assert f"glTF primitive(s) from {asset}" in result.stderr, result.stderr
    assert "[info gltf_reader.cc:" in result.stderr, result.stderr
    assert "vertices: count = " not in result.stderr, result.stderr  # debug, off at info


def test_debug_adds_the_mesh_lines(tmp_path):
    result = _run_load(_unique_asset(tmp_path), "debug")
    assert "vertices: count = " in result.stderr, result.stderr
    assert "bounds = (" in result.stderr, result.stderr
    assert "[debug " in result.stderr, result.stderr


def test_at_error_a_warning_stays_off_stderr(tmp_path):
    asset, token = _untextured_gltf(tmp_path)
    result = _run_load(asset, "error")
    assert token not in result.stderr, result.stderr


def test_a_refused_level_leaves_the_level_as_it_was(tmp_path):
    asset = _unique_asset(tmp_path)
    refused = (
        "try:\n"
        "    pyedge.set_log_level('loud')\n"
        "except ValueError:\n"
        "    pass\n"
        "else:\n"
        "    raise SystemExit('accepted')\n"
    )
    result = _run_load(asset, "info", before=refused)
    assert f"glTF primitive(s) from {asset}" in result.stderr, result.stderr


def _files_under(*roots: Path) -> dict[Path, tuple[int, int]]:
    """Every file below the roots with its size and mtime: a new file or an appended one shows."""
    return {p: (p.stat().st_size, p.stat().st_mtime_ns) for root in roots if root.exists()
            for p in root.rglob("*") if p.is_file()}


def test_nothing_is_written_to_disk(tmp_path):
    asset, token = _untextured_gltf(tmp_path)
    workdir = tmp_path / "cwd"
    workdir.mkdir()
    edge_dir = Path(__file__).resolve().parents[1]
    roots = (workdir, edge_dir / "pyedge", edge_dir / "logs")
    before = _files_under(*roots)
    result = _run_load(asset, "debug", cwd=workdir)
    written = {p for p, stamp in _files_under(*roots).items() if before.get(p) != stamp}
    assert written == set(), written
    assert token in result.stderr, result.stderr
