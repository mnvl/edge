# SPDX-License-Identifier: MIT
# Copyright (c) 2004-2026 Manvel Avetisian

"""Writes the skinned glTFs test_gltf_loader.cc expects the loader to refuse (plain Python, no dependencies): one
triangle skinned to a chain of joints, each file wrong in one way.

  test_skin_bad_joint_index.glb    a skin of 2 joints; the third vertex is weighted 100% to joint 5
  test_skin_joints_no_weights.glb  JOINTS_0 and no WEIGHTS_0
  test_skin_257_joints.glb         a skin of 257 joints, one more than a draw has joint matrices for (MAX_JOINT_MATRICES)
"""
import json
import struct
from pathlib import Path

HERE = Path(__file__).resolve().parent


def build(joint_count, joints, weights):
    pos = [(0, 0, 0), (1, 0, 0), (0, 1, 0)]
    parts = [
        ("POSITION", b"".join(struct.pack("<3f", *p) for p in pos), 5126, "VEC3", 34962, 3),
        ("NORMAL", b"".join(struct.pack("<3f", 0, 0, 1) for _ in pos), 5126, "VEC3", 34962, 3),
        ("TEXCOORD_0", b"".join(struct.pack("<2f", p[0], p[1]) for p in pos), 5126, "VEC2", 34962, 3),
        ("JOINTS_0", b"".join(struct.pack("<4H", *j) for j in joints), 5123, "VEC4", 34962, 3),
    ]
    if weights is not None:
        parts.append(("WEIGHTS_0", b"".join(struct.pack("<4f", *w) for w in weights), 5126, "VEC4", 34962, 3))
    parts.append((None, struct.pack("<3I", 0, 1, 2), 5125, "SCALAR", 34963, 3))
    identity = struct.pack("<16f", 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1)
    parts.append((None, identity * joint_count, 5126, "MAT4", None, joint_count))
    bin_, views, accessors, attributes = b"", [], [], {}
    for name, data, ctype, kind, target, count in parts:
        view = {"buffer": 0, "byteOffset": len(bin_), "byteLength": len(data)}
        if target:
            view["target"] = target
        views.append(view)
        acc = {"bufferView": len(views) - 1, "componentType": ctype, "count": count, "type": kind}
        if name == "POSITION":
            acc["min"], acc["max"] = [0, 0, 0], [1, 1, 0]
        accessors.append(acc)
        if name:
            attributes[name] = len(accessors) - 1
        bin_ += data
    # joints: a chain 0 -> 1 -> ... ; the mesh node comes after them
    nodes = [{"name": f"joint{i}", **({"children": [i + 1]} if i + 1 < joint_count else {})} for i in range(joint_count)]
    nodes.append({"name": "body", "mesh": 0, "skin": 0})
    doc = {
        "asset": {"version": "2.0"}, "scene": 0, "scenes": [{"nodes": [0, joint_count]}],
        "nodes": nodes,
        "skins": [{"joints": list(range(joint_count)), "inverseBindMatrices": len(accessors) - 1, "skeleton": 0}],
        "meshes": [{"name": "body", "primitives": [{"attributes": attributes, "indices": len(accessors) - 2, "mode": 4}]}],
        "accessors": accessors, "bufferViews": views, "buffers": [{"byteLength": len(bin_)}],
    }
    js = json.dumps(doc).encode()
    js += b" " * (-len(js) % 4)
    bin_ += b"\0" * (-len(bin_) % 4)
    out = b"glTF" + struct.pack("<II", 2, 12 + 8 + len(js) + 8 + len(bin_))
    out += struct.pack("<I4s", len(js), b"JSON") + js + struct.pack("<I4s", len(bin_), b"BIN\0") + bin_
    return out


def main():
    whole = [(1, 0, 0, 0)] * 3
    (HERE / "test_skin_bad_joint_index.glb").write_bytes(build(2, [(0, 0, 0, 0), (1, 0, 0, 0), (5, 0, 0, 0)], whole))
    (HERE / "test_skin_joints_no_weights.glb").write_bytes(build(2, [(0, 0, 0, 0), (1, 0, 0, 0), (1, 0, 0, 0)], None))
    (HERE / "test_skin_257_joints.glb").write_bytes(build(257, [(0, 0, 0, 0), (1, 0, 0, 0), (256, 0, 0, 0)], whole))


main()
