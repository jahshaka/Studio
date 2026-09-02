#!/usr/bin/env python3
"""Generates tests/avatar/fixtures/rig2.glb — the skinned SINGLE-MESH fixture.

Why a generated file and not a downloaded character: the suites need a rig that
takes the `aiScene::mNumMeshes == 1` path (that is what AVATAR_MODULE_SPEC's
ITEM ZERO is about, and what every Mixamo export is), with real bones, real
inverse-bind matrices and named clips — but a real character is megabytes of
third-party content. This writes ~3 KB of glTF by hand, with no dependencies
beyond the standard library, so the fixture is reproducible and reviewable.

Shape (deliberately the same two-bone arm tests/skeletal builds in code, so the
two suites can be reasoned about together):

    Armature                     (root node, the fragment root)
      jointRoot                  bind at the origin
        jointTip                 bind at (0, 1, 0)
      arm                        the ONE mesh, skin = [jointRoot, jointTip]

    six vertices in a flat strip at z = 0: rows y = 0 and y = 1 weighted to
    jointRoot, row y = 2 weighted to jointTip.

Clips:
    "Idle"        1.0 s  — jointRoot swings -60 degrees about Z (so jointTip's
                           WORLD position moves, and the pose is assertable
                           through avatar.bones(), not only through pixels)
    "mixamo.com"  0.5 s  — jointTip swings +45 degrees about Z
                           (the junk name every Mixamo clip carries: the module
                           derives a display name from the source file, and the
                           suite asserts it)

Run:  python3 make_rig_glb.py            (writes rig2.glb beside this script)
"""

import json
import math
import struct
import os

HW = 0.15

# ---- geometry -------------------------------------------------------------
positions = [
    (-HW, 0.0, 0.0), (HW, 0.0, 0.0),
    (-HW, 1.0, 0.0), (HW, 1.0, 0.0),
    (-HW, 2.0, 0.0), (HW, 2.0, 0.0),
]
normals = [(0.0, 0.0, 1.0)] * 6
joints = [(0, 0, 0, 0)] * 4 + [(1, 0, 0, 0)] * 2
weights = [(1.0, 0.0, 0.0, 0.0)] * 6
indices = [0, 1, 3, 0, 3, 2, 2, 3, 5, 2, 5, 4]

# ---- skin -----------------------------------------------------------------
# glTF matrices are COLUMN-major. jointRoot binds at the origin (identity),
# jointTip binds at (0, 1, 0) so its inverse bind translates by (0, -1, 0).
ibm_root = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]
ibm_tip = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, -1, 0, 1]


def quat_z(degrees):
    """glTF quaternion (x, y, z, w) for a rotation about +Z."""
    half = math.radians(degrees) / 2.0
    return (0.0, 0.0, math.sin(half), math.cos(half))


# (name, target node, key times, key rotations). "Idle" swings jointRoot, so
# BOTH the mesh and jointTip's world position move — that is what makes the
# pose assertable through avatar.bones() and not only through pixels (a bone's
# OWN rotation never moves its own origin). "mixamo.com" swings the tip, and
# carries the junk name every Mixamo clip has so the display-name rule is
# exercised by a real file.
JOINT_ROOT, JOINT_TIP = 1, 2
clips = [
    ("Idle", JOINT_ROOT, [0.0, 1.0], [quat_z(0.0), quat_z(-60.0)]),
    ("mixamo.com", JOINT_TIP, [0.0, 0.5], [quat_z(0.0), quat_z(45.0)]),
]

# ---- buffer ---------------------------------------------------------------
blob = bytearray()
views = []
accessors = []


def align4():
    while len(blob) % 4:
        blob.append(0)


def add_view(data, target=None):
    align4()
    offset = len(blob)
    blob.extend(data)
    view = {"buffer": 0, "byteOffset": offset, "byteLength": len(data)}
    if target is not None:
        view["target"] = target
    views.append(view)
    return len(views) - 1


def add_accessor(view, component_type, count, type_, mn=None, mx=None):
    acc = {"bufferView": view, "componentType": component_type,
           "count": count, "type": type_}
    if mn is not None:
        acc["min"] = mn
        acc["max"] = mx
    accessors.append(acc)
    return len(accessors) - 1


FLOAT, USHORT, UBYTE = 5126, 5123, 5121
ARRAY_BUFFER, ELEMENT_ARRAY_BUFFER = 34962, 34963

pos_data = b"".join(struct.pack("<3f", *p) for p in positions)
acc_pos = add_accessor(add_view(pos_data, ARRAY_BUFFER), FLOAT, 6, "VEC3",
                       [-HW, 0.0, 0.0], [HW, 2.0, 0.0])
nrm_data = b"".join(struct.pack("<3f", *n) for n in normals)
acc_nrm = add_accessor(add_view(nrm_data, ARRAY_BUFFER), FLOAT, 6, "VEC3")
jnt_data = b"".join(struct.pack("<4B", *j) for j in joints)
acc_jnt = add_accessor(add_view(jnt_data, ARRAY_BUFFER), UBYTE, 6, "VEC4")
wgt_data = b"".join(struct.pack("<4f", *w) for w in weights)
acc_wgt = add_accessor(add_view(wgt_data, ARRAY_BUFFER), FLOAT, 6, "VEC4")
idx_data = struct.pack("<%dH" % len(indices), *indices)
acc_idx = add_accessor(add_view(idx_data, ELEMENT_ARRAY_BUFFER), USHORT,
                       len(indices), "SCALAR")
ibm_data = struct.pack("<32f", *(ibm_root + ibm_tip))
acc_ibm = add_accessor(add_view(ibm_data), FLOAT, 2, "MAT4")

animations = []
for name, target, times, quats in clips:
    t_data = struct.pack("<%df" % len(times), *times)
    acc_t = add_accessor(add_view(t_data), FLOAT, len(times), "SCALAR",
                         [min(times)], [max(times)])
    q_data = b"".join(struct.pack("<4f", *q) for q in quats)
    acc_q = add_accessor(add_view(q_data), FLOAT, len(quats), "VEC4")
    animations.append({
        "name": name,
        "samplers": [{"input": acc_t, "output": acc_q, "interpolation": "LINEAR"}],
        "channels": [{"sampler": 0, "target": {"node": target, "path": "rotation"}}],
    })

gltf = {
    "asset": {"version": "2.0", "generator": "jahshaka tests/avatar make_rig_glb.py"},
    "scene": 0,
    "scenes": [{"nodes": [0]}],
    "nodes": [
        {"name": "Armature", "children": [1, 3]},                       # 0
        {"name": "jointRoot", "children": [2]},                         # 1
        {"name": "jointTip", "translation": [0.0, 1.0, 0.0]},           # 2
        {"name": "arm", "mesh": 0, "skin": 0},                          # 3
    ],
    "skins": [{"inverseBindMatrices": acc_ibm, "skeleton": 1, "joints": [1, 2]}],
    "meshes": [{
        "name": "arm",
        "primitives": [{
            "attributes": {"POSITION": acc_pos, "NORMAL": acc_nrm,
                           "JOINTS_0": acc_jnt, "WEIGHTS_0": acc_wgt},
            "indices": acc_idx,
            "material": 0,
        }],
    }],
    "materials": [{
        "name": "armMat",
        "pbrMetallicRoughness": {"baseColorFactor": [0.9, 0.15, 0.15, 1.0],
                                 "metallicFactor": 0.0, "roughnessFactor": 0.6},
    }],
    "bufferViews": views,
    "accessors": accessors,
    "animations": animations,
    "buffers": [{"byteLength": len(blob)}],
}

json_chunk = json.dumps(gltf, separators=(",", ":")).encode("utf-8")
json_chunk += b" " * ((4 - len(json_chunk) % 4) % 4)
bin_chunk = bytes(blob)
bin_chunk += b"\0" * ((4 - len(bin_chunk) % 4) % 4)

glb = struct.pack("<III", 0x46546C67, 2, 12 + 8 + len(json_chunk) + 8 + len(bin_chunk))
glb += struct.pack("<II", len(json_chunk), 0x4E4F534A) + json_chunk
glb += struct.pack("<II", len(bin_chunk), 0x004E4942) + bin_chunk

out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "rig2.glb")
with open(out, "wb") as f:
    f.write(glb)
print("wrote %s (%d bytes)" % (out, len(glb)))
