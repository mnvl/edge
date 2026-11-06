# SPDX-License-Identifier: MIT
# Copyright (c) 2004-2026 Manvel Avetisian

"""A smoke test of an installed pyedge (a wheel's), run from outside the source tree:

    python wheel_smoke.py <model.glb> [vulkan|null]

It says where the module and its shaders are, makes a renderer (Vulkan where the machine has a device, lavapipe will do; else the null
one, or the backend asked for), loads the glTF, draws it once into a target and reads it back, and checks that pyrove's types cross
into pyedge's module and back (they are two modules, built apart, that share rove's types through nanobind)."""

import math
import sys
from pathlib import Path

import numpy as np

import pyedge
import pyrove

SIZE = 96


def main() -> int:
    model_file = Path(sys.argv[1])
    module = Path(pyedge._bindings.__file__)
    print(f"pyedge {pyedge.__version__}: {module}")
    print(f"pyrove {pyrove.__version__}: {pyrove.pyrove_bind.__file__}")
    package = Path(pyedge.__file__).parent
    if module.parent != package:
        raise SystemExit(f"the module is not the package's: {module} (package {package})")

    backends = pyedge.available_renderer_backends()
    print(f"backends: {backends}")
    backend = sys.argv[2] if len(sys.argv) > 2 else None
    if backend is None:
        try:
            devices = pyedge.enumerate_physical_devices() if "vulkan" in backends else []
        except RuntimeError as e:          # (no Vulkan driver here: a build container)
            print(f"no Vulkan device: {e}")
            devices = []
        backend = "vulkan" if devices else "null"
    if "vulkan" in backends:
        shaders = Path(pyedge.vulkan_shader_dir())
        print(f"vulkan shaders: {shaders}")
        if shaders != package / "shaders" / "edge":
            raise SystemExit(f"edge's shaders are not the package's: {shaders}")
    view = Path(pyedge.view_shader_dir())
    print(f"view shaders: {view}")
    if view != package / "view_shaders":
        raise SystemExit(f"the view shaders are not the package's: {view}")

    renderer = pyedge.create_renderer(SIZE, SIZE, backend=backend)
    print(f"renderer: {renderer.backend}")
    scheduler = renderer.create_scheduler()
    model = pyedge.load_model(renderer, scheduler, str(model_file))
    print(f"model: {type(model).__name__}, {model.meshes_count} meshes")

    # pyrove's types into pyedge and back
    world = pyrove.mat4()
    world.identity()
    bounds = model.bounds(world)
    centre = (bounds.lo + bounds.hi) * 0.5
    radius = (bounds.hi - bounds.lo).length() * 0.5
    camera = pyedge.Camera()
    camera.set_perspective_projection(math.radians(50.0), 1.0, 0.1 * radius, 10.0 * radius)
    camera.look_at(centre + pyrove.vec3(0.0, -3.0 * radius, 0.5 * radius), centre, pyrove.vec3(0.0, 0.0, 1.0))
    view_matrix = camera.view_matrix
    if not isinstance(view_matrix, pyrove.mat4):
        raise SystemExit(f"Camera.view_matrix is not a pyrove.mat4: {type(view_matrix)}")

    target = renderer.create_render_target(SIZE, SIZE)
    clear = pyedge.RenderPassClearState()
    clear.clear_color_r = clear.clear_color_g = clear.clear_color_b = 0.0
    clear.clear_color_a = 1.0
    renderer.begin_frame()
    model.draw(camera, world)
    renderer.begin_pass(target, clear)
    scheduler.flush()
    renderer.end_pass()
    renderer.finish_frame()
    if renderer.backend != "null":
        pixels = np.frombuffer(target.color_texture.download(), np.uint8).reshape(SIZE, SIZE, 4)
        drawn = int(np.count_nonzero(pixels[..., :3].any(axis=-1)))
        print(f"drawn: {drawn} of {SIZE * SIZE} pixels")
        if drawn == 0:
            raise SystemExit("nothing was drawn")
    print("ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
