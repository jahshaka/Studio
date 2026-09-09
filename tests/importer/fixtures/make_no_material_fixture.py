#!/usr/bin/env python3
"""Generates no_material.glb — the fixture for the "a mesh with no material"
case in tests/importer/test_importer_materials.cpp.

WHY THIS IS ITS OWN FILE, and not another quad in material_workflows.glb.

A glTF primitive with no `material` gets, from assimp, the DEFAULT material
that glTF2Importer::ImportMaterials appends after the file's own
(`aim->mMaterialIndex = mScene->mNumMaterials - 1`, glTF2Importer.cpp:897).
That appended material carries assimp's own struct defaults — white base
colour, metallicFactor 1, roughnessFactor 1 — and it has no entry in the
file's `materials` array, which is the whole point: our importer's file-facts
lookup runs off the end of that array and used to fall back to "trust assimp's
always-present keys", i.e. FULL METAL, FULL ROUGH. Black.

But the canonical import preset includes `aiProcess_RemoveRedundantMaterials`
(irisgl/import/importflags.h -> aiProcessPreset_TargetRealtime_Quality), and
material_workflows.glb contains `nopbr` — a material with a name and nothing
else, which is BYTE-FOR-BYTE what assimp's appended default looks like. assimp
merged the two and remapped the no-material mesh onto `nopbr`'s index, so the
appended material never survived to be converted and the case was never
reached. (Measured, 2026-09-09: `numMaterials=5` for six meshes, with
`mesh[5] -> material 2`.)

So this file has exactly ONE authored material and it is deliberately NOT
default-shaped: a red metallic-roughness block. The appended default is then
redundant with nothing, survives the merge, and the no-material mesh really
does arrive carrying a material index past the end of the JSON array.

    red           pbrMetallicRoughness, red baseColorFactor, metallic 0.
    nomaterial    a primitive with NO `material` key at all.

Run:  python3 make_no_material_fixture.py     (writes the .glb beside this script)
"""
import json, os, struct

HERE = os.path.dirname(os.path.abspath(__file__))


def build():
    positions = [(-1, -1, 0), (1, -1, 0), (1, 1, 0), (-1, 1, 0)]
    normals = [(0, 0, 1)] * 4
    uvs = [(0, 1), (1, 1), (1, 0), (0, 0)]
    indices = [0, 1, 2, 0, 2, 3]

    blobs = [
        b"".join(struct.pack("<3f", *p) for p in positions),
        b"".join(struct.pack("<3f", *n) for n in normals),
        b"".join(struct.pack("<2f", *u) for u in uvs),
        b"".join(struct.pack("<H", i) for i in indices),
    ]
    views, buf = [], bytearray()
    for b in blobs:
        buf += b"\x00" * ((-len(buf)) % 4)
        views.append({"buffer": 0, "byteOffset": len(buf), "byteLength": len(b)})
        buf += b
    views[0]["target"] = 34962
    views[1]["target"] = 34962
    views[2]["target"] = 34962
    views[3]["target"] = 34963

    accessors = [
        {"bufferView": 0, "componentType": 5126, "count": 4, "type": "VEC3",
         "min": [-1, -1, 0], "max": [1, 1, 0]},
        {"bufferView": 1, "componentType": 5126, "count": 4, "type": "VEC3"},
        {"bufferView": 2, "componentType": 5126, "count": 4, "type": "VEC2"},
        {"bufferView": 3, "componentType": 5123, "count": 6, "type": "SCALAR"},
    ]

    attrs = {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2}
    gltf = {
        "asset": {"version": "2.0", "generator": "jahshaka test fixture"},
        "scene": 0,
        "scenes": [{"nodes": [0, 1]}],
        "nodes": [
            {"name": "red", "mesh": 0, "translation": [0.0, 0.0, 0.0]},
            {"name": "nomaterial", "mesh": 1, "translation": [3.0, 0.0, 0.0]},
        ],
        "meshes": [
            {"name": "red", "primitives": [
                {"attributes": attrs, "indices": 3, "material": 0}]},
            # NO `material` key: this is the whole fixture.
            {"name": "nomaterial", "primitives": [
                {"attributes": attrs, "indices": 3}]},
        ],
        "materials": [
            {"name": "red",
             "pbrMetallicRoughness": {"baseColorFactor": [0.8, 0.1, 0.1, 1.0],
                                      "metallicFactor": 0.0,
                                      "roughnessFactor": 0.6}},
        ],
        "accessors": accessors,
        "bufferViews": views,
        "buffers": [{"byteLength": len(buf)}],
    }

    js = json.dumps(gltf, separators=(",", ":")).encode()
    js += b" " * ((-len(js)) % 4)
    bin_ = bytes(buf) + b"\x00" * ((-len(buf)) % 4)
    glb = (struct.pack("<III", 0x46546C67, 2, 12 + 8 + len(js) + 8 + len(bin_))
           + struct.pack("<II", len(js), 0x4E4F534A) + js
           + struct.pack("<II", len(bin_), 0x004E4942) + bin_)
    out = os.path.join(HERE, "no_material.glb")
    with open(out, "wb") as f:
        f.write(glb)
    print("wrote", out, len(glb), "bytes")


if __name__ == "__main__":
    build()
