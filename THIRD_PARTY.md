# Third-party software

edge's repository contains no third-party source code. At configure time CMake fetches the projects below at the pinned
versions (`FetchContent`), or finds them on the system. Their licenses apply to the builds that include them. The notices that a binary
carrying them must have are in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md), which goes in the wheel and in an install. A pyedge wheel holds
fastgltf, simdjson, cereal, stb, meshoptimizer, bc7enc_rdo, astc-encoder, xatlas and nanobind; it does not hold
Dear ImGui, SDL3, SPIRV-Cross or Boost.Test.

| Project | Used for | Pinned | License |
|---|---|---|---|
| [rove](https://github.com/mnvl/rove) | Math and geometry | sibling checkout or `find_package` | MIT |
| [fastgltf](https://github.com/spnda/fastgltf) | glTF parsing (with `etc/patches/`) | v0.9.0 | MIT |
| [simdjson](https://github.com/simdjson/simdjson) | fastgltf's JSON parser, fetched by fastgltf | fastgltf's pin | Apache-2.0 or MIT |
| [cereal](https://github.com/USCiLab/cereal) | `.edge` file serialization | v1.3.2 | BSD-3-Clause |
| [stb](https://github.com/nothings/stb) | Image decoding and writing, DXT compression, font rasterising | 5736b15 | MIT or public domain |
| [meshoptimizer](https://github.com/zeux/meshoptimizer) | Mesh simplification for levels of detail | v0.22 | MIT |
| [bc7enc_rdo](https://github.com/richgel999/bc7enc_rdo) | BC7 texture compression | b943862 | MIT or public domain |
| [astc-encoder](https://github.com/ARM-software/astc-encoder) | ASTC texture compression | 5.3.0 | Apache-2.0 |
| [xatlas](https://github.com/jpcy/xatlas) | Texture atlas packing | f700c77 | MIT |
| [SPIRV-Cross](https://github.com/KhronosGroup/SPIRV-Cross) | Shader translation for GLES and Metal (build-time tool) | vulkan-sdk-1.4.350.1 | Apache-2.0 |
| [Dear ImGui](https://github.com/ocornut/imgui) | the tests of `edge_imgui` (when edge is built by itself); not installed, a program uses its own | f1cc2ae | MIT |
| [nanobind](https://github.com/wjakob/nanobind) | Python bindings | v2.4.0 | BSD-3-Clause |
| [SDL3](https://github.com/libsdl-org/SDL) | `edge_sdl` (system package) | — | Zlib |
| [Vulkan headers and loader](https://github.com/KhronosGroup/Vulkan-Loader) | Vulkan backend (system package) | — | Apache-2.0 |
| [Boost.Test](https://www.boost.org/) | Tests only (system package) | ≥ 1.74 | BSL-1.0 |

The test assets in `test/data/` are made by the `make_test_*.py` scripts beside them, or were modelled for edge's tests.
