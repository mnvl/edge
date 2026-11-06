# SPDX-License-Identifier: MIT
# Copyright (c) 2004-2026 Manvel Avetisian

"""test_external_buffer.gltf and .bin: test_uv_set_1.gltf with its buffer in a file of its own, as a .gltf exported with "separate"
buffers has it.  test_gltf_loader.cc loads it and the original: the same model.

    python3 test/data/make_test_external_buffer.py
"""
import base64
import json
from pathlib import Path

HERE = Path(__file__).resolve().parent

gltf = json.loads((HERE / "test_uv_set_1.gltf").read_text())
(buffer,) = gltf["buffers"]
prefix, data = buffer["uri"].split(",", 1)
assert prefix == "data:application/octet-stream;base64", prefix
(HERE / "test_external_buffer.bin").write_bytes(base64.b64decode(data))
buffer["uri"] = "test_external_buffer.bin"
(HERE / "test_external_buffer.gltf").write_text(json.dumps(gltf, indent=1) + "\n")
