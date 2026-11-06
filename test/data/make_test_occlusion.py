# SPDX-License-Identifier: MIT
# Copyright (c) 2004-2026 Manvel Avetisian

"""Writes the glTF files test_occlusion.cc reads (plain Python, no dependencies), each a quad in z = 0 (x, y in -1..1), its images
PNGs in data: URIs:

test_occlusion.gltf: five primitives of the quad, one for each material:
  "orm"             metallic-roughness and occlusion of one image (glTF's ORM: occlusion red, roughness green, metallic blue)
                    through two textures, occlusion strength 0.5;
  "separate"        metallic-roughness of a 2 x 2 image, occlusion of another, 4 x 4 (its green and blue are not occlusion): packed
                    into one of 4 x 4, the metallic-roughness image sampled as the GPU samples it; strength 1 (glTF's, not given);
  "occlusion_only"  the 4 x 4 occlusion image and no metallic-roughness texture, strength 0.25;
  "plain"           the 2 x 2 metallic-roughness image, no occlusion;
  "separate_again"  the images of "separate" again: the same packed texture;
  "mr_of_orm"       the ORM image as metallic-roughness alone: the texture "orm" has;
  "small_occlusion" metallic-roughness of the 4 x 4 image, occlusion of the 2 x 2 ORM image: packed into one of 4 x 4, the
                    occlusion sampled as the GPU samples it.
test_occlusion_uv1.gltf: occlusion reading TEXCOORD_1 where the base colour reads TEXCOORD_0.
test_occlusion_dds.gltf: occlusion of a DDS (test_dds_bc5.dds, through MSFT_texture_dds) beside a PNG metallic-roughness image.
test_occlusion_strength.gltf: an occlusion strength of 1.5 (glTF's is 0 to 1).
"""
import base64
import json
import struct
import zlib
from pathlib import Path

HERE = Path(__file__).resolve().parent

# images, rows top first, (r, g, b, a) texels
ORM = [[(64, 128, 192, 255), (32, 96, 160, 255)], [(16, 80, 144, 255), (8, 72, 136, 255)]]          # 2 x 2
MR = [[(0, 0, 255, 255), (0, 200, 0, 255)], [(0, 100, 40, 255), (0, 40, 100, 255)]]                 # 2 x 2: r unused
OCCLUSION = [[(16 * (4 * y + x), 7, 9, 255) for x in range(4)] for y in range(4)]                   # 4 x 4: r 0, 16 .. 240


def png(rows):
    height, width = len(rows), len(rows[0])
    raw = b"".join(b"\0" + b"".join(bytes(t) for t in row) for row in rows)

    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))


def data_uri(data, mime):
    return f"data:{mime};base64," + base64.b64encode(data).decode()


def gltf(images, textures, materials, two_uv_sets=False, extensions=()):
    pos = [(-1, -1, 0), (1, -1, 0), (1, 1, 0), (-1, 1, 0)]
    uv = [(0, 1), (1, 1), (1, 0), (0, 0)]
    blob = b"".join(struct.pack("<3f", *p) for p in pos) + b"".join(struct.pack("<3f", 0, 0, 1) for _ in pos) \
        + b"".join(struct.pack("<2f", *t) for t in uv) + struct.pack("<6H", 0, 1, 2, 0, 2, 3) + b"\0\0"
    views = [{"buffer": 0, "byteOffset": 0, "byteLength": 48}, {"buffer": 0, "byteOffset": 48, "byteLength": 48},
             {"buffer": 0, "byteOffset": 96, "byteLength": 32}, {"buffer": 0, "byteOffset": 128, "byteLength": 12}]
    accessors = [{"bufferView": 0, "componentType": 5126, "count": 4, "type": "VEC3", "min": [-1, -1, 0], "max": [1, 1, 0]},
                 {"bufferView": 1, "componentType": 5126, "count": 4, "type": "VEC3"},
                 {"bufferView": 2, "componentType": 5126, "count": 4, "type": "VEC2"},
                 {"bufferView": 3, "componentType": 5123, "count": 6, "type": "SCALAR"}]
    attributes = {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2}
    if two_uv_sets:
        attributes["TEXCOORD_1"] = 2
    doc = {"asset": {"version": "2.0"}, "scene": 0, "scenes": [{"nodes": [0]}], "nodes": [{"mesh": 0, "name": "quad"}],
           "meshes": [{"primitives": [{"attributes": attributes, "indices": 3, "material": i} for i in range(len(materials))]}],
           "accessors": accessors, "bufferViews": views,
           "buffers": [{"byteLength": len(blob), "uri": data_uri(blob, "application/octet-stream")}],
           "images": images, "samplers": [{}], "textures": textures, "materials": materials}
    if extensions:
        doc["extensionsUsed"] = list(extensions)
    return json.dumps(doc, indent=1)


def image(rows):
    return {"uri": data_uri(png(rows), "image/png")}


images = [image(ORM), image(MR), image(OCCLUSION)]
textures = [{"source": 0, "sampler": 0}, {"source": 0, "sampler": 0}, {"source": 1, "sampler": 0}, {"source": 2, "sampler": 0}]
materials = [
    {"name": "orm", "pbrMetallicRoughness": {"metallicRoughnessTexture": {"index": 0}}, "occlusionTexture": {"index": 1, "strength": 0.5}},
    {"name": "separate", "pbrMetallicRoughness": {"metallicRoughnessTexture": {"index": 2}}, "occlusionTexture": {"index": 3}},
    {"name": "occlusion_only", "occlusionTexture": {"index": 3, "strength": 0.25}},
    {"name": "plain", "pbrMetallicRoughness": {"metallicRoughnessTexture": {"index": 2}}},
    {"name": "separate_again", "pbrMetallicRoughness": {"metallicRoughnessTexture": {"index": 2}}, "occlusionTexture": {"index": 3}},
    {"name": "mr_of_orm", "pbrMetallicRoughness": {"metallicRoughnessTexture": {"index": 0}}},
    {"name": "small_occlusion", "pbrMetallicRoughness": {"metallicRoughnessTexture": {"index": 3}}, "occlusionTexture": {"index": 1}},
]
(HERE / "test_occlusion.gltf").write_text(gltf(images, textures, materials))

uv1 = [{"name": "uv1", "pbrMetallicRoughness": {"baseColorTexture": {"index": 0}, "metallicRoughnessTexture": {"index": 0}},
        "occlusionTexture": {"index": 1, "texCoord": 1}}]
(HERE / "test_occlusion_uv1.gltf").write_text(gltf([image(MR), image(OCCLUSION)], [{"source": 0}, {"source": 1}], uv1, True))

dds = [{"name": "dds", "pbrMetallicRoughness": {"metallicRoughnessTexture": {"index": 0}}, "occlusionTexture": {"index": 1}}]
(HERE / "test_occlusion_dds.gltf").write_text(gltf(
    [image(MR), {"uri": "test_dds_bc5.dds", "mimeType": "image/vnd-ms.dds"}],
    [{"source": 0}, {"extensions": {"MSFT_texture_dds": {"source": 1}}}], dds, extensions=["MSFT_texture_dds"]))

strong = [{"name": "strong", "pbrMetallicRoughness": {"metallicRoughnessTexture": {"index": 0}}, "occlusionTexture": {"index": 0, "strength": 1.5}}]
(HERE / "test_occlusion_strength.gltf").write_text(gltf([image(ORM)], [{"source": 0}], strong))
print("wrote test_occlusion.gltf, test_occlusion_uv1.gltf, test_occlusion_dds.gltf, test_occlusion_strength.gltf")
