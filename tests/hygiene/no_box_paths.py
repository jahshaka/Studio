#!/usr/bin/env python3
"""source.no_box_paths - THE STUDIO REPO KNOWS NO BOX (CODING_RULES § 7, the owner and the lead 2026-11-21): no tracked file of
the Studio tree or of irisgl names the lead repo's box-local root (`~/Developer/local`, box data: BOX-LOCAL-1) - a box input
reaches a Studio tool as an argument or a JAH_* variable the lead's tools set (the warm store: JAH_WARM_STORE, URGENT #193's
fix), never a default path into the lead repo. A naming is LITERAL (`Developer/local`) or COMPOSED (`.../Developer}/local`,
the default of a variable spelled around the brace - rig.sh's form, which a literal grep misses).
THE RATCHET: tests/hygiene/box_paths_pending.txt names today's namings with their fix; it only SHRINKS - a naming it does not
list is RED, and a listed one that is gone is RED until the list drops it (stage 3 of BOX-SILO-1, items S1 and S2, removes them).
The vendored trees are not read. The self-test runs first: a literal and a composed naming red, a near miss counts nothing.
Run: no_box_paths.py <source root> [<build dir>]   (the build dir: the store census below)
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


# THE SECOND LAW (URGENT #193's own red, 2026-11-21): a row whose OWN script invokes scripts/warm-app-cache.sh declares its store
# (jah_no_warm_store: JAH_WARM_STORE unset in its ENVIRONMENT_MODIFICATION, or its own value in ENVIRONMENT) - a toy that
# inherited the box's store from a lead tool published toy caches at the real key. fresh_home.sh and row-env.sh call it with
# --key only, which exits before any store (their lines are checked to say so).
KEY_ONLY = ("tests/support/fresh_home.sh", "scripts/row-env.sh")
CALL = re.compile(r"warm-app-cache\.sh")


RUNS = re.compile(r"subprocess|Popen|check_output|[\"']bash[\"']")


def calls_warm(text, python=False):
    """a line that RUNS the script: in a shell file any code line naming it; in a python file only a line that also spawns a
    process (testing_rules.py READS the script's text - that is no call)"""
    return any(CALL.search(l) and (not python or RUNS.search(l)) for l in text.splitlines() if not l.lstrip().startswith("#"))


def store_rows(root, build):
    """(undeclared rows, key-only breaches): rows whose own script (an argument under the tree) invokes warm-app-cache.sh with no
    JAH_WARM_STORE in their environment; KEY_ONLY files whose invocation is not --key alone"""
    import json
    r = subprocess.run(["ctest", "--test-dir", build, "--show-only=json-v1"], capture_output=True, text=True)
    tests = json.loads(r.stdout or "{}").get("tests", []) if r.returncode == 0 else []
    bad, rootp = [], os.path.realpath(root)
    for t in tests:
        props = {p["name"]: p["value"] for p in t.get("properties", [])}
        env = " ".join(props.get("ENVIRONMENT", []) + props.get("ENVIRONMENT_MODIFICATION", []))
        for a in t.get("command") or []:
            ap = os.path.realpath(a) if a.startswith("/") else ""
            if not ap.startswith(rootp + "/") or not os.path.isfile(ap):
                continue
            rel = os.path.relpath(ap, rootp)
            if rel in KEY_ONLY or rel == "tests/hygiene/no_box_paths.py" or not rel.endswith((".sh", ".py")):
                continue
            if calls_warm(open(ap, errors="replace").read(), rel.endswith(".py")) and "JAH_WARM_STORE" not in env:
                bad.append("%s (%s)" % (t["name"], rel))
    breach = []
    for rel in KEY_ONLY:
        try:
            lines = [l for l in open(os.path.join(root, rel), errors="replace") if CALL.search(l) and not l.lstrip().startswith("#")]
        except OSError:
            continue
        text = "".join(open(os.path.join(root, rel), errors="replace"))
        if lines and "--key" not in text:
            breach.append(rel)
    return bad, breach


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
    srows, breach = store_rows(root, sys.argv[2]) if len(sys.argv) > 2 else ([], [])
    if len(sys.argv) > 2:
        if (not calls_warm('x=$(bash "$S/scripts/warm-app-cache.sh" "$B" --check)\n') or calls_warm("# warm-app-cache.sh in a comment\n")
                or not calls_warm('r = subprocess.run(["bash", "scripts/warm-app-cache.sh", b])\n', True)
                or calls_warm('w = open(os.path.join(s, "scripts", "warm-app-cache.sh")).read()\n', True)):
            print("FAIL: the store census's own self-test (a call counts; a comment, or a python read of its text, does not)")
            return 1
        print("  ok   the store census: every row whose own script runs warm-app-cache.sh declares its store (%d undeclared)" % len(srows))
    for r in srows:
        print("  FAIL %s drives scripts/warm-app-cache.sh with no JAH_WARM_STORE of its own (jah_no_warm_store)" % r)
    for r in breach:
        print("  FAIL %s calls warm-app-cache.sh beyond --key (the store census exempts it as --key only)" % r)
    ok = not new and not stale and not srows and not breach
    print("source.no_box_paths: %s (%d file(s) name it, %d listed, %d new, %d stale; %d store row(s) undeclared)" %
          ("PASSED" if ok else "FAILED", len(files), len(listed), len(new), len(stale), len(srows)))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
