#!/usr/bin/env python3
"""Convert legacy shader-based materials in the bundled sample archives to PBR.

The engine viewport renders the document's material types (PbrMaterial, and the
Default-family CustomMaterial properties SceneMirror maps: diffuseColor/
diffuseTexture/normalTexture/...). The legacy GLSL shader pipeline is gone
(MATERIALS_EVALUATOR phase 5), so materials whose look lived in a shader body -
the builtin Matcap (00...06) and Glass (00...05) shaders - render as the shared
neutral-grey fallback. The shipped samples are the one data artifact Jahshaka
ships; they must look right through the pipeline the app OWNS.

This script rewrites the scene blob inside each sample archive's catalog
snapshot (.db), converting those materials to the drawer's PBR presets - the
same conversion the owner applied by hand to the dev library on 2026-08-29
(Matcap->Silver/Gold by matcap tone, Glass->Glass):

  Matcap with a gold-toned matcap texture (mc16)  -> Gold PBR
  Matcap with any other matcap texture            -> Silver PBR
  Glass                                           -> Glass PBR (alphaMode 3)

Default/DefaultAnimated/EdgeMaterial materials are left alone: their properties
map through SceneMirror today and remain supported until the builtin-shader
retirement program.

Idempotent: materials already carrying materialType "pbr" are skipped.
Run from anywhere:  python3 scenes/tools/convert_legacy_materials.py [scenes_dir]
"""

import io
import json
import os
import sqlite3
import sys
import tempfile
import zipfile

SHADER_GLASS = "00000000-0000-0000-0000-000000000005"
SHADER_MATCAP = "00000000-0000-0000-0000-000000000006"

# Drawer preset values (app/content/materials/{Silver,Gold,Glass}-pbr.material).
SILVER = {"baseColor": "#F5F5F7", "metallic": 1.0, "roughness": 0.22}
GOLD = {"baseColor": "#FFD700", "metallic": 1.0, "roughness": 0.3}
GLASS = {"baseColor": "#EEF4F8", "metallic": 0.0, "roughness": 0.05,
         "alphaMode": 3, "alpha": 0.3}

# Matcap textures whose dominant tone is gold -> Gold preset.
GOLD_MATCAPS = {"mc16.jpg"}

SAMPLES = ["Matcaps.zip", "Particles.zip", "Skeletal Animation.zip",
           "World Background.zip", "Physics.zip"]


def pbr_material(name, values):
    return {"name": name, "materialType": "pbr", "version": 2, "values": dict(values)}


def convert_material(mat, asset_names):
    """Return a replacement material dict, or None to keep the original."""
    if mat.get("materialType") == "pbr":
        return None
    shader = mat.get("shaderGuid") or mat.get("guid") or ""
    values = mat.get("values", mat)  # v1 materials keep params at top level
    if shader == SHADER_MATCAP:
        tex = asset_names.get(values.get("matTexture", ""), "")
        if tex in GOLD_MATCAPS:
            return pbr_material("Gold PBR", GOLD)
        return pbr_material("Silver PBR", SILVER)
    if shader == SHADER_GLASS:
        return pbr_material("Glass PBR", GLASS)
    return None


def convert_scene(scene_json, asset_names):
    changed = []

    def walk(node):
        mat = node.get("material")
        if isinstance(mat, dict):
            repl = convert_material(mat, asset_names)
            if repl is not None:
                changed.append((node.get("name", "?"), mat.get("name", "?"), repl["name"]))
                node["material"] = repl
        for child in node.get("children", []):
            walk(child)

    walk(scene_json["scene"]["rootNode"])
    return changed


def convert_db_bytes(db_bytes):
    with tempfile.NamedTemporaryFile(suffix=".db", delete=False) as tf:
        tf.write(db_bytes)
        tmp = tf.name
    try:
        conn = sqlite3.connect(tmp)
        asset_names = {g: n for g, n in conn.execute("select guid, name from assets")}
        rows = conn.execute("select guid, scene from projects").fetchall()
        all_changes = []
        for guid, blob in rows:
            scene_json = json.loads(blob)
            changes = convert_scene(scene_json, asset_names)
            if changes:
                conn.execute("update projects set scene = ? where guid = ?",
                             (json.dumps(scene_json), guid))
                all_changes.extend(changes)
        conn.commit()
        conn.close()
        with open(tmp, "rb") as f:
            return f.read(), all_changes
    finally:
        os.unlink(tmp)


def convert_archive(path):
    with zipfile.ZipFile(path) as zf:
        entries = [(info, zf.read(info.filename)) for info in zf.infolist()]
    changes = []
    out = io.BytesIO()
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as zf:
        for info, data in entries:
            if info.filename.endswith(".db"):
                data, changes = convert_db_bytes(data)
            zf.writestr(info, data)
    if changes:
        with open(path, "wb") as f:
            f.write(out.getvalue())
    return changes


def main():
    scenes_dir = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "..")
    total = 0
    for name in SAMPLES:
        path = os.path.join(scenes_dir, name)
        if not os.path.exists(path):
            print("MISSING:", path)
            return 1
        changes = convert_archive(path)
        for node, old, new in changes:
            print("%s: %s  %s -> %s" % (name, node, old, new))
        if not changes:
            print("%s: no legacy shader materials (unchanged)" % name)
        total += len(changes)
    print("converted %d materials" % total)
    return 0


if __name__ == "__main__":
    sys.exit(main())
