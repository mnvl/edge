# SPDX-License-Identifier: MIT
# Copyright (c) 2004-2026 Manvel Avetisian

"""edge's Python bindings: renderers (Vulkan, null), models, materials, cameras and textures.

An installed pyedge (pip) has its compiled module, _pyedge, inside the package, and edge's shaders beside it.  A source checkout finds the
module CMake built: $EDGE_PYTHON_BINDINGS_DIR, else build/lib of the checkout or of the project it is a subdirectory of.
"""
from __future__ import annotations

import importlib
import importlib.machinery
import os
import re
import sys
import threading
import warnings
from pathlib import Path
from types import ModuleType

__version__ = "3.0.1"


def _binding_dirs() -> list[Path]:
    explicit = os.environ.get("EDGE_PYTHON_BINDINGS_DIR")
    if explicit:
        return [Path(explicit).expanduser()]

    repo_root = Path(__file__).resolve().parents[1]
    return [
        # Unified build from a parent project (edge as subdirectory)
        repo_root.parent / "build" / "lib",
        # Standalone edge build
        repo_root / "build" / "lib",
        repo_root / "cmake-build-debug" / "lib",
        repo_root / "cmake-build-release" / "lib",
    ]


def _module_files(bindings_dir: Path, module_name: str) -> list[Path]:
    patterns = (
        f"{module_name}*.so",
        f"{module_name}*.pyd",
        f"{module_name}*.dylib",
        f"{module_name}*.dll",
    )
    result: list[Path] = []
    for pattern in patterns:
        result.extend(sorted(bindings_dir.glob(pattern)))
    return result


def _running_python_tag() -> str:
    """The tag an extension needs to load here.  Not cache_tag: a
    free-threaded interpreter shares that with the GIL build of its version,
    and only the extension suffix says cpython-314t rather than cpython-314."""
    return re.search(r"(cpython-\d+t?)", importlib.machinery.EXTENSION_SUFFIXES[0]).group(1)


def _guess_python_tags(paths: list[Path]) -> list[str]:
    tags = set()
    for path in paths:
        match = re.search(r"(cpython-\d+t?)", path.name)
        if match:
            tags.add(match.group(1))
    return sorted(tags)


def _put_binding_dirs_on_path() -> None:
    for candidate in _binding_dirs():
        if candidate.exists() and str(candidate) not in sys.path:
            sys.path.insert(0, str(candidate))


def _import_required_binding(module_name: str) -> ModuleType:
    # Installed (a wheel): the module is inside the package (and known by its own name too, as a build's is).  The variable picks another
    # where it names a directory that has one; one that names nothing is no reason to fail with the right module beside this file.
    explicit = os.environ.get("EDGE_PYTHON_BINDINGS_DIR")
    if _module_files(Path(__file__).resolve().parent, module_name) and not (explicit and _module_files(Path(explicit).expanduser(), module_name)):
        if explicit:
            warnings.warn(f"EDGE_PYTHON_BINDINGS_DIR={explicit} has no {module_name}: using the one this pyedge is installed with",
                          RuntimeWarning, stacklevel=2)
        module = importlib.import_module(f"{__name__}.{module_name}")
        sys.modules.setdefault(module_name, module)
        return module

    # A source checkout: the module CMake built (e.g. build/lib/_pyedge*.so).
    bindings_dirs = _binding_dirs()
    _put_binding_dirs_on_path()

    try:
        return importlib.import_module(module_name)
    except (ModuleNotFoundError, ImportError) as exc:
        extension_suffixes = ", ".join(importlib.machinery.EXTENSION_SUFFIXES)
        built_files: list[Path] = []
        for candidate in bindings_dirs:
            if candidate.exists():
                built_files.extend(_module_files(candidate, module_name))

        running_tag = _running_python_tag()
        built_tags = _guess_python_tags(built_files)
        mismatch_note = ""
        if built_tags and running_tag not in built_tags:
            mismatch_note = (
                "\nDetected ABI mismatch:\n"
                f"- active interpreter tag: {running_tag}\n"
                f"- built module tags: {', '.join(built_tags)}\n"
            )

        raise ImportError(
            f"Could not import required binding module '{module_name}'.\n"
            "Install pyedge with pip (pip install pyedge, or pip install . in edge's checkout), or build the\n"
            "native bindings with the same interpreter and put the checkout on PYTHONPATH:\n"
            "  cmake -S . -B build -DPython_EXECUTABLE=$(which python)\n"
            "  cmake --build build --target pyedge\n"
            f"extension_suffixes={extension_suffixes}\n"
            f"module_candidates={[path.name for path in built_files]}"
            f"{mismatch_note}"
        ) from exc


# The binding (and pyrove's, which it imports) is loaded by the first name that needs it, not by importing pyedge: a program that
# imports it and needs no name of it (a tool that puts edge on the path only to find its test assets) runs where no binding was built, or
# in a Python that cannot load it (Blender's).
_binding: ModuleType | None = None
_binding_lock = threading.Lock()


def _loaded_binding() -> ModuleType:
    global _binding
    if _binding is None:
        with _binding_lock:
            if _binding is None:
                binding = _import_required_binding("_pyedge")
                # The native side's log (edge/src/log.h) goes to $EDGE_LOG from the moment the binding is loaded, before the first
                # renderer or model a process builds.  Without it nothing is written to disk; warnings and errors reach stderr either
                # way.  init_logging() moves it.
                binding.init_logging(os.environ.get("EDGE_LOG"))
                _check_pyrove_works(importlib.import_module("pyrove"), binding)
                _binding = binding
    return _binding


def _check_pyrove_works(pyrove: ModuleType, binding: ModuleType) -> None:
    """pyedge and pyrove share rove's types only where nanobind's ABI tags match, which the two builds' compilers decide.  pip cannot
    see it: its check says all is well, and every call that passes a vector then raises TypeError.  Said once, here, with what to do."""
    try:
        binding._takes_a_pyrove_vector(pyrove.vec3(0.0, 0.0, 0.0))
    except TypeError as exc:
        raise ImportError(
            "pyedge and pyrove cannot pass rove's types to each other: they were built by different compilers (nanobind's ABI tags differ). "
            "Build both the same way: pip install --force-reinstall --no-binary pyrove,pyedge pyedge"
        ) from exc


def __getattr__(name: str):
    # (a probe of a dunder name -- hasattr(module, "__wrapped__") by inspect, doctest, pytest -- answers without the binding: an
    # ImportError would escape hasattr)
    if name.startswith("__") and name.endswith("__") and name != "__all__":
        raise AttributeError(f"module 'pyedge' has no attribute '{name}'")
    try:
        binding = _loaded_binding()
    except ImportError as exc:
        # (hasattr() and friends answer False, and the error says why: pyedge needs a binding that may not load here)
        raise AttributeError(f"module 'pyedge' has no attribute '{name}': {exc}") from exc
    if name == "__all__":
        return [n for n in dir(binding) if not n.startswith("_")]
    if name == "_bindings":          # (the binding module itself, for a caller that needs where its files are)
        return binding
    try:
        return getattr(binding, name)
    except AttributeError as exc:
        raise AttributeError(f"module 'pyedge' has no attribute '{name}'") from exc


def __dir__() -> list[str]:
    return sorted(name for name in dir(_loaded_binding()) if not name.startswith("_"))


_DESCRIPTION = __doc__ or ""


def _description_and_signatures() -> str:
    """The description, and every function with its overloads: help() lists a nanobind function under no heading of its own.  Without a
    binding that can load, the description alone: help() is no place to fail."""
    try:
        binding = _loaded_binding()
    except ImportError:
        return _DESCRIPTION
    lines = []
    for name in sorted(n for n in dir(binding) if not n.startswith("_")):
        for signature, _, defaults in getattr(getattr(binding, name), "__nb_signature__", ()):
            # (nanobind writes a default as \\N, or \\=N where a string stands for it, for the Nth of `defaults`)
            text = re.sub(r"\\=?(\d+)", lambda m: repr(defaults[int(m.group(1))]), signature)
            lines.append("    " + re.sub(r"\b(?:pyedge\.)?_pyedge\.|\b(?:pyrove\.)?pyrove_bind\.", "", text).removeprefix("def "))
    return _DESCRIPTION + "\nFunctions:\n\n" + "\n".join(lines) + "\n"


class _Package(ModuleType):
    # (made when asked: reading it loads the binding, which an import does not)
    __doc__ = property(lambda self: _description_and_signatures())


sys.modules[__name__].__class__ = _Package


def _check_edge_log() -> None:
    # (an empty EDGE_LOG is a mistake whether or not the binding is ever loaded: said at the import)
    path = os.environ.get("EDGE_LOG")
    if path is not None and not path:
        raise RuntimeError("EDGE_LOG is set but empty; set it to a log file path or unset it")


_check_edge_log()
# (the build's bindings importable from here on, loading none: pyrove's beside pyedge's, for a program that imports pyrove after pyedge;
# an installed pyedge's pyrove is pip's)
if os.environ.get("EDGE_PYTHON_BINDINGS_DIR") or not _module_files(Path(__file__).resolve().parent, "_pyedge"):
    _put_binding_dirs_on_path()
