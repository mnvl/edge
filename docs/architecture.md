# Architecture

## Conventions

- **A struct is data, a class keeps something.** A `struct`'s members are all public, named plainly, given defaults, and read and written
  directly (`Joint`, `Animation`, `GenericVertex`, the `VERTEX_*`, `TextureUsage`, `RenderTargetFormat`, the `Serialized*` records, the
  options); it may have functions that work on its data where any values of the members are valid (`Joint::build_frame_transforms()`,
  `Animation::pose(t)`). A `class` keeps an invariant, owns a resource or keeps what is worked out from its data up to date: its data
  is private (protected where a derived class builds it), named with a trailing `_`, and reached through accessors. No class has public
  data.
- **Accessors: `x()` reads, `set_x(v)` sets.** A function says what it does, so a setter is a verb: `camera.view_matrix()`,
  `camera.set_view_matrix(m)` (rove's rule too: `bounds()`, `set_local_to_world(m)`). C++ has no properties, and a setter named as
  its getter (`x(v)`) reads as a query, cannot be taken by `&C::x` without a cast, and can be taken for the getter where both have
  arguments.
  - A getter has no `get_`, is `const`, and returns a value or a `const &`; an index or key is its argument (`diffuse_texture(i)`,
    `joint_by_name(n)`).
  - The getter of what has a setter is named as the setter without `set_` (`zwrite_enabled()` beside `set_zwrite_enabled(b)`, not
    `is_zwrite_enabled()`); a question that sets nothing is `is_x()` or `has_x()` (`is_visible()`, `has_previous()`).
  - A setter is `set_x(v)`, where the class keeps the value or what follows from it (`set_view_matrix` keeps the inverse and the eye,
    `set_occlusion_strength` checks its range); several values set together are one setter (`set_perspective_projection(fov, aspect,
    near, far)`). Its index, where it has one, goes where reads best (`set_extra_parameter(i, v)`, `set_diffuse_texture(t, i = 0)`).
  - What does more than keep a value is a verb, not a setter: `look_at(eye, at, up)`, `pose(t)`, `apply(t)`, `commit()`, `clear()`.
  - What is plain data, any value valid and nothing following from it, is not hidden behind a getter and a setter: it is a public
    member of a struct (above).
  - To change part of a class's data, read it, change the copy and set it
    (`auto groups = mesh->triangles(); groups[0].material_ptr = m; mesh->set_triangles(groups);`).
- **Names.** Types are `CamelCase`, functions and variables `snake_case`, constants and enumerators `UPPER_CASE`. Enums are
  `enum class`, but for the bits and slots that go to the shaders as numbers (`shader::Flag`, `shader::Binding`).
- **Templates say what they take** with concepts (`vertex_layout`, `skinned_vertex_layout`).
- **A resource is held by RAII**: a buffer's or a texture's bytes by a `Mapping` (`read()`, `write()`), never a lock to be paired with
  an unlock; a choice is an enum (`BufferUsage`, `Contents`), not a `bool` argument.
- **pyedge** follows the C++: what takes nothing and reads a value is a property, read-write where it can be set (`camera.view_matrix`,
  `camera.view_matrix = m`), and the setter is also the method `set_x(v)` of the C++ name, neither deprecated; the rest are methods of
  the C++ names (`material.diffuse_texture(i)`, `camera.look_at(eye, at, up)`). A name that is gone is kept, with a
  `DeprecationWarning` naming the new one (`pyedge::deprecated`), for a release.
- **New code** is held to the C++20 forms by `.clang-tidy`.

## Rendering Pipeline

```
renderer.begin_frame()
    model.draw(camera, world_matrix)      # queues draw calls on the model's scheduler
    renderer.begin_pass(target, clear)    # the target, cleared
    scheduler.flush()                     # sorted by priority, culled, executed
    renderer.end_pass()
renderer.submit_frame()                   # or finish_frame(), which also waits for the GPU
```

## Frames (Vulkan)

A frame is one command buffer.  `begin_frame()` starts it (waiting first for the oldest frame in flight, when as many as the
renderer keeps are in flight); passes and draws are recorded into it; `finish_frame()` submits it and waits until the GPU has drawn
it (what is read after, a download or the GPU's times, is that frame's); `submit_frame()` submits it without waiting, so the host
builds the next frame meanwhile.  `VulkanRenderer(..., frames_in_flight)` says how many frames may be in flight, 1 to 4; what the host
writes for a frame (draw constants, joint matrices, staged uploads) is kept apart for each.  A download waits for the frames it needs.

A renderer draws offscreen into render targets.  To show frames in a window it takes over a surface (`attach_surface(surface,
vsync)`, from `instance()`, or `attach_sdl_window()` in `edge_sdl`) and `present(texture, width, height)` copies a frame to it;
`FramePresenter` draws a UI over the frame in the same pass.  A surface lost to the window system (a phone's app sent to the
background) makes `present()` return false and `surface_lost()` true, until `detach_surface()` and the window's next surface.

## Renderer Backends

| Backend | Description |
|---------|-------------|
| `VulkanRenderer` | Vulkan 1.0 with `VK_KHR_maintenance1` (core in 1.1): desktop, Android, macOS (MoltenVK) |
| `GLESRenderer` | OpenGL ES 3.0 and WebGL 2 (Emscripten), `EDGE_GLES`; single-buffered; readable depth (no stencil texture: ES 3.0 cannot sample one); render targets of several colours, layers and cubes, but no array textures written from the CPU, and no stencil writes; `show_frame()` shows a frame in the host's window |
| `MetalRenderer` | Metal on Apple-silicon Macs and iOS 15 on, `EDGE_METAL`; two frames in flight (`submit_frame()` does not wait); the shaders are `shader_cross --msl` translations (`x.msl.map`, `x.<sdk>.metallib` beside `x.spv`); the same as GLES in what it leaves out (array textures written from the CPU, stencil writes), with a readable depth; BC where the device has it, ASTC; what the CPU writes is never put in a buffer or texture a frame not yet drawn uses (it is made again); an attribute a vertex lacks reads (0, 0, 0, 1); `show_frame()` draws into the host window's `CAMetalLayer` (`MetalSDLSurface`), `swap_window()` presents |
| `NullRenderer` | no GPU: keeps every rule of the others (passes, CPU copies) and draws nothing, for tests |

pyedge's `create_renderer(backend="auto")` is the GPU or an error (it no longer falls back to the null renderer, which renders black).

## Key Constraints and Gotchas

### Threads

Everything a renderer made -- its models and their instances, its `ModelCache`, the pose caches the instances of one prototype
share (`AnimatedMeshCache`), materials, textures, buffers, draw calls, schedulers -- is used by one thread at a time: none of it
locks. Two renderers share nothing (the log, which locks, read-only tables, and the process's Vulkan instance, which is made and let
go under a lock, aside), so threads that each drive a renderer of their own need no lock (`test/test_threads.cc`,
`test/test_vulkan_instance.cc`). pyedge is built free-threaded (no GIL on python3.14t) on that promise: a
program that hands one renderer's objects to two threads at once locks around them itself.

### Joint Matrix Initialization

`rove::matrix<4,4>` is the identity when made: a joint made with
`add_joint()` has an identity matrix until it is set.

### nanobind Shared Pointer Control Blocks

Python bindings use nanobind which creates **independent control blocks** for
`shared_ptr` objects. This means a `shared_ptr<Mesh>` returned to Python and then
passed back to C++ may have a different control block than the one stored internally.
`weak_ptr` created from such a shared_ptr will never lock successfully.

The fix (implemented in `attach_mesh_to_joint`): look up the mesh by raw pointer
in the internal `meshes` vector and assign the `weak_ptr` from that entry.

### Index Buffer Size

Models' meshes (`StaticMesh`, `AnimatedMesh`) index their vertices with 32-bit indices; `DynamicMesh` takes 16- or 32-bit ones, and
`create_index_array<uint16_t>` / `<uint32_t>` are both there.

### Frustum Culling

Models are frustum-culled by default using their oriented bounding box.
Set `model.always_visible = True` to disable culling (useful for debugging
or when the camera is inside the model's bounds).

## Design Decisions

### Renderers that share nothing

A renderer and everything it made belong to one thread at a time, and two renderers share nothing but the Vulkan instance (see
Threads), so a process can run several at once: a simulation renders many scenes in parallel, offscreen, on one GPU or several.  A game's renderer draws offscreen as well, into targets, and
presents a finished frame to a window's surface (Frames, above).

Image loading uses **stb_image/stb_image_write** and font rendering uses **stb_truetype**: single-header libraries without global
state, fetched at configure.

