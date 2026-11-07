# SPDX-License-Identifier: MIT
# Copyright (c) 2004-2026 Manvel Avetisian

"""The CMake helpers a game builds with (etc/cmake): edge_convert_models / edge_convert_model, which make .edge models from glTF and
make one again only when what it is made from changed (by content, not by file time: git-annex sync rewrites file times), and
edge_bundle_runtime, which puts what edge reads at run time beside a game's binary.  Each conversion test configures and builds a
small project of its own that uses the helpers, with a converter that logs every model it makes and then runs edge_convert.

ctest passes the tools through the environment: EDGE_CONVERT, CMAKE_COMMAND, EDGE_NINJA, EDGE_ROOT, EDGE_SPIRV_DIR and
EDGE_TEST_BUNDLE (the bundle the test build makes), and EDGE_MODEL_BINARY_VERSION as edge's CMake has it.
"""

import json
import os
import shutil
import stat
import struct
import subprocess
import sys
from pathlib import Path

import pytest

EDGE_ROOT = Path(os.environ["EDGE_ROOT"])
# (empty where edge is built without its asset tools, EDGE_ASSET_TOOLS: the conversion tests are skipped, the rest run)
EDGE_CONVERT = Path(os.environ["EDGE_CONVERT"]) if os.environ.get("EDGE_CONVERT") else None
needs_convert = pytest.mark.skipif(EDGE_CONVERT is None, reason="edge is built without its asset tools (EDGE_ASSET_TOOLS)")
CMAKE = os.environ["CMAKE_COMMAND"]
NINJA = os.environ["EDGE_NINJA"]
DATA = EDGE_ROOT / "test" / "data"


def format_version_of(model: Path) -> int:
    magic, version = struct.unpack("<II", model.read_bytes()[:8])
    return version


class Project:
    """A project that converts the models under src/ into out/ with edge_convert_models, built with Ninja."""

    def __init__(self, root: Path):
        self.root = root
        self.src = root / "src"
        self.out = root / "build" / "out"
        self.log = root / "conversions.log"
        self.converter = root / "converter"
        self.src.mkdir(parents=True)
        self.write_converter()

    def write_converter(self, extra: str = "", version: str = ""):
        """A converter that logs the models it makes (not the --inputs asked of it) and hands everything to edge_convert; while
        there is a file named crash beside the project, it writes half a model instead and fails, as a converter that crashes."""
        self.converter.write_text(
            "#!/bin/sh\n"
            f"{extra}\n"
            + (f'[ "$1" = "--version" ] && {{ echo "{version}"; exit 0; }}\n' if version else "")
            + f'[ "$1" = "--inputs" ] || [ "$1" = "--version" ] || echo "$@" >> "{self.log}"\n'
            f'if [ "$1" != "--inputs" ] && [ "$1" != "--version" ] && [ -f "{self.root}/crash" ]; then for last; do :; done; echo half > "$last"; exit 3; fi\n'
            f'exec "{EDGE_CONVERT}" "$@"\n')
        self.converter.chmod(self.converter.stat().st_mode | stat.S_IEXEC)

    def configure(self, options="--no-compress"):
        (self.root / "CMakeLists.txt").write_text(
            "cmake_minimum_required(VERSION 3.20)\n"
            "project(models NONE)\n"
            f'include("{EDGE_ROOT}/etc/cmake/EdgeModels.cmake")\n'
            "add_executable(edge_convert IMPORTED)\n"
            f'set_property(TARGET edge_convert PROPERTY IMPORTED_LOCATION "{self.converter}")\n'
            f'edge_convert_models(models SOURCE_DIR "{self.src}" DESTINATION "${{CMAKE_BINARY_DIR}}/out" OPTIONS {options})\n')
        subprocess.run([CMAKE, "-S", self.root, "-B", self.root / "build", "-G", "Ninja", f"-DCMAKE_MAKE_PROGRAM={NINJA}"],
                       check=True, capture_output=True)

    def build(self, check=True) -> subprocess.CompletedProcess:
        result = subprocess.run([NINJA, "-C", self.root / "build"], capture_output=True, text=True)
        if check and result.returncode != 0:
            raise AssertionError(f"the build failed:\n{result.stdout}\n{result.stderr}")
        return result

    def conversions(self) -> list[str]:
        """The glTF files converted so far, in order."""
        if not self.log.exists():
            return []
        return [line.split()[-2] for line in self.log.read_text().splitlines()]


@pytest.fixture
def project(tmp_path):
    p = Project(tmp_path)
    shutil.copy(DATA / "test_gltf_loder_plane.glb", p.src / "plane.glb")
    (p.src / "world").mkdir()
    shutil.copy(DATA / "test_uv_set_1.gltf", p.src / "world" / "island.gltf")
    shutil.copy(DATA / "test_white.png", p.src / "world" / "test_white.png")
    return p


def pipeline_of(model: Path) -> str:
    """The pipeline a .edge file says made it: after the magic and the version, a string (its length in 8 bytes, its bytes)."""
    data = model.read_bytes()
    (length,) = struct.unpack("<Q", data[8:16])
    return data[16:16 + length].decode()


@needs_convert
def test_edge_convert_says_the_format_version_and_the_pipeline_its_files_have(tmp_path):
    out = subprocess.run([EDGE_CONVERT, "--version"], capture_output=True, text=True, check=True).stdout
    model = tmp_path / "plane.edge"
    subprocess.run([EDGE_CONVERT, "--no-compress", DATA / "test_gltf_loder_plane.glb", model], check=True, capture_output=True)
    assert out.split() == ["edge_convert", ".edge", "format", str(format_version_of(model)), "pipeline", pipeline_of(model)]
    assert len(pipeline_of(model)) == 16


@needs_convert
def test_cmake_says_the_format_version_edge_convert_writes(tmp_path):
    model = tmp_path / "plane.edge"
    subprocess.run([EDGE_CONVERT, "--no-compress", DATA / "test_gltf_loder_plane.glb", model], check=True, capture_output=True)
    assert os.environ["EDGE_MODEL_BINARY_VERSION"] == str(format_version_of(model))


@needs_convert
def test_edge_convert_makes_astc_models_only_of_compressed_textures(tmp_path):
    bc7, astc = tmp_path / "bc7.edge", tmp_path / "astc.edge"
    subprocess.run([EDGE_CONVERT, DATA / "test_gltf_loader_pbr.glb", bc7], check=True, capture_output=True)
    subprocess.run([EDGE_CONVERT, "--astc", DATA / "test_gltf_loader_pbr.glb", astc], check=True, capture_output=True)
    assert astc.read_bytes() != bc7.read_bytes()
    refused = subprocess.run([EDGE_CONVERT, "--astc", "--no-compress", DATA / "test_gltf_loader_pbr.glb", tmp_path / "raw.edge"],
                             capture_output=True, text=True)
    assert refused.returncode == 1
    assert "ASTC" in refused.stderr
    assert not (tmp_path / "raw.edge").exists()


@needs_convert
def test_edge_convert_lists_the_files_a_model_is_made_from():
    out = subprocess.run([EDGE_CONVERT, "--inputs", DATA / "test_uv_set_1.gltf"], capture_output=True, text=True, check=True).stdout
    assert out.splitlines() == [str(DATA / "test_uv_set_1.gltf"), str(DATA / "test_white.png")]


@needs_convert
def test_edge_convert_refuses_to_list_the_files_of_what_is_not_there(tmp_path):
    result = subprocess.run([EDGE_CONVERT, "--inputs", tmp_path / "none.glb"], capture_output=True, text=True)
    assert result.returncode == 1
    assert "none.glb" in result.stderr
    assert result.stdout == ""


@needs_convert
@needs_convert
def test_relative_directories_are_of_the_list_file_that_names_them(project, tmp_path):
    # (what docs/building.md writes: it found nothing from one working directory, and no such directory from another)
    project.configure()
    cmake_lists = project.root / "CMakeLists.txt"
    text = cmake_lists.read_text()
    text = text.replace(f'SOURCE_DIR "{project.src}" DESTINATION "${{CMAKE_BINARY_DIR}}/out"', "SOURCE_DIR src DESTINATION out")
    assert "SOURCE_DIR src DESTINATION out" in text
    cmake_lists.write_text(text)
    elsewhere = tmp_path / "elsewhere"
    elsewhere.mkdir()
    subprocess.run([CMAKE, "-S", project.root, "-B", project.root / "build", "-G", "Ninja", f"-DCMAKE_MAKE_PROGRAM={NINJA}"],
                   check=True, capture_output=True, cwd=elsewhere)
    project.build()
    assert (project.out / "plane.edge").exists()


def test_every_gltf_under_the_source_becomes_a_model_at_its_path_as_edge_convert_makes_it(project, tmp_path):
    project.configure()
    project.build()
    assert sorted(project.conversions()) == sorted([str(project.src / "plane.glb"), str(project.src / "world" / "island.gltf")])
    for rel, source in [("plane.edge", "plane.glb"), ("world/island.edge", "world/island.gltf")]:
        direct = tmp_path / "direct.edge"
        subprocess.run([EDGE_CONVERT, "--no-compress", project.src / source, direct], check=True, capture_output=True)
        assert (project.out / rel).read_bytes() == direct.read_bytes(), rel


@needs_convert
def test_nothing_is_made_again_when_nothing_changed(project):
    project.configure()
    project.build()
    assert "no work to do" in project.build().stdout
    assert len(project.conversions()) == 2


@needs_convert
def test_a_file_whose_time_changed_but_not_its_bytes_makes_nothing_again(project):
    project.configure()
    project.build()
    before = (project.out / "world" / "island.edge").read_bytes()
    texture = project.src / "world" / "test_white.png"
    os.utime(texture)   # now, as a sync leaves the files it wrote
    project.build()
    assert len(project.conversions()) == 2
    assert (project.out / "world" / "island.edge").read_bytes() == before
    assert "no work to do" in project.build().stdout


@needs_convert
def test_a_changed_texture_makes_the_model_that_reads_it_again_and_no_other(project):
    project.configure()
    project.build()
    shutil.copy(DATA / "test_gray.png", project.src / "world" / "test_white.png")
    project.build()
    assert project.conversions()[2:] == [str(project.src / "world" / "island.gltf")]


@needs_convert
def test_a_changed_gltf_makes_its_model_again(project):
    project.configure()
    project.build()
    shutil.copy(DATA / "cone.glb", project.src / "plane.glb")
    project.build()
    assert project.conversions()[2:] == [str(project.src / "plane.glb")]


@needs_convert
def test_other_options_make_every_model_again(project):
    project.configure()
    project.build()
    project.configure("--no-compress --no-emissive")
    project.build()
    assert sorted(project.conversions()[2:]) == sorted([str(project.src / "plane.glb"), str(project.src / "world" / "island.gltf")])
    assert all("--no-emissive" in line for line in project.log.read_text().splitlines()[2:])


@needs_convert
def test_a_converter_of_another_pipeline_makes_every_model_again(project):
    project.configure()
    project.build()
    project.write_converter(version="edge_convert .edge format 10 pipeline 0123456789abcdef")
    project.build()
    assert len(project.conversions()) == 4


@needs_convert
def test_a_converter_linked_anew_with_the_same_pipeline_makes_nothing_again(project):
    project.configure()
    project.build()
    project.write_converter("# the same edge, linked again")
    project.build()
    assert len(project.conversions()) == 2


@needs_convert
def test_a_glTF_that_cannot_be_read_fails_the_build_naming_it(project):
    project.configure()
    project.build()
    good = (project.src / "plane.glb").read_bytes()
    (project.src / "plane.glb").write_bytes(b"not a glb")
    result = project.build(check=False)
    assert result.returncode != 0
    assert "plane.glb" in result.stdout + result.stderr
    # back as it was: the model of those bytes is still there, and the build is whole again
    (project.src / "plane.glb").write_bytes(good)
    project.build()
    assert "no work to do" in project.build().stdout


@needs_convert
def test_a_conversion_that_crashes_fails_the_build_and_what_it_left_is_never_taken_for_a_model(project):
    project.configure()
    project.build()
    good = (project.src / "plane.glb").read_bytes()
    shutil.copy(DATA / "cone.glb", project.src / "plane.glb")
    (project.root / "crash").touch()
    assert project.build(check=False).returncode != 0
    # the glTF back as the model's record names it: what the crash left is not that model
    (project.root / "crash").unlink()
    (project.src / "plane.glb").write_bytes(good)
    project.build()
    assert project.conversions()[2:] == [str(project.src / "plane.glb")] * 2
    assert format_version_of(project.out / "plane.edge") == format_version_of(project.out / "world" / "island.edge")


@needs_convert
def test_a_texture_that_is_not_there_fails_the_build_naming_it(project):
    project.configure()
    (project.src / "world" / "test_white.png").unlink()
    result = project.build(check=False)
    assert result.returncode != 0
    assert "test_white.png" in result.stdout + result.stderr


@needs_convert
def test_a_model_whose_glTF_is_gone_is_not_made(project):
    project.configure()
    project.build()
    (project.src / "plane.glb").unlink()
    project.configure()
    project.build()
    assert len(project.conversions()) == 2


@needs_convert
@pytest.mark.parametrize("missing", ["SOURCE_DIR", "DESTINATION"])
def test_the_helper_refuses_to_be_called_without_what_it_needs(tmp_path, missing):
    args = {"SOURCE_DIR": f'"{tmp_path}"', "DESTINATION": f'"{tmp_path}/out"'}
    del args[missing]
    (tmp_path / "CMakeLists.txt").write_text(
        "cmake_minimum_required(VERSION 3.20)\nproject(models NONE)\n"
        f'include("{EDGE_ROOT}/etc/cmake/EdgeModels.cmake")\n'
        f'add_executable(edge_convert IMPORTED)\nset_property(TARGET edge_convert PROPERTY IMPORTED_LOCATION "{EDGE_CONVERT}")\n'
        f"edge_convert_models(models {' '.join(k + ' ' + v for k, v in args.items())})\n")
    result = subprocess.run([CMAKE, "-S", tmp_path, "-B", tmp_path / "build", "-G", "Ninja", f"-DCMAKE_MAKE_PROGRAM={NINJA}"],
                            capture_output=True, text=True)
    assert result.returncode != 0
    assert missing in result.stderr


def test_the_bundle_has_edge_s_shaders_and_on_macos_its_vulkan_driver():
    bundle = Path(os.environ["EDGE_TEST_BUNDLE"])
    spirv = Path(os.environ["EDGE_SPIRV_DIR"])
    for name in ["vertex", "vertex_blend", "vertex_skin", "fragment"]:
        assert (bundle / "shaders" / "edge" / f"{name}.spv").read_bytes() == (spirv / f"{name}.spv").read_bytes(), name
    driver = bundle / "vulkan"
    if sys.platform == "darwin":
        manifest = json.loads((driver / "MoltenVK_icd.json").read_text())
        library = driver / manifest["ICD"]["library_path"]
        assert manifest["ICD"]["library_path"] == "./libMoltenVK.dylib"
        assert library.is_file() and library.stat().st_size > 0
    else:
        assert not driver.exists()


def run_pipeline(tmp_path, files, out):
    listing = tmp_path / "sources.txt"
    listing.write_text("".join(f"{f}\n" for f in files))
    return subprocess.run([CMAKE, f"-DLIST={listing}", f"-DROOT={tmp_path}", f"-DOUT={out}", "-P", EDGE_ROOT / "etc/cmake/edge_pipeline.cmake"],
                          capture_output=True, text=True)


def hash_in(out: Path) -> str:
    text = out.read_text()
    return text[text.index('return "') + 8:text.index('";')]


def test_the_pipeline_hash_is_of_the_bytes_of_the_sources_not_their_order_or_times(tmp_path):
    a, b = tmp_path / "a.cc", tmp_path / "b.h"
    a.write_text("int a;")
    b.write_text("int b;")
    out = tmp_path / "edge_pipeline.cc"
    assert run_pipeline(tmp_path, [a, b], out).returncode == 0
    first = hash_in(out)
    assert len(first) == 16
    written = out.stat().st_mtime_ns
    os.utime(a)
    assert run_pipeline(tmp_path, [b, a], out).returncode == 0
    assert hash_in(out) == first
    assert out.stat().st_mtime_ns == written          # not written again: nothing that includes it is rebuilt
    a.write_text("int a = 1;")
    assert run_pipeline(tmp_path, [a, b], out).returncode == 0
    assert hash_in(out) != first


def test_the_pipeline_hash_refuses_a_source_that_is_not_there(tmp_path):
    result = run_pipeline(tmp_path, [tmp_path / "gone.cc"], tmp_path / "edge_pipeline.cc")
    assert result.returncode != 0
    assert "gone.cc" in result.stderr


def test_the_pipeline_is_what_makes_a_file_and_not_what_draws_it():
    sources = [Path(line).name for line in Path(os.environ["EDGE_PIPELINE_SOURCES"]).read_text().split()]
    for made_by in ["gltf_reader.cc", "gltf_meshes.cc", "model.cc", "serialized_model.h", "model_textures.cc", "image_compress.cc",
                    "texture_io.cc", "static_mesh.cc", "animated_mesh.cc", "edge_convert.cc", "CMakeLists.txt", "vec.h"]:
        assert made_by in sources, made_by
    assert not [s for s in sources if s.startswith(("vulkan_", "gles_", "bind_"))]
