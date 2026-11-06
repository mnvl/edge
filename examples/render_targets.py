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


def _create_checker_texture(renderer, color_a: int, color_b: int):
    texture = renderer.create_texture(4, 4, pyedge.PIXEL_FORMAT_A8R8G8B8)
    pixels: list[int] = []
    for y in range(4):
        for x in range(4):
            pixels.append(color_a if ((x + y) & 1) == 0 else color_b)
    texture.upload(struct.pack("<16I", *pixels))
    return texture


def _create_quad_buffers(renderer):
    vertex_stride = pyedge.vertex_stride(pyedge.VERTEX_FORMAT_PT)
    vertex_buffer = renderer.create_vertex_buffer(pyedge.VERTEX_FORMAT_PT, vertex_stride * 4)
    index_buffer = renderer.create_index_buffer(6 * 2)

    vertex_buffer.upload(
        struct.pack(
            "<20f",
            -1.0, -1.0, 0.0, 0.0, 1.0,
            1.0, -1.0, 0.0, 1.0, 1.0,
            1.0, 1.0, 0.0, 1.0, 0.0,
            -1.0, 1.0, 0.0, 0.0, 0.0,
        )
    )
    index_buffer.upload(struct.pack("<6H", 0, 1, 2, 0, 2, 3))
    return vertex_buffer, index_buffer


def _create_draw_call(renderer, camera, vertex_buffer, index_buffer, material):
    draw_call = renderer.create_draw_call()
    draw_call.set_always_visible(True)
    draw_call.camera = camera
    draw_call.material = material
    draw_call.vertex_buffer = vertex_buffer
    draw_call.index_buffer = index_buffer
    draw_call.triangles_count = 2
    return draw_call


def _make_world(pyrove, x: float, y: float, scale: float, angle: float):
    world = pyrove.mat4()
    world.identity()
    world.rotation(pyrove.vec3(0.0, 0.0, 1.0), angle)
    world.scale(scale, scale, 1.0)
    world.translate(x, y, 0.0)
    return world


def run(seconds: float, fps: float, backend: str) -> None:
    available_backends = set(pyedge.available_renderer_backends())

    renderer = pyedge.create_renderer(960, 600, backend=backend)
    print(f"Renderer backend: {renderer.backend}")
    if renderer.backend == "null":
        print("No GPU renderer available in this environment; running headless.")
        if "vulkan" in available_backends:
            print("Tip: run with --backend vulkan on a machine with Vulkan support.")

    camera = pyedge.Camera()
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

    vertex_buffer, index_buffer = _create_quad_buffers(renderer)

    source_textures = [
        _create_checker_texture(renderer, 0xFFFF4040, 0xFF661010),
        _create_checker_texture(renderer, 0xFF40FF40, 0xFF106610),
        _create_checker_texture(renderer, 0xFF4040FF, 0xFF101066),
    ]

    offscreen_targets = [
        renderer.create_render_target(256, 256, pyedge.PIXEL_FORMAT_A8R8G8B8),
        renderer.create_render_target(256, 256, pyedge.PIXEL_FORMAT_A8R8G8B8),
        renderer.create_render_target(256, 256, pyedge.PIXEL_FORMAT_A8R8G8B8),
    ]

    write_draw_calls = []
    present_draw_calls = []
    for i in range(3):
        write_material = renderer.create_material()
        write_material.cull_mode = pyedge.CullMode.NONE
        write_material.set_diffuse_texture(source_textures[i])

        present_material = renderer.create_material()
        present_material.cull_mode = pyedge.CullMode.NONE
        present_material.set_diffuse_texture(offscreen_targets[i].color_texture)

        write_draw_calls.append(
            _create_draw_call(
                renderer=renderer,
                camera=camera,
                vertex_buffer=vertex_buffer,
                index_buffer=index_buffer,
                material=write_material,
            )
        )
        present_draw_calls.append(
            _create_draw_call(
                renderer=renderer,
                camera=camera,
                vertex_buffer=vertex_buffer,
                index_buffer=index_buffer,
                material=present_material,
            )
        )

    clear_states = []
    for color in ((0.12, 0.04, 0.04), (0.04, 0.12, 0.04), (0.04, 0.04, 0.12)):
        clear_state = pyedge.RenderPassClearState()
        clear_state.clear_color_r = color[0]
        clear_state.clear_color_g = color[1]
        clear_state.clear_color_b = color[2]
        clear_states.append(clear_state)

    main_clear = pyedge.RenderPassClearState()
    main_clear.clear_color_r = 0.03
    main_clear.clear_color_g = 0.03
    main_clear.clear_color_b = 0.03

    present_x_positions = (-1.35, 0.0, 1.35)
    for i, draw_call in enumerate(present_draw_calls):
        draw_call.set_world_matrix(_make_world(pyrove, present_x_positions[i], 0.0, 0.55, 0.0))
        draw_call.commit()

    started_at = time.perf_counter()
    frame = 0
    print(
        f"Rendering for {seconds:.1f}s at {fps:.0f} FPS "
        "(3 offscreen targets -> main window)."
    )
    while True:
        now = time.perf_counter()
        elapsed = now - started_at
        if elapsed >= seconds:
            break

        for i, draw_call in enumerate(write_draw_calls):
            angle = elapsed * (1.5 + 0.35 * i) + (i * 0.35)
            draw_call.set_world_matrix(_make_world(pyrove, 0.0, 0.0, 0.85, angle))
            draw_call.commit()

        renderer.begin_frame()

        for i in range(3):
            renderer.begin_pass(offscreen_targets[i], clear_states[i])
            write_draw_calls[i].execute()
            renderer.end_pass()

        renderer.begin_pass(renderer.default_render_target, main_clear)
        for draw_call in present_draw_calls:
            draw_call.execute()
        renderer.end_pass()

        renderer.finish_frame()

        if frame % max(1, int(fps)) == 0:
            print(f"t={elapsed:4.1f}s frame={frame:4d}")

        frame += 1
        next_frame_at = started_at + (frame / fps)
        sleep_time = next_frame_at - time.perf_counter()
        if sleep_time > 0.0:
            time.sleep(sleep_time)

    print("Done.")


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Render one quad into 3 render targets and compose them in the main pass."
    )
    parser.add_argument(
        "--seconds",
        type=float,
        default=10.0,
        help="How long to run before exiting.",
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
