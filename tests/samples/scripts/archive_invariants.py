#!/usr/bin/env python3
"""The archive's own invariants (samples.archive_fidelity.*; TESTING-SPEED-B2 — no reference picture).

usage: archive_invariants.py counts <archive.zip>          prints "<assets> <objects>", exit 1 if it does not close
       archive_invariants.py same <first.zip> <second.zip>   exit 1 unless both close and hold the same assets
       archive_invariants.py holes <frame.png>                exit 1 unless the tornado's funnel is see-through

THE CLOSURE: every dependency guid an asset names is an asset of the same manifest, and every file an asset
lists (its oid) is a member of the zip under objects/. The OBJECTS are the distinct oids the manifest lists.
THE SAME ASSETS: the two manifests' (name, type) multisets are equal, and so are their OBJECT SETS (the oids) — used for the
FIXED POINT (the archive this build exports, imported and exported again, is the same archive), never against the
shipped zip (a legitimate exporter change must not red the row).
THE HOLES: the sample's named failure is a funnel that lost its cut-out and opens SOLID. On the frame from the
sample's own camera (800x600, a pinned shader time), inside the funnel's envelope (its warm-or-white pixels in the
window x 230..580; 4 % below its top to 22 % above its bottom), the fraction of pixels that are NOT funnel — the
holes the cut-out punches — must be above zero. No reference picture: the bar is the property, not an old still.
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


XW0, XW1 = 230, 580


def torn(p):
    r, g, b = p
    return (r - b > 30) or min(p) > 200


def see_through(path):
    from PIL import Image
    im = Image.open(path).convert("RGB").resize((800, 600))
    W, H = im.size
    px = im.load()
    mask = [[XW0 <= x < XW1 and torn(px[x, y]) for x in range(W)] for y in range(H)]
    rows = [y for y in range(H) if sum(mask[y]) > 8]
    if not rows:
        return 0.0, 0
    top, bot = rows[0], rows[-1]
    span = bot - top
    env = gap = 0
    for y in range(top + int(0.04 * span), bot - int(0.22 * span)):
        xs = [x for x in range(W) if mask[y][x]]
        if len(xs) < 5:
            continue
        env += xs[-1] - xs[0] + 1
        gap += sum(1 for x in range(xs[0], xs[-1] + 1) if not mask[y][x])
    return (gap / env if env else 0.0), env


def main(argv):
    if len(argv) == 3 and argv[1] == "holes":
        frac, env = see_through(argv[2])
        if env == 0:
            print("FAIL: no funnel in the frame %s" % argv[2])
            return 1
        ok = frac > 0.0
        print("%s: the funnel is see-through: %.4f of its %d envelope pixels are holes (bar > 0)"
              % ("ok" if ok else "FAIL", frac, env))
        return 0 if ok else 1
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
        for tag, b in (("first", b1), ("second", b2)):
            for x in b:
                print("FAIL: the %s archive does not close: %s" % (tag, x))
                ok = False
        k1 = collections.Counter((a["name"], a["type"]) for a in a1)
        k2 = collections.Counter((a["name"], a["type"]) for a in a2)
        if k1 != k2:
            print("FAIL: the archive is no fixed point: only in the first export %s, only in the second %s"
                  % (sorted((k1 - k2).elements()), sorted((k2 - k1).elements())))
            ok = False
        if o1 != o2:
            print("FAIL: the objects differ: %d only in the first export, %d only in the second (%s / %s)"
                  % (len(o1 - o2), len(o2 - o1), sorted(o1 - o2)[:3], sorted(o2 - o1)[:3]))
            ok = False
        if ok:
            print("ok: a fixed point: exported, imported and exported again, the archive closes with the same %d assets and the same %d objects (oid for oid)"
                  % (len(a1), len(o1)))
        return 0 if ok else 1
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
