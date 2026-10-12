#!/usr/bin/env python3
"""source.no_renders_tracked — A RUN LEAVES NOTHING, AND NOTHING A RUN LEFT IS TRACKED (TESTING-SPEED-A item 7;
SPECS/TESTING_SPEED_SPEC.md §2 A8). Every row's home and shots are deleted on a green exit (tests/support/fresh_home.sh's
wipe after) and kept once as evidence on a red one (build-linux/tests/.evidence/) — never in git. `git ls-files` must
hold no render (*.png *.jpg *.xwd *.ppm *.exr), no `*home*/` directory and no `Testing/` directory, except the FIXTURE
INPUTS a row reads (FIXTURE_INPUTS: the spec's list), under the paths a run writes to (tests/, scripts/, testing/ — the
product's own images under app/ are content). Green on the base (nothing tracked there); fed a violation first.

Run: no_renders_tracked.py <source-dir> [<build-dir>]
"""
import fnmatch
import os
import re
import subprocess
import sys

RENDER = re.compile(r"\.(png|jpg|jpeg|xwd|ppm|exr)$", re.I)
# THE FIXTURE INPUTS (the spec's list): images a row READS, never one it wrote — tests/samples/data/tornado_reference.jpg
# until Part B decides it (an old-picture reference: forward-only)
FIXTURE_INPUTS = [
    "tests/mcp/fixtures/truncated.png",
    "tests/scripting/fixtures/cutout_checker.png",
    "tests/scripting/fixtures/implane_alpha.png",
    "tests/scripting/fixtures/implane_tall.png",
    "tests/scripting/fixtures/implane_wide.png",
    "tests/scripting/fixtures/tiny.png",
    "tests/shadow/fixtures/*",
    "tests/samples/data/tornado_reference.jpg",
]
FAILURES = []


def check(ok, what):
    print(("  ok: " if ok else "  FAIL: ") + what)
    if not ok:
        FAILURES.append(what)


def bad_paths(files):
    out = []
    for f in files:
        if any(fnmatch.fnmatch(f, pat) for pat in FIXTURE_INPUTS):
            continue
        parts = f.split("/")
        if RENDER.search(f) or any("home" in p.lower() and i < len(parts) - 1 for i, p in enumerate(parts)) \
                or "Testing" in parts[:-1]:
            out.append(f)
    return out


def main(source):
    check(bad_paths(["tests/gi/out/shot.png", "tests/x/e2e-home-a/run/lib.db", "tests/Testing/Temporary/LastTest.log",
                     "tests/mcp/fixtures/truncated.png", "tests/shadow/fixtures/caster.png", "src/home/thing.cpp",
                     "docs/HOMEWORK.md"]) == ["tests/gi/out/shot.png", "tests/x/e2e-home-a/run/lib.db",
                                              "tests/Testing/Temporary/LastTest.log", "src/home/thing.cpp"],
          "a render, a home's file and a Testing/ file are flagged; the fixture inputs and a file NAMED home are not")
    files = subprocess.run(["git", "ls-files"], cwd=source, capture_output=True, text=True).stdout.split()
    # the tests' tree and the tools': where a run writes (the product's own images — app/ icons — are content)
    files = [f for f in files if f.startswith(("tests/", "scripts/", "testing/"))]
    bad = bad_paths(files)
    check(not bad, "no tracked render, home or Testing/ outside the fixture inputs (%d: %s)" % (len(bad), bad[:8]))
    if FAILURES:
        print("source.no_renders_tracked: FAILED (%d)" % len(FAILURES))
        return 1
    print("source.no_renders_tracked: PASSED")
    return 0


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(2)
    sys.exit(main(os.path.abspath(sys.argv[1])))
