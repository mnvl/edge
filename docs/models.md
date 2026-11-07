# Models and the .edge format

## Model Types

| Class | Vertex Format | Use Case |
|-------|--------------|----------|
| `StaticModelPNT` | Position, Normal, TexCoords | Static geometry without normal maps |
| `StaticModelPTBNT` | Position, Tangent, Binormal, Normal, TexCoords | Static geometry with normal maps |
| `StaticModelPTBNTC` | + Color (glTF `COLOR_0`, a vec4 at shader location 10) | For a material's own shader that reads vertex colors |
| `AnimatedModelPNJWT` | + JointIndices, JointWeights | Skeletal animation |
| `AnimatedModelPTBNJWT` | + Tangent, Binormal, JointIndices, JointWeights | Skeletal animation with normal maps |
| `ModelBuilder` | any | Programmatic construction (CPU-side only) |

## ModelBuilder Workflow

ModelBuilder constructs geometry from numpy arrays but **cannot render directly**.
The workflow is: build -> save to `.edge` -> load with a StaticModel/AnimatedModel for rendering.

```python
import pyedge
import numpy as np

renderer = pyedge.create_renderer(64, 64, backend="auto", offscreen=True)
scheduler = renderer.create_scheduler()

# Build
builder = pyedge.ModelBuilder(renderer, scheduler)
material = renderer.create_material()
material.name = "floor"
material.set_diffuse_texture("textures/floor.png")

mesh = builder.add_mesh_builder("floor", "floor")
mesh.set_vertices_numpy(
    positions,                     # [N, 3] float32
    normals=normals,               # [N, 3] float32
    texcoords=texcoords,           # [N, 2] float32
    tangents=tangents,             # [N, 3] float32  (for PTBNT)
    binormals=binormals,           # [N, 3] float32  (for PTBNT)
)
mesh.set_indices_numpy(indices)    # [M] uint32  or  [T, 3] uint16/uint32
mesh.set_default_triangles(material)
builder.attach_mesh_to_joint(mesh)

builder.save_to_file("scene.edge")

# Render
model = pyedge.StaticModelPTBNT(renderer, scheduler)
model.load_from_file("scene.edge")
model.check()
model.always_visible = True

camera = pyedge.Camera()
camera.set_perspective_projection(fov, aspect, z_near, z_far)
camera.look_at(eye, target, up)

world = pyrove.mat4()
world.identity()

rt = renderer.create_render_target(width, height)
clear = pyedge.RenderPassClearState()

renderer.begin_frame()
model.draw(camera, world)
renderer.begin_pass(rt, clear)
scheduler.flush()
renderer.end_pass()
renderer.finish_frame()

pixels = rt.color_texture.download()  # bytes, BGRX format
```

## .edge File Format

Binary format using cereal serialization (the structures of `src/serialized_model.h`, laid out by `src/serialized_model_archive.h`,
which is edge's own: a program reads and writes a file with `Model::read_file()` and `Model::write_file()`). Versions
`MODEL_BINARY_OLDEST_VERSION` (10, whose materials have no occlusion) to `MODEL_BINARY_VERSION` (11, which is written): a file of another
version is refused with a message to convert it again from its glTF. So is a damaged
one: a length the rest of the file has no room for is refused before anything that long is allocated. Stores everything a model is
except what the renderer owns, and what made it:
- The pipeline (`edge::pipeline_hash()`, which `edge_convert --version` says) and the converter's options (none when the model saved itself)
- Textures, each once however many materials use it: size, pixel format, sRGB, mipmaps, the mip levels a DDS brought, and every texel
- Materials: every setting (cull and blend state, extra parameters, base colour, metallic, roughness, emissive, alpha cutoff, normal
  scale, occlusion and its strength) and their textures (diffuse, blend weights, normal, metallic-roughness with occlusion in its red,
  emissive)
- Meshes: vertex and index data (a vertex as the components of the mesh's vertex type and no more, packed), morph targets and weights, triangle groups with material indices, skin (inverse bind matrices), custom attributes (any glTF vertex attribute whose name starts with an underscore, kept as floats for the application), and for a
  skinned mesh its pose box
- Joints: hierarchy (parent and the order of the children), matrices, transforms
- Animations: the clips, joint samplers and morph weight samplers

**Not stored**: shader programs, GPU resources. Whether the CPU keeps a copy of the texels is the loader's choice
(`load_from_file(filename, keep_cpu_copy)`; `ModelCache` takes it from its `GltfOptions`).

**`edge_convert`** (`tools/edge_convert.cc`, built with edge) makes the file from a glTF:
`edge_convert [--srgb-color] [--mipmaps] [--no-compress] [--astc] [--pose-margin FRACTION] [--lods RATIO,...] [--lod-min-triangles N] [--lod-lock-border] [--tile METRES] [--atlas none|grid|xatlas] [--atlas-size N] [--atlas-keep MATERIAL]... [--no-emissive] [--no-occlusion] in.glb out.edge` (`--inputs`, `--version` for a build). A glTF with a skin becomes
an `AnimatedModelPTBNJWT` file, one with vertex colors (`COLOR_0`) a `StaticModelPTBNTC` one, any other a `StaticModelPTBNT` one (a
skinned one with vertex colors drops them with a warning); the file is written whole or not at all, and the same input gives the
same bytes. Its textures are made BC7 with the whole mip chain (`src/model_textures.h`): colour (diffuse, emissive) is encoded for the eye and, with
`--srgb-color`, averaged into its mips in linear light, each texel weighed by its coverage (alpha: a clear texel lends no colour, an area all clear keeps its plain mean); normal maps are averaged as vectors; metallic-roughness and blend weights as numbers; a
texture a glTF brings as DDS is kept as it is. `--no-compress` keeps the 8-bit texels instead (`--mipmaps` then has the GPU make the mips). `convert_gltf_to_edge()` (`src/model_convert.h`) is the same from C++.

**Atlases** (`--atlas none|grid|xatlas`, `--atlas-size N`; `src/model_atlas.h`): a model from a stock site has a texture set for each of its materials, and
a draw call each. An atlas makes them one set (colour, metallic-roughness, normal map, emissive: each role its own texture, a material with none in a role
keeps none, and keeps its own factors, which only multiply its own part). `--atlas-size` is the most texels along a side; the atlas is as small as
holds the materials (a power-of-two square or half of one for `grid`, a multiple of 4 for `xatlas`) and they are made smaller together to fit.
- `grid`: a square cell for each set of textures, as large as a power of two nearest the side of the largest texture, packed along the Z-order curve, with
  a gutter of the texture's own edge texels. The texture coordinates move into the cell. A material whose coordinates leave 0..1 (a texture that tiles)
  is left with its own textures, and a warning.
- `xatlas`: the meshes are unwrapped anew (xatlas, fetched; vertices are split at the seams, everything else a vertex carries follows) and each
  triangle's texels are drawn into the atlas from its material's textures, at the density the textures had: no more texels than a texture has, so a
  tiling texture is flattened to its own size (use `none` for a ground), filtered down by mip level, with the chart edges drawn out 4 texels.
- Left alone in both: materials with a texture that is block-compressed already, with more than one diffuse texture or with blend weights. Nothing is
  done for fewer than two different sets of textures. A model whose meshes have morph targets that move texture coordinates, a vertex that two
  packed materials share (`grid`), or levels of detail already is refused. The atlas is made before the levels of detail.
- `--no-emissive` leaves out the light of the materials (no emissive texture, no emissive colour). Occlusion is in the red of the
  metallic-roughness texture (glTF's ORM), which the atlas carries as numbers; `--no-occlusion` leaves it out (`GltfOptions::occlusion`).

**The pose box.** A skinned mesh is culled against a box in model space: that of the pose it is in. In a cached pose
(`Model::set_cached_pose(clip, time)`), the pose's own box, shared by every instance in it; posed by hand (joints set and rebuilt, a clip's time
set: `Model::posed_by_hand()`), `AnimatedMeshCache::bounds_in()` of its palette -- each joint's box of the vertices it pulls, through its
matrix -- worked out once per pose, which holds any pose however it was made. Before it is posed, the union of its vertices in the bind
pose and at 60 samples a second of every clip, grown by half the farthest a vertex moves between two samples and by
`GltfOptions::pose_margin` of its size on each side (`BasicAnimatedMesh::set_pose_bounds(box)` sets that box).

