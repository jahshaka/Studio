#!/usr/bin/env python3
"""The archive's own invariants (samples.archive_fidelity.*; TESTING-SPEED-B2 — no reference picture).

usage: archive_invariants.py counts <archive.zip>          prints "<assets> <objects>", exit 1 if it does not close
       archive_invariants.py same <shipped.zip> <again.zip>  exit 1 unless both close and hold the same assets

THE CLOSURE: every dependency guid an asset names is an asset of the same manifest, and every file an asset
lists (its oid) is a member of the zip under objects/. The OBJECTS are the distinct oids the manifest lists.
THE SAME ASSETS: the two manifests' (name, type) multisets are equal, and so are their object counts.
"""
import collections
import json
import sys
import zipfile


def read(path):
    z = zipfile.ZipFile(path)
    names = set(z.namelist())
    m = json.loads(z.read("jah.manifest.json"))
    assets = m["assets"]
    guids = {a["guid"] for a in assets}
    members = {n.split("/", 1)[1].split(".", 1)[0] for n in names if n.startswith("objects/") and n != "objects/"}
    bad = []
    oids = set()
    for a in assets:
        for d in a.get("dependencies") or []:
            if d not in guids:
                bad.append("%s (%s) depends on %s, not in the archive" % (a["name"], a["type"], d))
        for f in a.get("files") or []:
            oids.add(f["oid"])
            if f["oid"] not in members:
                bad.append("%s (%s): its file %s (%s) is not a member" % (a["name"], a["type"], f["name"], f["oid"]))
    return assets, oids, bad


def main(argv):
    if len(argv) == 3 and argv[1] == "counts":
        assets, oids, bad = read(argv[2])
        if bad:
            print("; ".join(bad))
            return 1
        print(len(assets), len(oids))
        return 0
    if len(argv) == 4 and argv[1] == "same":
        a1, o1, b1 = read(argv[2])
        a2, o2, b2 = read(argv[3])
        ok = True
        for tag, b in (("shipped", b1), ("re-exported", b2)):
            for x in b:
                print("FAIL: the %s archive does not close: %s" % (tag, x))
                ok = False
        k1 = collections.Counter((a["name"], a["type"]) for a in a1)
        k2 = collections.Counter((a["name"], a["type"]) for a in a2)
        if k1 != k2:
            print("FAIL: the re-exported archive's assets differ: only shipped %s, only re-exported %s"
                  % (sorted((k1 - k2).elements()), sorted((k2 - k1).elements())))
            ok = False
        if len(o1) != len(o2):
            print("FAIL: objects %d shipped, %d re-exported" % (len(o1), len(o2)))
            ok = False
        if ok:
            print("ok: the re-exported archive closes and holds the shipped archive's %d assets and %d objects"
                  % (len(a1), len(o1)))
        return 0 if ok else 1
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
