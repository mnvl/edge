# SPDX-License-Identifier: MIT
# Copyright (c) 2004-2026 Manvel Avetisian

"""edge_shot (tools/edge_shot.cc), judged as a program: the PNG it writes for a model, a contact sheet of several, and what it refuses.

ctest passes the tools through the environment: EDGE_SHOT (the program), EDGE_CONVERT (to make a .edge file), EDGE_ROOT (test/data is its),
EDGE_SHOT_GUI ("1" when edge was built with edge_sdl, which has --gui; "0" when not, and --gui is then refused saying so).
"""
import os
import subprocess
from pathlib import Path

import numpy as np
import pytest
from PIL import Image

SHOT = os.environ["EDGE_SHOT"]
CONVERT = os.environ["EDGE_CONVERT"]
DATA = Path(os.environ["EDGE_ROOT"]) / "test" / "data"
HAS_GUI = {"1": True, "0": False}[os.environ["EDGE_SHOT_GUI"]]
BACKGROUND = (40, 40, 48)

PBR = DATA / "test_gltf_loader_pbr.glb"
CONE = DATA / "cone.glb"
FIGURE = DATA / "test_figure.glb"       # skinned, animations Dance, Idle, Walk (made by data/make_test_figure.py)
VERTEX_COLORS = DATA / "test_vertex_colors.glb"


def shot(*args, check=True, timeout=120):
    result = subprocess.run([SHOT, *map(str, args)], capture_output=True, text=True, timeout=timeout)
    if check:
        assert result.returncode == 0, f"edge_shot {args}: {result.stderr}"
    return result


def pixels(path) -> np.ndarray:
    image = Image.open(path)
    assert image.format == "PNG"
    assert image.mode == "RGB", f"{path}: {image.mode}"          # no alpha: a picture to look at, over its background
    return np.asarray(image)


def drawn(frame: np.ndarray, background=BACKGROUND) -> np.ndarray:
    return np.any(frame != np.array(background, np.uint8), axis=-1)


def box(mask: np.ndarray) -> tuple[int, int, int, int]:
    ys, xs = np.nonzero(mask)
    return xs.min(), ys.min(), xs.max(), ys.max()


@pytest.fixture
def out(tmp_path):
    return tmp_path / "out.png"


# ---- one shot ------------------------------------------------------------------------------------------------------

@pytest.mark.parametrize("model", [PBR, CONE, FIGURE, VERTEX_COLORS], ids=lambda p: p.name)
def test_a_shot_frames_the_whole_model_in_the_middle_of_the_picture(model, out):
    shot("--size", "320x200", model, out)
    frame = pixels(out)
    assert frame.shape == (200, 320, 3)
    mask = drawn(frame)
    assert mask.sum() > 0.02 * mask.size, "the model is a speck or not drawn"
    x0, y0, x1, y1 = box(mask)
    # all of it is in the picture (not cut by its edge) ...
    assert x0 > 0 and y0 > 0 and x1 < 319 and y1 < 199, (x0, y0, x1, y1)
    # ... and the picture is not mostly empty around it: the box of the model fills most of one dimension at least
    assert max((x1 - x0 + 1) / 320, (y1 - y0 + 1) / 200) > 0.5, (x0, y0, x1, y1)
    # centred (a model is framed by the middle of its bounds, so the box of what is drawn is near the middle)
    assert abs((x0 + x1) / 2 - 160) < 0.15 * 320 and abs((y0 + y1) / 2 - 100) < 0.15 * 200, (x0, y0, x1, y1)


def test_a_shot_is_the_same_picture_every_time(out, tmp_path):
    again = tmp_path / "again.png"
    shot("--size", "200x200", PBR, out)
    shot("--size", "200x200", PBR, again)
    assert out.read_bytes() == again.read_bytes()


def test_the_default_size_is_square_and_the_background_is_the_viewers(out):
    shot(CONE, out)
    frame = pixels(out)
    assert frame.shape == (512, 512, 3)
    assert tuple(frame[0, 0]) == BACKGROUND and tuple(frame[-1, -1]) == BACKGROUND


def test_the_background_colour_is_the_one_given_exactly(out):
    shot("--size", "64x64", "--background", "250,10,7", CONE, out)
    frame = pixels(out)
    assert tuple(frame[0, 0]) == (250, 10, 7) and tuple(frame[-1, 0]) == (250, 10, 7)
    assert drawn(frame, (250, 10, 7)).sum() > 0


def test_the_view_turns_with_yaw_and_pitch_and_a_full_turn_is_the_same_view(out, tmp_path):
    base, turned, tilted, full = (tmp_path / n for n in ("base.png", "turned.png", "tilted.png", "full.png"))
    shot("--size", "160x160", "--yaw", "0", FIGURE, base)
    shot("--size", "160x160", "--yaw", "90", FIGURE, turned)
    shot("--size", "160x160", "--yaw", "0", "--pitch", "60", FIGURE, tilted)
    shot("--size", "160x160", "--yaw", "360", FIGURE, full)
    a, b, c, d = (pixels(p).astype(int) for p in (base, turned, tilted, full))
    assert np.abs(a - b).mean() > 1.0, "a quarter turn does not change the picture"
    assert np.abs(a - c).mean() > 1.0, "a tilt does not change the picture"
    assert np.abs(a - d).max() <= 2, "a full turn is another view"


def test_the_field_of_view_sets_how_much_of_the_picture_the_model_fills(out, tmp_path):
    # the camera is placed to frame the model whatever the lens: a narrow lens stands further away and the model fills as much
    narrow = tmp_path / "narrow.png"
    shot("--size", "200x200", "--fov", "20", CONE, narrow)
    shot("--size", "200x200", "--fov", "80", CONE, out)
    for path in (narrow, out):
        x0, y0, x1, y1 = box(drawn(pixels(path)))
        assert x0 > 0 and y0 > 0 and x1 < 199 and y1 < 199, (path.name, x0, y0, x1, y1)
    assert not np.array_equal(pixels(narrow), pixels(out)), "a lens makes no difference (perspective)"


def test_an_animation_is_posed_by_name_or_number_at_a_time(tmp_path):
    def shot_of(name, *args):
        path = tmp_path / f"{name}.png"
        shot("--size", "160x160", *args, FIGURE, path)
        return pixels(path).astype(int)
    default = shot_of("default", )
    dance = shot_of("dance", "--animation", "Dance", "--time", "0")
    idle = shot_of("idle", "--animation", "Idle", "--time", "0.4")
    idle_by_number = shot_of("idle_n", "--animation", "1", "--time", "0.4")
    walk = shot_of("walk", "--animation", "Walk", "--time", "0.4")
    later = shot_of("walk_later", "--animation", "Walk", "--time", "0.7")
    assert np.array_equal(default, dance), "the default is not the first clip at 0"
    assert np.array_equal(idle, idle_by_number), "by number is not by name"
    assert np.abs(idle - walk).mean() > 0.2, "two clips look the same"
    assert np.abs(walk - later).mean() > 0.2, "time does not move the pose"


def test_a_time_with_no_animation_or_an_unknown_animation_is_refused_naming_it(out):
    unknown = shot("--animation", "Run", FIGURE, out, check=False)
    assert unknown.returncode == 1 and "Run" in unknown.stderr and "Idle" in unknown.stderr, unknown.stderr
    assert not out.exists()
    out_of_range = shot("--animation", "3", FIGURE, out, check=False)
    assert out_of_range.returncode == 1 and "3" in out_of_range.stderr and not out.exists(), out_of_range.stderr
    no_clips = shot("--animation", "0", CONE, out, check=False)
    assert no_clips.returncode == 1 and "cone.glb" in no_clips.stderr and not out.exists(), no_clips.stderr
    no_time = shot("--time", "1", CONE, out, check=False)
    assert no_time.returncode == 1 and "cone.glb" in no_time.stderr and not out.exists(), no_time.stderr


def test_an_edge_file_is_shot_as_the_glb_it_was_made_from_looks(out, tmp_path):
    edge_file = tmp_path / "pbr.edge"
    subprocess.run([CONVERT, "--no-compress", str(PBR), str(edge_file)], check=True, capture_output=True)
    from_edge = tmp_path / "from_edge.png"
    shot("--size", "128x128", edge_file, from_edge)
    shot("--size", "128x128", PBR, out)
    a, b = pixels(from_edge).astype(int), pixels(out).astype(int)
    assert np.array_equal(drawn(a), drawn(b)), "the silhouette differs"
    assert np.abs(a - b).mean() < 3.0


# ---- a contact sheet ------------------------------------------------------------------------------------------------

def cell(frame: np.ndarray, index: int, columns: int, width: int, height: int) -> np.ndarray:
    row, column = divmod(index, columns)
    return frame[row * height:(row + 1) * height, column * width:(column + 1) * width]


def test_a_sheet_holds_each_model_as_its_own_shot_in_a_grid(out, tmp_path):
    models = [PBR, CONE, FIGURE, VERTEX_COLORS, PBR]
    shot("--sheet", out, "--cell", "96x64", "--columns", "2", *models)
    sheet = pixels(out)
    assert sheet.shape == (3 * 64, 2 * 96, 3)               # (five cells, two a row: three rows)
    for i, model in enumerate(models):
        single = tmp_path / f"{i}.png"
        shot("--size", "96x64", model, single)
        assert np.array_equal(cell(sheet, i, 2, 96, 64), pixels(single)), f"cell {i} ({model.name}) is not its shot"
    assert (cell(sheet, 5, 2, 96, 64) == np.array(BACKGROUND, np.uint8)).all(), "the cell after the last is not background"


def test_a_sheet_of_one_model_is_its_views_turned_round(out, tmp_path):
    shot("--sheet", out, "--cell", "80x80", "--views", "4", "--yaw", "10", FIGURE)
    sheet = pixels(out)
    assert sheet.shape == (80, 4 * 80, 3)                  # (views side by side by default)
    for i in range(4):
        single = tmp_path / f"{i}.png"
        shot("--size", "80x80", "--yaw", str(10 + 90 * i), FIGURE, single)
        assert np.abs(cell(sheet, i, 4, 80, 80).astype(int) - pixels(single).astype(int)).max() <= 2, f"view {i}"


def test_a_sheet_of_several_models_in_several_views_has_a_row_for_each_model(out, tmp_path):
    shot("--sheet", out, "--cell", "64x64", "--views", "3", CONE, PBR)
    sheet = pixels(out)
    assert sheet.shape == (2 * 64, 3 * 64, 3)
    for row, model in enumerate((CONE, PBR)):
        for view in range(3):
            single = tmp_path / f"{row}{view}.png"
            shot("--size", "64x64", "--yaw", str(35 + 120 * view), model, single)
            assert np.abs(cell(sheet, row * 3 + view, 3, 64, 64).astype(int) - pixels(single).astype(int)).max() <= 2, (row, view)


def test_a_sheet_is_the_same_picture_every_time(out, tmp_path):
    again = tmp_path / "again.png"
    shot("--sheet", out, "--cell", "64x64", CONE, PBR, FIGURE)
    shot("--sheet", again, "--cell", "64x64", CONE, PBR, FIGURE)
    assert out.read_bytes() == again.read_bytes()


def test_a_sheet_with_a_model_that_cannot_be_read_is_refused_whole(out, tmp_path):
    broken = tmp_path / "broken.glb"
    broken.write_bytes(b"glTF nothing")
    result = shot("--sheet", out, CONE, broken, check=False)
    assert result.returncode == 1 and "broken.glb" in result.stderr, result.stderr
    assert not out.exists(), "half a sheet was written"


# ---- what it refuses -------------------------------------------------------------------------------------------------

def test_a_model_that_is_not_there_is_refused_naming_it_and_writes_nothing(out, tmp_path):
    result = shot(tmp_path / "missing.glb", out, check=False)
    assert result.returncode == 1 and "missing.glb" in result.stderr, result.stderr
    assert not out.exists()


def test_a_file_that_is_not_a_model_is_refused_naming_it(out, tmp_path):
    text = tmp_path / "notes.txt"
    text.write_text("not a model")
    result = shot(text, out, check=False)
    assert result.returncode == 1 and "notes.txt" in result.stderr, result.stderr
    assert not out.exists()


def test_a_broken_glb_is_refused_naming_it_and_writes_nothing(out, tmp_path):
    broken = tmp_path / "broken.glb"
    broken.write_bytes((DATA / "cone.glb").read_bytes()[:200])
    result = shot(broken, out, check=False)
    assert result.returncode == 1 and "broken.glb" in result.stderr, result.stderr
    assert not out.exists()


@pytest.mark.parametrize("args, named", [
    (["--size", "320", CONE], "--size"),                  # not WxH
    (["--size", "0x100", CONE], "--size"),
    (["--size", "-4x100", CONE], "--size"),
    (["--size", "100x100x3", CONE], "--size"),
    (["--yaw", "left", CONE], "--yaw"),
    (["--yaw", "1e999", CONE], "--yaw"),
    (["--pitch", "90", CONE], "--pitch"),                 # straight up: the view's up is undefined
    (["--pitch", "-90", CONE], "--pitch"),
    (["--fov", "0", CONE], "--fov"),
    (["--fov", "180", CONE], "--fov"),
    (["--background", "1,2", CONE], "--background"),
    (["--background", "1,2,256", CONE], "--background"),
    (["--background", "1,2,-3", CONE], "--background"),
    (["--time", "-1", FIGURE], "--time"),
    (["--time", "nan", FIGURE], "--time"),
    (["--renderer", "cpu", CONE], "--renderer"),
    (["--no-such-option", CONE], "--no-such-option"),
])
def test_an_option_that_is_not_one_is_refused_naming_it_and_writes_nothing(args, named, out):
    result = shot(*args, out, check=False)
    assert result.returncode == 1 and named in result.stderr, (result.returncode, result.stderr)
    assert not out.exists()


def test_the_output_is_a_png(tmp_path):
    result = shot(CONE, tmp_path / "out.jpg", check=False)
    assert result.returncode == 1 and "out.jpg" in result.stderr and ".png" in result.stderr, result.stderr
    assert not (tmp_path / "out.jpg").exists()


def test_a_shot_needs_one_model_and_one_output():
    for args in ([], [CONE], [CONE, CONE, "out.png"]):
        result = shot(*args, check=False)
        assert result.returncode == 1 and "usage" in result.stderr.lower(), (args, result.stderr)


def test_a_sheet_needs_a_model_and_does_not_take_a_shot_option_it_would_ignore(out):
    assert shot("--sheet", out, check=False).returncode == 1
    assert shot("--views", "2", CONE, out, check=False).returncode == 1          # (--views and --columns and --cell are a sheet's)
    assert shot("--cell", "64x64", CONE, out, check=False).returncode == 1
    assert shot("--sheet", out, "--size", "64x64", CONE, check=False).returncode == 1      # (a sheet's cell is --cell)
    assert shot("--sheet", out, "--views", "0", CONE, check=False).returncode == 1
    assert shot("--sheet", out, "--columns", "0", CONE, check=False).returncode == 1
    assert not out.exists()


def features() -> list[str]:
    result = shot("--version")
    assert result.stdout.startswith("edge_shot:"), result.stdout
    return result.stdout.split()[1:]


def test_version_names_the_program_and_what_it_was_built_with():
    built = features()
    assert ("gui" in built) == HAS_GUI, built
    assert set(built) - {"gui"} and set(built) <= {"gui", "metal", "vulkan"}, built


@pytest.mark.parametrize("renderer", ["metal", "vulkan"])
def test_each_renderer_the_build_has_draws_the_same_picture_and_one_it_has_not_is_refused(renderer, out, tmp_path):
    built = features()
    if renderer not in built:
        result = shot("--renderer", renderer, CONE, out, check=False)
        assert result.returncode == 1 and renderer in result.stderr and not out.exists(), result.stderr
        return
    default = tmp_path / "default.png"
    shot("--size", "160x160", FIGURE, default)
    shot("--size", "160x160", "--renderer", renderer, FIGURE, out)
    a, b = pixels(default).astype(int), pixels(out).astype(int)
    assert np.array_equal(drawn(a), drawn(b)), "the silhouette differs"
    assert np.abs(a - b).mean() < 2.0, "the renderers draw different pictures"


# ---- the window ------------------------------------------------------------------------------------------------------

def test_a_window_opens_draws_some_frames_and_closes_where_edge_has_one():
    # (--frames N: draw N frames, then close, for this test and for a script)
    result = shot("--gui", "--frames", "3", FIGURE, check=False, timeout=60)
    if HAS_GUI:
        assert result.returncode == 0, result.stderr
    else:
        assert result.returncode == 1 and "edge_sdl" in result.stderr, result.stderr
