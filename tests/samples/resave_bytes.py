#!/usr/bin/env python3
"""samples.resave_bytes - THE SHIPPED SAMPLES' SAVED BYTES (NOQT-DOCUMENT-1, stage 3's first item; the spec's open call 2).

THE LAW, over every archive this tree ships (scenes/*.zip and scenes/ogre/*.zip, 21): a sample imported, opened and saved
gives blob A; the same project opened and saved again gives blob B; and
  (1) THE FIXED POINT: A == B byte for byte - the reader and the writer are a round trip on every shipped scene;
  (2) THE RECORD: sha256(A) equals the sample's line in resave_bytes.sha256 - taken at NOQT-DOCUMENT-1 stage 2's tip
      (Studio 6609ee6b5 / irisgl 7effba96), before the document's Qt types are replaced: no stored byte moves (the
      spec's ruling 2). A ruled format change rewrites the list in its own commit, by `--write`.
THE CONTROL: every listed archive gives a blob, each over 0 bytes, and the list names exactly the archives shipped.
THE ROUTE, HEADLESS (both are `document` verbs, docs/SCRIPTING.md): project.importArchive (rows, objects, every model
baked) then project.open and project.save; no verb hands a script a scene blob, so this half reads the project's row
(the library's `projects.scene`) between the two --headless processes.

usage: resave_bytes.py <Jahshaka binary> <resave_bytes.js> <scenes dir> <hash list> [--write]
  --write  (by hand only) rewrites the hash list from this run's A blobs, after the fixed point holds.
Exit 0 green, 1 red, 2 refused."""
import hashlib
import json
import os
import re
import sqlite3
import subprocess
import sys


def say(msg):
    print(msg, flush=True)


def archives(scenes):
    out = []
    for sub in ("", "ogre"):
        d = os.path.join(scenes, sub)
        out += [os.path.join(d, f) for f in sorted(os.listdir(d)) if f.endswith(".zip")]
    return out


def run(binary, js, phase, archive_list, guids, tag):
    with open(js, encoding="utf-8") as fh:
        body = fh.read()
    head = "var PHASE = %s, ARCHIVES = %s, GUIDS = %s;\n" % (json.dumps(phase), json.dumps(archive_list), json.dumps(guids))
    script = os.path.join(os.getcwd(), tag + ".run.js")
    with open(script, "w", encoding="utf-8") as fh:
        fh.write(head + body)
    log = os.path.join(os.getcwd(), tag + ".log")
    with open(log, "w") as fh:
        rc = subprocess.call([binary, "--headless", "--script", script], stdout=fh, stderr=subprocess.STDOUT)
    with open(log, encoding="utf-8", errors="replace") as fh:
        text = fh.read()
    seen = dict(re.findall(r"^RESAVE (.+\.zip) ([0-9a-fA-F-]{8,})$", text, re.M))
    for line in re.findall(r"^.*assert failed.*$", text, re.M):
        say("  " + line)
    return rc, seen


def blobs(db, seen):
    con = sqlite3.connect("file:%s?mode=ro" % db, uri=True)
    try:
        out = {}
        for name, guid in seen.items():
            row = con.execute("SELECT scene FROM projects WHERE guid = ?", (guid,)).fetchone()
            out[name] = bytes(row[0]) if row and row[0] is not None else b""
        return out
    finally:
        con.close()


def main(argv):
    if len(argv) not in (5, 6) or (len(argv) == 6 and argv[5] != "--write"):
        say(__doc__)
        return 2
    binary, js, scenes, hashlist = argv[1:5]
    write = len(argv) == 6
    root = os.environ.get("JAHSHAKA_DATA_ROOT", "")
    home = os.environ.get("HOME", "")
    if not root or not home.endswith("/samples-home-resave") or not os.getcwd().endswith("/samples-home-resave/run"):
        say("samples.resave_bytes: REFUSED - it runs only in its own scratch home (HOME */samples-home-resave, "
            "cwd */samples-home-resave/run, JAHSHAKA_DATA_ROOT set)")
        return 2
    db = os.path.join(root, "JahLibrary.db")
    shipped = archives(scenes)
    names = [os.path.basename(p) for p in shipped]
    fails = 0

    def check(ok, msg):
        nonlocal fails
        say(("ok:   " if ok else "FAIL: ") + msg)
        if not ok:
            fails += 1

    rc, first = run(binary, js, "import", shipped, {}, "import")
    check(rc == 0 and sorted(first) == sorted(names),
          "process 1 imports, opens and saves all %d shipped archives (exit %d, %d saved)" % (len(names), rc, len(first)))
    if rc != 0 or sorted(first) != sorted(names):
        say("samples.resave_bytes: FAILED (%d)" % fails)
        return 1
    a = blobs(db, first)
    rc, second = run(binary, js, "again", [], first, "again")
    check(rc == 0 and second == first, "process 2 opens and saves each of them again (exit %d)" % rc)
    b = blobs(db, first)

    # THE CONTROL: every archive gave a blob over 0 bytes.
    sizes = [len(a[n]) for n in names]
    check(len(sizes) == len(names) and min(sizes) > 0,
          "the control: %d blobs compared, each over 0 bytes (smallest %d B, total %d B)"
          % (len(sizes), min(sizes), sum(sizes)))
    # (1) THE FIXED POINT.
    for n in names:
        if a[n] != b[n]:
            first_diff = next((i for i in range(min(len(a[n]), len(b[n]))) if a[n][i] != b[n][i]),
                              min(len(a[n]), len(b[n])))
            check(False, "%s: the second save equals the first byte for byte (%d B vs %d B, first difference at %d)"
                  % (n, len(a[n]), len(b[n]), first_diff))
    check(all(a[n] == b[n] for n in names), "THE FIXED POINT: open-save-open-save is byte-identical for all %d" % len(names))
    # (2) THE RECORD.
    got = {n: hashlib.sha256(a[n]).hexdigest() for n in names}
    if write:
        if fails:
            say("--write refused: the run is red")
            return 1
        with open(hashlist, "w", encoding="utf-8") as fh:
            fh.write("# samples.resave_bytes' RECORD (tests/samples/resave_bytes.py): sha256 of each shipped sample's scene blob after\n"
                     "# one import, open and save. Rewritten only by a ruled format change, with `--write`, in its own commit.\n")
            for n in names:
                fh.write("%s  %s\n" % (got[n], n))
        say("wrote %s (%d lines)" % (hashlist, len(names)))
    with open(hashlist, encoding="utf-8") as fh:
        listed = {}
        for line in fh:
            if line.strip() and not line.startswith("#"):
                h, n = line.rstrip("\n").split("  ", 1)
                listed[n] = h
    check(sorted(listed) == sorted(names), "the record names exactly the %d shipped archives (it names %d)"
          % (len(names), len(listed)))
    for n in names:
        if listed.get(n) != got[n]:
            check(False, "%s: its saved blob is the recorded one (sha256 %s, recorded %s)" % (n, got[n], listed.get(n)))
    check(all(listed.get(n) == got[n] for n in names), "THE RECORD: every saved blob's sha256 is the recorded one")
    say("samples.resave_bytes: PASSED" if not fails else "samples.resave_bytes: FAILED (%d)" % fails)
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
