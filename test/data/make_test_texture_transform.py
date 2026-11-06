# SPDX-License-Identifier: MIT
# Copyright (c) 2004-2026 Manvel Avetisian

"""Writes the glTFs test_gltf_loader.cc reads for the UV set of a material (plain Python, no dependencies): one quad, its
TEXCOORD_0 the corners (0,0) (1,0) (1,1) (0,1) and its TEXCOORD_1 the same plus (10, 20), drawn with a material whose textures are
test_white.png.

  test_uv_transform.gltf        base colour and normal map moved by KHR_texture_transform: offset (0.5, 0.25), scale (2, 3)
  test_uv_transform_turned.gltf  the same turned by a quarter turn (rotation pi/2)
  test_uv_set_1.gltf             base colour and normal map read TEXCOORD_1 (texCoord 1); the normal map scaled by 0.5
  test_uv_disagree.gltf          base colour reads TEXCOORD_0, the normal map TEXCOORD_1: one mesh cannot have both
"""
import base64
import json
import math
import struct
from pathlib import Path

HERE = Path(__file__).resolve().parent

CORNERS = [(0, 0), (1, 0), (1, 1), (0, 1)]


def build(base_color_info, normal_info):
    pos = [(x, y, 0) for x, y in CORNERS]
    parts = [
        ("POSITION", b"".join(struct.pack("<3f", *p) for p in pos), 5126, "VEC3", 34962, 4),
        ("NORMAL", b"".join(struct.pack("<3f", 0, 0, 1) for _ in pos), 5126, "VEC3", 34962, 4),
        ("TANGENT", b"".join(struct.pack("<4f", 1, 0, 0, 1) for _ in pos), 5126, "VEC4", 34962, 4),
        ("TEXCOORD_0", b"".join(struct.pack("<2f", u, v) for u, v in CORNERS), 5126, "VEC2", 34962, 4),
        ("TEXCOORD_1", b"".join(struct.pack("<2f", u + 10, v + 20) for u, v in CORNERS), 5126, "VEC2", 34962, 4),
        (None, struct.pack("<6I", 0, 1, 2, 0, 2, 3), 5125, "SCALAR", 34963, 6),
    ]
    bin_, views, accessors, attributes = b"", [], [], {}
    for name, data, ctype, kind, target, count in parts:
        views.append({"buffer": 0, "byteOffset": len(bin_), "byteLength": len(data), "target": target})
        acc = {"bufferView": len(views) - 1, "componentType": ctype, "count": count, "type": kind}
        if name == "POSITION":
            acc["min"], acc["max"] = [0, 0, 0], [1, 1, 0]
        accessors.append(acc)
        if name:
            attributes[name] = len(accessors) - 1
        bin_ += data
    doc = {
        "asset": {"version": "2.0"}, "scene": 0, "scenes": [{"nodes": [0]}],
        "extensionsUsed": ["KHR_texture_transform"],
        "nodes": [{"name": "quad", "mesh": 0}],
        "images": [{"uri": "test_white.png"}],
        "samplers": [{}],
        "textures": [{"source": 0, "sampler": 0}],
        "materials": [{"name": "uv", "pbrMetallicRoughness": {"baseColorTexture": base_color_info}, "normalTexture": normal_info}],
        "meshes": [{"name": "quad", "primitives": [{"attributes": attributes, "indices": len(accessors) - 1, "material": 0, "mode": 4}]}],
        "accessors": accessors, "bufferViews": views,
        "buffers": [{"byteLength": len(bin_), "uri": "data:application/octet-stream;base64," + base64.b64encode(bin_).decode()}],
    }
    return json.dumps(doc, indent=1)


def info(tex_coord=0, transform=None, scale=None):
    result = {"index": 0}
    if tex_coord:
        result["texCoord"] = tex_coord
    if transform:
        result["extensions"] = {"KHR_texture_transform": transform}
    if scale is not None:
        result["scale"] = scale
    return result


def main():
    moved = {"offset": [0.5, 0.25], "scale": [2, 3]}
    turned = {"offset": [0.5, 0.25], "scale": [2, 3], "rotation": math.pi / 2}
    (HERE / "test_uv_transform.gltf").write_text(build(info(transform=moved), info(transform=moved)))
    (HERE / "test_uv_transform_turned.gltf").write_text(build(info(transform=turned), info(transform=turned)))
    (HERE / "test_uv_set_1.gltf").write_text(build(info(tex_coord=1), info(tex_coord=1, scale=0.5)))
    (HERE / "test_uv_disagree.gltf").write_text(build(info(), info(tex_coord=1)))


main()
