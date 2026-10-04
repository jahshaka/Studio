#!/usr/bin/env python3
# A SKELETAL CLIP IS REFERENCED BY ITS ASSET GUID (CLIP-REF-1).
#
# A scene persists a clip as {"guid", "name"} and the reader resolves the guid
# through the store; the path ("source") is no reference any more and the
# reader has no path arm. This re-authors the SHIPPED SAMPLES the same way:
# every "skeletalAnimation" object with no guid gets the guid of the archive's
# Mesh row (typeId 6) whose name is the base name of its "source", and the
# "source" key is dropped. That is the model's own clip — the only kind a
# shipped sample carries. Both the project scene and every asset blob
# (the library Object's import blob) are rewritten.
#
# Run it, do not hand-edit the archives:
#
#   python3 scenes/tools/clip_refs_by_guid.py scenes/*.zip
#
# IDEMPOTENT: a second run finds nothing to change and rewrites nothing. Only
# the text of the skeletalAnimation objects changes (a TEXTUAL edit of the
# exact occurrences, the writer's indentation kept); every other archive member
# is copied through unchanged, in order. A clip whose source names no Mesh row
# stops the run: nothing is guessed.
import os
import re
import shutil
import sqlite3
import sys
import tempfile
import zipfile

PATTERN = re.compile(r'"skeletalAnimation"\s*:\s*\{(?P<body>[^{}]*)\}')
FIELD = re.compile(r'"(?P<key>[a-zA-Z]+)"\s*:\s*"(?P<value>(?:[^"\\]|\\.)*)"')


def rewrite_text(text, meshes, where):
    changed = []

    def rep(m):
        body = m.group("body")
        fields = {f.group("key"): f.group("value") for f in FIELD.finditer(body)}
        if fields.get("guid"):
            return m.group(0)
        source = fields.get("source", "")
        base = re.split(r"[\\/]", source)[-1]
        guid = meshes.get(base)
        if not guid:
            raise SystemExit("%s: the clip '%s' names '%s', and no Mesh row is called '%s'"
                             % (where, fields.get("name", ""), source, base))
        indent = re.search(r"\n([ \t]*)\"", body)
        close = re.search(r"\n([ \t]*)$", body)
        ind = indent.group(1) if indent else ""
        end = close.group(1) if close else ""
        changed.append(fields.get("name", ""))
        return ('"skeletalAnimation": {\n%s"guid": "%s",\n%s"name": "%s"\n%s}'
                % (ind, guid, ind, fields.get("name", ""), end))

    return PATTERN.sub(rep, text), changed


def rewrite_archive(path):
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
        changed = []
        for table, column in (("projects", "scene"), ("assets", "asset")):
            rows = con.execute("SELECT rowid, %s FROM %s" % (column, table)).fetchall()
            for rowid, value in rows:
                if value is None:
                    continue
                isbytes = isinstance(value, (bytes, bytearray))
                text = value.decode("utf-8") if isbytes else value
                new, names = rewrite_text(text, meshes, "%s %s.%s row %d" % (path, table, column, rowid))
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
        print("%s: %d clip(s) referenced by guid: %s" % (path, len(changed), ", ".join(changed)))
    finally:
        shutil.rmtree(tmp)


if __name__ == "__main__":
    if len(sys.argv) < 2:
        raise SystemExit("usage: clip_refs_by_guid.py <archive.zip>...")
    for p in sys.argv[1:]:
        rewrite_archive(p)
