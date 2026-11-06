#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2004-2026 Manvel Avetisian

from __future__ import annotations

import argparse
import math
import struct
import time

try:
    import pyedge
    import pyrove
except (ModuleNotFoundError, ImportError) as exc:
    raise RuntimeError(
        "pyedge package is not importable.\n"
        "Install it with pip (pip install pyedge, or pip install . in edge's checkout, which needs pyrove: pip install pyrove),\n"
        "or build the native bindings with CMake and put the checkout on PYTHONPATH:\n"
        "  cmake --build build --target pyedge"
    ) from exc


def run(seconds: float, fps: float, backend: str) -> None:
    available_backends = set(pyedge.available_renderer_backends())

    renderer = pyedge.create_renderer(800, 600, backend=backend)
    print(f"Renderer backend: {renderer.backend}")
    if renderer.backend == "null":
        print("No GPU renderer available in this environment; running headless.")
        if "vulkan" in available_backends:
            print("Tip: run with --backend vulkan on a machine with Vulkan support.")
    camera = pyedge.Camera()

    view = pyrove.mat4()
    view.identity()
    camera.view_matrix = view
    camera.set_perspective_projection(
        math.pi / 3.0,
        renderer.screen_width / float(renderer.screen_height),
        0.1,
        100.0,
    )
    camera.look_at(
        pyrove.vec3(0.0, 0.0, -4.0),
        pyrove.vec3(0.0, 0.0, 0.0),
        pyrove.vec3(0.0, 1.0, 0.0),
    )

    material = renderer.create_material()
    material.name = "hello_world_material"
    material.cull_mode = pyedge.CullMode.NONE

    texture = renderer.create_texture(2, 2, pyedge.PIXEL_FORMAT_A8R8G8B8)
    texture.upload(
        struct.pack(
            "<4I",
            0xFFFFFFFF,  # white
            0xFFFF5555,  # red-ish
            0xFF55FF55,  # green-ish
            0xFF5555FF,  # blue-ish
        )
    )
    material.set_diffuse_texture(texture)

    vertex_bytes = pyedge.vertex_stride(pyedge.VERTEX_FORMAT_PT) * 3
    vertex_buffer = renderer.create_vertex_buffer(pyedge.VERTEX_FORMAT_PT, vertex_bytes)
    index_buffer = renderer.create_index_buffer(6)
    vertex_buffer.upload(
        struct.pack(
            "<15f",
            -1.0, -1.0, 0.0, 0.0, 0.0,
            1.0, -1.0, 0.0, 1.0, 0.0,
            0.0, 1.0, 0.0, 0.5, 1.0,
        )
    )
    index_buffer.upload(struct.pack("<3H", 0, 1, 2))

    draw_call = renderer.create_draw_call()
    draw_call.set_always_visible(True)
    draw_call.camera = camera
    draw_call.material = material
    draw_call.vertex_buffer = vertex_buffer
    draw_call.index_buffer = index_buffer
    draw_call.triangles_count = 1

    scheduler = renderer.create_scheduler()

    axis = pyrove.vec3(0.0, 1.0, 0.0)
    started_at = time.perf_counter()
    frame = 0

    print(f"Running hello world for {seconds:.1f}s at {fps:.0f} FPS...")
    while True:
        now = time.perf_counter()
        elapsed = now - started_at
        if elapsed >= seconds:
            break

        angle = elapsed * 2.0
        x_offset = math.sin(elapsed * 1.5) * 0.75

        world = pyrove.mat4()
        world.identity()
        world.rotation(axis, angle)
        world.translate(x_offset, 0.0, 0.0)

        draw_call.set_world_matrix(world)
        draw_call.commit()

        renderer.begin_frame()
        renderer.begin_pass(renderer.default_render_target, pyedge.RenderPassClearState())
        scheduler.add(draw_call)
        scheduler.flush()
        renderer.end_pass()
        renderer.finish_frame()

        if frame % max(1, int(fps)) == 0:
            print(f"t={elapsed:4.1f}s angle={angle:5.2f} x={x_offset:5.2f}")

        frame += 1
        next_frame = started_at + (frame / fps)
        delay = next_frame - time.perf_counter()
        if delay > 0:
            time.sleep(delay)

    print("Done.")


def main() -> int:
    parser = argparse.ArgumentParser(description="Minimal pyedge hello-world animation.")
    parser.add_argument(
        "--seconds",
        type=float,
        default=10.0,
        help="How long to run the animation before exiting.",
    )
    parser.add_argument(
        "--fps",
        type=float,
        default=60.0,
        help="Target frames per second.",
    )
    parser.add_argument(
        "--backend",
        type=str,
        default="auto",
        help="Renderer backend: auto|vulkan|null",
    )
    args = parser.parse_args()

    run(seconds=max(0.1, args.seconds), fps=max(1.0, args.fps), backend=args.backend)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
