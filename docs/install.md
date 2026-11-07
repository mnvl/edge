# Installing edge

edge is a set of static C++20 libraries, plus an optional Python module. It needs [rove](https://github.com/mnvl/rove), the math
library, and finds or fetches it the way each section below says. Each way gives a CMake project the target `edge::edge` (and, where
built, `edge::asset`, `edge::imgui`, `edge::sdl`) and the helpers `edge_add_shaders`, `edge_bundle_runtime` and
`edge_convert_models`; code includes edge's headers as `<edge/...>`:

```cpp
#include <edge/vulkan_renderer.h>
#include <edge/load_model.h>
```

Requirements for C++: CMake 3.20 or newer, a C++20 compiler (GCC 12, Clang 15, Apple Clang 15 or newer), and for the Vulkan backend
the Vulkan headers and loader and `glslangValidator` (Debian and Ubuntu: `libvulkan-dev glslang-tools`; Conan and vcpkg provide
them). Linux is the platform edge is built and tested on; macOS (Metal) builds; Windows is not supported yet.

| Way | rove comes from | edge's dependencies come from |
|-----|-----------------|-------------------------------|
| [pip](#python-pip) | PyPI (`pyrove`) | built into the wheel |
| [Git submodules](#c-rove-and-edge-as-git-submodules) | the sibling submodule | fetched at configure, pinned |
| [Conan](#c-conan-2) | rove's recipe | ConanCenter |
| [vcpkg](#c-vcpkg) | rove's port | vcpkg |
| [From source](#c-installed-from-source) | `../rove`, an installed rove, or fetched | fetched at configure, pinned |

Whichever way, fastgltf (which edge patches) and the asset tools' encoders (bc7enc_rdo, astc-encoder, xatlas) are fetched with git
when edge is built, so the build needs network access. [THIRD_PARTY.md](../THIRD_PARTY.md) lists every dependency and its license.

## Python: pip

```bash
pip install pyedge                                    # a released version, from PyPI
pip install ./rove ./edge                             # local checkouts, side by side; builds from source
```

PyPI has wheels for Linux (x86_64 and aarch64, glibc 2.28 or newer). The wheel holds edge and its shaders; it uses the system's
Vulkan loader (`libvulkan.so.1`) and a Vulkan driver (any GPU's, or Mesa's lavapipe without one), and pip installs `pyrove` with it.
Elsewhere pip builds from source, which needs the Vulkan headers and loader, `glslangValidator`, CMake and a C++20 compiler.

pyedge has the Vulkan and null backends; OpenGL ES and Metal are for C++. `import pyedge` succeeds without the loader, and the first use says it is missing.

Build `pyrove` and `pyedge` the same way. They share rove's types through nanobind, and only between modules built with the same
C++ ABI: a `pyedge` built from source beside a `pyrove` wheel can install and import, then raise `TypeError` when a `pyrove` type
reaches it. Where pip would build `pyedge` from source, build both:

```bash
pip install --no-binary pyrove,pyedge pyedge
```

```python
import pyedge
renderer = pyedge.create_renderer(640, 480)
```

`examples/` has complete scripts; [python.md](python.md) has more.

## C++: rove and edge as git submodules

The layout edge is made for: rove and edge side by side in your project, each added by it.

```bash
git submodule add https://github.com/mnvl/rove external/rove
git submodule add https://github.com/mnvl/edge external/edge
```

```cmake
add_subdirectory(external/rove)
add_subdirectory(external/edge)                  # takes your target rove: nothing of rove is looked for or fetched
target_link_libraries(game PRIVATE edge::edge)

edge_add_shaders(game_shaders DESTINATION ${CMAKE_BINARY_DIR}/shaders SOURCES src/sea.vert src/sea.frag)
edge_bundle_runtime(game_runtime DESTINATION ${CMAKE_BINARY_DIR})   # edge's shaders beside the game's binary
```

Added as a subdirectory, edge builds the library and pyedge, without tests, install rules, `edge_sdl`, `edge_imgui` or `edge_shot`
(turn them on with `EDGE_TESTS`, `EDGE_INSTALL`, `EDGE_SDL`, `EDGE_IMGUI`, `EDGE_SHOT`). `-DEDGE_PYTHON=OFF` leaves pyedge out, and
`-DEDGE_ASSET_TOOLS=OFF` the model conversion. [building.md](building.md) lists the options.

## C++: Conan 2

The recipe is `conanfile.py` in this repository; it takes rove from rove's recipe and the other dependencies from ConanCenter. Until
rove and edge are in ConanCenter, create both packages from checkouts:

```bash
git clone --branch v3.0.1 https://github.com/mnvl/rove
git clone --branch v3.0.1 https://github.com/mnvl/edge
conan create rove -s compiler.cppstd=20
conan create edge -s compiler.cppstd=20 --build=missing
```

`conan create` builds edge and runs `test_package/`, a small program that links it. On Linux, ConanCenter's Vulkan loader needs the
system's X11 and Wayland development packages: add `-c tools.system.package_manager:mode=install -c
tools.system.package_manager:sudo=True` to let Conan install them. Then,
in your project's `conanfile.txt`:

```ini
[requires]
edge/3.0.1

[tool_requires]
glslang/1.4.313.0

[generators]
CMakeDeps
CMakeToolchain
VirtualBuildEnv

[layout]
cmake_layout
```

```bash
conan install . -s compiler.cppstd=20 --build=missing
cmake --preset conan-release
cmake --build --preset conan-release
```

```cmake
find_package(edge CONFIG REQUIRED)
target_link_libraries(game PRIVATE edge::edge)
```

`glslang` gives `edge_add_shaders` its `glslangValidator`; leave it out if you compile no shaders of your own. The package is
edge's own CMake package (with the helpers), not one Conan generates.

| Option | Default | |
|--------|---------|---|
| `edge/*:asset_tools` | `True` | `edge::asset` and `edge_convert`: making `.edge` models (needs meshoptimizer) |
| `edge/*:with_imgui` | `False` | `edge::imgui`: Dear ImGui drawn through edge |
| `edge/*:with_sdl` | `False` | `edge::sdl`: an SDL3 window's Vulkan surface |

## C++: vcpkg

The port is in `ports/edge`, beside rove's in rove's `ports/`. Until they are in vcpkg's registry, use both as overlay ports, from `main`:
a port names a release and the SHA512 of its archive, and a tag's own copy cannot hold the hash of the archive made from it. In
manifest mode, your project's `vcpkg.json`:

```json
{
  "dependencies": ["edge"]
}
```

```bash
git clone https://github.com/mnvl/rove
git clone https://github.com/mnvl/edge
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake "-DVCPKG_OVERLAY_PORTS=$PWD/rove/ports;$PWD/edge/ports"
```

or in classic mode: `vcpkg install edge --overlay-ports=rove/ports --overlay-ports=edge/ports`. Then:

```cmake
find_package(edge CONFIG REQUIRED)
target_link_libraries(game PRIVATE edge::edge)
```

Features: `asset-tools` (on by default), `imgui`, `sdl3`, the Conan options above (`"edge[imgui,sdl3]"`). The port builds
`glslangValidator` too, for `edge_add_shaders`; `edge_convert` is in `tools/edge`.

## C++: installed from source

```bash
git clone https://github.com/mnvl/rove
git clone https://github.com/mnvl/edge
cmake -S edge -B edge/build -DCMAKE_BUILD_TYPE=Release -DEDGE_TESTS=OFF -DEDGE_PYTHON=OFF
cmake --build edge/build
cmake --install edge/build --prefix /opt/edge
```

edge takes rove from `../rove` and installs it beside itself. Then `find_package(edge CONFIG REQUIRED)` with
`-DCMAKE_PREFIX_PATH=/opt/edge`. [building.md](building.md) lists what the install holds, and `-DEDGE_SYSTEM_DEPS=ON`, which takes
spdlog, cereal, stb, meshoptimizer, ImGui and rove from the system or a package manager instead of fetching them.

## Shaders at run time

edge reads its compiled shaders at the first draw that needs them. A program built against edge finds them in `shaders/edge` beside its
binary, which `edge_bundle_runtime(<target> DESTINATION <dir>)` fills; `EDGE_VK_SHADER_DIR` names another directory. The wheel
carries its own. [building.md](building.md#where-edge-finds-its-shaders-at-run-time) has the whole lookup.

An installed `libedge.a` remembers the build's shader directory and the prefix given to CMake when it was configured, not one given later to
`cmake --install --prefix`, so bundle the shaders with a program you ship: the build tree may be gone where it runs.

## Versions

edge shares its version numbers with rove and follows semantic versioning: `find_package(edge 3)` accepts any 3.x, and edge 3 needs
rove 3. What changed between versions is in [CHANGELOG.md](../CHANGELOG.md).
