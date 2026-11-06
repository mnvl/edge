# SPDX-License-Identifier: MIT
# Copyright (c) 2004-2026 Manvel Avetisian

"""Writes the DDS test assets test_dds.cc reads (needs numpy and etcpak):

test_dds_bc7_mips.dds   8 x 8 BC7 (DXGI 99, sRGB) with 4 levels, each one colour: level 0 red,
                        1 green, 2 blue, 3 white (so a test can tell which level was sampled).
test_dds_bc5.dds        8 x 8 BC5 (DXGI 83), red 200 / 255, green 50 / 255, 1 level.
test_dds_truncated.dds  test_dds_bc5.dds without its last byte.
test_dds_astc4x4_mips.dds, test_dds_astc6x6_mips.dds, test_dds_astc8x8_mips.dds
                        ASTC (DXGI 135, 151, 163: sRGB) of 8 x 8, 12 x 12 and 16 x 16 with the same coloured levels as the
                        BC7 file; needs the astcenc CLI, named by the environment variable ASTCENC.
test_gltf_loader_dds.glb  a quad whose base colour texture has a PNG source and, through
                        MSFT_texture_dds, test_dds_bc7_mips.dds.
"""
import os
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

import etcpak
import numpy as np

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import make_test_gltf_pbr as pbr  # noqa: E402


def dds(width, height, dxgi, levels):
    header = bytearray(124)
    struct.pack_into("<IIIIIII", header, 0, 124, 0x1 | 0x2 | 0x4 | 0x1000 | 0x20000, height, width, 0, 0, len(levels))
    struct.pack_into("<II4s", header, 72, 32, 0x4, b"DX10")          # pixel format: FourCC
    struct.pack_into("<I", header, 104, 0x1000 | 0x400000 | 0x8)      # caps: texture, mipmap, complex
    dx10 = struct.pack("<IIIII", dxgi, 3, 0, 1, 0)
    return b"DDS " + bytes(header) + dx10 + b"".join(levels)


def solid(w, h, rgba):
    img = np.zeros((max(4, h), max(4, w), 4), np.uint8)
    img[...] = rgba
    return img.tobytes(), max(4, w), max(4, h)


colours = [(255, 0, 0, 255), (0, 255, 0, 255), (0, 0, 255, 255), (255, 255, 255, 255)]
levels = []
for i, c in enumerate(colours):
    data, w, h = solid(8 >> i, 8 >> i, c)
    levels.append(etcpak.compress_bc7(data, w, h, None))
(HERE / "test_dds_bc7_mips.dds").write_bytes(dds(8, 8, 99, levels))
data, w, h = solid(8, 8, (200, 50, 0, 255))
bc5 = dds(8, 8, 83, [etcpak.compress_bc5(data, w, h)])
(HERE / "test_dds_bc5.dds").write_bytes(bc5)
(HERE / "test_dds_truncated.dds").write_bytes(bc5[:-1])



def astc_blocks(w, h, block, rgba):
    """The blocks of a w x h solid-colour image as astcenc (sRGB) makes them."""
    from PIL import Image
    with tempfile.TemporaryDirectory() as d:
        Image.new("RGBA", (w, h), rgba).save(f"{d}/in.png")
        subprocess.run([os.environ["ASTCENC"], "-cs", f"{d}/in.png", f"{d}/out.astc", f"{block}x{block}", "-medium"], check=True, capture_output=True)
        data = Path(f"{d}/out.astc").read_bytes()
    return data[16:]                                   # the .astc header is 16 bytes


for block, side, dxgi in ((4, 8, 135), (6, 12, 151), (8, 16, 163)):
    astc_levels = [astc_blocks(max(1, side >> i), max(1, side >> i), block, c) for i, c in enumerate(colours)]
    (HERE / f"test_dds_astc{block}x{block}_mips.dds").write_bytes(dds(side, side, dxgi, astc_levels))

# a glb with a PNG fallback and the DDS through MSFT_texture_dds
g = pbr.glb([pbr.png((128, 128, 128, 255))], [{"name": "dds", "pbrMetallicRoughness": {"baseColorTexture": {"index": 0}}}],
            [{"source": 0, "sampler": 0, "extensions": {"MSFT_texture_dds": {"source": 1}}}])
import json  # noqa: E402
n = struct.unpack_from("<I", g, 12)[0]
doc = json.loads(g[20:20 + n])
doc["images"].append({"uri": "test_dds_bc7_mips.dds", "mimeType": "image/vnd-ms.dds"})
doc["extensionsUsed"] = doc.get("extensionsUsed", []) + ["MSFT_texture_dds"]
j = json.dumps(doc).encode()
while len(j) % 4:
    j += b" "
rest = g[20 + n:]
body = struct.pack("<I4s", len(j), b"JSON") + j + rest
(HERE / "test_gltf_loader_dds.glb").write_bytes(struct.pack("<4sII", b"glTF", 2, 12 + len(body)) + body)
print("wrote the DDS test assets")
