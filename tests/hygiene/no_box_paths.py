#!/usr/bin/env python3
"""source.no_box_paths - THE STUDIO REPO KNOWS NO BOX (CODING_RULES § 7, the owner and the lead 2026-11-21): no tracked file of
the Studio tree or of irisgl names the lead repo's box-local root (`~/Developer/local`, box data: BOX-LOCAL-1) - a box input
reaches a Studio tool as an argument or a JAH_* variable the lead's tools set (the warm store: JAH_WARM_STORE, URGENT #193's
fix), never a default path into the lead repo. A naming is LITERAL (`Developer/local`) or COMPOSED (`.../Developer}/local`,
the default of a variable spelled around the brace - rig.sh's form, which a literal grep misses).
THE RATCHET: tests/hygiene/box_paths_pending.txt names today's namings with their fix; it only SHRINKS - a naming it does not
list is RED, and a listed one that is gone is RED until the list drops it (stage 3 of BOX-SILO-1, items S1 and S2, removes them).
The vendored trees are not read. The self-test runs first: a literal and a composed naming red, a near miss counts nothing.
Run: no_box_paths.py <source root>
"""
import os
import re
import subprocess
import sys

NAMING = re.compile(r"Developer\}*/local\b")
VENDORED = ("irisgl/thirdparty/", "thirdparty/")
PENDING = "tests/hygiene/box_paths_pending.txt"


def namings(root):
    """[(path, line no, text)] of every tracked file of the Studio repo and irisgl that names the box root"""
    out = []
    for repo, prefix in ((root, ""), (os.path.join(root, "irisgl"), "irisgl/")):
        r = subprocess.run(["git", "-C", repo, "ls-files", "-z"], capture_output=True)
        if r.returncode:
            continue
        for rel in r.stdout.decode(errors="replace").split("\0"):
            path = prefix + rel
            if not rel or path.startswith(VENDORED) or path == PENDING or path == "tests/hygiene/no_box_paths.py":
                continue
            out += scan(path, os.path.join(repo, rel))
    return out


def scan(path, full):
    try:
        with open(full, "rb") as f:
            data = f.read()
    except OSError:
        return []
    if b"\0" in data[:4096]:
        return []
    return [(path, i + 1, line.strip()) for i, line in enumerate(data.decode(errors="replace").splitlines())
            if NAMING.search(line)]


def pending(root):
    """{path: why} of the ratchet's list (`<path>  <why>`, # comments)"""
    out = {}
    with open(os.path.join(root, PENDING)) as f:
        for line in f:
            line = line.strip()
            if line and not line.startswith("#"):
                p, _, why = line.partition(" ")
                out[p] = why.strip()
    return out


def self_test(tmp):
    bad = []
    cases = [("x/lit.sh", 'h="$HOME/Developer/local/lock"\n', 1), ("x/comp.sh", 'f="${R:-${L:-$u/Developer}/local}/CPUS"\n', 1),
             ("x/miss.sh", 'a="Developer/locale"; b="~/Developer/localhost"; c="Developer/locals"\n', 0)]
    for rel, text, want in cases:
        full = os.path.join(tmp, rel.replace("/", "_"))
        with open(full, "w") as f:
            f.write(text)
        got = len(scan(rel, full))
        if got != want:
            bad.append("%s %r counted %d, want %d" % (rel, text.strip(), got, want))
    return bad


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else "."
    import tempfile
    with tempfile.TemporaryDirectory(prefix="no-box-paths-") as tmp:
        bad = self_test(tmp)
    if bad:
        print("FAIL: the self-test: " + "; ".join(bad))
        return 1
    print("  ok   the self-test: a literal and a composed naming of the box root count, a near miss (locale, localhost) does not")
    found = namings(root)
    listed = pending(root)
    files = sorted({p for p, _, _ in found})
    new = [x for x in found if x[0] not in listed]
    stale = sorted(p for p in listed if p not in files)
    for p in files:
        print("  %s %s%s" % ("listed" if p in listed else "NEW   ", p, ("  - " + listed[p]) if p in listed else ""))
    for p, n, t in new:
        print("  FAIL %s:%d names the lead repo's box root: %s" % (p, n, t[:140]))
    for p in stale:
        print("  FAIL %s is listed in %s but no longer names it - drop the line (the ratchet only shrinks)" % (p, PENDING))
    print("source.no_box_paths: %s (%d file(s) name it, %d listed, %d new, %d stale)" %
          ("PASSED" if not new and not stale else "FAILED", len(files), len(listed), len(new), len(stale)))
    return 1 if new or stale else 0


if __name__ == "__main__":
    sys.exit(main())
