# SPDX-License-Identifier: MIT
# Copyright (c) 2004-2026 Manvel Avetisian

"""Writes the glTF files test_vertex_colors.cc reads (plain Python, no dependencies; the writer is make_test_attributes_glb.py's): a
triangle with a position, a normal (+Z) and a texcoord at each vertex, and COLOR_0 of the several kinds glTF allows, or none.

  test_vertex_colors.glb         COLOR_0 float VEC4: (1, 0, 0, 0.5) (0, 1, 0, 2.5) (0, 0, 1, 4): a value above 1 is an emission strength
  test_vertex_colors_vec3.glb    COLOR_0 float VEC3: (0.25, 0.5, 0.75) (1, 0, 0) (0, 1, 0.5): the alpha is 1, as glTF says
  test_vertex_colors_ubyte.glb   COLOR_0 normalised unsigned byte VEC4: (255, 0, 51, 102) (0, 255, 0, 255) (102, 51, 255, 0)
  test_vertex_colors_ushort.glb  COLOR_0 normalised unsigned short VEC4: (65535, 0, 13107, 26214) (0, 65535, 0, 65535) (26214, 13107, 65535, 0)
  test_vertex_colors_two.glb     two primitives of one mesh, each with COLOR_0: red 0.5 on the first three vertices, blue 2 on the next
  test_vertex_colors_mixed.glb   two primitives of one mesh, the first with COLOR_0 and the second without
  test_vertex_colors_none.glb    no COLOR_0
"""
from make_test_attributes_glb import FLOAT, UBYTE, USHORT, attribute, write

NORMALS = attribute("VEC3", FLOAT, [(0.0, 0.0, 1.0)] * 3)
UVS = attribute("VEC2", FLOAT, [(0.0, 0.0), (1.0, 0.0), (0.0, 1.0)])


def primitive(colors=None):
    out = {"NORMAL": NORMALS, "TEXCOORD_0": UVS}
    if colors is not None:
        out["COLOR_0"] = colors
    return out


def main():
    write("test_vertex_colors.glb", [primitive(attribute("VEC4", FLOAT, [(1.0, 0.0, 0.0, 0.5), (0.0, 1.0, 0.0, 2.5), (0.0, 0.0, 1.0, 4.0)]))])
    write("test_vertex_colors_vec3.glb", [primitive(attribute("VEC3", FLOAT, [(0.25, 0.5, 0.75), (1.0, 0.0, 0.0), (0.0, 1.0, 0.5)]))])
    write("test_vertex_colors_ubyte.glb", [primitive(attribute("VEC4", UBYTE, [(255, 0, 51, 102), (0, 255, 0, 255), (102, 51, 255, 0)], normalized=True))])
    write("test_vertex_colors_ushort.glb", [primitive(attribute("VEC4", USHORT, [(65535, 0, 13107, 26214), (0, 65535, 0, 65535), (26214, 13107, 65535, 0)], normalized=True))])
    write("test_vertex_colors_two.glb", [primitive(attribute("VEC4", FLOAT, [(1.0, 0.0, 0.0, 0.5)] * 3)), primitive(attribute("VEC4", FLOAT, [(0.0, 0.0, 1.0, 2.0)] * 3))])
    write("test_vertex_colors_mixed.glb", [primitive(attribute("VEC4", FLOAT, [(1.0, 0.0, 0.0, 0.5)] * 3)), primitive()])
    write("test_vertex_colors_none.glb", [primitive()])


main()
