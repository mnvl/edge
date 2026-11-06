# SPDX-License-Identifier: MIT
# Copyright (c) 2004-2026 Manvel Avetisian

"""Writes test_skinned_groups.glb, which test_custom_attributes.cc and test_edge_file.cc read, and test_skinned_colors.glb, the same
with COLOR_0 (test_vertex_colors.cc) (plain Python, no dependencies): a skinned
mesh whose vertices are in three groups, the group of each given by a custom vertex attribute `_GROUP` (a whole number in a float).

Joints: node "root" at the origin, its child "tip" at (0, 1, 0); the inverse bind matrices are identity for root and a
translation of (0, -1, 0) for tip.  Every group has vertices of its own (nothing is shared between groups):
  group 0: one triangle at x 0..1, y 0..1, skinned 100% to root
  group 1: one triangle at x 2..3, y 0..1, skinned 50% root, 50% tip (a soft one: it spans the bend)
  group 2: a quad (two triangles) at x 4..5, y 1..2, skinned 100% to tip
Normals are +Z, tangents +X.  The group numbers are in a non-trivial order in the file: the vertices of group 2 come first.
"""
import json
import struct
from pathlib import Path

HERE = Path(__file__).resolve().parent

# (group, [positions], [(joint a, weight a, joint b, weight b)] per vertex), listed with group 2 first
GROUPS = [
    (2, [(4, 1, 0), (5, 1, 0), (5, 2, 0), (4, 2, 0)], [(1, 1.0, 0, 0.0)] * 4),
    (0, [(0, 0, 0), (1, 0, 0), (0, 1, 0)], [(0, 1.0, 0, 0.0)] * 3),
    (1, [(2, 0, 0), (3, 0, 0), (2, 1, 0)], [(0, 0.5, 1, 0.5)] * 3),
]


def build(with_colors):
    pos, joints, weights, group, indices = [], [], [], [], []
    for number, ps, skin in GROUPS:
        base = len(pos)
        pos += ps
        joints += [(a, b, 0, 0) for a, _, b, _ in skin]
        weights += [(wa, wb, 0, 0) for _, wa, _, wb in skin]
        group += [float(number)] * len(ps)
        indices += [base, base + 1, base + 2] + ([base, base + 2, base + 3] if len(ps) == 4 else [])
    n = len(pos)
    colors = [("COLOR_0", b"".join(struct.pack("<4f", 1, 0, 0, 1) for _ in pos), 5126, "VEC4", 34962)] if with_colors else []
    parts = [
        ("POSITION", b"".join(struct.pack("<3f", *p) for p in pos), 5126, "VEC3", 34962),
        ("NORMAL", b"".join(struct.pack("<3f", 0, 0, 1) for _ in pos), 5126, "VEC3", 34962),
        ("TANGENT", b"".join(struct.pack("<4f", 1, 0, 0, 1) for _ in pos), 5126, "VEC4", 34962),
        ("TEXCOORD_0", b"".join(struct.pack("<2f", p[0] / 5, p[1] / 2) for p in pos), 5126, "VEC2", 34962),
        ("JOINTS_0", b"".join(struct.pack("<4B", *j) for j in joints), 5121, "VEC4", 34962),
        ("WEIGHTS_0", b"".join(struct.pack("<4f", *w) for w in weights), 5126, "VEC4", 34962),
        ("_GROUP", b"".join(struct.pack("<f", c) for c in group), 5126, "SCALAR", 34962),
        *colors,
        (None, b"".join(struct.pack("<I", i) for i in indices), 5125, "SCALAR", 34963),
        (None, struct.pack("<16f", *[1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, ][:16]) +
         struct.pack("<16f", 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, -1, 0, 1), 5126, "MAT4", None),
    ]
    bin_, views, accessors, attributes = b"", [], [], {}
    for name, data, ctype, kind, target in parts:
        view = {"buffer": 0, "byteOffset": len(bin_), "byteLength": len(data)}
        if target:
            view["target"] = target
        views.append(view)
        count = len(indices) if name is None and kind == "SCALAR" else (2 if kind == "MAT4" else n)
        acc = {"bufferView": len(views) - 1, "componentType": ctype, "count": count, "type": kind}
        if name == "POSITION":
            acc["min"], acc["max"] = [0, 0, 0], [5, 2, 0]
        accessors.append(acc)
        if name:
            attributes[name] = len(accessors) - 1
        bin_ += data
    doc = {
        "asset": {"version": "2.0"}, "scene": 0, "scenes": [{"nodes": [0, 2]}],
        "nodes": [{"name": "root", "children": [1]}, {"name": "tip", "translation": [0, 1, 0]},
                  {"name": "body", "mesh": 0, "skin": 0}],
        "skins": [{"joints": [0, 1], "inverseBindMatrices": len(accessors) - 1, "skeleton": 0}],
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
    (HERE / "test_skinned_groups.glb").write_bytes(build(False))
    (HERE / "test_skinned_colors.glb").write_bytes(build(True))


main()
