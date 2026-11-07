# Building edge

## Requirements

- CMake `>= 3.20`, Ninja, a C++20 toolchain, network access at the first configure (FetchContent)
- `rove` (math library) and the Boost headers it uses: the parent's target `rove`, else `-DEDGE_ROVE_SOURCE_DIR`, else the sibling
  checkout `../rove`, else an installed rove 3 (`find_package(rove 3 CONFIG)`), else fetched at `EDGE_ROVE_GIT_TAG`
- Apple: Xcode (the `metal` compiler: `xcrun -sdk macosx metal`) for the Metal backend
- Vulkan headers and loader and `glslangValidator` (the Vulkan backend; without them edge builds without it); for
  `-DEDGE_GLES=ON` `GLES3/gl3.h` and libGLESv3; SDL3 for `edge_sdl` (on when edge is built by itself, and with GLES)
- Boost.Test for the tests (`EDGE_TESTS`); Python with nanobind's needs, numpy and pytest for pyedge (`EDGE_PYTHON`)

By default fetched at configure, pinned: fastgltf (patched, `etc/patches`), cereal, stb, meshoptimizer, bc7enc,
astc-encoder, xatlas, nanobind, SPIRV-Cross (GLES), Dear ImGui (for the tests of `edge_imgui`, of edge built by itself; never installed).

### Dependencies from a package manager (`EDGE_SYSTEM_DEPS`)

`-DEDGE_SYSTEM_DEPS=ON` finds what a package manager (Conan, vcpkg) or the system provides instead of fetching it:

| Dependency | Found with | Target edge links |
|------------|------------|-------------------|
| cereal | `find_package(cereal CONFIG)` | `cereal::cereal` |
| stb | `find_package(stb CONFIG)` (Conan), else `find_path(STB_INCLUDE_DIRS stb_image.h PATH_SUFFIXES stb)` (vcpkg, Debian) | `stb::stb`, or the headers |
| meshoptimizer (0.21 or newer) | `find_package(meshoptimizer CONFIG)` | `meshoptimizer::meshoptimizer` |
| Dear ImGui (`EDGE_IMGUI`, unless the parent has a target `imgui`) | `find_package(imgui CONFIG)` | `imgui::imgui` |
| nanobind (`EDGE_PYTHON`) | `find_package(nanobind CONFIG)`, pip's (`python -m nanobind --cmake_dir`) | |
| rove (unless the parent has a target `rove`, or `EDGE_ROVE_SOURCE_DIR` is given) | `find_package(rove 3 CONFIG)` | `rove::rove` |

Fetched either way: fastgltf (edge patches it; the package managers' are not patched) with the simdjson it pins, the asset tools'
encoders no package manager has (bc7enc_rdo, astc-encoder, xatlas), and SPIRV-Cross for `shader_cross` (GLES, Metal).  SDL3, Vulkan
and Boost.Test are found either way.  With `EDGE_SYSTEM_DEPS` rove's Python module is not built: pyedge imports the environment's
pyrove (pip), which must be built with the same nanobind (2.4.0).

## In a game: rove and edge side by side

The layout edge is made for has rove and edge as sibling git submodules (or checkouts) of the game, each added by the game:

```cmake
add_subdirectory(rove)
add_subdirectory(edge)                   # takes the parent's target rove: nothing of rove is looked for or fetched
target_link_libraries(game PRIVATE edge::edge)
edge_bundle_runtime(game_runtime DESTINATION ${GAME_BIN})
```

edge built by itself finds `../rove` the same way.  A parent that installs edge (`-DEDGE_INSTALL=ON`) installs rove too
(`-DROVE_INSTALL=ON`): edge's package needs rove's.

## CMake Options

| Option | Description |
|--------|-------------|
| `-DEDGE_TESTS=OFF` | No test_edge (a cross build), and no Boost.Test |
| `-DEDGE_PYTHON=OFF` | No pyedge (and no Python, nanobind) |
| `-DEDGE_GLES=ON` | The OpenGL ES backend |
| `-DEDGE_METAL=ON` | The Metal backend (Objective-C++, Apple silicon; on by default on Apple platforms). `EDGE_METAL_SDK` (`macosx`, `iphoneos`, `iphonesimulator`) is the sdk its shader libraries are built for |
| `-DEDGE_SDL=ON` | edge_sdl (SDL3): an SDL window's Vulkan surface, a GLES context made with SDL, an SDL window's Metal layer (on with GLES or Metal, and built by itself) |
| `-DEDGE_IMGUI=ON` | edge_imgui: Dear ImGui drawn through edge, `imgui_renderer.h`, a header compiled against the parent's `imgui` target, 1.92 or newer (on built by itself) |
| `-DEDGE_ASSET_TOOLS=OFF` | The runtime alone: no `edge_asset`, `edge_convert`, `edge_shot` or pyedge `convert_gltf_to_edge`, and none of meshoptimizer, bc7enc_rdo, astc-encoder, xatlas (their tests are left out; edge still loads `.edge` and glTF files) |
| `-DEDGE_SYSTEM_DEPS=ON` | Find the dependencies a package manager provides (above) instead of fetching them |
| `-DEDGE_INSTALL=OFF` | No install rules and no CMake package (on when edge is built by itself; a parent that turns it on turns rove's `ROVE_INSTALL` on too) |
| `-DEDGE_ROVE_SOURCE_DIR=<path>` | Explicit rove source path |
| `-DCMAKE_BUILD_TYPE=Debug` | Build type (RelWithDebInfo when edge is built by itself and none is given) |

## Notes

- The first configure fetches edge's dependencies (FetchContent, pinned) and so needs the network; later ones do not.
- The Vulkan backend is built when the Vulkan headers, loader and glslangValidator are found; the GLES one with `-DEDGE_GLES=ON`, the Metal one (Apple platforms) unless `-DEDGE_METAL=OFF`.

## Where edge finds its shaders at run time

edge's compiled shaders are read at run time (`src/shader_dirs.h`).  Each set is looked for in this order, the first place that holds
it taken:

| Set | Variable (taken as it is when set) | Beside the binary edge is linked into | Then |
|-----|------------------------------------|----------------------------------------|------|
| Vulkan SPIR-V (`vertex.spv`, ...) | `EDGE_VK_SHADER_DIR` | `shaders/edge`, `../share/edge/shaders` | the build's directory, the install's |
| GLSL ES translations (`gles_shader_dir()`) | `EDGE_GLES_SHADER_DIR` | `shaders/edge`, `../share/edge/shaders` | the build's directory, the install's |
| Metal translations (`metal_shader_dir()`) | `EDGE_METAL_SHADER_DIR` | `shaders/edge`, `../share/edge/shaders` | the build's directory, the install's |
| View shaders (`lit.frag.spv`: `edge_shot`, `pyedge.view_shader_dir()`) | `EDGE_VIEW_SHADER_DIR` | `view_shaders`, `../share/edge/view_shaders` | the build's directory, the install's |

"The binary edge is linked into" is the executable, or the shared library that holds edge (pyedge's module), found with `dladdr`
(`GetModuleHandleEx` on Windows) from an address of edge's.  So `shaders/edge` is the bundle `edge_bundle_runtime` copies beside a
game's binary (and a wheel's `pyedge` package), and `../share/edge/shaders` an installed edge seen from its `bin/` or `lib/`.  The
install's directory is `<CMAKE_INSTALL_PREFIX>/share/edge/...` as configured (where edge has install rules), tried last.  A wheel names
no build's directory, as its build is gone when it runs; its shaders are in its package.  When no place holds them the error names
every place tried.

## Installing, and the CMake package

`cmake --install build --prefix <prefix>` (with `EDGE_INSTALL`, on when edge is built by itself) installs:

| Where | What |
|-------|------|
| `lib/` | `libedge.a`, and where built `libedge_sdl.a`, `libedge_asset.a`; the libraries edge builds of its dependencies and links privately, under edge's names: `libedge_fastgltf.a` (patched), and with the asset tools `libedge_meshoptimizer.a`, `libedge_bc7enc.a`, `libedge_astcenc.a`, `libedge_xatlas.a` |
| `include/edge/` | the public headers (those `include/edge/` has a shim of, and no other: a backend's internals are not installed), the real ones: `<edge/renderer.h>`; only `include/` is on the include path |
| `include/` | rove (its own install) when edge added rove itself |
| `share/edge/shaders/` | edge's compiled shaders (SPIR-V, and their GLSL ES and Metal translations where built) |
| `share/edge/view_shaders/` | the view shaders (`lit.frag.spv`, `edge_shot`'s) |
| `share/edge/glsl/` | `edge.glsl` and what a shader may include beside it |
| `bin/` | `edge_convert`, `edge_shot` where built; `edge_shader_cross` with the GLES or Metal backend |
| `lib/cmake/edge/` | `edgeConfig.cmake`, `edgeConfigVersion.cmake` (SameMajorVersion), the exported targets, and the helpers `EdgeShaders.cmake`, `EdgeRuntime.cmake`, `EdgeModels.cmake` (with `EdgeGles.cmake`, `EdgeMetal.cmake`, `edge_convert_model.cmake`) |
| `share/doc/edge/` | `LICENSE.md`, `THIRD_PARTY.md` |

A project uses it with

```cmake
find_package(edge 3 CONFIG REQUIRED)              # COMPONENTS sdl imgui asset: fail where edge was built without them
target_link_libraries(game PRIVATE edge::edge)    # edge::sdl, edge::imgui, edge::asset
edge_add_shaders(game_shaders DESTINATION ${BIN}/shaders SOURCES sea.vert sea.frag)
edge_bundle_runtime(game_runtime DESTINATION ${BIN})
edge_convert_models(game_models SOURCE_DIR assets DESTINATION ${BIN}/models)   # runs edge::convert
```

The package finds what the public headers and the link interface need: `rove 3`, `Vulkan` (with the Vulkan backend), `SDL3`
(for `edge::sdl`).  `edge::imgui` is `<edge/imgui_renderer.h>`, compiled against the program's ImGui (1.92 or
newer): nothing of ImGui is in the install, and the program links its own ImGui beside it.  Inside edge's own build the same targets have the same names (`edge::edge` is an alias of `edge`).

## pyedge as a wheel (pip)

`pyproject.toml` builds pyedge with scikit-build-core: `pip install .` (or `python -m build --wheel`) builds edge and the native
module `_pyedge` into the `pyedge` package, with edge's shaders beside it (`pyedge/shaders/edge`, `pyedge/view_shaders`, where
`src/shader_dirs.h` finds them).  It depends on exactly the pyrove edge builds against (`pyrove==3.0.1`, released in lockstep, pyrove first) and does not build rove's Python module: pyedge imports pip's pyrove,
and both are built with nanobind 2.4.0 (pinned exactly) so that the two modules share rove's types, which also takes the same C++ ABI
(GCC 14's `pyrove` wheel and a GCC 15 `pyedge` do not share them: [install.md](install.md#python-pip)).  rove's C++ sources are
`EDGE_ROVE_SOURCE_DIR` when given (`CMAKE_ARGS=-DEDGE_ROVE_SOURCE_DIR=...`), else `../rove`, else the tag `EDGE_ROVE_GIT_TAG` fetched.
The wheel has Vulkan on Linux and Metal on macOS (the Vulkan headers, loader and `glslangValidator` are needed to build the first, `glslangValidator` and Xcode the second) and links the system's `libvulkan.so.1`, which
it does not carry.  `[tool.cibuildwheel]` and `.github/workflows/wheels.yml` build the Linux wheels (manylinux_2_28, x86_64 and
aarch64); `test/wheel_smoke.py <model.glb>` checks an installed one from outside the tree.
