#!/usr/bin/env python3
"""Brings a sample archive's LIBRARY OBJECT blobs to today's document form
(SAMPLES-1, 2026-10-01). Step 2 of the three-step re-authoring in
scenes/tools/reauthor_samples.js's header, which is how it is run:

  python3 samples_library_objects.py <archive.zip> <import-colours.json> <out.zip>

`out.zip` is a SCRATCH copy: the shipped archive is rewritten only by the app's
own project.exportArchive in step 3, after the app has read every converted
blob back. `import-colours.json` is what step 1 MEASURED (see COLOUR).

WHAT A LIBRARY OBJECT BLOB IS. An `assets` row of type Object (5) whose `asset`
column holds the node tree the importer wrote when the model came in
(SceneWriter::writeSceneNode) — the tile the editor's asset tray drags into a
scene. The scene's own nodes are re-written on every save; these blobs are
re-written by nothing, so the samples' blobs are still in the form of the build
that imported them. Today's reader reads neither of their two old forms
(FORWARD-ONLY-1 deleted both arms), so a dragged-in object arrived UNROTATED
and wearing the DEFAULT material:

1. ROTATION. An euler `rot` {x, y, z} (degrees) — no `scalar` key — becomes
   the quaternion {x, y, z, scalar} the writer writes, through the exact
   function the deleted reader arm called:
   iris::Quat::fromEulerAngles(pitch = x, yaw = y, roll = z).normalized()
   (irisgl/core/math/quat.h; the arm: scenereader.cpp before 1a2650638).

2. MATERIAL. A material without `materialType: "pbr"` (the pre-PBR shader
   materials, keyed by the retired builtin shader guids) becomes a PBR material
   in the writer's shape {materialType: "pbr", version: 2, values: {...}}; an
   absent value is the PbrMaterial constructor's (the reader-defaults law).
   THE MAPPING (the table the scene blobs were converted with, HLMS_ADOPTION P4b,
   formerly convert_legacy_materials.py + builtinmaterials.cpp — this file is
   now its only copy):

     Default / DefaultAnimated (Phong):
       baseColor    <- diffuseColor (see COLOUR)
       roughness    <- 1 - sqrt(clamp(shininess, 0, 128) / 128) * 0.9
                       (the old mirror's remap: shininess 0 -> 1.0, 128 -> 0.1)
       metallic     <- 0 (a Phong shader is a dielectric: its specularColor
                       tinted a highlight and is NOT metalness — it, ambientColor
                       and the reflection pair have no metallic-roughness home
                       and are dropped)
       baseColorMap <- diffuseTexture    normalMap    <- normalTexture
       normalFactor <- normalIntensity   textureScale <- textureScale
       alphaMode    <- 2 (BLEND) when useAlpha — what the same objects' SCENE
                       instances were given by the same table
     EdgeMaterial: baseColor <- color, metallic 0, roughness 0.5
     Flat:         baseColor <- color, shadingModel 1 (unlit)
     Glass:        the Glass PBR preset; Matcap: the Gold (mc16) / Silver preset

   COLOUR — MEASURED, NEVER GUESSED (reauthor_samples.js's provenance rule of
   2026-09-13, applied to the blobs). Since LIGHTS-2 a document colour is sRGB
   and the importer ENCODES the linear factor a file carries; an old importer
   pushed the raw linear factor into the colour. Step 1 re-imports every model
   file the scene uses with TODAY's importer and records, per Mesh row guid and
   mesh index, the colour it produces. A blob colour within 2/255 of that
   colour's linear spelling IS the old importer's raw factor and takes today's
   value; any other colour is carried as written (a document colour is the sRGB
   value the eye saw, which is what the old gamma-less pipeline put on screen).

Idempotent: a quaternion `rot` and a PBR material are left alone.
"""

import io
import json
import math
import os
import sqlite3
import struct
import sys
import tempfile
import zipfile

OBJECT_TYPE = 5

SHADER_DEFAULT = "00000000-0000-0000-0000-000000000001"
SHADER_DEFAULT_ANIMATED = "00000000-0000-0000-0000-000000000002"
SHADER_EDGE = "00000000-0000-0000-0000-000000000003"
SHADER_FLAT = "00000000-0000-0000-0000-000000000004"
SHADER_GLASS = "00000000-0000-0000-0000-000000000005"
SHADER_MATCAP = "00000000-0000-0000-0000-000000000006"

# The drawer presets (app/content/materials/{Silver,Gold,Glass}-pbr.material).
SILVER = {"baseColor": "#f5f5f7", "metallic": 1.0, "roughness": 0.22}
GOLD = {"baseColor": "#ffd700", "metallic": 1.0, "roughness": 0.3}
GLASS = {"baseColor": "#eef4f8", "metallic": 0.0, "roughness": 0.05, "alphaMode": 3, "alpha": 0.3}
GOLD_MATCAPS = {"mc16.jpg"}


def f32(v):
    return struct.unpack("f", struct.pack("f", v))[0]


def quat_from_euler(pitch, yaw, roll):
    """iris::Quat::fromEulerAngles(pitch, yaw, roll).normalized()."""
    pitch, yaw, roll = f32(pitch * 0.5), f32(yaw * 0.5), f32(roll * 0.5)
    rad = math.pi / 180.0
    c1, s1 = math.cos(yaw * rad), math.sin(yaw * rad)
    c2, s2 = math.cos(roll * rad), math.sin(roll * rad)
    c3, s3 = math.cos(pitch * rad), math.sin(pitch * rad)
    c1c2, s1s2 = c1 * c2, s1 * s2
    w = c1c2 * c3 + s1s2 * s3
    x = c1c2 * s3 + s1s2 * c3
    y = s1 * c2 * c3 - c1 * s2 * s3
    z = c1 * s2 * c3 - s1 * c2 * s3
    n = math.sqrt(w * w + x * x + y * y + z * z)
    if n < 1e-12:
        return {"x": 0.0, "y": 0.0, "z": 0.0, "scalar": 0.0}
    return {"x": f32(x / n), "y": f32(y / n), "z": f32(z / n), "scalar": f32(w / n)}


def roughness_from_shininess(shininess):
    s = max(0.0, min(float(shininess), 128.0))
    return f32(1.0 - (s / 128.0) ** 0.5 * 0.9)


def hex_rgb(h):
    h = str(h).lstrip("#")
    return [int(h[i:i + 2], 16) for i in (0, 2, 4)] if len(h) >= 6 else None


def linear_spelling(h):
    """The 8-bit colour the pre-srgbOf importer wrote for the file that today
    imports as `h`: today's value is the sRGB encoding of the file's linear
    factor, so the old one is its decode (reauthor_samples.js oldSpellingOf)."""
    out = []
    for c in hex_rgb(h):
        v = c / 255.0
        lin = v / 12.92 if v <= 0.04045 else ((v + 0.055) / 1.055) ** 2.4
        out.append(int(round(lin * 255)))
    return "#%02x%02x%02x" % tuple(out)


def close(a, b, tol=2):
    ra, rb = hex_rgb(a), hex_rgb(b)
    return ra is not None and rb is not None and all(abs(x - y) <= tol for x, y in zip(ra, rb))


def pbr(values):
    return {"materialType": "pbr", "version": 2, "values": dict(values)}


def base_colour(colour, today):
    """(colour to write, note)."""
    if today and close(colour, linear_spelling(today)):
        return today, ("baseColor %s -> %s (the old importer's raw linear factor: today's import "
                       "of this file says %s, linear spelling %s)"
                       % (colour, today, today, linear_spelling(today)))
    return colour, "baseColor %s kept (today's import of this file says %s, linear spelling %s)" % (
        colour, today or "-", linear_spelling(today) if today else "-")


def convert_material(mat, asset_names, today):
    """(replacement, note) or (None, None) to keep `mat`."""
    if mat.get("materialType") == "pbr":
        return None, None
    shader = mat.get("shaderGuid") or mat.get("guid") or ""
    values = mat.get("values", mat)
    if shader == SHADER_MATCAP:
        tex = asset_names.get(values.get("matTexture", ""), "")
        return pbr(GOLD if tex in GOLD_MATCAPS else SILVER), "Matcap -> preset"
    if shader == SHADER_GLASS:
        return pbr(GLASS), "Glass -> Glass preset"
    if shader in (SHADER_DEFAULT, SHADER_DEFAULT_ANIMATED) or (not shader and "diffuseColor" in values):
        out = {"metallic": 0.0}
        notes = []
        colour = values.get("diffuseColor") or values.get("color")
        if isinstance(colour, str) and colour:
            out["baseColor"], note = base_colour(colour, today)
            notes.append(note)
        if "roughness" in values:
            out["roughness"] = values["roughness"]
        elif "shininess" in values:
            out["roughness"] = roughness_from_shininess(values["shininess"])
            notes.append("shininess %g -> roughness %.4f" % (float(values["shininess"]), out["roughness"]))
        for src, dst in (("textureScale", "textureScale"), ("normalIntensity", "normalFactor")):
            if src in values:
                out[dst] = values[src]
        if values.get("useAlpha") is True:
            out["alphaMode"] = 2
        for src, dst in (("diffuseTexture", "baseColorMap"), ("normalTexture", "normalMap")):
            if isinstance(values.get(src), str) and values[src]:
                out[dst] = values[src]
                notes.append("%s -> %s" % (src, dst))
        return pbr(out), "%s: %s, metallic 0" % (mat.get("name") or "Default", ", ".join(notes))
    if shader == SHADER_EDGE:
        out = {"metallic": 0.0, "roughness": 0.5}
        if isinstance(values.get("color"), str) and values["color"]:
            out["baseColor"] = values["color"]
        return pbr(out), "EdgeMaterial: baseColor %s, roughness 0.5, metallic 0" % out.get("baseColor")
    if shader == SHADER_FLAT:
        out = {"shadingModel": 1}
        if isinstance(values.get("color"), str) and values["color"]:
            out["baseColor"] = values["color"]
        return pbr(out), "Flat -> unlit"
    raise SystemExit("unknown pre-PBR material %r — extend the table, do not guess" % mat)


def convert_db(path, import_colours, log):
    con = sqlite3.connect(path)
    asset_names = {g: n for g, n in con.execute("select guid, name from assets")}
    totals = {"rows": 0, "euler": 0, "eulerNonZero": 0, "materials": 0}
    rows = con.execute("select guid, name, asset from assets where type = ? and asset is not null",
                       (OBJECT_TYPE,)).fetchall()
    for guid, name, blob in rows:
        try:
            tree = json.loads(blob)
        except ValueError:
            continue
        if not isinstance(tree, dict):
            continue
        changed = [False]

        def walk(n):
            r = n.get("rot")
            if isinstance(r, dict) and "scalar" not in r:
                e = (float(r.get("x", 0)), float(r.get("y", 0)), float(r.get("z", 0)))
                n["rot"] = quat_from_euler(*e)
                totals["euler"] += 1
                if any(abs(v) > 1e-9 for v in e):
                    totals["eulerNonZero"] += 1
                changed[0] = True
            m = n.get("material")
            if isinstance(m, dict) and m:
                today = import_colours.get(str(n.get("mesh", "")), {}).get(str(n.get("meshIndex", 0)))
                repl, note = convert_material(m, asset_names, today)
                if repl is not None:
                    n["material"] = repl
                    totals["materials"] += 1
                    changed[0] = True
                    log("  %s / %s: %s" % (name, n.get("name"), note))
            for c in n.get("children", []) or []:
                walk(c)

        walk(tree)
        if changed[0]:
            totals["rows"] += 1
            con.execute("update assets set asset = ? where guid = ?",
                        (json.dumps(tree, indent=4).encode("utf-8"), guid))
    con.commit()
    con.close()
    return totals


def main(argv):
    if len(argv) != 4:
        print(__doc__.split("\n\n")[1])
        return 2
    src, table, dst = argv[1], argv[2], argv[3]
    if os.path.abspath(src) == os.path.abspath(dst):
        raise SystemExit("refusing to rewrite the archive in place — the app's export writes it")
    with open(table) as f:
        import_colours = json.load(f)
    with zipfile.ZipFile(src) as zf:
        entries = [(info, zf.read(info.filename)) for info in zf.infolist()]
    totals = None
    out = io.BytesIO()
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as zf:
        for info, data in entries:
            if info.filename.endswith(".db"):
                with tempfile.TemporaryDirectory() as td:
                    p = os.path.join(td, "catalog.db")
                    with open(p, "wb") as f:
                        f.write(data)
                    totals = convert_db(p, import_colours, print)
                    with open(p, "rb") as f:
                        data = f.read()
            zf.writestr(info, data)
    with open(dst, "wb") as f:
        f.write(out.getvalue())
    print("[library-objects] %s: %d object row(s) rewritten, %d euler rot(s) -> quaternion "
          "(%d non-zero), %d pre-PBR material(s) -> PBR"
          % (os.path.basename(src), totals["rows"], totals["euler"], totals["eulerNonZero"],
             totals["materials"]))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
