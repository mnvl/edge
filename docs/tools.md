# Tools and CMake helpers

## `edge_shot`

`tools/edge_shot.cc` (`EDGE_SHOT`, on where edge is built by itself and has Vulkan or Metal; a parent asks for it) draws a model (.edge, .glb, .gltf), framed whole and lit by `tools/shaders/lit.frag`:

```bash
edge_shot model.glb shot.png                              # one shot: --size WxH --yaw --pitch --fov --background R,G,B
edge_shot --animation Walk --time 0.4 model.glb shot.png  # posed by a clip, by name or number
edge_shot --sheet sheet.png a.glb b.glb c.glb             # a contact sheet: a cell for each (--cell WxH --columns N)
edge_shot --sheet turn.png --views 8 model.glb            # one model in 8 views turned round (a row each model, a column each view)
edge_shot --gui model.glb                                 # a window (where edge_sdl is): drag turns, shift+drag pans, wheel zooms, F, space, Tab
```

`--renderer metal|vulkan` chooses the renderer (the first this build has; `edge_shot --version` lists them).  It exits 1 with a message naming
what is wrong (a model it cannot read, an option out of range), writing a file whole or not at all.  Its test is the ctest test `edge_shot_py`
(`test/test_edge_shot.py`: the PNG judged as a picture, a sheet as the shots it holds, what it refuses; the window opens, draws three frames and
closes).

## CMake helpers for a game

Once edge is added (`add_subdirectory(edge)`), a game's CMakeLists has these (`etc/cmake`, each file documents its own):

```cmake
edge_add_shaders(game_shaders DESTINATION ${BIN}/shaders SOURCES src/sea.vert src/sea.frag)   # against edge.glsl
edge_bundle_runtime(game_runtime DESTINATION ${BIN})        # edge's shaders, MoltenVK on macOS; edge::use_bundled_runtime(BIN) at run time
edge_convert_models(game_models SOURCE_DIR ${PROJECT_SOURCE_DIR}/assets/models DESTINATION ${BIN}/models OPTIONS --srgb-color)
```

