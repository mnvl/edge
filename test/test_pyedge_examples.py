# SPDX-License-Identifier: MIT
# Copyright (c) 2004-2026 Manvel Avetisian

"""The examples (examples/) run as they are documented to: briefly, on the GPU, and exit 0 -- a binding they use that changed fails here."""
from __future__ import annotations

import os
import subprocess
import sys
from pathlib import Path

EDGE = Path(__file__).resolve().parents[1]


def run(*args: str) -> None:
    result = subprocess.run([sys.executable, *args], cwd=EDGE, env=dict(os.environ), capture_output=True, text=True, timeout=300)
    assert result.returncode == 0, f"{args[0]} exited {result.returncode}:\n{result.stdout}\n{result.stderr}"


def test_pyedge_hello_world_draws_its_animation():
    run("examples/hello_world.py", "--seconds", "0.2", "--backend", "vulkan")


def test_pyedge_render_targets_draws_into_its_targets():
    run("examples/render_targets.py", "--seconds", "0.2", "--backend", "vulkan")


def test_pyedge_benchmark_load_loads_a_glb():
    run("examples/benchmark_load.py", str(EDGE / "test" / "data" / "test_figure.glb"), "--backend", "vulkan")
