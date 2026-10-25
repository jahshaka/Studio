#!/usr/bin/env python3
"""gate.reach_cache (URGENT FIX LIST #57, the builder's half) - A CACHED REACH IS THE FRESH ONE: ci_gate_check.py serve's
Reach.of(a) - gate-scope's selection of the range a..tip, per ancestor a that holds a record - is cached on disk
(<build>/ci-reach/<key>.json), keyed by a, the tip, the build's stamp, its ctest inventory's files and the selector's
scripts. THE LAWS, on two ancestors of the tree's HEAD: (1) the answer a second Reach reads from the disk (a hit, nothing
computed) equals the answer computed with the disk off; (2) the key moves with the ancestor and with the tip; (3) a
corrupt cache file is recomputed, never read. Needs the built tree (the graph) and git. No display.
usage: gate_reach_cache.py <source dir> <build dir>"""
import os
import subprocess
import sys

src, build = sys.argv[1], sys.argv[2]
sys.path.insert(0, os.path.join(src, "scripts"))
import ci_gate_check as cgc  # noqa: E402

gs = cgc.load_gs()
build = gs.resolve_build(build)


def git(*a):
    r = subprocess.run(["git"] + list(a), cwd=src, capture_output=True, text=True)
    return r.stdout.strip() if r.returncode == 0 else ""


tip = git("rev-parse", "HEAD")
ancestors = [x for x in (git("rev-parse", "HEAD~1"), git("rev-parse", "HEAD~4")) if x]
fails = []


def law(ok, text):
    print(("ok   " if ok else "FAIL ") + text)
    if not ok:
        fails.append(text)


if not tip or len(ancestors) < 2:
    print("FAIL: the tree's HEAD and two ancestors are needed")
    sys.exit(1)
fresh = cgc.Reach(gs, build, tip, disk=False)
for a in ancestors:
    want = fresh.of(a)
    first = cgc.Reach(gs, build, tip)
    first.of(a)                                   # (a hit already, or computed and written)
    second = cgc.Reach(gs, build, tip)
    got = second.of(a)
    law(second.hits == 1 and second.computed == 0 and got == want,
        "(1) %s..%s: the disk's answer (%s rows) equals the fresh one (%s rows), read with nothing computed"
        % (a[:9], tip[:9], "every" if got is None else len(got), "every" if want is None else len(want)))
k0 = cgc.reach_key(build, ancestors[0], tip)
law(k0 != cgc.reach_key(build, ancestors[1], tip) and k0 != cgc.reach_key(build, ancestors[0], ancestors[1]),
    "(2) the key moves with the ancestor and with the tip")
path = os.path.join(build, "ci-reach", k0 + ".json")
with open(path, "w") as fh:
    fh.write("{ not json")
third = cgc.Reach(gs, build, tip)
got = third.of(ancestors[0])
law(third.hits == 0 and third.computed == 1 and got == fresh.of(ancestors[0]),
    "(3) a corrupt cache file is recomputed (and rewritten), never read")
print("gate.reach_cache: %s" % ("FAIL" if fails else "PASS"))
sys.exit(1 if fails else 0)
