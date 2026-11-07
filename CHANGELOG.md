# Changelog

All notable changes to edge are written here. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/);
versions follow [Semantic Versioning](https://semver.org/).

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

- `sdl_vulkan_device_for_window`: the device that can present to a window, on a machine with several.

### Changed

- `Texture` of no size or a negative size is `std::invalid_argument`.
- A name of pyedge whose binding cannot load raises `AttributeError` from the `ImportError`, which was raised directly.
- pyedge's null backend raises `ValueError` for `frames_in_flight`, `joint_matrices` and `device_uuid`; it was `RuntimeError`.
- ImGui's headers install under `include/edge/imgui`, on `edge::imgui_core`'s include path.
- Dependencies are fetched by commit; Conan pins spdlog 1.15.3 and meshoptimizer 0.22.
- pyedge requires exactly the pyrove edge builds against, not a range: the two are released in lockstep, pyrove first. Publishing pyedge fails
  if that pyrove is not on PyPI yet.

## [3.0.0] - 2026-11-06

First public release.
