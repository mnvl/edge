# SPDX-License-Identifier: MIT
# Copyright (c) 2004-2026 Manvel Avetisian

"""pyedge.Joint exposes the hierarchy and the matrix draw() places meshes with.

The oracle is the glTF file itself: its node tree and each node's TRS,
composed here with numpy, independently of the loader.
"""
import json
import struct
from pathlib import Path

import numpy as np
import pytest

import pyedge
import pyrove

ASSET = Path(__file__).resolve().parent / "data" / "test_gltf_loder_animation.glb"


def _gltf_json(path: Path) -> dict:
    data = path.read_bytes()
    (length,) = struct.unpack_from("<I", data, 12)
    return json.loads(data[20:20 + length])


def _local(node: dict) -> np.ndarray:
    if "matrix" in node:
        return np.array(node["matrix"], dtype=np.float64).reshape(4, 4).T
    x, y, z, w = node.get("rotation", (0.0, 0.0, 0.0, 1.0))
    r = np.array([
        [1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
        [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
        [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)],
    ])
    m = np.eye(4)
    m[:3, :3] = r * np.asarray(node.get("scale", (1.0, 1.0, 1.0)))
    m[:3, 3] = node.get("translation", (0.0, 0.0, 0.0))
    return m


@pytest.fixture
def model():
    renderer = pyedge.Renderer(backend="null")
    model = pyedge.AnimatedModelPTBNJWT(renderer, renderer.create_scheduler())
    model.load_from_gltf(str(ASSET))
    return model


def _joints_by_name(model) -> dict:
    joints = [model.joint(i) for i in range(model.joints_count)]
    by_name = {j.name: j for j in joints}
    assert len(by_name) == len(joints), "asset node names must be unique for this test"
    return by_name


def test_joint_parents_mirror_the_gltf_node_tree(model):
    doc = _gltf_json(ASSET)
    parent_of = {c: i for i, n in enumerate(doc["nodes"]) for c in n.get("children", ())}
    by_name = _joints_by_name(model)
    assert set(by_name) == {n["name"] for n in doc["nodes"]}

    for i, node in enumerate(doc["nodes"]):
        parent = by_name[node["name"]].parent
        if i in parent_of:
            assert parent is not None and parent.name == doc["nodes"][parent_of[i]]["name"]
        else:
            assert parent is None


def test_joint_matrix_at_rest_is_the_composed_node_transforms(model):
    doc = _gltf_json(ASSET)
    parent_of = {c: i for i, n in enumerate(doc["nodes"]) for c in n.get("children", ())}
    by_name = _joints_by_name(model)

    for i, node in enumerate(doc["nodes"]):
        expected = _local(node)
        p = parent_of.get(i)
        while p is not None:
            expected = _local(doc["nodes"][p]) @ expected
            p = parent_of.get(p)
        got = by_name[node["name"]].matrix
        assert isinstance(got, pyrove.mat4)
        np.testing.assert_allclose(got.to_numpy(), expected, atol=1e-5, err_msg=node["name"])


def test_joint_matrix_follows_the_animation_and_is_a_copy(model):
    by_name = _joints_by_name(model)
    tip = by_name["joint3"]
    animation = model.animations[0]

    animation.pose(0.0)
    at_start = tip.matrix.to_numpy()
    animation.pose(animation.duration * 0.5)
    halfway = tip.matrix.to_numpy()
    assert not np.allclose(at_start, halfway, atol=1e-4), "the joint did not move with the animation"

    copy = tip.matrix
    copy.identity()
    np.testing.assert_array_equal(tip.matrix.to_numpy(), halfway)
