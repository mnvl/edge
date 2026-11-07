# SPDX-License-Identifier: MIT
# Copyright (c) 2004-2026 Manvel Avetisian

"""A smoke test of an installed pyedge (a wheel's), run from outside the source tree:

    python wheel_smoke.py <model.glb> [vulkan|metal|null]

It says where the module and its shaders are, checks that a copy of the module without them refuses (the build it came from is
gone, so it names no directory of it), makes a renderer (Metal on a Mac, Vulkan where the machine has a device, lavapipe will do; else the null
one, or the backend asked for), loads the glTF, draws it once into a target and reads it back, and checks that pyrove's types cross
into pyedge's module and back (they are two modules, built apart, that share rove's types through nanobind)."""

import math
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np

import pyedge
import pyrove

SIZE = 96


def refuses_without_its_shaders(module: Path, lookup: str, variable: str) -> None:
    """A copy of the module with nothing beside it must say the shaders are not found, and what to set, naming no build directory."""
    with tempfile.TemporaryDirectory() as directory:
        shutil.copy2(module, directory)
        env = {k: v for k, v in os.environ.items() if k != variable}
        env.update(EDGE_PYTHON_BINDINGS_DIR=directory, ROVE_PYTHON_BINDINGS_DIR=str(Path(pyrove.pyrove_bind.__file__).parent))
        result = subprocess.run([sys.executable, "-c", f"import pyedge\nprint(pyedge.{lookup}())\n"], cwd=directory, env=env,
                                capture_output=True, text=True)
    if result.returncode == 0:
        raise SystemExit(f"a copy of the module without its shaders found them in {result.stdout.strip()}")
    if "are not found" not in result.stderr or variable not in result.stderr:
        raise SystemExit(f"a copy of the module without its shaders failed otherwise: {result.stderr}")
    if "the build's" in result.stderr:
        raise SystemExit(f"the module names the build it came from: {result.stderr}")


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
        if "metal" in backends:
            try:
                pyedge.create_renderer(SIZE, SIZE, backend="metal")
                backend = "metal"
            except RuntimeError as e:      # (no Metal device: a virtual machine with no GPU)
                print(f"no Metal device: {e}")
    if "vulkan" in backends:
        shaders = Path(pyedge.vulkan_shader_dir())
        print(f"vulkan shaders: {shaders}")
        if shaders != package / "shaders" / "edge":
            raise SystemExit(f"edge's shaders are not the package's: {shaders}")
        refuses_without_its_shaders(module, "vulkan_shader_dir", "EDGE_VK_SHADER_DIR")
    if "metal" in backends:
        shaders = Path(pyedge.metal_shader_dir())
        print(f"metal shaders: {shaders}")
        if shaders != package / "shaders" / "edge":
            raise SystemExit(f"edge's shaders are not the package's: {shaders}")
        refuses_without_its_shaders(module, "metal_shader_dir", "EDGE_METAL_SHADER_DIR")
    view = Path(pyedge.view_shader_dir())
    print(f"view shaders: {view}")
    if view != package / "view_shaders":
        raise SystemExit(f"the view shaders are not the package's: {view}")
    refuses_without_its_shaders(module, "view_shader_dir", "EDGE_VIEW_SHADER_DIR")

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
