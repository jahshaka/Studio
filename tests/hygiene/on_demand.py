#!/usr/bin/env python3
"""source.on_demand_rows + source.on_demand_toy - A ROW WHOSE BINARY IS OUT OF THE DEFAULT BUILD IS BUILT BEFORE IT RUNS, AND IS
NEVER RUN ABSENT OR STALE (ENGINE-HEADERS-1 stage 0; tests/CMakeLists.txt jah_on_demand, tests/support/on_demand.sh). CPU only.

  rows <source dir> <build dir>   EVERY ROW (ctest's own listing, --show-only=json-v1) whose command runs an executable of the
      build that a default build does not make (outside the closure of build.ninja's `all`: an EXCLUDE_FROM_ALL target - the
      sanitizer twin's `*_asan` executables) runs it through tests/support/on_demand.sh <build> <target> <that executable>, and
      carries the property JAH_BUILD_TARGETS naming that same target, a target of the build - the lead tools build it before
      the row (spec.sh test, --code, sprint.sh ship). Red with every row named: such a row would run a binary no build made
      (a skip, an exit 127) or one an older build made (a stale green).
  toy                             tests/support/on_demand.sh on a scratch build: the stamp absent, the binary absent, its own
      stamp absent, its stamp at another state of the sources - each exit 1 with `ON DEMAND: FAILED` and the command that
      builds it, the binary never run; at the stamp's own state the binary runs with its arguments and its exit is the row's.
usage: on_demand.py rows <source dir> <build dir> | on_demand.py toy <source dir>"""
import json
import os
import shutil
import subprocess
import sys
import tempfile

fails = 0


def want(ok, what):
    global fails
    print(("  ok   " if ok else "  FAIL ") + what, flush=True)
    fails += 0 if ok else 1


def rows(src, build):
    sys.path.insert(0, os.path.join(src, "scripts"))
    import gate_graph
    g = gate_graph.NinjaGraph.load(build)
    raw, rc = gate_graph.ctest_inventory(build)
    if g is None or rc != 0:
        want(False, "the build's graph and ctest's listing read (%s)" % build)
        return
    outside = {os.path.realpath(e) for e in g.exes if e in g.outside_all}
    guard = os.path.realpath(os.path.join(src, "tests", "support", "on_demand.sh"))
    targets = set(g.targets)
    bad, n = [], 0
    for t in json.loads(raw).get("tests", []):
        cmd = t.get("command") or []
        props = {p["name"]: p["value"] for p in t.get("properties", [])}
        runs = [w for w in cmd if os.path.isabs(w) and os.path.realpath(w) in outside]
        if not runs:
            continue
        n += 1
        bt = props.get("JAH_BUILD_TARGETS")
        bt = [bt] if isinstance(bt, str) else list(bt or [])
        exe = runs[0]
        i = next((k for k, w in enumerate(cmd) if os.path.realpath(w) == guard), None)
        if i is None or len(cmd) <= i + 3 or os.path.realpath(cmd[i + 1]) != os.path.realpath(build) or cmd[i + 3] != exe:
            bad.append("%s (runs %s, built on demand, not through on_demand.sh <build> <target> <it>)" % (t["name"], exe))
            continue
        if cmd[i + 2] not in bt:
            bad.append("%s (JAH_BUILD_TARGETS %s does not name its guard's target %s)" % (t["name"], bt, cmd[i + 2]))
        elif cmd[i + 2] not in targets:
            bad.append("%s (its on-demand target %s is no target of the build)" % (t["name"], cmd[i + 2]))
    want(not bad, "every row whose binary a default build does not make runs it through on_demand.sh and names its target "
                  "in JAH_BUILD_TARGETS (%d row(s) on demand)%s" % (n, "".join("\n         " + b for b in bad)))
    want(n > 0 or not outside, "the rows on demand are counted (%d executable(s) outside `all`, %d row(s))" % (len(outside), n))


def toy(src):
    sh = os.path.join(src, "tests", "support", "on_demand.sh")
    w = tempfile.mkdtemp(prefix="on-demand-toy-")
    try:
        b = os.path.join(w, "build")
        os.makedirs(b)
        exe = os.path.join(b, "test_x_asan")
        ran = os.path.join(w, "ran")
        run = lambda: subprocess.run(["sh", sh, b, "test_x_asan_on_demand", exe, "one", "two"],   # noqa: E731
                                     capture_output=True, text=True)

        def refused(p, word):
            return (p.returncode == 1 and "ON DEMAND: FAILED" in p.stdout and word in p.stdout
                    and "cmake --build %s --target test_x_asan_on_demand" % b in p.stdout and not os.path.exists(ran))

        p = run()
        want(refused(p, "BUILT_FROM is missing"), "no build stamp: FAILED by name, the binary not run (exit %d)" % p.returncode)
        open(os.path.join(b, "BUILT_FROM"), "w").write("studio=aaa\ncontent=1\n")
        p = run()
        want(refused(p, "NOT BUILT"), "the binary absent: FAILED by name - never a skip (exit %d: %s)" % (p.returncode, p.stdout.strip()))
        open(exe, "w").write('#!/bin/sh\necho "$@" > "%s"\nexit 3\n' % ran)
        os.chmod(exe, 0o755)
        p = run()
        want(refused(p, "no stamp"), "the binary without its stamp (not built by its target): FAILED (exit %d)" % p.returncode)
        open(exe + ".built_from", "w").write("studio=aaa\ncontent=0\n")
        p = run()
        want(refused(p, "STALE"), "the binary stamped at another state of the sources: FAILED as STALE (exit %d: %s)"
             % (p.returncode, p.stdout.strip()))
        shutil.copy(os.path.join(b, "BUILT_FROM"), exe + ".built_from")
        p = run()
        got = open(ran).read().strip() if os.path.exists(ran) else None
        want(p.returncode == 3 and got == "one two", "at the stamp's own state the binary runs with its arguments, its exit "
             "the row's (exit %d, argv %r)" % (p.returncode, got))
    finally:
        shutil.rmtree(w, ignore_errors=True)


if __name__ == "__main__":
    if len(sys.argv) >= 4 and sys.argv[1] == "rows":
        rows(os.path.realpath(sys.argv[2]), os.path.abspath(sys.argv[3]))
        name = "source.on_demand_rows"
    elif len(sys.argv) >= 3 and sys.argv[1] == "toy":
        toy(os.path.realpath(sys.argv[2]))
        name = "source.on_demand_toy"
    else:
        print(__doc__)
        sys.exit(2)
    print("%s: %s" % (name, "PASSED" if not fails else "FAILED (%d)" % fails))
    sys.exit(1 if fails else 0)
