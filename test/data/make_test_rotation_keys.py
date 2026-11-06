# SPDX-License-Identifier: MIT
# Copyright (c) 2004-2026 Manvel Avetisian

"""test_gltf_loder_animation_zero_rotation.gltf and test_gltf_loder_animation_nan_rotation.gltf: a triangle whose node turns from no
rotation to a quaternion that is no rotation, (0, 0, 0, 0) in one, (NaN, 0, 0, 1) in the other.  glTF's rotations are unit
quaternions; test_gltf_loader.cc expects each to be a load error naming the key.

    python3 test/data/make_test_rotation_keys.py
"""
import base64
import json
import math
import struct
from pathlib import Path

HERE = Path(__file__).resolve().parent


def write(name, bad_key):
    positions = struct.pack("<9f", 0, 0, 0, 1, 0, 0, 0, 1, 0)
    indices = struct.pack("<3H", 0, 1, 2) + b"\0\0"
    times = struct.pack("<2f", 0, 1)
    rotations = struct.pack("<8f", 0, 0, 0, 1, *bad_key)
    data = positions + indices + times + rotations
    views, offset = [], 0
    for part, target in ((positions, 34962), (indices, 34963), (times, None), (rotations, None)):
        view = {"buffer": 0, "byteOffset": offset, "byteLength": len(part)}
        if target:
            view["target"] = target
        views.append(view)
        offset += len(part)
    views[1]["byteLength"] = 6
    gltf = {
        "asset": {"version": "2.0"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"mesh": 0, "name": "turning_node"}],
        "meshes": [{"primitives": [{"attributes": {"POSITION": 0}, "indices": 1}]}],
        "animations": [{"name": "turn", "channels": [{"sampler": 0, "target": {"node": 0, "path": "rotation"}}],
                        "samplers": [{"input": 2, "output": 3, "interpolation": "LINEAR"}]}],
        "buffers": [{"byteLength": len(data), "uri": "data:application/octet-stream;base64," + base64.b64encode(data).decode()}],
        "bufferViews": views,
        "accessors": [
            {"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3", "min": [0, 0, 0], "max": [1, 1, 0]},
            {"bufferView": 1, "componentType": 5123, "count": 3, "type": "SCALAR"},
            {"bufferView": 2, "componentType": 5126, "count": 2, "type": "SCALAR", "min": [0], "max": [1]},
            {"bufferView": 3, "componentType": 5126, "count": 2, "type": "VEC4"},
        ],
    }
    (HERE / name).write_text(json.dumps(gltf, indent=1) + "\n")


write("test_gltf_loder_animation_zero_rotation.gltf", (0, 0, 0, 0))
write("test_gltf_loder_animation_nan_rotation.gltf", (math.nan, 0, 0, 1))
