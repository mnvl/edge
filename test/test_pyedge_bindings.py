# SPDX-License-Identifier: MIT
# Copyright (c) 2004-2026 Manvel Avetisian

from pathlib import Path

import importlib
import struct

import pytest


def _import_required(module_name: str):
    try:
        return importlib.import_module(module_name)
    except (ModuleNotFoundError, ImportError) as exc:
        raise RuntimeError(
            f"Required module '{module_name}' is not importable. "
            "Install pyrove and build bindings first:\n"
            "  pip install -e rove\n"
            "  cmake --build build --target pyedge"
        ) from exc


pyedge = _import_required("pyedge")
pyrove = _import_required("pyrove")


def test_package_modules():
    assert pyrove.__name__ == "pyrove"
    assert hasattr(pyedge, "Renderer")


def test_exports_and_constants():
    assert hasattr(pyedge, "Renderer")
    assert hasattr(pyedge, "Camera")
    assert hasattr(pyedge, "ShaderProgram")
    assert hasattr(pyedge, "Material")
    assert hasattr(pyedge, "Texture")
    assert hasattr(pyedge, "RenderTarget")
    assert hasattr(pyedge, "RenderPassClearState")
    assert hasattr(pyedge, "DrawCall")
    assert hasattr(pyedge, "Scheduler")
    assert hasattr(pyedge, "StaticModelPNT")
    assert hasattr(pyedge, "StaticModelPTBNT")
    assert hasattr(pyedge, "AnimatedModelPNJWT")
    assert hasattr(pyedge, "AnimatedModelPTBNJWT")
    assert hasattr(pyedge, "ModelBuilder")
    assert hasattr(pyedge, "MeshBuilder")
    assert hasattr(pyedge, "create_renderer")
    assert "null" in pyedge.available_renderer_backends()

    assert pyedge.PIXEL_FORMAT_X8R8G8B8 == 1
    assert pyedge.PIXEL_FORMAT_A8R8G8B8 == 2
    assert pyedge.PIXEL_FORMAT_R16G16B16A16F == 3
    assert pyedge.PIXEL_FORMAT_R32G32B32A32F == 4
    assert pyedge.PIXEL_FORMAT_DEFAULT == pyedge.PIXEL_FORMAT_X8R8G8B8
    assert pyedge.INDEX_FORMAT_U16 == 101
    # the formats are enums that are numbers too; the names before 2026-11 are the numbers, and the enums' old names their values
    assert pyedge.PixelFormat.R32F == pyedge.PIXEL_FORMAT_R32F == 7
    assert pyedge.IndexFormat.U32 == pyedge.INDEX_FORMAT_U32 == 102
    assert pyedge.CullMode.CULL_NONE is pyedge.CullMode.NONE
    assert pyedge.BlendMode.BLEND_INV_SRC_ALPHA is pyedge.BlendMode.INV_SRC_ALPHA

    assert pyedge.vertex_stride(pyedge.VERTEX_FORMAT_PT) > 0
    with pytest.raises(RuntimeError):
        pyedge.vertex_stride(0xFFFF_FFFF)


def test_create_renderer_accepts_offscreen_argument():
    renderer = pyedge.create_renderer(backend="null", offscreen=True)
    assert renderer.backend == "null"


def test_a_renderer_is_offscreen():
    # there is no window to draw into: asking for one is an error, not a renderer that ignores the request
    assert pyedge.create_renderer(backend="null").backend == "null"
    with pytest.raises(ValueError):
        pyedge.create_renderer(backend="null", offscreen=False)


def test_auto_is_the_gpu_and_a_gpu_that_cannot_be_had_is_an_error():
    # not the null renderer, which draws nothing: the images it gives are black and nothing says why
    assert "auto" in pyedge.available_renderer_backends()
    assert pyedge.create_renderer(64, 64, backend="auto").backend == "vulkan"
    import os
    import subprocess
    import sys
    env = dict(os.environ, VK_DRIVER_FILES="/nonexistent/icd.json", VK_ICD_FILENAMES="/nonexistent/icd.json")
    probe = "import pyedge\ntry:\n    r = pyedge.create_renderer(64, 64, backend='auto')\n    print('made', r.backend)\nexcept Exception as e:\n    print('refused', type(e).__name__)\n"
    result = subprocess.run([sys.executable, "-c", probe], env=env, capture_output=True, text=True, timeout=120)
    assert result.stdout.strip().startswith("refused"), result.stdout + result.stderr


def test_camera_accepts_rove_math_types():
    camera = pyedge.Camera()

    view = pyrove.mat4()
    view.identity()
    camera.view_matrix = view

    eye = pyrove.vec3(0.0, 0.0, 0.0)
    at = pyrove.vec3(0.0, 0.0, 1.0)
    up = pyrove.vec3(0.0, 1.0, 0.0)
    camera.look_at(eye, at, up)
    assert not hasattr(camera, "set_translation_rotation")

    projection = camera.projection_matrix
    assert type(projection).__name__ == "mat4"

    eye_point = camera.eye_point
    assert type(eye_point).__name__ == "vec3"


def test_renderer_resource_creation_and_material_setup():
    renderer = pyedge.Renderer(320, 200, backend="null")
    assert renderer.backend == "null"

    assert renderer.screen_width == 320
    assert renderer.screen_height == 200

    texture = renderer.create_texture(8, 8, pyedge.PIXEL_FORMAT_A8R8G8B8)
    texture.upload(b"\xFF" * (8 * 8 * 4))
    assert texture.width == 8
    assert texture.height == 8
    assert texture.pixel_format == pyedge.PIXEL_FORMAT_A8R8G8B8

    material = renderer.create_material()
    material.name = "mat_a"
    material.cull_mode = pyedge.CullMode.NONE
    material.src_blend_mode = pyedge.BlendMode.ONE
    material.dest_blend_mode = pyedge.BlendMode.ZERO
    material.zwrite_enabled = True
    material.alpha_blend_enabled = False
    program = renderer.create_shader_program("vertex.spv", "fragment.spv")
    material.set_shader_program(program)
    material.set_extra_parameter(0, pyrove.vec4(7.0, 0.0, 0.0, 0.0))
    material.set_diffuse_texture(texture)

    assert material.name == "mat_a"
    assert material.diffuse_textures_count == 1
    assert material.diffuse_texture(0).width == 8
    assert material.shader_program.fragment_shader_filename == "fragment.spv"
    assert material.extra_parameter(0).x == pytest.approx(7.0)
    material.clear_shader_program()
    assert material.shader_program is None


def test_render_target_pass_api_and_clear_state():
    renderer = pyedge.Renderer(320, 200, backend="null")

    default_target = renderer.default_render_target
    assert default_target.width == 320
    assert default_target.height == 200

    offscreen_target = renderer.create_render_target(64, 48, pyedge.PIXEL_FORMAT_A8R8G8B8)
    assert offscreen_target.width == 64
    assert offscreen_target.height == 48
    assert offscreen_target.color_texture.width == 64
    assert offscreen_target.color_texture.height == 48

    clear_state = pyedge.RenderPassClearState()
    assert clear_state.clear_color is True
    assert clear_state.clear_depth is True
    assert clear_state.clear_depth_value == pytest.approx(1.0)
    assert clear_state.clear_stencil == 0

    clear_state.clear_color_r = 0.25
    clear_state.clear_color_g = 0.5
    clear_state.clear_color_b = 0.75
    clear_state.clear_color_a = 1.0
    clear_state.clear_depth_value = 0.6
    clear_state.clear_stencil = 3

    renderer.begin_frame()
    renderer.begin_pass(offscreen_target, clear_state)
    renderer.end_pass()
    renderer.begin_pass(default_target)
    renderer.end_pass()
    renderer.finish_frame()


def test_draw_call_commit_and_scheduler_flush():
    renderer = pyedge.Renderer(backend="null")
    camera = pyedge.Camera()
    view = pyrove.mat4()
    view.identity()
    camera.view_matrix = view

    world = pyrove.mat4()
    world.identity()

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
    material = renderer.create_material()

    draw_call = renderer.create_draw_call()
    draw_call.set_always_visible(True)
    draw_call.set_world_matrix(world)
    draw_call.camera = camera
    draw_call.material = material
    draw_call.vertex_buffer = vertex_buffer
    draw_call.index_buffer = index_buffer
    draw_call.triangles_count = 1
    draw_call.commit()

    scheduler = renderer.create_scheduler()
    scheduler.add(draw_call)
    renderer.begin_frame()
    renderer.begin_pass(renderer.default_render_target)
    scheduler.flush()
    renderer.end_pass()
    renderer.finish_frame()

    assert scheduler.visible_calls_count == 1
    assert scheduler.invisible_calls_count == 0


def test_raw_upload_bounds_validation():
    renderer = pyedge.Renderer(backend="null")

    vb = renderer.create_vertex_buffer(pyedge.VERTEX_FORMAT_PT, 16)
    with pytest.raises(RuntimeError):
        vb.upload(b"\x00" * 17)

    ib = renderer.create_index_buffer(6)
    with pytest.raises(RuntimeError):
        ib.upload(b"\x00" * 7)

    tex = renderer.create_texture(2, 2, pyedge.PIXEL_FORMAT_A8R8G8B8)
    with pytest.raises(RuntimeError):
        tex.upload(b"\x00" * 17)


def test_texture_save_to_file(tmp_path: Path):
    renderer = pyedge.create_renderer(backend="null")
    texture = renderer.create_texture(4, 4)

    out_file = tmp_path / "texture.bmp"
    texture.save_to_file(str(out_file))

    assert out_file.exists()
    assert out_file.stat().st_size > 0


def test_texture_download_roundtrip_and_render_target_color_texture():
    renderer = pyedge.Renderer(backend="null")

    texture = renderer.create_texture(2, 2, pyedge.PIXEL_FORMAT_A8R8G8B8)
    expected = struct.pack("<4I", 0xFF0000FF, 0xFF00FF00, 0xFFFF0000, 0xFFFFFFFF)
    texture.upload(expected)
    assert texture.download() == expected

    target = renderer.create_render_target(2, 2, pyedge.PIXEL_FORMAT_A8R8G8B8)
    target_expected = struct.pack("<4I", 0xFF112233, 0xFF445566, 0xFF778899, 0xFFAABBCC)
    target.color_texture.upload(target_expected)
    assert target.color_texture.download() == target_expected


def test_float_texture_and_render_target_roundtrip():
    renderer = pyedge.Renderer(backend="null")

    texture = renderer.create_texture(2, 1, pyedge.PIXEL_FORMAT_R32G32B32A32F)
    expected = struct.pack(
        "<8f",
        0.1, 0.2, 0.3, 0.4,
        1.0, 0.75, 0.5, 0.25,
    )
    texture.upload(expected)
    assert texture.download() == expected

    target = renderer.create_render_target(2, 1, pyedge.PIXEL_FORMAT_R32G32B32A32F)
    target.color_texture.upload(expected)
    assert target.color_texture.download() == expected


# --- which interpreter the binding was built for ------------------------------

def _expected_tag():
    import sys
    import sysconfig
    free_threaded = "t" if sysconfig.get_config_var("Py_GIL_DISABLED") else ""
    return f"cpython-{sys.version_info.major}{sys.version_info.minor}{free_threaded}"


def test_the_binding_is_built_for_this_interpreters_abi():
    """A free-threaded interpreter shares its cache_tag with the GIL build
    of the same version; only the extension's tag tells them apart."""
    import pyedge
    assert pyedge._running_python_tag() == _expected_tag()
    assert pyedge._guess_python_tags([Path(pyedge._bindings.__file__)]) == [_expected_tag()]


def test_a_free_threaded_build_is_told_apart_from_a_gil_build():
    import pyedge
    built = [Path("_pyedge.cpython-314t-darwin.so"), Path("_pyedge.cpython-314-x86_64-linux-gnu.so")]
    assert pyedge._guess_python_tags(built) == ["cpython-314", "cpython-314t"]
