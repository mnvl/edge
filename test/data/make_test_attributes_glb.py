# SPDX-License-Identifier: MIT
# Copyright (c) 2004-2026 Manvel Avetisian

"""Writes the glTF files test_custom_attributes.cc reads (plain Python, no dependencies): triangles with application-defined vertex
attributes (names starting with an underscore, which glTF leaves to the application) of several kinds, and the ways a file can get
them wrong.

  test_attributes.glb          one primitive, three vertices: _SCALAR float 10 20 30; _VEC3 float (1,2,3) (4,5,6) (7,8,9);
                               _BYTES normalised unsigned byte VEC2 (0,255) (51,102) (255,0); _INTS unsigned short 7 8 9
  test_attributes_two.glb      two primitives of one mesh, each with _FOO float: 1 2 3 and 4 5 6
  test_attributes_mixed.glb    two primitives of one mesh, the first with _FOO and the second without
  test_attributes_widths.glb   two primitives of one mesh, _FOO a scalar on the first and a VEC2 on the second
  test_attributes_matrix.glb   _MAT a MAT2 of float: a custom attribute that is not a vector
"""
import json
import struct
from pathlib import Path

HERE = Path(__file__).resolve().parent

FLOAT, UBYTE, USHORT, UINT = 5126, 5121, 5123, 5125
WIDTH = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT2": 4}
FORMAT = {FLOAT: "f", UBYTE: "B", USHORT: "H", UINT: "I"}
TRIANGLE = [(0, 0, 0), (1, 0, 0), (0, 1, 0)]


def attribute(kind, ctype, rows, normalized=False):
    return kind, ctype, rows, normalized


def write(name, primitives):
    """`primitives`: one dict of attribute name -> attribute(...) per primitive, each over the three vertices of TRIANGLE."""
    bin_, views, accessors, doc_primitives = b"", [], [], []

    def add(data, ctype, kind, count, target, normalized=False, bounds=None):
        nonlocal bin_
        views.append({"buffer": 0, "byteOffset": len(bin_), "byteLength": len(data), "target": target})
        acc = {"bufferView": len(views) - 1, "componentType": ctype, "count": count, "type": kind}
        if normalized:
            acc["normalized"] = True
        if bounds:
            acc["min"], acc["max"] = bounds
        accessors.append(acc)
        bin_ += data + b"\0" * (-len(data) % 4)
        return len(accessors) - 1

    for attributes in primitives:
        out = {"POSITION": add(b"".join(struct.pack("<3f", *p) for p in TRIANGLE), FLOAT, "VEC3", 3, 34962, bounds=([0, 0, 0], [1, 1, 0]))}
        for key, (kind, ctype, rows, normalized) in attributes.items():
            flat = [x for row in rows for x in (row if isinstance(row, tuple) else (row,))]
            data = struct.pack("<%d%s" % (len(flat), FORMAT[ctype]), *flat)
            out[key] = add(data, ctype, kind, len(rows), 34962, normalized)
        indices = add(struct.pack("<3I", 0, 1, 2), UINT, "SCALAR", 3, 34963)
        doc_primitives.append({"attributes": out, "indices": indices, "mode": 4})
    doc = {
        "asset": {"version": "2.0"}, "scene": 0, "scenes": [{"nodes": [0]}], "nodes": [{"name": "thing", "mesh": 0}],
        "meshes": [{"name": "thing", "primitives": doc_primitives}],
        "accessors": accessors, "bufferViews": views, "buffers": [{"byteLength": len(bin_)}],
    }
    js = json.dumps(doc).encode()
    js += b" " * (-len(js) % 4)
    blob = b"glTF" + struct.pack("<II", 2, 12 + 8 + len(js) + 8 + len(bin_))
    blob += struct.pack("<I4s", len(js), b"JSON") + js + struct.pack("<I4s", len(bin_), b"BIN\0") + bin_
    (HERE / name).write_bytes(blob)


def main():
    write("test_attributes.glb", [{
        "_SCALAR": attribute("SCALAR", FLOAT, [10.0, 20.0, 30.0]),
        "_VEC3": attribute("VEC3", FLOAT, [(1.0, 2.0, 3.0), (4.0, 5.0, 6.0), (7.0, 8.0, 9.0)]),
        "_BYTES": attribute("VEC2", UBYTE, [(0, 255), (51, 102), (255, 0)], normalized=True),
        "_INTS": attribute("SCALAR", USHORT, [7, 8, 9]),
    }])
    write("test_attributes_two.glb", [{"_FOO": attribute("SCALAR", FLOAT, [1.0, 2.0, 3.0])},
                                      {"_FOO": attribute("SCALAR", FLOAT, [4.0, 5.0, 6.0])}])
    write("test_attributes_mixed.glb", [{"_FOO": attribute("SCALAR", FLOAT, [1.0, 2.0, 3.0])}, {}])
    write("test_attributes_widths.glb", [{"_FOO": attribute("SCALAR", FLOAT, [1.0, 2.0, 3.0])},
                                         {"_FOO": attribute("VEC2", FLOAT, [(1.0, 2.0), (3.0, 4.0), (5.0, 6.0)])}])
    write("test_attributes_matrix.glb", [{"_MAT": attribute("MAT2", FLOAT, [(1.0, 0.0, 0.0, 1.0)] * 3)}])


if __name__ == "__main__":
    main()
