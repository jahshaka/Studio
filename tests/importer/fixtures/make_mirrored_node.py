#!/usr/bin/env python3
"""mirrored_node.gltf — ONE mesh under a node whose scale is (-1, 1, 1).

IMPORT-1 (SPECS/IMPORT_DIALOG_SPEC.md §4.2, the single-mesh fold): the fold
bakes a single-mesh file's node transform into its vertices and emits an
identity root. A MIRROR is the case that cannot be folded naively — while the
scale sat on the node the renderer compensated (Ogre flips culling on a
negative node scale), and in the vertices there is no node left to notice, so
the face WINDING has to be reversed with it. This fixture is the smallest file
that states it: one quad in the XY plane, +Z normals, counter-clockwise as
authored, under a single -X node.

Regenerate:  python3 make_mirrored_node.py
"""
import base64, json, struct

# A unit quad in the XY plane at z = 0, wound counter-clockwise seen from +Z.
positions = [(0.0, 0.0, 0.0), (1.0, 0.0, 0.0), (1.0, 1.0, 0.0), (0.0, 1.0, 0.0)]
normals   = [(0.0, 0.0, 1.0)] * 4
indices   = [0, 1, 2, 0, 2, 3]

pos_bytes = b"".join(struct.pack("<3f", *p) for p in positions)
nrm_bytes = b"".join(struct.pack("<3f", *n) for n in normals)
idx_bytes = b"".join(struct.pack("<H", i) for i in indices)
pad = lambda b: b + b"\0" * ((4 - len(b) % 4) % 4)
blob = pad(pos_bytes) + pad(nrm_bytes) + pad(idx_bytes)

pos_off, nrm_off, idx_off = 0, len(pad(pos_bytes)), len(pad(pos_bytes)) + len(pad(nrm_bytes))

doc = {
    "asset": {"version": "2.0", "generator": "jahshaka make_mirrored_node.py"},
    "scene": 0,
    "scenes": [{"nodes": [0]}],
    "nodes": [{"mesh": 0, "scale": [-1.0, 1.0, 1.0], "name": "mirrored"}],
    "meshes": [{"name": "quad", "primitives": [
        {"attributes": {"POSITION": 0, "NORMAL": 1}, "indices": 2, "mode": 4}]}],
    "accessors": [
        {"bufferView": 0, "componentType": 5126, "count": 4, "type": "VEC3",
         "min": [0.0, 0.0, 0.0], "max": [1.0, 1.0, 0.0]},
        {"bufferView": 1, "componentType": 5126, "count": 4, "type": "VEC3"},
        {"bufferView": 2, "componentType": 5123, "count": 6, "type": "SCALAR"},
    ],
    "bufferViews": [
        {"buffer": 0, "byteOffset": pos_off, "byteLength": len(pos_bytes), "target": 34962},
        {"buffer": 0, "byteOffset": nrm_off, "byteLength": len(nrm_bytes), "target": 34962},
        {"buffer": 0, "byteOffset": idx_off, "byteLength": len(idx_bytes), "target": 34963},
    ],
    "buffers": [{"byteLength": len(blob),
                 "uri": "data:application/octet-stream;base64," +
                        base64.b64encode(blob).decode("ascii")}],
}
with open("mirrored_node.gltf", "w") as f:
    json.dump(doc, f, indent=1)
print("wrote mirrored_node.gltf")
