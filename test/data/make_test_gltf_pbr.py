# SPDX-License-Identifier: MIT
# Copyright (c) 2004-2026 Manvel Avetisian

"""Writes the glTF files test_gltf_pbr.cc reads (plain Python, no dependencies):

test_gltf_loader_pbr.glb: one quad in two primitives.
  material "pbr":   base colour, metallic-roughness, normal and emissive textures (4 x 4 PNGs),
                    metallicFactor 0.25, roughnessFactor 0.5, emissiveFactor (1, 0.5, 0.25) with
                    KHR_materials_emissive_strength 4, alphaMode MASK at 0.3.
  material "plain": nothing set (glTF defaults).
  The base colour and the emissive texture share one image, so the image is used as colour (sRGB)
  and the metallic-roughness texture uses it as data too (linear).
test_gltf_loader_bad_image.glb: the same quad, its base colour image bytes not an image.
"""
import json
import struct
import zlib
from pathlib import Path

HERE = Path(__file__).resolve().parent


def png(rgba):
    raw = b"".join(b"\0" + bytes(rgba) * 4 for _ in range(4))
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 4, 4, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))


def glb(images, materials, textures):
    pos = [(-1, 0, -1), (1, 0, -1), (1, 0, 1), (-1, 0, 1)]
    nrm = [(0, 1, 0)] * 4
    uv = [(0, 0), (1, 0), (1, 1), (0, 1)]
    bin_ = b"".join(struct.pack("<3f", *p) for p in pos) + b"".join(struct.pack("<3f", *n) for n in nrm) \
        + b"".join(struct.pack("<2f", *t) for t in uv) + struct.pack("<6H", 0, 2, 1, 0, 3, 2)
    views = [{"buffer": 0, "byteOffset": 0, "byteLength": 48}, {"buffer": 0, "byteOffset": 48, "byteLength": 48},
             {"buffer": 0, "byteOffset": 96, "byteLength": 32}, {"buffer": 0, "byteOffset": 128, "byteLength": 12}]
    while len(bin_) % 4:
        bin_ += b"\0"
    imgs = []
    for data in images:
        views.append({"buffer": 0, "byteOffset": len(bin_), "byteLength": len(data)})
        imgs.append({"bufferView": len(views) - 1, "mimeType": "image/png"})
        bin_ += data
        while len(bin_) % 4:
            bin_ += b"\0"
    acc = [{"bufferView": 0, "componentType": 5126, "count": 4, "type": "VEC3", "min": [-1, 0, -1], "max": [1, 0, 1]},
           {"bufferView": 1, "componentType": 5126, "count": 4, "type": "VEC3"},
           {"bufferView": 2, "componentType": 5126, "count": 4, "type": "VEC2"},
           {"bufferView": 3, "componentType": 5123, "count": 6, "type": "SCALAR"}]
    attrs = {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2}
    doc = {"asset": {"version": "2.0"}, "scene": 0, "scenes": [{"nodes": [0]}], "nodes": [{"mesh": 0, "name": "quad"}],
           "meshes": [{"primitives": [{"attributes": attrs, "indices": 3, "material": i} for i in range(len(materials))]}],
           "accessors": acc, "bufferViews": views, "buffers": [{"byteLength": len(bin_)}],
           "images": imgs, "samplers": [{}], "textures": textures, "materials": materials,
           "extensionsUsed": ["KHR_materials_emissive_strength"]}
    j = json.dumps(doc).encode()
    while len(j) % 4:
        j += b" "
    body = struct.pack("<I4s", len(j), b"JSON") + j + struct.pack("<I4s", len(bin_), b"BIN\0") + bin_
    return struct.pack("<4sII", b"glTF", 2, 12 + len(body)) + body


gray, mr, normal = png((128, 128, 128, 255)), png((0, 192, 128, 255)), png((128, 128, 255, 255))
textures = [{"source": i, "sampler": 0} for i in range(3)]
pbr = {"name": "pbr", "alphaMode": "MASK", "alphaCutoff": 0.3,
       "pbrMetallicRoughness": {"baseColorTexture": {"index": 0}, "metallicRoughnessTexture": {"index": 1},
                                "metallicFactor": 0.25, "roughnessFactor": 0.5},
       "normalTexture": {"index": 2}, "emissiveTexture": {"index": 0}, "emissiveFactor": [1, 0.5, 0.25],
       "extensions": {"KHR_materials_emissive_strength": {"emissiveStrength": 4.0}}}
(HERE / "test_gltf_loader_pbr.glb").write_bytes(glb([gray, mr, normal], [pbr, {"name": "plain"}], textures))
bad = {"name": "bad", "pbrMetallicRoughness": {"baseColorTexture": {"index": 0}}}
(HERE / "test_gltf_loader_bad_image.glb").write_bytes(
    glb([b"this is not an image at all"], [bad], [{"source": 0, "sampler": 0}]))
print("wrote test_gltf_loader_pbr.glb, test_gltf_loader_bad_image.glb")
