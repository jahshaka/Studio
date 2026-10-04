#!/usr/bin/env python3
# A SKELETAL CLIP IS REFERENCED BY ITS ASSET GUID AND ITS BAKE'S NAME (CLIP-REF-1).
#
# A scene persists a clip as {"guid", "name"}: the reader resolves the guid
# through the store and takes the clip of exactly that NAME from the asset's
# bake — no path arm, no "the only clip" guess. This re-authors the SHIPPED
# SAMPLES the same way:
#   * a "skeletalAnimation" with no guid gets the guid of the archive's Mesh row
#     (typeId 6) whose name is the base name of its "source" (the model's own
#     clip — the only kind a shipped sample carries); "source" is dropped;
#   * its "name" becomes the name the BAKE holds. The names are read from the
#     app itself (`assets.clips`, the scene reader's own extraction) on the
#     archive's stored source bytes, in a throwaway data root: a stored name the
#     bake holds is kept; a bake with exactly ONE clip names it; anything else
#     stops the run — nothing is guessed at run time, and nothing here either.
# Both the project scene and every asset blob (the library Object's import
# blob) are rewritten.
#
# Run it, do not hand-edit the archives:
#
#   python3 scenes/tools/clip_refs_by_guid.py --app build-linux/bin/Jahshaka scenes/*.zip
#
# IDEMPOTENT: a second run finds nothing to change and rewrites nothing. Only
# the text of the skeletalAnimation objects changes (a TEXTUAL edit of the
# exact occurrences, the writer's indentation kept); every other archive member
# is copied through unchanged, in order. A clip whose source names no Mesh row
# stops the run: nothing is guessed.
import json
import os
import re
import shutil
import sqlite3
import subprocess
import sys
import tempfile
import zipfile

PATTERN = re.compile(r'"skeletalAnimation"\s*:\s*\{(?P<body>[^{}]*)\}')
FIELD = re.compile(r'"(?P<key>[a-zA-Z]+)"\s*:\s*"(?P<value>(?:[^"\\]|\\.)*)"')


def bake_clip_names(app, source_path):
    """The clip names the app's bake of `source_path` holds (assets.clips), from a throwaway
    data root so nothing of the user's is read or written."""
    home = tempfile.mkdtemp(prefix="clip-names-")
    try:
        script = os.path.join(home, "names.js")
        with open(script, "w") as f:
            f.write("var g = assets.import(%s);\n" % json.dumps(source_path))
            f.write("console.log('CLIPNAMES ' + JSON.stringify(assets.clips(g)));\n")
        env = dict(os.environ, HOME=home)
        out = subprocess.run([app, "--headless", "--data-root", os.path.join(home, "data"),
                              "--script", script], cwd=home, env=env,
                             capture_output=True, text=True, timeout=600)
        m = re.search(r"CLIPNAMES (\[.*\])", out.stdout + out.stderr)
        if not m:
            raise SystemExit("%s: the app reported no clip names (exit %d)" % (source_path, out.returncode))
        return json.loads(m.group(1))
    finally:
        shutil.rmtree(home, ignore_errors=True)


def rewrite_text(text, meshes, names_of, where):
    changed = []

    def rep(m):
        body = m.group("body")
        fields = {f.group("key"): f.group("value") for f in FIELD.finditer(body)}
        guid = fields.get("guid", "")
        if not guid:
            source = fields.get("source", "")
            base = re.split(r"[\\/]", source)[-1]
            guid = meshes.get(base, "")
            if not guid:
                raise SystemExit("%s: the clip '%s' names '%s', and no Mesh row is called '%s'"
                                 % (where, fields.get("name", ""), source, base))
        name = fields.get("name", "")
        held = names_of(guid)
        if name not in held:
            if len(held) != 1:
                raise SystemExit("%s: the clip '%s' of %s is not in its bake (it holds %r); "
                                 "nothing is guessed" % (where, name, guid, held))
            name = held[0]
        if guid == fields.get("guid") and name == fields.get("name") and "source" not in fields:
            return m.group(0)
        indent = re.search(r"\n([ \t]*)\"", body)
        close = re.search(r"\n([ \t]*)$", body)
        ind = indent.group(1) if indent else ""
        end = close.group(1) if close else ""
        changed.append(name)
        return ('"skeletalAnimation": {\n%s"guid": "%s",\n%s"name": "%s"\n%s}'
                % (ind, guid, ind, name, end))

    return PATTERN.sub(rep, text), changed


def rewrite_archive(app, path):
    with zipfile.ZipFile(path) as z:
        infos = z.infolist()
        blobs = {i.filename: z.read(i.filename) for i in infos}
    dbs = [i.filename for i in infos if i.filename.endswith(".db") and "/" not in i.filename]
    if len(dbs) != 1:
        raise SystemExit("%s: expected one project database, found %r" % (path, dbs))
    tmp = tempfile.mkdtemp(prefix="clip-refs-")
    try:
        dbpath = os.path.join(tmp, "p.db")
        with open(dbpath, "wb") as f:
            f.write(blobs[dbs[0]])
        con = sqlite3.connect(dbpath)
        meshes = {name: guid for guid, name in
                  con.execute("SELECT guid, name FROM assets WHERE type = 6").fetchall()}
        oids = {i.filename.split("/")[-1].split(".")[0]: i.filename
                for i in infos if i.filename.startswith("objects/")}
        manifest = json.loads(blobs.get("jah.manifest.json", b"{}") or b"{}")
        cache = {}

        def names_of(guid):
            if guid in cache:
                return cache[guid]
            entry = next((a for a in manifest.get("assets", []) if a.get("guid") == guid), None)
            src = next((f for f in (entry or {}).get("files", []) if f.get("role") == "source"), None)
            if not src or src.get("oid") not in oids:
                raise SystemExit("%s: the clip asset %s has no stored source in the archive" % (path, guid))
            staged = os.path.join(tmp, src["name"])
            with open(staged, "wb") as f:
                f.write(blobs[oids[src["oid"]]])
            cache[guid] = bake_clip_names(app, staged)
            print("%s: %s (%s) bakes clips %r" % (path, src["name"], guid, cache[guid]))
            return cache[guid]
        changed = []
        for table, column in (("projects", "scene"), ("assets", "asset")):
            rows = con.execute("SELECT rowid, %s FROM %s" % (column, table)).fetchall()
            for rowid, value in rows:
                if value is None:
                    continue
                isbytes = isinstance(value, (bytes, bytearray))
                text = value.decode("utf-8") if isbytes else value
                if '"skeletalAnimation"' not in text:
                    continue
                new, names = rewrite_text(text, meshes, names_of,
                                          "%s %s.%s row %d" % (path, table, column, rowid))
                if names:
                    con.execute("UPDATE %s SET %s = ? WHERE rowid = ?" % (table, column),
                                (new.encode("utf-8") if isbytes else new, rowid))
                    changed += ["%s:%s" % (table, n) for n in names]
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
        print("%s: %d clip reference(s) rewritten: %s" % (path, len(changed), ", ".join(changed)))
    finally:
        shutil.rmtree(tmp)


if __name__ == "__main__":
    if len(sys.argv) < 4 or sys.argv[1] != "--app":
        raise SystemExit("usage: clip_refs_by_guid.py --app <Jahshaka binary> <archive.zip>...")
    for p in sys.argv[3:]:
        rewrite_archive(os.path.abspath(sys.argv[2]), p)
