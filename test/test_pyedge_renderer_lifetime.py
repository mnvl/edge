# SPDX-License-Identifier: MIT
# Copyright (c) 2004-2026 Manvel Avetisian

"""Contract: a Vulkan resource may be released after the Renderer that made it.

Regression guard for a hard SIGABRT.  ``VulkanTexture`` and ``VulkanRenderTarget``
kept a raw ``VulkanRenderer *`` and called ``renderer_->get_device()`` from their
destructors.  Python owns those objects independently — nothing links a
``pyedge.Texture`` back to the ``pyedge.Renderer`` it came from — so whenever the
interpreter freed the renderer first, the surviving texture destroyed its image
with a dangling device handle and the Vulkan loader aborted the process:

    [Vulkan Loader] ERROR: vkUnmapMemory: Invalid device [VUID-vkUnmapMemory-device-parameter]
    Fatal Python error: Aborted
      edge::VulkanTexture::~VulkanTexture
      edge::VulkanRenderTarget::~VulkanRenderTarget

That killed the whole pytest process mid-test, so these cases have to run in
subprocesses and be judged by exit status: an in-process assertion never gets a
chance to run when the loader calls abort().
"""

import os
import subprocess
import sys
import tempfile
import textwrap
from pathlib import Path

import pytest

import pyedge


EDGE_ROOT = Path(__file__).resolve().parents[1]

# The renderer-lifetime bug is Vulkan-specific: the "null" backend allocates no
# device objects and cannot exercise it.  Fail (never skip) if this build has no
# Vulkan backend — that means the guard is not being tested at all.
assert "vulkan" in pyedge.available_renderer_backends(), (
    f"pyedge built without the Vulkan backend: {pyedge.available_renderer_backends()}"
)


def run_in_subprocess(body: str) -> subprocess.CompletedProcess:
    """Run `body` in a fresh interpreter that can import this pyedge build."""
    script = "import gc\nimport pyedge\n" + textwrap.dedent(body) + '\nprint("DONE")\n'

    env = dict(os.environ)
    # Hand the child exactly the paths this process resolved pyedge/pyrove from.
    env["PYTHONPATH"] = os.pathsep.join(p for p in sys.path if p)

    return subprocess.run(
        [sys.executable, "-c", script],
        # (not from the checkout, whose pyedge package would come before an installed one: the child imports what this process did)
        capture_output=True, text=True, cwd=tempfile.gettempdir(), env=env, timeout=300,
    )


def assert_clean_exit(result: subprocess.CompletedProcess) -> None:
    assert result.returncode == 0, (
        f"child exited with {result.returncode}\n"
        f"--- stdout ---\n{result.stdout}\n--- stderr ---\n{result.stderr}"
    )
    assert "DONE" in result.stdout, f"child never reached the end:\n{result.stdout}"
    # abort() is not the only failure mode — the loader logs the invalid handle
    # first, and a driver that tolerates it would otherwise pass silently.
    assert "Vulkan Loader] ERROR" not in result.stderr, result.stderr
    assert "Invalid device" not in result.stderr, result.stderr


def test_texture_released_after_renderer():
    result = run_in_subprocess("""
        renderer = pyedge.Renderer(64, 64, backend="vulkan")
        texture = renderer.create_texture(16, 16, pyedge.PIXEL_FORMAT_A8R8G8B8)

        del renderer
        gc.collect()

        del texture
        gc.collect()
    """)
    assert_clean_exit(result)


def test_render_target_released_after_renderer():
    result = run_in_subprocess("""
        renderer = pyedge.Renderer(64, 64, backend="vulkan")
        target = renderer.create_render_target(32, 32, pyedge.PIXEL_FORMAT_A8R8G8B8)

        del renderer
        gc.collect()

        del target
        gc.collect()
    """)
    assert_clean_exit(result)


def test_color_texture_released_after_its_render_target_and_renderer():
    """The texture is the last owner of the device here, not the render target."""
    result = run_in_subprocess("""
        renderer = pyedge.Renderer(64, 64, backend="vulkan")
        target = renderer.create_render_target(32, 32, pyedge.PIXEL_FORMAT_A8R8G8B8)
        color = target.color_texture

        del renderer
        del target
        gc.collect()

        assert color.width == 32
        del color
        gc.collect()
    """)
    assert_clean_exit(result)


def test_rendered_target_released_after_renderer():
    """Same ordering, but after a real frame has touched the target's images."""
    result = run_in_subprocess("""
        renderer = pyedge.Renderer(64, 64, backend="vulkan")
        target = renderer.create_render_target(32, 32, pyedge.PIXEL_FORMAT_A8R8G8B8)

        clear_state = pyedge.RenderPassClearState()
        clear_state.clear_color_r = 1.0
        renderer.begin_frame()
        renderer.begin_pass(target, clear_state)
        renderer.end_pass()
        renderer.finish_frame()

        color = target.color_texture
        color.download()
        assert len(color.read_pixels()) == 32 * 32 * 4

        del renderer
        gc.collect()
        del target
        del color
        gc.collect()
    """)
    assert_clean_exit(result)


def test_loaded_texture_released_after_renderer():
    """Model/material textures come from load_texture, not create_texture."""
    image_path = EDGE_ROOT / "test" / "data" / "test_stones.jpeg"
    assert image_path.exists(), f"missing test asset: {image_path}"

    result = run_in_subprocess(f"""
        renderer = pyedge.Renderer(64, 64, backend="vulkan")
        texture = renderer.load_texture({str(image_path)!r}, True)
        assert texture.width > 0

        del renderer
        gc.collect()

        del texture
        gc.collect()
    """)
    assert_clean_exit(result)


def test_texture_use_after_renderer_destroyed_raises():
    """Operations need the renderer, not just the device: report, do not crash."""
    result = run_in_subprocess("""
        renderer = pyedge.Renderer(64, 64, backend="vulkan")
        texture = renderer.create_texture(16, 16, pyedge.PIXEL_FORMAT_A8R8G8B8)

        del renderer
        gc.collect()

        try:
            texture.download()
        except Exception as exc:
            print("RAISED", type(exc).__name__, exc)
        else:
            raise AssertionError("download() after renderer destruction did not raise")

        del texture
        gc.collect()
    """)
    assert_clean_exit(result)
    assert "RAISED" in result.stdout, result.stdout


def test_normal_destruction_order_still_works_repeatedly():
    """Renderer-last teardown must keep working — and not leak a device per round.

    A device leak would only surface as exhaustion after many rounds, so this is
    a coarse guard: it proves repeated create/destroy cycles stay crash-free and
    that resources are not double-destroyed, not that nothing leaks.
    """
    result = run_in_subprocess("""
        for i in range(8):
            renderer = pyedge.Renderer(64, 64, backend="vulkan")
            texture = renderer.create_texture(16, 16, pyedge.PIXEL_FORMAT_A8R8G8B8)
            target = renderer.create_render_target(32, 32, pyedge.PIXEL_FORMAT_A8R8G8B8)

            renderer.begin_frame()
            renderer.begin_pass(target, pyedge.RenderPassClearState())
            renderer.end_pass()
            renderer.finish_frame()

            del texture
            del target
            gc.collect()
            del renderer
            gc.collect()
    """)
    assert_clean_exit(result)


@pytest.mark.parametrize("backend", ["null", "vulkan"])
def test_renderer_released_before_its_default_render_target(backend):
    """The renderer's own default target is handed out to Python too."""
    result = run_in_subprocess(f"""
        renderer = pyedge.Renderer(64, 64, backend="{backend}")
        target = renderer.default_render_target

        del renderer
        gc.collect()

        assert target.width == 64
        del target
        gc.collect()
    """)
    assert_clean_exit(result)
