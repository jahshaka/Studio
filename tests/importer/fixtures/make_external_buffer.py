#!/usr/bin/env python3
"""external_buffer.gltf + external_buffer.bin — a .gltf whose geometry lives in
a SEPARATE FILE beside it.

Why it exists (IMPORT-2, the second read's F7): every other glTF fixture in this
tree embeds its buffer (a data: URI or a .glb container), so nothing measured
what happens to a model whose source is MORE THAN ONE FILE — through the
content-addressed store, through a reimport, and through the import dialog's
pre-read, which reads the stored bytes straight out of the CAS where the
sibling .bin is not sitting next to them.

A 1 m cube, so its extent is a round number to assert on.

Regenerate:  python3 make_external_buffer.py
"""
import json, os, struct

s = 1.0
# eight corners, 0..1 on every axis, as six quads (24 verts, flat normals)
faces = [
    ([(0,0,1),(1,0,1),(1,1,1),(0,1,1)], (0,0,1)),
    ([(1,0,0),(0,0,0),(0,1,0),(1,1,0)], (0,0,-1)),
    ([(1,0,1),(1,0,0),(1,1,0),(1,1,1)], (1,0,0)),
    ([(0,0,0),(0,0,1),(0,1,1),(0,1,0)], (-1,0,0)),
    ([(0,1,1),(1,1,1),(1,1,0),(0,1,0)], (0,1,0)),
    ([(0,0,0),(1,0,0),(1,0,1),(0,0,1)], (0,-1,0)),
]
positions, normals, indices = [], [], []
for quad, n in faces:
    base = len(positions)
    for p in quad:
        positions.append((p[0]*s, p[1]*s, p[2]*s))
        normals.append(n)
    indices += [base, base+1, base+2, base, base+2, base+3]

pos_bytes = b"".join(struct.pack("<3f", *p) for p in positions)
nrm_bytes = b"".join(struct.pack("<3f", *n) for n in normals)
idx_bytes = b"".join(struct.pack("<H", i) for i in indices)
pad = lambda b: b + b"\0" * ((4 - len(b) % 4) % 4)
blob = pad(pos_bytes) + pad(nrm_bytes) + pad(idx_bytes)
pos_off, nrm_off = 0, len(pad(pos_bytes))
idx_off = nrm_off + len(pad(nrm_bytes))

doc = {
    "asset": {"version": "2.0", "generator": "jahshaka make_external_buffer.py"},
    "scene": 0,
    "scenes": [{"nodes": [0]}],
    "nodes": [{"mesh": 0, "name": "cube"}],
    "meshes": [{"name": "cube", "primitives": [
        {"attributes": {"POSITION": 0, "NORMAL": 1}, "indices": 2, "mode": 4}]}],
    "accessors": [
        {"bufferView": 0, "componentType": 5126, "count": len(positions), "type": "VEC3",
         "min": [0.0, 0.0, 0.0], "max": [s, s, s]},
        {"bufferView": 1, "componentType": 5126, "count": len(normals), "type": "VEC3"},
        {"bufferView": 2, "componentType": 5123, "count": len(indices), "type": "SCALAR"},
    ],
    "bufferViews": [
        {"buffer": 0, "byteOffset": pos_off, "byteLength": len(pos_bytes), "target": 34962},
        {"buffer": 0, "byteOffset": nrm_off, "byteLength": len(nrm_bytes), "target": 34962},
        {"buffer": 0, "byteOffset": idx_off, "byteLength": len(idx_bytes), "target": 34963},
    ],
    # THE POINT OF THE FIXTURE: an external URI, not a data: one.
    "buffers": [{"uri": "external_buffer.bin", "byteLength": len(blob)}],
}

here = os.path.dirname(os.path.abspath(__file__))
open(os.path.join(here, "external_buffer.bin"), "wb").write(blob)
open(os.path.join(here, "external_buffer.gltf"), "w").write(json.dumps(doc, indent=1) + "\n")
print("wrote external_buffer.gltf + external_buffer.bin (%d bytes)" % len(blob))
