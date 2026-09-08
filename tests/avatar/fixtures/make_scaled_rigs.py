#!/usr/bin/env python3
"""Generates rig2_giant.glb and rig2_tiny.glb — the HEIGHT-NORMALIZATION fixtures.

WHY (owner report 2026-09-08, "Dreyar is massively huge, head touches the
ceiling"). Honouring an FBX file's UnitScaleFactor fixes the systematic 100x
(irisgl/import/importflags.h), but it cannot fix a package whose DECLARATION is
wrong: the owner's Dreyar download says centimetres and is authored in
millimetres, so it still arrives 17.25 m tall — and its clips are authored to
match, which is why the answer is to scale the CHARACTER rather than to
re-declare the file. The Avatar module therefore normalizes any subject whose
measured height is outside 0.5..3.0 m to 1.75 m.

These two fixtures are rig2.glb — the 2 m two-bone arm every avatar suite
already uses — rewritten at two implausible sizes, so the rule can be gated
from both directions with a control that needs no third-party content:

    rig2_giant.glb   x8.625 -> 17.25 m, the Dreyar case (must normalize DOWN)
    rig2_tiny.glb    x0.05  ->  0.10 m, the other end   (must normalize UP)
    rig2.glb         x1     ->  2.00 m, plausible       (must be left ALONE)

The rewrite is exactly what assimp's own ScaleProcess does to a unit-converted
file: vertex positions, accessor min/max, node translations and the inverse
bind matrices' translation column, all multiplied — rotations and scales
untouched. Run: python3 make_scaled_rigs.py (writes both beside this script).
"""

import json
import os
import struct

HERE = os.path.dirname(os.path.abspath(__file__))


def read_glb(path):
    with open(path, "rb") as f:
        data = f.read()
    magic, version, _length = struct.unpack_from("<III", data, 0)
    assert magic == 0x46546C67 and version == 2, "not a glTF 2 binary"
    off = 12
    doc, blob = None, b""
    while off < len(data):
        clen, ctype = struct.unpack_from("<II", data, off)
        chunk = data[off + 8: off + 8 + clen]
        if ctype == 0x4E4F534A:
            doc = json.loads(chunk.decode("utf-8"))
        elif ctype == 0x004E4942:
            blob = chunk
        off += 8 + clen
    return doc, bytearray(blob)


def write_glb(doc, blob, name):
    json_chunk = json.dumps(doc, separators=(",", ":")).encode("utf-8")
    json_chunk += b" " * ((4 - len(json_chunk) % 4) % 4)
    bin_chunk = bytes(blob)
    bin_chunk += b"\0" * ((4 - len(bin_chunk) % 4) % 4)
    glb = struct.pack("<III", 0x46546C67, 2, 12 + 8 + len(json_chunk) + 8 + len(bin_chunk))
    glb += struct.pack("<II", len(json_chunk), 0x4E4F534A) + json_chunk
    glb += struct.pack("<II", len(bin_chunk), 0x004E4942) + bin_chunk
    path = os.path.join(HERE, name)
    with open(path, "wb") as f:
        f.write(glb)
    print("wrote %s (%d bytes)" % (path, len(glb)))


def accessor_offset(doc, index):
    acc = doc["accessors"][index]
    view = doc["bufferViews"][acc["bufferView"]]
    return view.get("byteOffset", 0) + acc.get("byteOffset", 0), acc


def scale_vec3_accessor(doc, blob, index, s):
    off, acc = accessor_offset(doc, index)
    assert acc["type"] == "VEC3" and acc["componentType"] == 5126
    for i in range(acc["count"]):
        at = off + i * 12
        x, y, z = struct.unpack_from("<3f", blob, at)
        struct.pack_into("<3f", blob, at, x * s, y * s, z * s)
    for key in ("min", "max"):
        if key in acc:
            acc[key] = [v * s for v in acc[key]]


def scale_ibm_accessor(doc, blob, index, s):
    """MAT4, column-major: elements 12..14 are the translation column."""
    off, acc = accessor_offset(doc, index)
    assert acc["type"] == "MAT4" and acc["componentType"] == 5126
    for i in range(acc["count"]):
        at = off + i * 64
        m = list(struct.unpack_from("<16f", blob, at))
        m[12] *= s
        m[13] *= s
        m[14] *= s
        struct.pack_into("<16f", blob, at, *m)


def build(name, s, note):
    doc, blob = read_glb(os.path.join(HERE, "rig2.glb"))
    doc["asset"]["generator"] = "jahshaka tests/avatar make_scaled_rigs.py (x%g)" % s
    for mesh in doc.get("meshes", []):
        for prim in mesh["primitives"]:
            scale_vec3_accessor(doc, blob, prim["attributes"]["POSITION"], s)
    for skin in doc.get("skins", []):
        if "inverseBindMatrices" in skin:
            scale_ibm_accessor(doc, blob, skin["inverseBindMatrices"], s)
    for node in doc.get("nodes", []):
        if "translation" in node:
            node["translation"] = [v * s for v in node["translation"]]
    # Animation channels here are rotations only (rig2.glb's two clips), so
    # there is nothing else to convert; a translation sampler would need the
    # same multiply its node's bind translation gets.
    for anim in doc.get("animations", []):
        for ch in anim["channels"]:
            assert ch["target"]["path"] != "translation", "translation sampler needs scaling too"
    print("  %s: %s" % (name, note))
    write_glb(doc, blob, name)


build("rig2_giant.glb", 8.625, "2 m arm at 17.25 m — the Dreyar case")
build("rig2_tiny.glb", 0.05, "2 m arm at 0.10 m — the small end")
