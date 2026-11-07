# SPDX-License-Identifier: MIT
# Copyright (c) 2004-2026 Manvel Avetisian

"""Importing pyedge loads nothing native: the binding (_pyedge, and pyrove's with it) is loaded by the first name that needs it, so a
program that imports pyedge for what is plain Python in it (its logging, its paths) runs where no binding was built.  Each case runs in an
interpreter of its own, as an import is once a process."""

import os
import subprocess
import sys
import tempfile

import pytest


def _run(script: str, env: dict, check: bool = True) -> subprocess.CompletedProcess:
    inherited = {k: v for k, v in os.environ.items() if k != "EDGE_LOG"}
    return subprocess.run([sys.executable, "-c", script], cwd=tempfile.gettempdir(), capture_output=True, text=True,
                          env={**inherited, "PYTHONDONTWRITEBYTECODE": "1", **env}, check=check)


@pytest.fixture
def no_binding(tmp_path):
    """Where the binding is looked for: a directory that has none."""
    empty = tmp_path / "no_binding"
    empty.mkdir()
    return {"EDGE_PYTHON_BINDINGS_DIR": str(empty)}


def test_a_name_of_the_binding_where_none_was_built_says_how_to_build_it(no_binding):
    # (an AttributeError, as a module's __getattr__ must raise for hasattr() to answer, from the ImportError)
    script = (
        "import pyedge\n"
        "for attempt in range(2):\n"          # (a failed load leaves nothing half made: the second says the same)
        "    try:\n"
        "        pyedge.Renderer\n"
        "    except AttributeError as e:\n"
        "        assert \"Could not import required binding module '_pyedge'\" in str(e), str(e)\n"
        "        assert 'cmake --build build --target pyedge' in str(e), str(e)\n"
        "        assert isinstance(e.__cause__, ImportError), repr(e.__cause__)\n"
        "    else:\n"
        "        raise SystemExit('pyedge.Renderer without a binding')\n"
        "print('refused twice')\n"
    )
    result = _run(script, no_binding)
    assert "refused twice" in result.stdout, result.stderr


def test_a_probe_of_a_dunder_name_answers_without_the_binding(no_binding):
    # (inspect, doctest and pytest ask hasattr(module, "__wrapped__") and the like; an ImportError would escape hasattr)
    script = (
        "import sys, pyedge\n"
        "assert not hasattr(pyedge, '__wrapped__') and not hasattr(pyedge, '__test__')\n"
        "assert pyedge.__name__ == 'pyedge' and hasattr(pyedge, '__path__')\n"
        "assert '_pyedge' not in sys.modules\n"
        "print('probed')\n"
    )
    result = _run(script, no_binding)
    assert "probed" in result.stdout, result.stderr


def test_an_empty_edge_log_still_fails_the_import_itself(no_binding):
    result = _run("import pyedge\n", {**no_binding, "EDGE_LOG": ""}, check=False)
    assert result.returncode != 0
    assert "EDGE_LOG is set but empty" in result.stderr, result.stderr


def test_importing_pyedge_leaves_a_built_binding_unloaded_until_a_name_needs_it():
    script = (
        "import sys\n"
        "import pyedge\n"
        "assert '_pyedge' not in sys.modules, 'loaded by the import'\n"
        "renderer = pyedge.Renderer\n"
        "assert renderer is sys.modules['_pyedge'].Renderer\n"
        "assert pyedge._bindings is sys.modules['_pyedge']\n"
        "print('loaded by its first name')\n"
    )
    result = _run(script, {})
    assert "loaded by its first name" in result.stdout, result.stderr


def test_importing_pyedge_still_makes_the_built_bindings_importable_loading_neither():
    # (pyedge puts the directory of its build's bindings on the path, where pyrove's is too: a program, or a test, imports pyrove after
    # pyedge and finds it there)
    script = (
        "import sys, pyedge\n"
        "assert '_pyedge' not in sys.modules and 'pyrove_bind' not in sys.modules\n"
        "import pyrove\n"
        "assert pyrove.vec3(1, 2, 3).y == 2\n"
        "print('pyrove found')\n"
    )
    result = _run(script, {})
    assert "pyrove found" in result.stdout, result.stderr


def test_the_bindings_names_are_pyedges_as_before():
    script = (
        "import pyedge\n"
        "from pyedge import *\n"
        "assert Renderer is pyedge.Renderer and StaticModelPTBNT is pyedge.StaticModelPTBNT\n"
        "assert 'Renderer' in pyedge.__all__ and not [n for n in pyedge.__all__ if n.startswith('_')]\n"
        "assert 'Renderer' in dir(pyedge)\n"
        "try:\n"
        "    pyedge.no_such_name\n"
        "except AttributeError as e:\n"
        "    assert 'no_such_name' in str(e), str(e)\n"
        "else:\n"
        "    raise SystemExit('pyedge.no_such_name answered')\n"
        "print('the same names')\n"
    )
    result = _run(script, {})
    assert "the same names" in result.stdout, result.stderr


def test_threads_racing_for_the_first_name_all_get_the_one_binding():
    script = (
        "import sys, threading, pyedge\n"
        "barrier = threading.Barrier(16)\n"
        "got, failed = [], []\n"
        "def first():\n"
        "    barrier.wait()\n"
        "    try:\n"
        "        got.append(pyedge.Renderer)\n"
        "    except Exception as e:\n"
        "        failed.append(repr(e))\n"
        "threads = [threading.Thread(target=first) for _ in range(16)]\n"
        "[t.start() for t in threads]\n"
        "[t.join() for t in threads]\n"
        "assert not failed, failed\n"
        "assert len(got) == 16 and all(r is sys.modules['_pyedge'].Renderer for r in got)\n"
        "print('one binding')\n"
    )
    result = _run(script, {})
    assert "one binding" in result.stdout, result.stderr
