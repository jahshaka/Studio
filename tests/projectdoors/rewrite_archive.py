#!/usr/bin/env python3
# project.refuses_unreadable's fixture maker: three archives from one real export, each with ONE thing a
# build must not read. Read-only on the source; every output is a new file.
#   manifest_v3.zip   jah.manifest.json says version 3 (a newer build's manifest)
#   scene_v3.zip      the nested catalog's scene blob says formatVersion 3 (a newer build's scene)
#   scene_garbage.zip the nested catalog's scene blob is not JSON
# and, in the LIBRARY (the app is not running): the row of the project named $4 gets a blob that is not JSON.
# $1 = the exported archive, $2 = the output directory, $3 = the library file, $4 = the project to corrupt
import json, os, sqlite3, sys, tempfile, zipfile

src, out, library, victim = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4]
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

def newer(blob):
    scene = json.loads(bytes(blob))
    assert scene["formatVersion"] == 2, scene["formatVersion"]
    scene["formatVersion"] = 3
    return json.dumps(scene).encode()

write("scene_v3.zip", {dbs[0]: with_scene(newer)})
write("scene_garbage.zip", {dbs[0]: with_scene(lambda blob: b"{this is not a scene")})

conn = sqlite3.connect(library)
hit = conn.execute("UPDATE projects SET scene = ? WHERE name = ?", (b"{this is not a scene", victim)).rowcount
conn.commit()
conn.close()
assert hit == 1, "the library holds %d project(s) named %r" % (hit, victim)
print("corrupted the library row of", victim)
