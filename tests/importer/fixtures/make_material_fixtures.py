#!/usr/bin/env python3
"""Generates material_workflows.glb, the fixture for tests/importer/test_importer_materials.cpp.

ONE GLB, five quads, five materials — one per glTF material shape the importer
has to tell apart. They are written here rather than downloaded so the exact
bytes under test are reviewable and no third-party content ships in the repo.

    specgloss     KHR_materials_pbrSpecularGlossiness ONLY (no
                  pbrMetallicRoughness object at all), specularFactor [0,0,0],
                  glossinessFactor 0.0178, white diffuseFactor + a diffuse
                  texture. The shape of ~/Downloads/tails_obj_free_3d_model.glb,
                  which imported as full metal + full rough (= black) because
                  assimp reports metallicFactor 1 for it anyway.
    specgloss_metal
                  The same extension with specularFactor [1,1,1] — the input
                  the conversion is SUPPOSED to read as metal, so the suite
                  proves the formula rather than a "always dielectric" shortcut.
    nopbr         A material with a name and nothing else: no
                  pbrMetallicRoughness, no extension. Policy (mesh.h): a
                  dielectric, not a mirror.
    mr_default    pbrMetallicRoughness PRESENT with only roughnessFactor —
                  metallicFactor omitted, which glTF defines as 1.0. The shape
                  of lotus_elise.glb; spec-faithful metal is the CORRECT import.
    unlit         KHR_materials_unlit with a BLACK baseColorFactor and the
                  artwork in emissiveTexture/emissiveFactor — the shape of
                  spirit_blossom_kindred.glb.
    nomaterial    A primitive with NO `material` at all. assimp synthesizes one
                  aiMaterial for these and APPENDS it after the file's own, so
                  it has no entry in the JSON `materials` array — and assimp's
                  synthesized material reports metallicFactor 1 / roughnessFactor
                  1 like every other. That is the "a mesh with no material
                  imports black" report; the policy answer is a dielectric.

Run:  python3 make_material_fixtures.py     (writes the .glb beside this script)
"""
import json, os, struct, zlib

HERE = os.path.dirname(os.path.abspath(__file__))


def png(rgb, size=4):
    """A tiny solid-colour PNG, hand-encoded (no image library at test time)."""
    raw = b"".join(b"\x00" + bytes(rgb) * size for _ in range(size))
    def chunk(tag, data):
        c = tag + data
        return struct.pack(">I", len(data)) + c + struct.pack(">I", zlib.crc32(c))
    return (b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw))
            + chunk(b"IEND", b""))


def build():
    # One unit quad, reused by every mesh: positions, normals, uvs, indices.
    positions = [(-1, -1, 0), (1, -1, 0), (1, 1, 0), (-1, 1, 0)]
    normals = [(0, 0, 1)] * 4
    uvs = [(0, 1), (1, 1), (1, 0), (0, 0)]
    indices = [0, 1, 2, 0, 2, 3]

    pos_bytes = b"".join(struct.pack("<3f", *p) for p in positions)
    nrm_bytes = b"".join(struct.pack("<3f", *n) for n in normals)
    uv_bytes = b"".join(struct.pack("<2f", *u) for u in uvs)
    idx_bytes = b"".join(struct.pack("<H", i) for i in indices)

    tex_diffuse = png((220, 120, 40))    # spec-gloss diffuse map
    tex_emissive = png((40, 200, 220))   # unlit "colour" map

    blobs = [pos_bytes, nrm_bytes, uv_bytes, idx_bytes, tex_diffuse, tex_emissive]
    views, buf, offset = [], bytearray(), 0
    for b in blobs:
        pad = (-len(buf)) % 4
        buf += b"\x00" * pad
        offset = len(buf)
        buf += b
        views.append({"buffer": 0, "byteOffset": offset, "byteLength": len(b)})
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

    materials = [
        {   # 0: spec-gloss only, black specular -> dielectric after conversion
            "name": "specgloss",
            "extensions": {"KHR_materials_pbrSpecularGlossiness": {
                "diffuseFactor": [1.0, 1.0, 1.0, 1.0],
                "diffuseTexture": {"index": 0},
                "specularFactor": [0.0, 0.0, 0.0],
                "glossinessFactor": 0.0177827941}},
        },
        {   # 1: spec-gloss, white specular -> metal after conversion
            "name": "specgloss_metal",
            "extensions": {"KHR_materials_pbrSpecularGlossiness": {
                "diffuseFactor": [1.0, 1.0, 1.0, 1.0],
                "specularFactor": [1.0, 1.0, 1.0],
                "glossinessFactor": 0.75}},
        },
        {"name": "nopbr"},                                    # 2: no workflow at all
        {   # 3: metallic-roughness present, metallicFactor omitted (= 1.0)
            "name": "mr_default",
            "pbrMetallicRoughness": {"baseColorFactor": [0.8, 0.8, 0.8, 1.0],
                                     "roughnessFactor": 0.4},
        },
        {   # 4: unlit, colour in the emissive slot, base colour black
            "name": "unlit",
            "pbrMetallicRoughness": {"baseColorFactor": [0.0, 0.0, 0.0, 1.0],
                                     "metallicFactor": 0.0},
            "emissiveFactor": [1.0, 1.0, 1.0],
            "emissiveTexture": {"index": 1},
            "extensions": {"KHR_materials_unlit": {}},
        },
    ]

    names = ["specgloss", "specgloss_metal", "nopbr", "mr_default", "unlit"]
    meshes, nodes = [], []
    for i, name in enumerate(names):
        meshes.append({"name": name, "primitives": [{
            "attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2},
            "indices": 3, "material": i}]})
        nodes.append({"name": name, "mesh": i, "translation": [i * 3.0, 0.0, 0.0]})

    # ...and the one primitive with NO `material` key. It is LAST on purpose:
    # assimp appends its synthesized default material after the file's own, so
    # this is the mesh whose aiMaterial index runs off the end of the JSON
    # array — the case the importer has to recognise as "states nothing".
    meshes.append({"name": "nomaterial", "primitives": [{
        "attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2},
        "indices": 3}]})
    nodes.append({"name": "nomaterial", "mesh": len(meshes) - 1,
                  "translation": [len(names) * 3.0, 0.0, 0.0]})

    gltf = {
        "asset": {"version": "2.0", "generator": "jahshaka test fixture"},
        "extensionsUsed": ["KHR_materials_pbrSpecularGlossiness", "KHR_materials_unlit"],
        "scene": 0,
        "scenes": [{"nodes": list(range(len(nodes)))}],
        "nodes": nodes,
        "meshes": meshes,
        "materials": materials,
        "accessors": accessors,
        "bufferViews": views,
        "buffers": [{"byteLength": len(buf)}],
        "images": [{"bufferView": 4, "mimeType": "image/png"},
                   {"bufferView": 5, "mimeType": "image/png"}],
        "samplers": [{}],
        "textures": [{"source": 0, "sampler": 0}, {"source": 1, "sampler": 0}],
    }

    js = json.dumps(gltf, separators=(",", ":")).encode()
    js += b" " * ((-len(js)) % 4)
    bin_ = bytes(buf) + b"\x00" * ((-len(buf)) % 4)
    glb = (struct.pack("<III", 0x46546C67, 2, 12 + 8 + len(js) + 8 + len(bin_))
           + struct.pack("<II", len(js), 0x4E4F534A) + js
           + struct.pack("<II", len(bin_), 0x004E4942) + bin_)
    out = os.path.join(HERE, "material_workflows.glb")
    with open(out, "wb") as f:
        f.write(glb)
    print("wrote", out, len(glb), "bytes")


if __name__ == "__main__":
    build()
