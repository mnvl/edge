# Changelog

All notable changes to edge are written here. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/);
versions follow [Semantic Versioning](https://semver.org/).

## [Unreleased]

### Changed

- The log is edge's own, on the standard library: spdlog (and its fmt) is no longer fetched, linked, installed or asked for by
  `find_package(edge)`, and no public header includes it. Lines at or above the level go to `std::cerr`, or to a sink:
  `edge::set_log_sink()`, `edge::set_log_level()` (`warn` by default), `pyedge.set_log_level()`. `EDGE_LOG_*` take
  `std::format` arguments.
- The oldest compilers and systems are those with `std::format`: GCC 13, Clang 17, Apple Clang 15, MSVC 19.3x; macOS 13.3 and
  iOS 16.3 (configure stops on an older `CMAKE_OSX_DEPLOYMENT_TARGET`).

### Removed

- `edge::init_logging()`, `edge::log()`, `pyedge.init_logging()` and `$EDGE_LOG`: edge writes no log file. A program that wants
  one gives a sink that writes it.

### Fixed

- pyedge from `pip install -e .` finds its compiled module: it looked only beside `__init__.py`, where an editable install has none, so every name
  of the binding raised `AttributeError`, saying to build what was built.

## [3.0.1] - 2026-11-07

### Fixed

- A wheel that cannot find its shaders no longer lists a directory of the build it came from among the places it tried.
- What a file says is checked when it is loaded: `.edge` skins, indices, triangle ranges, positions, mip levels, materials' modes and bools;
  glTF accessors against their buffers and types; DDS layers, mip levels and sizes. A damaged file is an error, never a read out of bounds, a
  hang or a crash.
- A draw of a range past its buffers, or of a buffer or texture of another renderer, is `std::invalid_argument` on every backend.
- The Vulkan instance stays for the process once made, so making and dropping renderers no longer runs a driver out of file descriptors.
  `VulkanInstance::release_unused()` lets it go.
- `Texture::save_to_file` refuses a format it cannot write, and writes what its extension says; an unknown extension is an error.
- `load_model("robot.glb")` works for a bare file name. The README's frame compiles and runs.
- Texture sizes are checked against the device's limits, and a failed creation leaks nothing. The null backend refuses what Vulkan refuses.
- As a subdirectory, edge no longer builds its tests. Relative directories to `edge_convert_models` are those of the calling list file.
- pyedge: an import against a pyrove of another compiler is an `ImportError` that says what to do. `hasattr(pyedge, name)` answers when the
  binding cannot load. The wheel's bundled libraries are not exported, and it carries their licence notices and `edge.glsl`.
  `help(pyedge)` lists every function and overload.
  `EDGE_PYTHON_BINDINGS_DIR` that names no module no longer costs an installed package its own: it warns and uses that.

### Added

- pyedge wheels for macOS on Apple silicon, and a Metal backend in pyedge, which `backend="auto"` takes on a Mac.
- `sdl_vulkan_device_for_window`: the device that can present to a window, on a machine with several.

### Changed

- `Camera::set_translation_rotation` is gone, from C++ and from Python: it took Euler angles in a vector. Build the matrix and give it to
  `set_view_matrix`.
- `Texture` of no size or a negative size is `std::invalid_argument`.
- A name of pyedge whose binding cannot load raises `AttributeError` from the `ImportError`, which was raised directly.
- pyedge's null backend raises `ValueError` for `frames_in_flight`, `joint_matrices` and `device_uuid`; it was `RuntimeError`.
- ImGui's headers install under `include/edge/imgui`, on `edge::imgui_core`'s include path.
- Dependencies are fetched by commit; Conan pins spdlog 1.15.3 and meshoptimizer 0.22.
- pyedge has no source distribution on PyPI: where no wheel fits, pip says so at once. `docs/install.md` has the build from source.
- pyedge requires exactly the pyrove edge builds against, not a range: the two are released in lockstep, pyrove first. Publishing pyedge fails
  if that pyrove is not on PyPI yet.

## [3.0.0] - 2026-11-06

First public release.
