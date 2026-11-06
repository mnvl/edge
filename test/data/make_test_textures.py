# SPDX-License-Identifier: MIT
# Copyright (c) 2004-2026 Manvel Avetisian

"""Writes the procedural textures of the tests (needs numpy and Pillow); every pixel is made here, from fixed seeds:

test_stones.jpeg          256 x 256 rounded stones in mortar (a Voronoi pattern), the photo-like diffuse texture of the blending
                          tests and of the texture lifetime test.
cylinder_30_degrees.glb   its base colour image (the last buffer view) replaced by bands of colour with noise, so the model's
                          texture is ours; the geometry is left as it is.
"""
import io
import json
import struct
from pathlib import Path

import numpy as np
from PIL import Image

HERE = Path(__file__).resolve().parent


def stones(size=256, cells=40, seed=7):
    rng = np.random.default_rng(seed)
    centres = rng.uniform(0, size, (cells, 2))
    tint = rng.uniform(0.75, 1.0, (cells, 1)) * np.array([[176, 150, 110]])
    y, x = np.mgrid[0:size, 0:size].astype(np.float64)
    # distances to every centre, wrapped so that the texture tiles
    d = np.empty((cells, size, size))
    for k, (cx, cy) in enumerate(centres):
        dx = np.minimum(np.abs(x - cx), size - np.abs(x - cx))
        dy = np.minimum(np.abs(y - cy), size - np.abs(y - cy))
        d[k] = np.hypot(dx, dy)
    order = np.sort(d, axis=0)
    nearest = np.argmin(d, axis=0)
    edge = order[1] - order[0]                                   # distance to the cell's border
    shade = np.clip(edge / 6.0, 0, 1)[..., None]                 # rounded towards the mortar
    grain = rng.normal(0, 8, (size, size, 1))
    mortar = np.array([90, 82, 70])
    rgb = mortar + (tint[nearest] - mortar) * shade + grain
    return Image.fromarray(np.clip(rgb, 0, 255).astype(np.uint8), "RGB")


def bands(size=256, seed=11):
    rng = np.random.default_rng(seed)
    y, x = np.mgrid[0:size, 0:size] / size
    t = (x + y) * 3.0
    rgb = np.stack([0.5 + 0.5 * np.sin(2 * np.pi * t), 0.2 + 0.2 * np.sin(2 * np.pi * (t + 0.3)),
                    0.6 + 0.4 * np.cos(2 * np.pi * t)], axis=-1) * 255
    rgb += rng.normal(0, 10, rgb.shape)
    return Image.fromarray(np.clip(rgb, 0, 255).astype(np.uint8), "RGB")


def jpeg(image):
    out = io.BytesIO()
    image.save(out, "JPEG", quality=90)
    return out.getvalue()


def replace_glb_image(path, image_index, data, mime):
    """The GLB at path with images[image_index] (in a buffer view of the binary chunk) replaced by data."""
    blob = path.read_bytes()
    json_length = struct.unpack_from("<I", blob, 12)[0]
    gltf = json.loads(blob[20:20 + json_length])
    bin_start = 20 + json_length + 8
    binary = blob[bin_start:bin_start + struct.unpack_from("<I", blob, 20 + json_length)[0]]
    image = gltf["images"][image_index]
    target = image["bufferView"]
    views = gltf["bufferViews"]
    chunks, offset = [], 0
    for i, view in enumerate(views):                             # every view repacked, in order, 4-byte aligned
        start = view.get("byteOffset", 0)
        payload = data if i == target else binary[start:start + view["byteLength"]]
        pad = (-offset) % 4
        chunks.append(b"\0" * pad)
        offset += pad
        view["byteOffset"], view["byteLength"] = offset, len(payload)
        chunks.append(payload)
        offset += len(payload)
    body = b"".join(chunks)
    body += b"\0" * ((-len(body)) % 4)
    gltf["buffers"][0]["byteLength"] = len(body)
    image["mimeType"] = mime
    image["name"] = "bands"
    text = json.dumps(gltf, separators=(",", ":")).encode()
    text += b" " * ((-len(text)) % 4)
    header = struct.pack("<4sII", b"glTF", 2, 12 + 8 + len(text) + 8 + len(body))
    path.write_bytes(header + struct.pack("<I4s", len(text), b"JSON") + text + struct.pack("<I4s", len(body), b"BIN\0") + body)


if __name__ == "__main__":
    (HERE / "test_stones.jpeg").write_bytes(jpeg(stones()))
    replace_glb_image(HERE / "cylinder_30_degrees.glb", 0, jpeg(bands()), "image/jpeg")
