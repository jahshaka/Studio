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

HLMS_ADOPTION P4b (the builtin-shader retirement this file said it was waiting
for) added the rest:

  Default / DefaultAnimated -> a PBR material carrying the same values:
      diffuseColor    -> baseColor
      shininess       -> roughness, through the mirror's own remap
                         1 - sqrt(clamp(s,0,128)/128)*0.9
      normalIntensity -> normalFactor      textureScale -> textureScale
      diffuseTexture  -> baseColorMap      normalTexture -> normalMap
      useAlpha        -> alphaMode 2 (BLEND)
      (ambientColor, specularColor/Texture and the reflection pair are DROPPED:
       there is no metallic-roughness parameter for any of them, and the
       renderer has not read one of them since the engine viewport shipped)
  EdgeMaterial              -> its base `color`, matte (the fresnel rim has no
                               material-parameter home; it is a shading effect)
  Flat                      -> an UNLIT PBR material (D-P4b), which is what the
                               shading model added in P4a makes possible

THIS SCRIPT AND src/io/builtinmaterials.cpp ARE THE SAME TABLE. The reader
converts a legacy blob at load; this converts the shipped archives on disk so
they stop being legacy blobs. If the two ever disagree, a sample looks different
from the same material a user opens — keep them in step.

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

SHADER_DEFAULT = "00000000-0000-0000-0000-000000000001"
SHADER_DEFAULT_ANIMATED = "00000000-0000-0000-0000-000000000002"
SHADER_EDGE = "00000000-0000-0000-0000-000000000003"
SHADER_FLAT = "00000000-0000-0000-0000-000000000004"
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
           "World Background.zip", "Physics.zip", "Showroom.zip"]


def roughness_from_shininess(shininess):
    """The mirror's remap, and it must stay the mirror's — see
    BuiltinMaterials::roughnessFromShininess for why the 128 clamp is
    load-bearing."""
    s = max(0.0, min(float(shininess), 128.0))
    return 1.0 - (s / 128.0) ** 0.5 * 0.9


def default_family(values):
    """Default / DefaultAnimated uniform values -> PBR values."""
    out = {}
    for key in ("diffuseColor", "color", "albedo", "baseColor"):
        if isinstance(values.get(key), str) and values[key]:
            out["baseColor"] = values[key]
            break
    if "roughness" in values:
        out["roughness"] = values["roughness"]
    elif "shininess" in values:
        out["roughness"] = roughness_from_shininess(values["shininess"])
    for src, dst in (("metallic", "metallic"), ("textureScale", "textureScale"),
                     ("normalIntensity", "normalFactor")):
        if src in values:
            out[dst] = values[src]
    if values.get("useAlpha") is True:
        out["alphaMode"] = 2
    for src, dst in (("diffuseTexture", "baseColorMap"), ("normalTexture", "normalMap"),
                     ("emissiveMap", "emissiveMap")):
        if isinstance(values.get(src), str) and values[src]:
            out[dst] = values[src]
    return out


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
    if shader in (SHADER_DEFAULT, SHADER_DEFAULT_ANIMATED):
        return pbr_material("Default", default_family(values))
    if shader == SHADER_EDGE:
        edge = {"metallic": 0.0, "roughness": 0.5}
        if isinstance(values.get("color"), str) and values["color"]:
            edge["baseColor"] = values["color"]
        return pbr_material("EdgeMaterial", edge)
    if shader == SHADER_FLAT:
        # D-P4b: Flat is an UNLIT material, which is the exact conversion rather
        # than an approximation (it needed the shading model P4a added).
        flat = {"shadingModel": 1}
        if isinstance(values.get("color"), str) and values["color"]:
            flat["baseColor"] = values["color"]
        return pbr_material("Flat", flat)
    # An UNTYPED material with no shader guid at all (a few sample nodes carry
    # one): its keys are still the Default family's, so convert them the same
    # way rather than leaving a blob that the reader has to guess at.
    if not shader and mat.get("materialType") is None:
        converted = default_family(values)
        if converted:
            return pbr_material("Default", converted)
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
