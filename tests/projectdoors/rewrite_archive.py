#!/usr/bin/env python3
# project.refuses_unreadable's fixture maker (STUDIO-HYGIENE-1): from one real export and one real library,
# the files a build must not read. Read-only on the export; the library is the row's own (the app is not
# running). Every archive output is a new file.
#   manifest_v3.zip    jah.manifest.json says version 3 (a newer build's manifest)
#   scene_v3.zip       the archive's scene blob says formatVersion 3 (a newer build's scene)
#   scene_garbage.zip  the archive's scene blob is not JSON
# and in the LIBRARY, by project name:
#   "Doors Corrupt Row"      its blob is not JSON
#   "Doors Newer Row"        formatVersion 3
#   "Doors Older Row"        formatVersion 1
#   "Doors Unversioned Row"  no formatVersion at all (what every build before format v2 wrote)
# The four rows' blobs are recorded (sha256) in <out>/rows.json, for the check that a refused open changed
# nothing (`check` mode, after the second process).
# usage: rewrite_archive.py make <archive> <out> <library>  |  rewrite_archive.py check <out> <library>
import hashlib, json, os, sqlite3, sys, tempfile, zipfile

ROWS = ("Doors Corrupt Row", "Doors Newer Row", "Doors Older Row", "Doors Unversioned Row")
GARBAGE = b"{this is not a scene"

def blobs(library):
    conn = sqlite3.connect(library)
    out = {}
    for name in ROWS:
        rows = conn.execute("SELECT scene FROM projects WHERE name = ?", (name,)).fetchall()
        assert len(rows) == 1, "the library holds %d project(s) named %r" % (len(rows), name)
        out[name] = hashlib.sha256(bytes(rows[0][0])).hexdigest()
    conn.close()
    return out

def with_version(blob, version):
    scene = json.loads(bytes(blob))
    assert scene["formatVersion"] == 2, scene["formatVersion"]
    if version is None:
        del scene["formatVersion"]
    else:
        scene["formatVersion"] = version
    return json.dumps(scene).encode()

def make(src, out, library):
    with zipfile.ZipFile(src) as z:
        entries = {i.filename: z.read(i.filename) for i in z.infolist()}
    dbs = [n for n in entries if n.endswith(".db") and "/" not in n]
    if len(dbs) != 1 or "jah.manifest.json" not in entries:
        sys.exit("not a project archive: %s (dbs %s)" % (src, dbs))

    def write(name, changed):
        path = os.path.join(out, name)
        with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as z:
            for n, b in entries.items():
                z.writestr(n, changed.get(n, b))
        print("wrote", path)

    manifest = json.loads(entries["jah.manifest.json"])
    assert manifest["version"] == 2, manifest["version"]
    manifest["version"] = 3
    write("manifest_v3.zip", {"jah.manifest.json": json.dumps(manifest, indent=2).encode() + b"\n"})

    def with_scene(transform):
        with tempfile.TemporaryDirectory() as tmp:
            db = os.path.join(tmp, dbs[0])
            with open(db, "wb") as f:
                f.write(entries[dbs[0]])
            conn = sqlite3.connect(db)
            rows = conn.execute("SELECT guid, scene FROM projects").fetchall()
            assert len(rows) == 1, len(rows)
            conn.execute("UPDATE projects SET scene = ? WHERE guid = ?", (transform(rows[0][1]), rows[0][0]))
            conn.commit()
            conn.execute("PRAGMA journal_mode = DELETE")
            conn.close()
            with open(db, "rb") as f:
                return f.read()

    write("scene_v3.zip", {dbs[0]: with_scene(lambda blob: with_version(blob, 3))})
    write("scene_garbage.zip", {dbs[0]: with_scene(lambda blob: GARBAGE)})

    conn = sqlite3.connect(library)
    edits = {"Doors Corrupt Row": lambda blob: GARBAGE,
             "Doors Newer Row": lambda blob: with_version(blob, 3),
             "Doors Older Row": lambda blob: with_version(blob, 1),
             "Doors Unversioned Row": lambda blob: with_version(blob, None)}
    for name, edit in edits.items():
        blob = conn.execute("SELECT scene FROM projects WHERE name = ?", (name,)).fetchone()[0]
        hit = conn.execute("UPDATE projects SET scene = ? WHERE name = ?", (edit(blob), name)).rowcount
        assert hit == 1, "the library holds %d project(s) named %r" % (hit, name)
        print("rewrote the library row of", name)
    conn.commit()
    conn.close()
    with open(os.path.join(out, "rows.json"), "w") as f:
        json.dump(blobs(library), f, indent=1)

def check(out, library):
    with open(os.path.join(out, "rows.json")) as f:
        before = json.load(f)
    after = blobs(library)
    changed = [n for n in ROWS if before[n] != after[n]]
    if changed:
        sys.exit("CHANGED by a refused open: %s" % changed)
    print("the four refused rows are byte-for-byte what they were:", ", ".join(ROWS))

if sys.argv[1] == "make":
    make(sys.argv[2], sys.argv[3], sys.argv[4])
elif sys.argv[1] == "check":
    check(sys.argv[2], sys.argv[3])
else:
    sys.exit("usage: rewrite_archive.py make <archive> <out> <library> | check <out> <library>")
