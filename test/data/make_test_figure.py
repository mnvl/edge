# SPDX-License-Identifier: MIT
# Copyright (c) 2004-2026 Manvel Avetisian

"""Writes test_figure.glb (needs numpy and Pillow; every vertex, key and texel is made here, from fixed seeds, so a second run writes
the same bytes): the skinned, animated character of the tests (skinning, pose boxes, culling, levels of detail, atlases, .edge
files, colliders, edge_shot) -- a stylised walking figure 1.75 m tall, Y up, facing +Z, its left along +X, made of capsules,
ellipsoids and tori.

Skeleton: one skin of 80 joints under a node "Armature" (not a joint): Root at the ground, Hips, a spine of three, neck, head (with jaw,
eyes and a ponytail of six), arms with five fingers of four joints each, legs down to the toes, a backpack, and four unweighted IK
targets.  Every joint's rest rotation is the identity, so its translation is its offset from its parent; the inverse bind matrices are
the rest pose's.

Meshes (one primitive each, one material each, all double-sided and opaque; 53,632 triangles in all, every mesh at least the 300
that levels of detail need):
  body       head, neck, arms, hands and fingers: the skin texture (JPEG) and a normal map
  shirt      torso and short sleeves, with COLOR_0 and COLOR_1, which its material does not use
  trousers   pelvis and legs
  shoes      their coordinates run to 3 (the texture tiles: an atlas leaves it out) with a normal map; JPEG
  hair       a cap and a ponytail
  eyes
  eyebrows   no texture, a base colour factor
  belt       a torus round the waist
  watch      a band round the left wrist
  backpack
Every vertex has up to four joints, its weights falling off smoothly with its distance from each bone it may follow.

Clips (linear, keyed at 24 per second from 1/24 s, as Blender exports them): "Dance" to 3 s, "Idle" to 6.417 s, "Walk" to 1.25 s (a
looping gait, its last key its first).  Each turns the same 41 joints, moves the hips (translation) and breathes with the chest (scale,
which the children follow), so every clip has translation, rotation and scale channels.
"""
import io
import json
import math
import struct
from pathlib import Path

import numpy as np
from PIL import Image

HERE = Path(__file__).resolve().parent
FPS = 24
START = 1.0 / FPS

# ---- skeleton -------------------------------------------------------------------------------------------------------------------

JOINTS = []          # (name, parent name or None, rest position in model space), parents first


def joint(name, parent, x, y, z):
    JOINTS.append((name, parent, np.array([x, y, z], float)))


joint("Root", None, 0, 0, 0)
joint("Hips", "Root", 0, 0.95, 0)
joint("Spine", "Hips", 0, 1.05, 0)
joint("Spine1", "Spine", 0, 1.17, -0.005)
joint("Spine2", "Spine1", 0, 1.29, -0.01)
joint("Neck", "Spine2", 0, 1.45, -0.01)
joint("Head", "Neck", 0, 1.54, 0)
joint("HeadTop_End", "Head", 0, 1.745, 0)
joint("Jaw", "Head", 0, 1.575, 0.03)
joint("LeftEye", "Head", 0.032, 1.635, 0.094)
joint("RightEye", "Head", -0.032, 1.635, 0.094)
for k in range(6):
    joint(f"Ponytail{k + 1}", "Head" if k == 0 else f"Ponytail{k}", 0, 1.66 - 0.045 * k, -0.105 - 0.015 * k)
joint("Backpack", "Spine2", 0, 1.21, -0.175)

FINGERS = {          # knuckle (x, y, z) relative to the hand, segment lengths, radius
    "Index": ((0.006, -0.085, 0.030), (0.040, 0.025, 0.022), 0.0085),
    "Middle": ((0.007, -0.090, 0.010), (0.045, 0.028, 0.024), 0.0088),
    "Ring": ((0.006, -0.086, -0.010), (0.042, 0.026, 0.022), 0.0083),
    "Pinky": ((0.004, -0.078, -0.028), (0.032, 0.020, 0.018), 0.0074),
}
THUMB = [(-0.010, -0.015, 0.025), (-0.017, -0.045, 0.050), (-0.019, -0.070, 0.065), (-0.020, -0.090, 0.075)]

for side, s in (("Left", 1), ("Right", -1)):
    joint(f"{side}Shoulder", "Spine2", s * 0.035, 1.42, -0.01)
    joint(f"{side}Arm", f"{side}Shoulder", s * 0.175, 1.405, -0.01)
    joint(f"{side}ForeArm", f"{side}Arm", s * 0.215, 1.125, -0.015)
    joint(f"{side}Hand", f"{side}ForeArm", s * 0.245, 0.875, 0.0)
    hand = np.array([s * 0.245, 0.875, 0.0])
    for k, (dx, dy, dz) in enumerate(THUMB):
        joint(f"{side}HandThumb{k + 1}", f"{side}Hand" if k == 0 else f"{side}HandThumb{k}", *(hand + [s * dx, dy, dz]))
    for finger, (knuckle, lengths, _) in FINGERS.items():
        p = hand + [s * knuckle[0], knuckle[1], knuckle[2]]
        joint(f"{side}Hand{finger}1", f"{side}Hand", *p)
        for k, length in enumerate(lengths):
            p = p + [0, -length, 0]
            joint(f"{side}Hand{finger}{k + 2}", f"{side}Hand{finger}{k + 1}", *p)
for side, s in (("Left", 1), ("Right", -1)):
    joint(f"{side}UpLeg", "Hips", s * 0.09, 0.92, 0)
    joint(f"{side}Leg", f"{side}UpLeg", s * 0.095, 0.505, 0.005)
    joint(f"{side}Foot", f"{side}Leg", s * 0.10, 0.085, -0.025)
    joint(f"{side}ToeBase", f"{side}Foot", s * 0.10, 0.025, 0.105)
    joint(f"{side}Toe_End", f"{side}ToeBase", s * 0.10, 0.025, 0.175)
for side, s in (("Left", 1), ("Right", -1)):
    joint(f"{side}HandIK", "Root", s * 0.245, 0.875, 0.0)
    joint(f"{side}FootIK", "Root", s * 0.10, 0.085, -0.025)

INDEX = {name: i for i, (name, _, _) in enumerate(JOINTS)}
POS = {name: p for name, _, p in JOINTS}


def bone(name, to=None, end=None):
    """A segment a vertex may follow, moved by joint @p name: from the joint to @p to (a joint's name) or to the point @p end."""
    return INDEX[name], POS[name], POS[to] if to is not None else np.asarray(end, float)


# ---- surfaces -----------------------------------------------------------------------------------------------------------------

def frame(tangent, hint):
    hint = np.asarray(hint, float)
    n1 = hint - np.dot(hint, tangent) * tangent
    if np.linalg.norm(n1) < 1e-6:
        n1 = np.array([0, 0, 1.0]) - tangent[2] * tangent
    n1 /= np.linalg.norm(n1)
    return n1, np.cross(tangent, n1)


def rings_to_mesh(rings, uvs, poles=(), pole_uvs=()):
    """Vertices and triangles of a surface given as rings of points (each with its seam point repeated at its end), joined one to the
    next, and the poles that close its first and last ring ((start, end), either None)."""
    count, around = rings.shape[0], rings.shape[1] - 1
    positions = [rings.reshape(-1, 3)]
    texcoords = [uvs.reshape(-1, 2)]
    tris = []
    k = np.arange(around)
    for r in range(count - 1):
        a, b = r * (around + 1) + k, (r + 1) * (around + 1) + k
        tris += [np.stack([a, b, b + 1], 1), np.stack([a, b + 1, a + 1], 1)]
    base = count * (around + 1)
    if poles and poles[0] is not None:
        positions.append(poles[0][None])
        texcoords.append(np.asarray(pole_uvs[0], float)[None])
        tris.append(np.stack([np.full(around, base), k + 1, k], 1))
        base += 1
    if poles and poles[1] is not None:
        positions.append(poles[1][None])
        texcoords.append(np.asarray(pole_uvs[1], float)[None])
        last = (count - 1) * (around + 1)
        tris.append(np.stack([np.full(around, base), last + k, last + k + 1], 1))
    return np.concatenate(positions), np.concatenate(texcoords), np.concatenate(tris)


def capsule(path, radii, around, along, caps=5, hint=(1, 0, 0), uv_scale=(1, 1), cap_depth=1.0):
    """A tube along the polyline @p path whose cross-section at each of its points is an ellipse of half-axes @p radii (r1 along @p
    hint, r2 across it), closed by a half-ellipsoid at either end
    (@p cap_depth times as deep as its smaller half-axis)."""
    path, radii = np.asarray(path, float), np.asarray(radii, float)
    if radii.ndim == 1:
        radii = np.stack([radii, radii], 1)
    lengths = np.r_[0, np.cumsum(np.linalg.norm(np.diff(path, axis=0), axis=1))]
    s = np.linspace(0, lengths[-1], along)
    centres = np.stack([np.interp(s, lengths, path[:, i]) for i in range(3)], 1)
    rr = np.stack([np.interp(s, lengths, radii[:, i]) for i in range(2)], 1)
    tangents = np.gradient(centres, axis=0)
    tangents /= np.linalg.norm(tangents, axis=1, keepdims=True)
    theta = np.linspace(0, 2 * np.pi, around + 1)
    stations = []          # (centre, n1, n2, r1, r2) of every ring, the caps' too

    def cap_rings(i, sign):
        n1, n2 = frame(tangents[i], hint)
        depth = min(rr[i]) * cap_depth
        out = []
        for j in range(1, caps):
            phi = j * (np.pi / 2) / caps
            out.append((centres[i] + sign * tangents[i] * depth * math.cos(phi), n1, n2, rr[i, 0] * math.sin(phi), rr[i, 1] * math.sin(phi)))
        return out, centres[i] + sign * tangents[i] * depth

    start, start_pole = cap_rings(0, -1)
    stations += start
    for i in range(along):
        n1, n2 = frame(tangents[i], hint)
        stations.append((centres[i], n1, n2, rr[i, 0], rr[i, 1]))
    end, end_pole = cap_rings(along - 1, 1)
    stations += end[::-1]
    rings = np.array([[c + r1 * math.cos(t) * n1 + r2 * math.sin(t) * n2 for t in theta] for c, n1, n2, r1, r2 in stations])
    v = np.linspace(0, 1, len(stations) + 2)[1:-1]
    uvs = np.stack(np.broadcast_arrays(theta[None, :] / (2 * np.pi), v[:, None]), -1) * uv_scale
    return rings_to_mesh(rings, uvs, (start_pole, end_pole), ((0.5 * uv_scale[0], 0), (0.5 * uv_scale[0], uv_scale[1])))


def ellipsoid(centre, radii, around, count, axis=1):
    """An ellipsoid with its poles along @p axis (0, 1, 2)."""
    centre, radii = np.asarray(centre, float), np.asarray(radii, float)
    a, b = [(1, 2), (2, 0), (0, 1)][axis]
    theta = np.linspace(0, 2 * np.pi, around + 1)
    phi = np.linspace(0, np.pi, count + 2)[1:-1]
    unit = np.zeros((count, around + 1, 3))
    unit[..., axis] = -np.cos(phi)[:, None]
    unit[..., a] = np.sin(phi)[:, None] * np.cos(theta)[None]
    unit[..., b] = np.sin(phi)[:, None] * np.sin(theta)[None]
    pole = np.zeros(3)
    pole[axis] = 1
    uvs = np.stack(np.broadcast_arrays(theta[None, :] / (2 * np.pi), (phi / np.pi)[:, None]), -1)
    return rings_to_mesh(centre + unit * radii, uvs, (centre - pole * radii, centre + pole * radii), ((0.5, 0), (0.5, 1)))


def torus(centre, axis, major, minor, around, count):
    """A ring round @p axis: its centre line an ellipse of half-axes @p major, its section one of half-axes @p minor (across the
    ring, along the axis)."""
    axis = np.asarray(axis, float) / np.linalg.norm(axis)
    n1, n2 = frame(axis, (1, 0, 0))
    theta = np.linspace(0, 2 * np.pi, around + 1)
    phi = np.linspace(0, 2 * np.pi, count + 1)
    out = (np.cos(theta)[:, None] * major[0] * n1 + np.sin(theta)[:, None] * major[1] * n2)          # centre line
    radial = out / np.linalg.norm(out, axis=1, keepdims=True)
    rings = centre + out[None] + np.cos(phi)[:, None, None] * minor[0] * radial[None] + np.sin(phi)[:, None, None] * minor[1] * axis
    uvs = np.stack(np.broadcast_arrays(theta[None, :] / (2 * np.pi), (phi / (2 * np.pi))[:, None]), -1)
    return rings_to_mesh(rings, uvs)


def normals_of(positions, tris):
    """Smooth normals (area-weighted, one for every point, whichever copies of it the seams make) and the triangles turned outward."""
    a, b, c = (positions[tris[:, i]] for i in range(3))
    if np.einsum("ij,ij->i", a, np.cross(b, c)).sum() < 0:
        tris = tris[:, [0, 2, 1]]
        b, c = c, b
    face = np.cross(b - a, c - a)
    _, point, = np.unique(np.round(positions * 1e5).astype(np.int64), axis=0, return_inverse=True)
    point = point.ravel()
    sums = np.zeros((point.max() + 1, 3))
    for i in range(3):
        np.add.at(sums, point[tris[:, i]], face)
    normals = sums[point]
    return normals / np.linalg.norm(normals, axis=1, keepdims=True), tris


def weights_of(positions, bones, falloff):
    """Up to four (joint, weight) for each vertex: exp(-(d - nearest) / falloff) over the distances d to the bones, the four largest
    kept and made to sum to 255 (unsigned bytes)."""
    d = []
    for _, a, b in bones:
        ab = b - a
        t = np.clip((positions - a) @ ab / max(ab @ ab, 1e-12), 0, 1)
        d.append(np.linalg.norm(positions - (a + t[:, None] * ab), axis=1))
    d = np.stack(d, 1)
    w = np.exp(-(d - d.min(axis=1, keepdims=True)) / falloff)
    w[w < 0.02] = 0
    if w.shape[1] < 4:
        w = np.pad(w, ((0, 0), (0, 4 - w.shape[1])))
        bones = list(bones) + [bones[0]] * (4 - len(bones))
    order = np.argsort(-w, axis=1, kind="stable")[:, :4]
    w = np.take_along_axis(w, order, 1)
    w /= w.sum(axis=1, keepdims=True)
    q = np.floor(w * 255).astype(int)
    rest = 255 - q.sum(axis=1)
    frac = w * 255 - q
    for n in range(len(q)):
        for k in np.argsort(-frac[n], kind="stable")[:rest[n]]:
            q[n, k] += 1
    joints = np.array([bone_[0] for bone_ in bones])[order]
    joints[q == 0] = 0
    return joints.astype(np.uint8), q.astype(np.uint8)


class Part:
    def __init__(self, surface, bones, falloff):
        positions, self.uvs, tris = surface
        self.normals, self.tris = normals_of(positions, tris)
        self.positions = positions
        self.joints, self.weights = weights_of(positions, bones, falloff)


def mirror(x, side):
    return np.array(x, float) * ([1, 1, 1] if side == "Left" else [-1, 1, 1])


def body_parts():
    parts = [
        Part(ellipsoid((0, 1.62, 0.01), (0.085, 0.11, 0.10), 56, 34),
             [bone("Head", "HeadTop_End"), bone("Neck", "Head"), bone("Jaw", end=(0, 1.52, 0.085))], 0.02),
        Part(capsule([(0, 1.40, -0.01), (0, 1.58, 0.0)], [0.048, 0.048], 28, 12),
             [bone("Spine2", "Neck"), bone("Neck", "Head"), bone("Head", "HeadTop_End")], 0.02),
    ]
    for side in ("Left", "Right"):
        arm, elbow, wrist, knuckle = (POS[side + n] for n in ("Arm", "ForeArm", "Hand", "HandMiddle1"))
        parts.append(Part(capsule([mirror((0.15, 1.425, -0.01), side), arm, elbow, wrist, wrist + (0, -0.02, 0)],
                                  [0.05, 0.048, 0.037, 0.029, 0.027], 36, 44),
                          [bone(side + "Shoulder", side + "Arm"), bone(side + "Arm", side + "ForeArm"),
                           bone(side + "ForeArm", side + "Hand"), bone(side + "Hand", side + "HandMiddle1")], 0.02))
        parts.append(Part(capsule([wrist + (0, -0.005, 0), knuckle + (0, 0.005, -0.004)], np.array([[0.016, 0.042]] * 2), 28, 14,
                                  hint=(1, 0, 0)),
                          [bone(side + "Hand", side + "HandMiddle1")] + [bone(f"{side}Hand{f}1", f"{side}Hand{f}2") for f in FINGERS]
                          + [bone(side + "HandThumb1", side + "HandThumb2"), bone(side + "ForeArm", side + "Hand")], 0.008))
        for finger, (_, _, radius) in list(FINGERS.items()) + [("Thumb", (None, None, 0.0105))]:
            chain = [f"{side}Hand{finger}{k}" for k in range(1, 5)]
            path = [POS[n] for n in chain]
            parts.append(Part(capsule(path, [radius, radius * 0.95, radius * 0.9, radius * 0.85][:len(path)], 14, 14),
                              [bone(a, b) for a, b in zip(chain, chain[1:])] + [bone(side + "Hand", side + "HandMiddle1")], 0.006))
    return parts


def shirt_parts():
    y = [0.89, 1.0, 1.12, 1.25, 1.36, 1.42, 1.46]
    rx = [0.150, 0.146, 0.155, 0.165, 0.168, 0.14, 0.10]
    rz = [0.105, 0.100, 0.108, 0.115, 0.110, 0.09, 0.07]
    parts = [Part(capsule([(0, yy, -0.008) for yy in y], np.stack([rx, rz], 1), 64, 46, caps=6, cap_depth=0.4),
                  [bone("Hips", "Spine"), bone("Spine", "Spine1"), bone("Spine1", "Spine2"), bone("Spine2", "Neck"),
                   bone("LeftShoulder", "LeftArm"), bone("RightShoulder", "RightArm")], 0.03)]
    for side in ("Left", "Right"):
        arm, fore = POS[side + "Arm"], POS[side + "ForeArm"]
        parts.append(Part(capsule([mirror((0.13, 1.43, -0.01), side), arm, arm + 0.42 * (fore - arm)], [0.062, 0.060, 0.050], 32, 14),
                          [bone(side + "Shoulder", side + "Arm"), bone(side + "Arm", side + "ForeArm"), bone("Spine2", "Neck")], 0.025))
    return parts


def trousers_parts():
    parts = [Part(ellipsoid((0, 0.93, -0.005), (0.158, 0.10, 0.112), 44, 20),
                  [bone("Hips", "Spine"), bone("LeftUpLeg", "LeftLeg"), bone("RightUpLeg", "RightLeg")], 0.03)]
    for side in ("Left", "Right"):
        parts.append(Part(capsule([mirror((0.088, 0.97, 0), side), POS[side + "Leg"], mirror((0.10, 0.10, -0.02), side)],
                                  [0.078, 0.056, 0.046], 36, 48),
                          [bone("Hips", end=mirror((0.09, 0.95, 0), side)),
                           bone(side + "UpLeg", side + "Leg"), bone(side + "Leg", side + "Foot")], 0.025))
    return parts


def shoes_parts():
    return [Part(capsule([mirror((0.10, 0.045, -0.065), side), mirror((0.10, 0.042, 0.05), side), mirror((0.10, 0.036, 0.185), side)],
                         [(0.045, 0.045), (0.047, 0.042), (0.042, 0.034)], 28, 24, uv_scale=(3, 2)),
                 [bone(side + "Foot", side + "ToeBase"), bone(side + "ToeBase", side + "Toe_End"), bone(side + "Leg", side + "Foot")], 0.02)
            for side in ("Left", "Right")]


def hair_parts():
    chain = [f"Ponytail{k}" for k in range(1, 7)]
    return [Part(ellipsoid((0, 1.65, -0.015), (0.092, 0.095, 0.105), 44, 26), [bone("Head", "HeadTop_End")], 0.02),
            Part(capsule([POS[n] for n in chain], np.linspace(0.034, 0.02, 6), 20, 26),
                 [bone(a, b) for a, b in zip(chain, chain[1:])] + [bone("Head", "HeadTop_End")], 0.015)]


def eyes_parts():
    return [Part(ellipsoid(POS[side + "Eye"], (0.015, 0.015, 0.015), 22, 12, axis=2), [bone(side + "Eye", end=POS[side + "Eye"] + (0, 0, 0.01))], 0.01)
            for side in ("Left", "Right")]


def eyebrows_parts():
    return [Part(capsule([mirror((0.018, 1.662, 0.099), side), mirror((0.048, 1.666, 0.092), side)], [0.0045, 0.004], 12, 10, caps=3),
                 [bone("Head", "HeadTop_End")], 0.02)
            for side in ("Left", "Right")]


def belt_parts():
    return [Part(torus(np.array([0, 0.95, -0.006]), (0, 1, 0), (0.160, 0.116), (0.010, 0.022), 72, 10),
                 [bone("Hips", "Spine"), bone("Spine", "Spine1")], 0.03)]


def watch_parts():
    fore, hand = POS["LeftForeArm"], POS["LeftHand"]
    return [Part(torus(fore + 0.8 * (hand - fore), hand - fore, (0.0345, 0.0345), (0.006, 0.011), 32, 8),
                 [bone("LeftForeArm", "LeftHand")], 0.02)]


def backpack_parts():
    return [Part(capsule([(0, 1.05, -0.175), (0, 1.21, -0.177), (0, 1.36, -0.175)], [(0.13, 0.056)] * 3, 36, 18),
                 [bone("Backpack", end=POS["Backpack"] + (0, 0.1, 0))], 0.02)]


# ---- textures -----------------------------------------------------------------------------------------------------------------

def noise(rng, size, scale):
    """Smooth noise that tiles, in about -1..1: white noise low-passed in frequency."""
    f = np.fft.fftfreq(size)
    fx, fy = np.meshgrid(f, f)
    spectrum = np.fft.fft2(rng.normal(size=(size, size))) * np.exp(-(fx ** 2 + fy ** 2) * (scale ** 2) * 20)
    n = np.real(np.fft.ifft2(spectrum))
    return n / (np.abs(n).max() + 1e-12)


def rgb(image):
    return Image.fromarray(np.clip(image, 0, 255).astype(np.uint8), "RGB")


def normal_map(height, strength):
    gy, gx = np.gradient(height)
    n = np.stack([-gx * strength, -gy * strength, np.ones_like(height)], -1)
    n /= np.linalg.norm(n, axis=-1, keepdims=True)
    return rgb((n * 0.5 + 0.5) * 255)


def textures():
    rng = np.random.default_rng(1701)
    out = {}
    n = noise(rng, 256, 12)[..., None]
    out["skin"] = rgb(np.array([222, 178, 146]) + 14 * n + rng.normal(0, 3, (256, 256, 1)))
    out["skin_normal"] = normal_map(noise(rng, 128, 4) * 3, 1.0)
    y = np.arange(128)[:, None, None]
    stripes = (y // 8) % 2
    out["shirt"] = rgb(np.where(stripes, [40, 70, 150], [225, 228, 235]) + 8 * noise(rng, 128, 3)[..., None])
    yy, xx = np.mgrid[0:128, 0:128]
    twill = ((xx + yy) % 4 < 2)[..., None]
    out["trousers"] = rgb(np.where(twill, [52, 72, 110], [70, 92, 132]) + 10 * noise(rng, 128, 2)[..., None])
    leather = noise(rng, 64, 3)
    out["shoes"] = rgb(np.array([92, 58, 34]) + 22 * leather[..., None] + rng.normal(0, 2, (64, 64, 1)))
    out["shoes_normal"] = normal_map(leather * 2, 1.5)
    strands = np.repeat(rng.normal(0, 1, (1, 128)), 128, 0) * 0.6 + 0.4 * noise(rng, 128, 2)
    out["hair"] = rgb(np.array([70, 42, 22]) + 25 * strands[..., None])
    v = (np.arange(64)[:, None, None] + 0.5) / 64          # the eyes' poles look forward: v = 1 is the pupil
    out["eyes"] = rgb(np.where(v > 0.93, [10, 10, 12], np.where(v > 0.78, [60, 110, 70], [238, 236, 230]))
                      + np.zeros((1, 64, 1)))
    yb, xb = np.mgrid[0:64, 0:64]
    buckle = ((np.abs(xb - 16) < 5) & (np.abs(yb - 32) < 18))[..., None]          # (u = 0.25 is the front)
    out["belt"] = rgb(np.where(buckle, [200, 190, 150], [55, 35, 25]) + 6 * noise(rng, 64, 2)[..., None])
    face = ((np.abs(xb - 48) < 7) & (np.abs(yb - 32) < 20))[..., None]
    out["watch"] = rgb(np.where(face, [210, 212, 220], [30, 30, 34]) + 4 * noise(rng, 64, 2)[..., None])
    weave = (((xx // 4) + (yy // 4)) % 2)[..., None]
    out["backpack"] = rgb(np.where(weave, [150, 50, 40], [170, 62, 48]) + 10 * noise(rng, 128, 4)[..., None])
    return out


def encoded(image, kind):
    data = io.BytesIO()
    if kind == "jpeg":
        image.save(data, "JPEG", quality=88)
    else:
        image.save(data, "PNG")
    return data.getvalue()


# ---- clips --------------------------------------------------------------------------------------------------------------------

DANCE_END, IDLE_END, WALK_END = 72 / FPS, 154 / FPS, 30 / FPS
FINGER_JOINTS = [f"Hand{f}1" for f in ("Thumb", "Index", "Middle", "Ring", "Pinky")]
TURNED = (["Hips", "Spine", "Spine1", "Spine2", "Neck", "Head", "Jaw", "LeftEye", "RightEye", "Backpack"]
          + [f"Ponytail{k}" for k in range(1, 6)]
          + [side + n for side in ("Left", "Right") for n in ("Shoulder", "Arm", "ForeArm", "Hand", "UpLeg", "Leg", "Foot", "ToeBase")]
          + [side + n for side in ("Left", "Right") for n in FINGER_JOINTS])


def quat(x=0.0, y=0.0, z=0.0):
    """The rotation about Z, then Y, then X (each about the joint's own axes), as glTF's (x, y, z, w)."""
    def about(axis, angle):
        q = np.zeros(4)
        q[axis], q[3] = math.sin(angle / 2), math.cos(angle / 2)
        return q

    def mul(a, b):
        ax, ay, az, aw = a
        bx, by, bz, bw = b
        return np.array([aw * bx + ax * bw + ay * bz - az * by, aw * by - ax * bz + ay * bw + az * bx,
                         aw * bz + ax * by - ay * bx + az * bw, aw * bw - ax * bx - ay * by - az * bz])

    return mul(mul(about(0, x), about(1, y)), about(2, z))


def legs(pose, side, hip, knee, foot, toe=0.0, spread=0.0):
    s = 1 if side == "Left" else -1
    pose[side + "UpLeg"] = (hip, 0, s * spread)
    pose[side + "Leg"] = (knee, 0, 0)
    pose[side + "Foot"] = (foot, 0, -s * spread)
    pose[side + "ToeBase"] = (toe, 0, 0)


def arms(pose, side, swing, raise_, elbow, wrist=0.0, curl=0.2, shrug=0.0):
    s = 1 if side == "Left" else -1
    pose[side + "Shoulder"] = (0, 0, s * shrug)
    pose[side + "Arm"] = (swing, 0, s * raise_)
    pose[side + "ForeArm"] = (elbow, 0, 0)
    pose[side + "Hand"] = (0, 0, s * wrist)
    for k, n in enumerate(FINGER_JOINTS):
        bend = curl * (0.6 if n.startswith("HandThumb") else 1.0)
        pose[side + n] = (0, 0, -s * bend) if not n.startswith("HandThumb") else (-bend * 0.5, 0, -s * bend * 0.4)


def walk(t):
    p = 2 * math.pi * (t - START) / (WALK_END - START)          # one stride of each leg: the last key is the first
    s, c = math.sin(p), math.cos(p)
    pose = {}
    pose["Hips"] = (0.04, 0.10 * s, 0.03 * c)
    for n, k in (("Spine", 0.06), ("Spine1", 0.035), ("Spine2", 0.02)):
        pose[n] = (0.01, -k * s, -0.3 * k * c)
    pose["Neck"] = (0.0, 0.05 * s, 0)
    pose["Head"] = (0.03 * math.sin(2 * p), 0.04 * s, 0)
    pose["Jaw"] = (0.02, 0, 0)
    for side, ph in (("Left", p), ("Right", p + math.pi)):
        hs, hc = math.sin(ph), math.cos(ph)
        knee = 0.12 + 0.62 * ((1 + hc) / 2) ** 3
        legs(pose, side, -0.42 * hs, knee, -0.5 * knee + 0.18 * hs, 0.25 * max(0.0, -hs) ** 2, 0.02)
        arms(pose, side, 0.36 * hs, 0.10, -0.28 - 0.22 * max(0.0, -hs), 0.05 * hs, 0.25 + 0.05 * hs)
    for side in ("Left", "Right"):
        pose[side + "Eye"] = (0, 0.06 * s, 0)
    for k in range(1, 6):
        pose[f"Ponytail{k}"] = (0.10 * math.sin(2 * p - 0.6 * k), 0, 0.06 * math.sin(p - 0.5 * k))
    pose["Backpack"] = (0.04 * math.sin(2 * p - 0.5), 0, 0.02 * s)
    hips = (0.015 * s, -0.01 + 0.015 * math.cos(2 * p), 0)
    chest = (1 + 0.01 * math.sin(2 * p), 1, 1 + 0.012 * math.sin(2 * p))
    return pose, hips, chest


def idle(t):
    q = 2 * math.pi * (t - START) / (IDLE_END - START)
    pose = {}
    sway = math.sin(q)
    breath = math.sin(3 * q)
    pose["Hips"] = (0.01, 0.05 * math.sin(q + 1), 0.03 * sway)
    pose["Spine"] = (0.015 * breath, -0.03 * math.sin(q + 1), -0.02 * sway)
    pose["Spine1"] = (0.01 * breath, -0.01 * math.sin(q + 1), -0.01 * sway)
    pose["Spine2"] = (0.01 * breath, 0, 0)
    pose["Neck"] = (0.02 * math.sin(2 * q), 0.18 * math.sin(2 * q + 0.3), 0)
    pose["Head"] = (0.06 * math.sin(3 * q + 0.5), 0.25 * math.sin(2 * q + 0.3), 0.03 * math.sin(q))
    pose["Jaw"] = (0.02 + 0.02 * math.sin(2 * q), 0, 0)
    for side, s in (("Left", 1), ("Right", -1)):
        legs(pose, side, -0.02, 0.05 + 0.04 * (1 + s * sway) / 2, -0.03, 0.0, -0.03 * sway * s)
        arms(pose, side, 0.03 * breath, 0.08 + 0.02 * breath, -0.15 - 0.05 * math.sin(2 * q + s), 0.04 * math.sin(q + s),
             0.30 + 0.10 * math.sin(q + s), 0.02 * breath)
        pose[side + "Eye"] = (0.05 * math.sin(4 * q), 0.25 * math.sin(5 * q), 0)
    for k in range(1, 6):
        pose[f"Ponytail{k}"] = (0.05 * math.sin(2 * q - 0.5 * k), 0, 0.04 * math.sin(q - 0.4 * k))
    pose["Backpack"] = (0.02 * breath, 0, 0)
    hips = (0.02 * sway, -0.005 + 0.004 * breath, 0)
    chest = (1 + 0.02 * breath, 1 + 0.005 * breath, 1 + 0.025 * breath)
    return pose, hips, chest


def dance(t):
    p = 2 * math.pi * (t - START) / (DANCE_END - START)
    b = 4 * p                                             # four beats of 0.74 s
    bounce = 0.5 - 0.5 * math.cos(b)
    pose = {}
    pose["Hips"] = (0.05 * bounce, 0.35 * math.sin(p), 0.06 * math.sin(2 * p))
    pose["Spine"] = (-0.03 * bounce, -0.15 * math.sin(p), 0.08 * math.sin(2 * p))
    pose["Spine1"] = (-0.02 * bounce, -0.08 * math.sin(p), 0.05 * math.sin(2 * p))
    pose["Spine2"] = (-0.01 * bounce, -0.05 * math.sin(p), 0.03 * math.sin(2 * p))
    pose["Neck"] = (0.05 * math.sin(b), -0.1 * math.sin(p), 0)
    pose["Head"] = (0.15 * math.sin(b), -0.1 * math.sin(p), 0.08 * math.sin(2 * p))
    pose["Jaw"] = (0.12 * (0.5 + 0.5 * math.sin(2 * b)), 0, 0)
    for side, s, ph in (("Left", 1, 0.0), ("Right", -1, math.pi)):
        legs(pose, side, -0.35 * bounce, 0.7 * bounce, -0.35 * bounce, 0.0, 0.06 + 0.04 * math.sin(2 * p + ph))
        arms(pose, side, -0.4 * (0.5 + 0.5 * math.sin(b + ph)), 0.55 + 0.45 * (0.5 + 0.5 * math.sin(2 * p + ph)),
             -1.0 - 0.4 * math.sin(b + 1 + ph), 0.3 * math.sin(b + ph), 0.6 * (0.5 + 0.5 * math.sin(b + ph)), 0.05 * bounce)
        pose[side + "Eye"] = (0, 0.2 * math.sin(2 * p), 0)
    for k in range(1, 6):
        pose[f"Ponytail{k}"] = (0.25 * math.sin(b - 0.7 * k), 0, 0.2 * math.sin(2 * p - 0.5 * k))
    pose["Backpack"] = (0.08 * math.sin(b - 0.6), 0, 0.03 * math.sin(2 * p))
    hips = (0.06 * math.sin(2 * p), -0.05 * bounce, 0)
    chest = (1 + 0.015 * math.sin(b), 1, 1 + 0.02 * math.sin(b))
    return pose, hips, chest


CLIPS = [("Dance", dance, DANCE_END), ("Idle", idle, IDLE_END), ("Walk", walk, WALK_END)]


# ---- the file -----------------------------------------------------------------------------------------------------------------

class Glb:
    def __init__(self):
        self.bin = bytearray()
        self.views, self.accessors = [], []

    def view(self, data, target=None):
        self.bin += b"\0" * (-len(self.bin) % 4)
        view = {"buffer": 0, "byteOffset": len(self.bin), "byteLength": len(data)}
        if target:
            view["target"] = target
        self.views.append(view)
        self.bin += data
        return len(self.views) - 1

    def accessor(self, array, kind, ctype, target=None, normalized=False, bounds=False):
        array = np.ascontiguousarray(array)
        accessor = {"bufferView": self.view(array.tobytes(), target), "componentType": ctype, "count": len(array), "type": kind}
        if normalized:
            accessor["normalized"] = True
        if bounds:
            flat = array.reshape(len(array), -1)
            accessor["min"] = [float(v) for v in flat.min(axis=0)]
            accessor["max"] = [float(v) for v in flat.max(axis=0)]
        self.accessors.append(accessor)
        return len(self.accessors) - 1


FLOAT, UBYTE, USHORT = 5126, 5121, 5123
ARRAY, ELEMENTS = 34962, 34963


def build():
    glb = Glb()
    meshes_spec = [
        ("body", body_parts(), {"baseColorTexture": "skin", "normalTexture": "skin_normal", "roughness": 0.6}),
        ("shirt", shirt_parts(), {"baseColorTexture": "shirt", "roughness": 0.8}),
        ("trousers", trousers_parts(), {"baseColorTexture": "trousers", "roughness": 0.85}),
        ("shoes", shoes_parts(), {"baseColorTexture": "shoes", "normalTexture": "shoes_normal", "roughness": 0.5}),
        ("hair", hair_parts(), {"baseColorTexture": "hair", "roughness": 0.7}),
        ("eyes", eyes_parts(), {"baseColorTexture": "eyes", "roughness": 0.2}),
        ("eyebrows", eyebrows_parts(), {"baseColorFactor": [0.12, 0.08, 0.05, 1.0], "roughness": 0.8}),
        ("belt", belt_parts(), {"baseColorTexture": "belt", "roughness": 0.4}),
        ("watch", watch_parts(), {"baseColorTexture": "watch", "roughness": 0.3}),
        ("backpack", backpack_parts(), {"baseColorTexture": "backpack", "roughness": 0.9}),
    ]

    # images: each once, in the order the materials first name them
    pictures = textures()
    images, image_of = [], {}
    for _, _, spec in meshes_spec:
        for role in ("baseColorTexture", "normalTexture"):
            name = spec.get(role)
            if name and name not in image_of:
                kind = "jpeg" if name in ("skin", "shoes") else "png"
                image_of[name] = len(images)
                images.append({"bufferView": glb.view(encoded(pictures[name], kind)), "mimeType": "image/" + kind, "name": "figure_" + name})

    materials, meshes = [], []
    for index, (name, parts, spec) in enumerate(meshes_spec):
        pbr = {"metallicFactor": 0.0, "roughnessFactor": spec["roughness"]}
        if "baseColorTexture" in spec:
            pbr["baseColorTexture"] = {"index": image_of[spec["baseColorTexture"]]}
        if "baseColorFactor" in spec:
            pbr["baseColorFactor"] = spec["baseColorFactor"]
        material = {"name": name, "doubleSided": True, "pbrMetallicRoughness": pbr}
        if "normalTexture" in spec:
            material["normalTexture"] = {"index": image_of[spec["normalTexture"]]}
        materials.append(material)

        positions, normals, uvs, joints, weights, tris = [], [], [], [], [], []
        base = 0
        for part in parts:
            positions.append(part.positions)
            normals.append(part.normals)
            uvs.append(part.uvs)
            joints.append(part.joints)
            weights.append(part.weights)
            tris.append(part.tris + base)
            base += len(part.positions)
        positions = np.concatenate(positions).astype("<f4")
        count = len(positions)
        assert count < 65536
        attributes = {
            "POSITION": glb.accessor(positions, "VEC3", FLOAT, ARRAY, bounds=True),
            "NORMAL": glb.accessor(np.concatenate(normals).astype("<f4"), "VEC3", FLOAT, ARRAY),
        }
        uvs = np.concatenate(uvs)
        if uvs.min() >= 0 and uvs.max() <= 1:          # (normalised: half the bytes)
            attributes["TEXCOORD_0"] = glb.accessor(np.round(uvs * 65535).astype("<u2"), "VEC2", USHORT, ARRAY, normalized=True)
        else:
            attributes["TEXCOORD_0"] = glb.accessor(uvs.astype("<f4"), "VEC2", FLOAT, ARRAY)
        if name == "shirt":          # colour layers the material does not use
            p = positions
            first = np.stack([(p[:, 1] - 0.85) / 0.7, 0.5 + p[:, 0], 0.5 + p[:, 2], np.ones(count)], 1)
            second = np.stack([0.5 + 0.5 * np.sin(20 * p[:, 1]), 0.5 + 0.5 * np.cos(20 * p[:, 0]), np.full(count, 0.25), np.ones(count)], 1)
            attributes["COLOR_0"] = glb.accessor(np.round(np.clip(first, 0, 1) * 255).astype(np.uint8), "VEC4", UBYTE, ARRAY, normalized=True)
            attributes["COLOR_1"] = glb.accessor(np.round(np.clip(second, 0, 1) * 255).astype(np.uint8), "VEC4", UBYTE, ARRAY, normalized=True)
        attributes["JOINTS_0"] = glb.accessor(np.concatenate(joints), "VEC4", UBYTE, ARRAY)
        attributes["WEIGHTS_0"] = glb.accessor(np.concatenate(weights), "VEC4", UBYTE, ARRAY, normalized=True)
        tris = np.concatenate(tris)
        assert len(tris) >= 300, name
        indices = glb.accessor(tris.astype("<u2").ravel(), "SCALAR", USHORT, ELEMENTS)
        meshes.append({"name": name, "primitives": [{"attributes": attributes, "indices": indices, "material": index, "mode": 4}]})

    # nodes: the joints, then a node for each mesh, then the armature that holds both
    nodes = []
    for name, parent, position in JOINTS:
        node = {"name": name}
        offset = position - (POS[parent] if parent else 0)
        if np.any(offset != 0):
            node["translation"] = [round(float(v), 6) for v in offset]
        children = [i for i, (_, p, _) in enumerate(JOINTS) if p == name]
        if children:
            node["children"] = children
        nodes.append(node)
    first_mesh = len(nodes)
    for index, (name, _, _) in enumerate(meshes_spec):
        nodes.append({"name": name, "mesh": index, "skin": 0})
    nodes.append({"name": "Armature", "children": [INDEX["Root"]] + list(range(first_mesh, first_mesh + len(meshes_spec)))})
    rest = np.array([[float(v) for v in n.get("translation", [0, 0, 0])] for n in nodes[:len(JOINTS)]])
    world = np.zeros((len(JOINTS), 3))
    for i, (_, parent, _) in enumerate(JOINTS):
        world[i] = rest[i] + (world[INDEX[parent]] if parent else 0)
    inverse_bind = np.tile(np.eye(4), (len(JOINTS), 1, 1))
    inverse_bind[:, 3, :3] = -world          # column-major: the translation is the fourth column
    skin = {"name": "Armature", "joints": list(range(len(JOINTS))),
            "inverseBindMatrices": glb.accessor(inverse_bind.astype("<f4").reshape(len(JOINTS), 16), "MAT4", FLOAT)}

    animations = []
    for clip, motion, end in CLIPS:
        times = np.arange(1, int(round(end * FPS)) + 1) / FPS
        keys = [motion(t) for t in times]
        time_accessor = glb.accessor(times.astype("<f4"), "SCALAR", FLOAT, bounds=True)
        samplers, channels = [], []

        def channel(node, path, values, kind):
            samplers.append({"input": time_accessor, "interpolation": "LINEAR",
                             "output": glb.accessor(np.asarray(values, "<f4"), kind, FLOAT)})
            channels.append({"sampler": len(samplers) - 1, "target": {"node": node, "path": path}})

        channel(INDEX["Hips"], "translation", [POS["Hips"] - POS["Root"] + hips for _, hips, _ in keys], "VEC3")
        for name in TURNED:
            channel(INDEX[name], "rotation", [quat(*pose[name]) for pose, _, _ in keys], "VEC4")
        channel(INDEX["Spine2"], "scale", [chest for _, _, chest in keys], "VEC3")
        animations.append({"name": clip, "samplers": samplers, "channels": channels})

    doc = {
        "asset": {"version": "2.0", "generator": "edge test/data/make_test_figure.py"},
        "scene": 0, "scenes": [{"name": "Scene", "nodes": [len(nodes) - 1]}],
        "nodes": nodes, "skins": [skin], "meshes": meshes, "materials": materials,
        "textures": [{"sampler": 0, "source": i} for i in range(len(images))], "images": images,
        "samplers": [{"magFilter": 9729, "minFilter": 9987, "wrapS": 10497, "wrapT": 10497}],
        "animations": animations,
        "accessors": glb.accessors, "bufferViews": glb.views, "buffers": [{"byteLength": len(glb.bin)}],
    }
    # (texture i is image i: the materials name images by the texture of the same index)
    js = json.dumps(doc, separators=(",", ":")).encode()
    js += b" " * (-len(js) % 4)
    data = bytes(glb.bin) + b"\0" * (-len(glb.bin) % 4)
    out = b"glTF" + struct.pack("<II", 2, 12 + 8 + len(js) + 8 + len(data))
    out += struct.pack("<I4s", len(js), b"JSON") + js + struct.pack("<I4s", len(data), b"BIN\0") + data
    triangles = sum(len(p.tris) for _, parts, _ in meshes_spec for p in parts)
    return out, triangles


def main():
    data, triangles = build()
    (HERE / "test_figure.glb").write_bytes(data)
    print(f"test_figure.glb: {len(data)} bytes, {triangles} triangles, {len(JOINTS)} joints")


main()
