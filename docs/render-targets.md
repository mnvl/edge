# Render targets

A target is colour attachments and a depth (`RenderTargetFormat`).  A fragment shader writes attachment i at its output location i:

```cpp
edge::RenderTargetFormat format;
format.color_formats = {edge::PixelFormat::R16G16B16A16F, edge::PixelFormat::R32F};   // layout(location = 0) out vec4; location 1: float
format.readable_depth = true;
edge::RenderTargetPtr gbuffer = renderer->create_render_target(w, h, format);
gbuffer->color_texture(1);                          // a texture of its own, sampled by later passes, downloaded
```

No colour and a readable depth is a shadow map.  With `layers` (an array) or `cube` (six faces) a pass draws one layer, through the
target's `layer(i)`, and later passes sample the whole as an array or a cube:

```cpp
format.color_formats = {};
format.layers = 4;                                      // shadow cascades: sampler2DArrayShadow, set_global_shadow_texture
edge::RenderTargetPtr cascades = renderer->create_render_target(2048, 2048, format);
for (int i = 0; i < 4; ++i)
{
    renderer->begin_pass(cascades->layer(i));           // layer i drawn, the others kept
    ...
    renderer->end_pass();
}
```

A cube's face i is drawn with `camera->set_cube_face(eye, i, near, far)`, and sampled with `edge_cube.glsl`:

```glsl
vec3 seen = texture(cube, edge_cube_direction(direction_from_eye)).rgb;
float lit = texture(shadow_cube, vec4(edge_cube_direction(p), edge_cube_depth(p, z_near, z_far)));   // p from the light
```

Edge draws images top row first, while a cube's faces are laid out for images drawn bottom row first: the faces hold the world mirrored
in y (`set_cube_face(eye, 2, ...)` looks along -y), and `edge_cube_direction` mirrors the direction back.  `test/test_render_targets.cc`
holds each of these to the pixels on Vulkan and OpenGL ES.

