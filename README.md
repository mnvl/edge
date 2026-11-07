# edge

Minimalistic C++20 rendering library with Vulkan, OpenGL ES 3.0 / WebGL 2 and Metal backends, and Python bindings (`pyedge`).

It has everything you need to load geometry, render it and develop your code in a platform and API independent manner.

It can read glTF and has an in-house fast-loading `.edge` format for models with support for LODs and mip chains.

It is meant to be read and changed: there is no scene editor, no scripting layer (except Python bindings) and no asset store.

It was refactored from a game engine from 2004 to render many scenes in parallel for robotics simulation (cameras, depth
and lidar images for training and testing), where a general-purpose engine was too slow. Much of the binding code, refactorings, new backends and documentation were written with Claude and curated by the author. The library was also used successfully to build computer games which are running on MacOS, Windows, Linux, iOS, Android, and even web browsers.

## Features

- **Backends**: Vulkan 1.0 with `VK_KHR_maintenance1` (Linux, Windows, Android, macOS through MoltenVK), OpenGL ES 3.0 and
  WebGL 2 (Emscripten), Metal (Apple silicon, macOS 13 and iOS 15 on), and a null renderer that keeps every rule and draws
  nothing (for tests). The frame and pass order and the error behaviour are the same on every backend.
- **Headless and parallel**: renderers share no state, so a process runs one per thread, each offscreen, on one GPU or
  several. Frames in flight (1 to 4), batched texture downloads, GPU timers.
- **glTF 2.0**: meshes, PBR metallic-roughness materials (normal, occlusion and emissive maps), `KHR_texture_transform`,
  skins and animations (linear, step and cubic), vertex colours, several UV sets. Loading is strict: a malformed file is an
  error that names what is wrong.
- **The `.edge` format and `edge_convert`**: models converted ahead of time, with BC7 or ASTC textures and their mip
  chains, levels of detail (meshoptimizer), texture atlases (grid or xatlas), tiled ground meshes and pose bounds for
  skinned meshes. CMake helpers rebuild a model only when its inputs change.
- **Rendering**: render targets with several colour attachments, layers and cube faces; readable depth and stencil;
  instancing; blended draws sorted back to front; levels of detail chosen by pixel error; frustum culling; debug lines;
  text (stb_truetype); a Dear ImGui renderer; a full-screen copy and a frame presenter for windows.
- **Shaders**: GLSL 4.50 compiled to SPIR-V, and translated for GLES and Metal at build time. `edge.glsl` is the binding
  contract a custom shader includes, with BRDF, material and skinning helpers beside it.
- **Python**: `pyedge` (nanobind) exposes renderers, models, materials, cameras and textures; downloads arrive as NumPy
  arrays, long calls release the GIL, and the module is built free-threaded for Python 3.14t.
- **`edge_shot`**: draws a model to a PNG, a contact sheet of many, or a window to inspect it.

## Status

edge is developed for the author's own projects, which use it in production. The API still changes between versions; see
[CHANGELOG.md](https://github.com/mnvl/edge/blob/main/CHANGELOG.md). Contributions are welcome: see [CONTRIBUTING.md](https://github.com/mnvl/edge/blob/main/CONTRIBUTING.md).

## Requirements

- CMake 3.20 or newer, Ninja, a C++20 compiler (GCC 12, Clang 15, Apple Clang 15, MSVC 19.3x)
- [rove](https://github.com/mnvl/rove), the math library, checked out beside edge (`../rove`) or given with
  `-DEDGE_ROVE_SOURCE_DIR=<path>`
- For Vulkan: the Vulkan headers and loader and `glslangValidator`. Without them edge builds without the Vulkan backend.
- For OpenGL ES (`-DEDGE_GLES=ON`): `GLES3/gl3.h` and libGLESv3. For Metal: Xcode.
- SDL3 for `edge_sdl` (windows and contexts), Boost.Test for the tests, Python 3.9 or newer with NumPy and pytest for pyedge
- Network access on the first configure: the other dependencies are fetched at pinned versions (see
  [THIRD_PARTY.md](https://github.com/mnvl/edge/blob/main/THIRD_PARTY.md))

## Install

pip (`pip install pyedge`, Linux wheels), Git submodules beside rove, Conan, vcpkg, or installed from source:
[docs/install.md](https://github.com/mnvl/edge/blob/main/docs/install.md) has each, with the CMake that uses it:

```cmake
add_subdirectory(rove)
add_subdirectory(edge)                            # or find_package(edge CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE edge::edge)  # brings rove, spdlog, cereal and Vulkan

edge_add_shaders(my_shaders DESTINATION ${CMAKE_BINARY_DIR}/shaders SOURCES src/sea.vert src/sea.frag)
edge_bundle_runtime(my_runtime DESTINATION ${CMAKE_BINARY_DIR})     # edge's shaders beside the binary
edge_convert_models(my_models SOURCE_DIR ${CMAKE_SOURCE_DIR}/assets DESTINATION ${CMAKE_BINARY_DIR}/models)
```

From Python, `pip install pyedge`, with wheels for Linux, macOS on Apple silicon and 64-bit Windows. On Linux and Windows it needs the system's
Vulkan loader (`libvulkan.so.1`, `apt install libvulkan1`; `vulkan-1.dll`, which a GPU's driver installs), even for the null backend, and a Vulkan
driver to draw (any GPU's, or Mesa's lavapipe). On a Mac it draws with Metal. pyedge has the Vulkan, Metal and null backends; OpenGL ES is C++ only.

## Build and test

```bash
git clone https://github.com/mnvl/rove
git clone https://github.com/mnvl/edge
cmake -S edge -B edge/build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build edge/build
ctest --test-dir edge/build -j$(nproc) --output-on-failure
```

On a real GPU run the Python tests one at a time (`pytest` without `-n`): two at once have lost the device on NVIDIA's driver. Set
`VK_DRIVER_FILES` to lavapipe's ICD to run them in parallel.

Without a GPU, Mesa's lavapipe (Vulkan) and llvmpipe (OpenGL ES) run the whole test suite on the CPU;
`etc/container/test.sh` builds and tests edge in such a Linux container.

## A frame

```cpp
#include <edge/camera.h>
#include <edge/load_model.h>
#include <edge/model.h>
#include <edge/scheduler.h>
#include <edge/vulkan_renderer.h>

auto renderer = std::make_shared<edge::VulkanRenderer>(640, 480);
auto scheduler = std::make_shared<edge::Scheduler>(renderer);
auto model = edge::load_model(renderer, scheduler, "robot.glb");     // a .glb, .gltf or .edge file
auto target = renderer->create_render_target(640, 480, edge::PixelFormat::A8R8G8B8);

auto camera = std::make_shared<edge::Camera>();
camera->set_perspective_projection(1.0f, 640.0f / 480.0f, 0.1f, 100.0f);
camera->look_at({0, 1, 4}, {0, 1, 0}, {0, 1, 0});

renderer->begin_frame();
model->draw(camera, rove::matrix<4, 4>());          // queues the model's draw calls
renderer->begin_pass(target);                        // cleared to black
scheduler->flush();                                  // sorted, culled, drawn
renderer->end_pass();
renderer->finish_frame();                            // waits for the GPU: the target can be downloaded now
```

From Python the calls are the same (`pyedge.create_renderer(640, 480)`, `renderer.begin_frame()`, ...), and a
downloaded texture is a NumPy array (`texture.to_numpy()`). `examples/` has complete scripts.

## Documentation

| | |
|---|---|
| [docs/install.md](https://github.com/mnvl/edge/blob/main/docs/install.md) | pip, Git submodules, Conan, vcpkg, installing from source |
| [docs/building.md](https://github.com/mnvl/edge/blob/main/docs/building.md) | Requirements, CMake options, the install tree, where shaders are found |
| [docs/architecture.md](https://github.com/mnvl/edge/blob/main/docs/architecture.md) | Conventions, the frame, backends, threads, design decisions |
| [docs/coordinates.md](https://github.com/mnvl/edge/blob/main/docs/coordinates.md) | World and clip space, matrix order, the viewport flip, winding, pixel formats |
| [docs/shaders.md](https://github.com/mnvl/edge/blob/main/docs/shaders.md) | Writing shaders against `edge.glsl` |
| [docs/render-targets.md](https://github.com/mnvl/edge/blob/main/docs/render-targets.md) | Targets of several colours, layers and cube faces |
| [docs/models.md](https://github.com/mnvl/edge/blob/main/docs/models.md) | Model types, `ModelBuilder`, the `.edge` format |
| [docs/python.md](https://github.com/mnvl/edge/blob/main/docs/python.md) | pyedge |
| [docs/tools.md](https://github.com/mnvl/edge/blob/main/docs/tools.md) | `edge_shot`, `edge_convert`, CMake helpers for a game |
| [docs/logging.md](https://github.com/mnvl/edge/blob/main/docs/logging.md) | The log and its levels |

The headers in `src/` document each class and function.

## License

MIT; see [LICENSE](https://github.com/mnvl/edge/blob/main/LICENSE.md). The libraries edge fetches when it is built are listed with their licenses in
[THIRD_PARTY.md](https://github.com/mnvl/edge/blob/main/THIRD_PARTY.md).
