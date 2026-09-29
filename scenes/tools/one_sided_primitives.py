#!/usr/bin/env python3
# PRIMITIVES ARE ONE-SIDED (CULL-MODE-2; the owner, 2026-09-29, ledger §1433).
#
# Studio used to stamp faceCullingMode "none" (two-sided) on every primitive and
# on the default floor. That stamp is gone from the code; this re-authors the
# SHIPPED SAMPLES the same way: every mesh node whose mesh is a shipped primitive
# (:/content/primitives/*) or the default floor, and whose cull reads "none",
# is written "material" — what a node born today carries (MeshNode's own
# default: the material decides, and a material is one-sided unless authored
# otherwise). Any other "none" (a glTF doubleSided import, an image plane the
# user left double-sided) is the user's and is kept.
#
# Run it, do not hand-edit the archives:
#
#   python3 scenes/tools/one_sided_primitives.py scenes/*.zip
#
# IDEMPOTENT: a second run finds nothing to change and rewrites nothing. Only the
# scene text of the archive's project database changes, and only the cull value
# of the nodes named above — a TEXTUAL edit of the exact occurrences, so the
# rest of the document keeps the writer's own bytes; every other archive member
# is copied through unchanged, in order.
import json
import os
import re
import shutil
import sqlite3
import sys
import tempfile
import zipfile

PATTERN = re.compile(r'("faceCullingMode"\s*:\s*)"([a-z]*)"')


def plan(obj, out):
    """Every faceCullingMode occurrence in TEXT order: True when it is flipped."""
    if isinstance(obj, dict):
        for key, value in obj.items():
            if key == "faceCullingMode":
                mesh = obj.get("mesh") or ""
                primitive = mesh.startswith(":/content/primitives/") or bool(obj.get("defaultFloor"))
                out.append((value == "none" and primitive, obj.get("name")))
            else:
                plan(value, out)
    elif isinstance(obj, list):
        for value in obj:
            plan(value, out)


def rewrite_scene(text):
    doc = json.loads(text)
    marks = []
    plan(doc, marks)
    hits = list(PATTERN.finditer(text))
    if len(hits) != len(marks):
        raise SystemExit("occurrence count mismatch: %d in text, %d in the document" % (len(hits), len(marks)))
    names = []
    pieces, last = [], 0
    for m, (flip, name) in zip(hits, marks):
        if not flip:
            continue
        pieces.append(text[last:m.start(2)])
        pieces.append("material")
        last = m.end(2)
        names.append(name)
    pieces.append(text[last:])
    return "".join(pieces), names


def rewrite_archive(path):
    with zipfile.ZipFile(path) as z:
        infos = z.infolist()
        blobs = {i.filename: z.read(i.filename) for i in infos}
    dbs = [i.filename for i in infos if i.filename.endswith(".db") and "/" not in i.filename]
    if len(dbs) != 1:
        raise SystemExit("%s: expected one project database, found %r" % (path, dbs))
    tmp = tempfile.mkdtemp(prefix="one-sided-")
    try:
        dbpath = os.path.join(tmp, "p.db")
        with open(dbpath, "wb") as f:
            f.write(blobs[dbs[0]])
        con = sqlite3.connect(dbpath)
        changed = []
        for rowid, scene in con.execute("SELECT rowid, scene FROM projects").fetchall():
            text = scene.decode("utf-8") if isinstance(scene, (bytes, bytearray)) else scene
            new, names = rewrite_scene(text)
            if names:
                con.execute("UPDATE projects SET scene = ? WHERE rowid = ?",
                            (new.encode("utf-8") if isinstance(scene, (bytes, bytearray)) else new, rowid))
                changed += names
        con.commit()
        con.close()
        if not changed:
            print("%s: nothing to change" % path)
            return
        with open(dbpath, "rb") as f:
            blobs[dbs[0]] = f.read()
        out = path + ".tmp"
        with zipfile.ZipFile(out, "w") as z:
            for i in infos:
                z.writestr(i, blobs[i.filename])
        os.replace(out, path)
        print("%s: %d node(s) one-sided: %s" % (path, len(changed), ", ".join(changed)))
    finally:
        shutil.rmtree(tmp)


if __name__ == "__main__":
    if len(sys.argv) < 2:
        raise SystemExit("usage: one_sided_primitives.py <archive.zip>...")
    for p in sys.argv[1:]:
        rewrite_archive(p)
