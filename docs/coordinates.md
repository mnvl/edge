# Coordinate system and conventions

## World space

| Axis | Direction |
|------|-----------|
| +X | right |
| +Y | up |
| +Z | forward (into the scene, away from viewer) |

The default camera sits at the origin looking toward `(0, 0, 1)` with `(0, 1, 0)` as up.
Visible objects have **positive view-space Z** values. This is a Z-forward convention (similar to Direct3D), distinct from standard OpenGL which uses a Z-backward camera (objects in front have negative view-space Z).

The table is **view** space. `Camera::look_at()` builds its view with `rove::matrix::lookat()`, whose right axis is forward × up: a mirror, so a right-handed world (glTF's, Maya's) is seen the way it was authored. Looking along world +Z with +Y up, world +X is on the **left** of the image; looking along −Z, on the right.

## Matrix storage and transform order

Matrices are stored **column-major** (`ij[column][row]`), matching GLSL `mat4` layout exactly. The vertex shader applies the combined world-view-projection matrix as a standard GLSL column-vector multiply:

```glsl
gl_Position = world_view_projection * vec4(in_position, 1.0);
```

On the CPU, transforms are composed in the same order (`rove::matrix`'s `a * b` applies `b` first; see `DrawCall::commit()`):

```
world_view_projection = projection  *  view  *  world
                        (view→clip) (world→view) (object→world)
```

## Clip space and NDC

After projection, the homogeneous divide yields normalised device coordinates (NDC):

| Axis | Range | Direction |
|------|-------|-----------|
| X | −1 … +1 | −1 = left, +1 = right |
| Y | −1 … +1 | −1 = bottom, +1 = top (**Y-up**) |
| Z | 0 … +1 | 0 = near plane, +1 = far plane |

The depth range [0, 1] matches Vulkan's default (not OpenGL's [−1, +1]).

## Viewport Y-flip

Vulkan's native screen-space convention is **Y-down** (NDC Y=+1 maps to the bottom of the framebuffer). Without correction the rendered image is upside-down relative to the Y-up NDC above.

The renderer compensates by setting a **negated viewport height**:

```cpp
viewport.y      = (float)height;   // start at bottom
viewport.height = -(float)height;  // extend upward
```

This is valid since Vulkan 1.1 (`VK_KHR_maintenance1`, core since 1.1).

**Effect on the framebuffer:**

```
NDC Y = +1  →  pixel row 0    (top of image)
NDC Y =  0  →  pixel row h/2
NDC Y = −1  →  pixel row h−1  (bottom of image)
```

Row 0 of the downloaded byte buffer is the **top** of the rendered scene.

## Winding order and culling

A negated-height viewport reverses the apparent winding order of triangles in screen space. The pipeline compensates by swapping the Vulkan front-face setting:

| `Material::cull_mode` | Meaning | CPU-side semantics | Vulkan `frontFace` (after Y-flip compensation) |
|-----------------------|---------|-------------------|-----------------------------------------------|
| `CULL_CCW` | Cull back faces | Front faces wound CCW in world space | `VK_FRONT_FACE_CLOCKWISE` |
| `CULL_CW` | Cull back faces (inside-out) | Front faces wound CW in world space | `VK_FRONT_FACE_COUNTER_CLOCKWISE` |
| `CULL_NONE` (default) | No culling | — | ignored |

From the user's perspective the names describe the **world-space winding** of front faces; the viewport flip is handled transparently.

## Pixel format (downloaded buffers)

Default render target pixel format is `X8R8G8B8` / `A8R8G8B8` — 32 bits per pixel, **BGRA byte order** in memory on little-endian platforms:

```
byte 0 = Blue
byte 1 = Green
byte 2 = Red
byte 3 = Alpha / unused (X)
```

`render_target.color_texture.download()` returns a `bytes` object in this layout.
Row stride = `width × 4`. Row 0 = top of image (see Y-flip above).

In C++ the same texels are an image whose pixel type says the order (`image.h`):

```cpp
edge::Image<rove::bgra<uint8_t>> frame = edge::read_image<rove::bgra<uint8_t>>(*target->color_texture());
uint8_t red = frame.at(x, y).r;                          // byte 2
texture->pixels<rove::bgra<uint8_t>>();                  // the CPU copy in place: no download, no copy
edge::load_image<rove::bgr<uint8_t>>("bar.png");         // a file, in the order asked for
```

The float formats keep red first (`rove::rgba<float>`, `rove::rgba<uint16_t>` for the
halves, `float` for R32F); a plain `rove::vec<4, T>` is four components that are no
colour. `pixels<P>()` of a texture whose format does not keep its texels as `P` throws.

## Texture coordinates

`(u, v) = (0, 0)` is the **top-left** corner of the image (Vulkan convention).
`(u, v) = (1, 1)` is the bottom-right corner.
Images loaded from disk via `stb_image` are stored top-row-first, matching this convention directly.

