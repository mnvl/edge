# Writing shaders

A shader program of your own (`renderer.create_shader_program(vert_spv, frag_spv)`)
includes `src/edge.glsl`, the binding contract: the descriptor bindings
(`EDGE_BINDING_*`), the push constants `pc` with their flags (`EDGE_FLAG_*`) and
the draw constants `dc`. Textures and the frame constants are declared by the
shader, at the header's bindings, under its own names:

```glsl
#version 450
#extension GL_GOOGLE_include_directive : require
#include "edge.glsl"

layout(set = 0, binding = EDGE_BINDING_GLOBAL_TEXTURE(1)) uniform sampler2D shadow_map;
layout(set = 0, binding = EDGE_BINDING_FRAME_CONSTANTS) uniform Frame { vec4 sun; } frame;
```

A vertex shader that draws instances includes `edge_instance.glsl` after `edge.glsl` and places a vertex by
`edge_instance_matrix()` (the identity outside an instanced draw):

```glsl
#include "edge_instance.glsl"
gl_Position = dc.world_view_projection * (edge_instance_matrix() * vec4(in_position, 1.0));
```

A shader that samples a cube render target includes `edge_cube.glsl` and samples it by `edge_cube_direction()` (Render Targets, below).

Compile with `glslangValidator -V -I<edge>/src`; from CMake, `EDGE_SHADER_INCLUDE_DIR`
is that directory. `test_frame_resources.cc` (suite `vulkan_edge_glsl`) checks that every
name in the header reaches what the C++ API puts there.

