# SPDX-License-Identifier: MIT
# Copyright (c) 2004-2026 Manvel Avetisian

"""shader_cross --msl (tools/shader_cross): SPIR-V to Metal Shading Language and the map of what the Metal renderer binds.

    shader_cross --msl in.spv out.metal out.map

The map's lines (Metal's own indices, an explicit table, not SPIRV-Cross's defaults):

    stage vertex|fragment
    ubo <block name> <buffer index>             set 0 binding N is buffer(N)
    sampler <name> <texture index> <sampler index>      set 0 binding N is texture(N) and sampler(N)
    push <buffer index>                         the push constants, one buffer, at the same index in every shader

ctest passes the tools through the environment: SHADER_CROSS, EDGE_SPIRV_DIR (edge's own .spv), GLSLANG_VALIDATOR, and
EDGE_SHADER_INCLUDE_DIR (where edge.glsl is).  Compiling MSL needs Xcode's `metal`: on a machine without it the tests fail, they do not skip.
"""

import os
import re
import subprocess
from pathlib import Path

import pytest

SHADER_CROSS = os.environ["SHADER_CROSS"]
SPIRV_DIR = Path(os.environ["EDGE_SPIRV_DIR"])
GLSLANG = os.environ["GLSLANG_VALIDATOR"]
PUSH_BUFFER = 30   # the one index the push constants take, in the tool and the renderer
SDKS = ["macosx", "iphoneos", "iphonesimulator"]


def cross(spv: Path, out: Path):
    metal, map_ = out / (spv.stem + ".metal"), out / (spv.stem + ".map")
    result = subprocess.run([SHADER_CROSS, "--msl", str(spv), str(metal), str(map_)], capture_output=True, text=True)
    return result, metal, map_


def compile_metal(metal: Path, sdk: str) -> subprocess.CompletedProcess:
    return subprocess.run(["xcrun", "-sdk", sdk, "metal", "-c", str(metal), "-o", str(metal.with_suffix(f".{sdk}.air"))],
                          capture_output=True, text=True)


def parse_map(path: Path) -> dict:
    out = {"stage": None, "ubo": {}, "sampler": {}, "push": None}
    for line in path.read_text().splitlines():
        words = line.split()
        if words[0] == "stage":
            out["stage"] = words[1]
        elif words[0] == "ubo":
            out["ubo"][words[1]] = int(words[2])
        elif words[0] == "sampler":
            out["sampler"][words[1]] = (int(words[2]), int(words[3]))
        elif words[0] == "push":
            out["push"] = int(words[1])
        else:
            raise AssertionError(f"unknown map line: {line}")
    return out


def glsl_to_spv(tmp: Path, name: str, source: str) -> Path:
    src = tmp / name
    src.write_text(source)
    spv = tmp / (name + ".spv")
    subprocess.run([GLSLANG, "-V", "--target-env", "vulkan1.0", str(src), "-o", str(spv)], check=True, capture_output=True)
    return spv


ALL_SPV = sorted(SPIRV_DIR.glob("*.spv"))


def test_edge_has_shaders_to_translate():
    # a directory with none would make the parametrized tests below vanish
    assert len(ALL_SPV) >= 8, f"{SPIRV_DIR}: {[p.name for p in ALL_SPV]}"


@pytest.mark.parametrize("spv", ALL_SPV, ids=lambda p: p.name)
@pytest.mark.parametrize("sdk", SDKS)
def test_every_edge_shader_translates_and_compiles(spv, sdk, tmp_path):
    result, metal, map_ = cross(spv, tmp_path)
    assert result.returncode == 0, result.stderr
    compiled = compile_metal(metal, sdk)
    assert compiled.returncode == 0, compiled.stderr


@pytest.mark.parametrize("spv", ALL_SPV, ids=lambda p: p.name)
def test_map_matches_the_bindings_in_the_spirv(spv, tmp_path):
    # judged by the SPIR-V's own text (spirv-dis), not by the tool's reflection
    result, metal, map_ = cross(spv, tmp_path)
    assert result.returncode == 0, result.stderr
    text = subprocess.run(["spirv-dis", str(spv)], capture_output=True, text=True, check=True).stdout
    bindings = {m.group(1): int(m.group(2)) for m in re.finditer(r"OpDecorate (%\w+) Binding (\d+)", text)}
    names = {m.group(1): m.group(2) for m in re.finditer(r'OpName (%\w+) "(\w+)"', text)}
    parsed = parse_map(map_)
    mapped = set(parsed["ubo"].values()) | {t for t, _ in parsed["sampler"].values()}
    in_spirv = set(bindings.values())
    assert mapped <= in_spirv, f"{spv.name}: the map has bindings the SPIR-V does not ({mapped - in_spirv})"
    # every sampler the SPIR-V names with a binding that the map has: its texture index is that binding, its sampler states are 0, 1, ...
    # in the order of the bindings
    for ident, binding in bindings.items():
        name = names.get(ident)
        if name in parsed["sampler"]:
            assert parsed["sampler"][name][0] == binding, (spv.name, name)
    by_binding = sorted(parsed["sampler"].values())
    assert [s for _, s in by_binding] == list(range(len(by_binding))), (spv.name, by_binding)
    assert parsed["stage"] == ("vertex" if ".vert" in spv.name or "vertex" in spv.name else "fragment")
    assert all(i < 16 or 20 <= i <= 27 for i in parsed["ubo"].values()), "uniform buffers must stay clear of the vertex buffers and the push buffer"


def test_explicit_indices_for_a_known_shader(tmp_path):
    spv = glsl_to_spv(tmp_path, "known.frag", """#version 450
layout(set = 0, binding = 3) uniform Block { vec4 tint; } block;
layout(set = 0, binding = 5) uniform sampler2D albedo;
layout(push_constant) uniform Push { vec4 offset; float scale; } pc;
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 color;
void main() { color = texture(albedo, uv) * block.tint * pc.scale + pc.offset; }
""")
    result, metal, map_ = cross(spv, tmp_path)
    assert result.returncode == 0, result.stderr
    parsed = parse_map(map_)
    assert parsed == {"stage": "fragment", "ubo": {"Block": 3}, "sampler": {"albedo": (5, 0)}, "push": PUSH_BUFFER}
    source = metal.read_text()
    assert re.search(r"constant\s+Block&\s+block\s+\[\[buffer\(3\)\]\]", source), source
    assert re.search(r"texture2d<float>\s+albedo\s+\[\[texture\(5\)\]\]", source), source
    assert re.search(r"sampler\s+albedoSmplr\s+\[\[sampler\(0\)\]\]", source), source
    assert f"[[buffer({PUSH_BUFFER})]]" in source, source
    assert compile_metal(metal, "macosx").returncode == 0


def test_unused_declared_resources_are_not_in_the_map(tmp_path):
    spv = glsl_to_spv(tmp_path, "unused.frag", """#version 450
layout(set = 0, binding = 2) uniform Used { vec4 a; } used;
layout(set = 0, binding = 4) uniform Unused { vec4 b; } unused;
layout(set = 0, binding = 6) uniform sampler2D never;
layout(location = 0) out vec4 color;
void main() { color = used.a; }
""")
    result, metal, map_ = cross(spv, tmp_path)
    assert result.returncode == 0, result.stderr
    parsed = parse_map(map_)
    assert parsed["ubo"] == {"Used": 2} and parsed["sampler"] == {} and parsed["push"] is None, parsed
    assert "buffer(4)" not in metal.read_text() and "texture(6)" not in metal.read_text()


def test_vertex_inputs_keep_their_locations(tmp_path):
    spv = glsl_to_spv(tmp_path, "inputs.vert", """#version 450
layout(location = 0) in vec3 position;
layout(location = 3) in vec2 uv;
layout(location = 0) out vec2 v_uv;
void main() { gl_Position = vec4(position, 1.0); v_uv = uv; }
""")
    result, metal, map_ = cross(spv, tmp_path)
    assert result.returncode == 0, result.stderr
    source = metal.read_text()
    assert re.search(r"position\s+\[\[attribute\(0\)\]\]", source), source
    assert re.search(r"uv\s+\[\[attribute\(3\)\]\]", source), source
    assert parse_map(map_)["stage"] == "vertex"


def test_clip_space_is_metals_not_gls(tmp_path):
    # Metal's clip z is 0..w, as Vulkan's: the vertex stage must not remap it the way the GLES output does (fixup_clipspace),
    # and must not flip y (the render target's row 0 is its top, as in Vulkan)
    spv = glsl_to_spv(tmp_path, "clip.vert", """#version 450
layout(location = 0) in vec4 position;
void main() { gl_Position = position; }
""")
    result, metal, map_ = cross(spv, tmp_path)
    assert result.returncode == 0, result.stderr
    source = metal.read_text()
    assert "out.gl_Position = in.position;" in source, source
    assert "gl_Position.y" not in source and "gl_Position.z" not in source and "Invert" not in source, source   # nothing rewrites it after


def test_storage_buffers_have_no_form_and_are_an_error(tmp_path):
    spv = glsl_to_spv(tmp_path, "storage.frag", """#version 450
layout(set = 0, binding = 1) buffer Data { vec4 data[]; } d;
layout(location = 0) out vec4 color;
void main() { color = d.data[0]; }
""")
    result, metal, map_ = cross(spv, tmp_path)
    assert result.returncode != 0
    assert "storage" in result.stderr
    assert not metal.exists() and not map_.exists(), "a failed translation must leave no output"


def test_a_uniform_buffer_binding_in_the_vertex_buffers_range_is_an_error(tmp_path):
    spv = glsl_to_spv(tmp_path, "high.frag", """#version 450
layout(set = 0, binding = 17) uniform Block { vec4 a; } b;
layout(location = 0) out vec4 color;
void main() { color = b.a; }
""")
    result, metal, map_ = cross(spv, tmp_path)
    assert result.returncode != 0
    assert "17" in result.stderr
    assert not metal.exists() and not map_.exists()


def test_not_spirv_is_an_error(tmp_path):
    bad = tmp_path / "bad.spv"
    bad.write_bytes(b"not a shader at all!!")
    result, metal, map_ = cross(bad, tmp_path)
    assert result.returncode != 0 and not metal.exists() and not map_.exists()


def test_a_second_descriptor_set_is_an_error(tmp_path):
    spv = glsl_to_spv(tmp_path, "set1.frag", """#version 450
layout(set = 1, binding = 0) uniform Block { vec4 a; } b;
layout(location = 0) out vec4 color;
void main() { color = b.a; }
""")
    result, metal, map_ = cross(spv, tmp_path)
    assert result.returncode != 0
    assert "set" in result.stderr


def test_usage_without_msl_flag_is_unchanged_glsl_form(tmp_path):
    # the GLES form (three arguments, no flag) must keep working beside the new one
    spv = glsl_to_spv(tmp_path, "gles.frag", """#version 450
layout(location = 0) out vec4 color;
void main() { color = vec4(1.0); }
""")
    out, map_ = tmp_path / "o.glsl", tmp_path / "o.map"
    result = subprocess.run([SHADER_CROSS, str(spv), str(out), str(map_)], capture_output=True, text=True)
    assert result.returncode == 0, result.stderr
    assert out.read_text().startswith("#version 300 es")


def test_samplers_past_the_sixteenth_binding_get_sampler_states_below_sixteen(tmp_path):
    # Metal has 16 sampler states a stage; edge's global textures sit at bindings 12 to 19
    spv = glsl_to_spv(tmp_path, "global.frag", """#version 450
layout(set = 0, binding = 19) uniform sampler2D last;
layout(set = 0, binding = 2) uniform sampler2D first;
layout(set = 0, binding = 12) uniform sampler2D middle;
layout(set = 0, binding = 20) uniform Params { vec4 p; } params;
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 color;
void main() { color = texture(first, uv) + texture(middle, uv) + texture(last, uv) + params.p; }
""")
    result, metal, map_ = cross(spv, tmp_path)
    assert result.returncode == 0, result.stderr
    parsed = parse_map(map_)
    assert parsed["sampler"] == {"first": (2, 0), "middle": (12, 1), "last": (19, 2)}, parsed
    assert parsed["ubo"] == {"Params": 20}, parsed
    assert compile_metal(metal, "macosx").returncode == 0

