#!/usr/bin/env python3
"""source.on_demand_rows + source.on_demand_toy - A ROW RUNS ONLY BINARIES THE CALL BUILT: an on-demand binary is built before it
runs and never runs absent or stale, and every CODE row says what it needs built (ENGINE-HEADERS-1 stage 0; tests/CMakeLists.txt
jah_on_demand, tests/support/on_demand.sh, scripts/row_targets.py: build by reach, CODING_RULES § 4). CPU only.

  rows <source dir> <build dir>   (1) EVERY ROW (ctest's own listing, --show-only=json-v1) whose command runs an executable of
      the build that a default build does not make (outside the closure of build.ninja's `all`: an EXCLUDE_FROM_ALL target - the
      sanitizer twin's `*_asan` executables) runs it through tests/support/on_demand.sh <build> <target> <that executable>, and
      carries the property JAH_BUILD_TARGETS naming that same target, a target of the build - the lead tools build it before
      the row (spec.sh test, --code, sprint.sh ship). Red with every row named: such a row would run a binary no build made
      (a skip, an exit 127) or one an older build made (a stale green).
      (2) EVERY CODE ROW WHOSE COMMAND IS A SCRIPT (its first word no artefact of the build: python3, sh, bash, cmake) DECLARES
      JAH_BUILD_TARGETS - the targets its script runs beyond its command line's artefacts, `none`, or `all` (it reads every
      binary) - each a target of the build: `spec.sh test --code` builds only the targets of the rows it runs, so a script
      that runs an unlisted binary would run it STALE. (An undeclared row costs the call the whole build: row_targets.py.)
  toy                             tests/support/on_demand.sh on a scratch build: the stamp absent, the binary absent, the stamp
      claiming other targets, `all` alone (a default build never builds an on-demand binary), a name it merely contains - each
      exit 1 with `ON DEMAND: FAILED` and the command that builds it, the binary never run; a stamp naming its target runs the
      binary with its arguments, its exit the row's.
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
    import row_targets
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
        bt = row_targets._names(props.get("JAH_BUILD_TARGETS"))
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
    nj = row_targets.Ninja.load(build)
    scripts, und, unk, mixed, alls = 0, [], [], [], []
    for t in json.loads(raw).get("tests", []):
        props = {p["name"]: p["value"] for p in t.get("properties", [])}
        labels = props.get("LABELS") or []
        if "code" not in ([labels] if isinstance(labels, str) else labels):
            continue
        cmd = t.get("command") or []
        if cmd and nj.target_of.get(os.path.realpath(cmd[0])) is None:
            scripts += 1
        _, u, k = row_targets.own_targets(t, nj)
        bt = row_targets._names(props.get("JAH_BUILD_TARGETS"))
        if u:
            und.append(t["name"])
        unk += ["%s (%s)" % (t["name"], x) for x in k]
        if "none" in bt and len(bt) > 1:
            mixed.append("%s (%s)" % (t["name"], ";".join(bt)))
        if "all" in bt:
            alls.append(t["name"])
    want(not und, "every code row whose command is a script declares JAH_BUILD_TARGETS (%d script row(s); %d undeclared)%s"
         % (scripts, len(und), "".join("\n         " + r for r in und)))
    want(not unk and not mixed, "every declared name is a target of the build, `all` or `none` alone%s"
         % "".join("\n         " + r for r in unk + mixed))
    want(scripts > 0, "the code rows whose command is a script are counted (%d; %d declare `all`: %s)"
         % (scripts, len(alls), ", ".join(alls) or "none"))


def toy(src):
    sh = os.path.join(src, "tests", "support", "on_demand.sh")
    w = tempfile.mkdtemp(prefix="on-demand-toy-")
    try:
        b = os.path.join(w, "build")
        os.makedirs(b)
        exe = os.path.join(b, "test_x_asan")
        ran = os.path.join(w, "ran")
        run = lambda: subprocess.run(["sh", sh, b, "test_x_asan", exe, "one", "two"],   # noqa: E731
                                     capture_output=True, text=True)

        def stamp(targets, stale="0", stale_fork="0"):
            open(os.path.join(b, "BUILT_FROM"), "w").write("studio=aaa\ncontent=1\nstale=%s\nstale_fork=%s\ntargets=%s\n"
                                                         % (stale, stale_fork, targets))

        def refused(p, word):
            return (p.returncode == 1 and "ON DEMAND: FAILED" in p.stdout and word in p.stdout
                    and "cmake --build %s --target test_x_asan" % b in p.stdout and not os.path.exists(ran))

        p = run()
        want(refused(p, "BUILT_FROM is missing"), "no build stamp: FAILED by name, the binary not run (exit %d)" % p.returncode)
        stamp("test_x_asan")
        p = run()
        want(refused(p, "NOT BUILT"), "the binary absent: FAILED by name - never a skip (exit %d: %s)" % (p.returncode, p.stdout.strip()))
        open(exe, "w").write('#!/bin/sh\necho "$@" > "%s"\nexit 3\n' % ran)
        os.chmod(exe, 0o755)
        for tg, why in (("all", "`all` alone (a default build never builds it)"), ("A,B", "other targets"),
                        ("test_x_asan_extra,x_test_x_asan", "names that merely contain its own"), ("", "an empty set")):
            stamp(tg)
            p = run()
            want(refused(p, "STALE"), "the stamp claims %s: FAILED as STALE (exit %d: %s)" % (why, p.returncode, p.stdout.strip()))
        for st, sf, why in (("1", "0", "stale=1 (a file ninja could not see)"), ("0", "1", "stale_fork=1 (a fork file)")):
            stamp("A,all,test_x_asan", st, sf)
            p = run()
            want(refused(p, "last pass was stale"), "the stamp claims its target on a STALE pass, %s: FAILED as STALE (exit %d: %s)"
                 % (why, p.returncode, p.stdout.strip()))
        stamp("A,all,test_x_asan")
        p = run()
        got = open(ran).read().strip() if os.path.exists(ran) else None
        want(p.returncode == 3 and got == "one two", "a stamp naming its target: the binary runs with its arguments, its exit "
             "the row's (exit %d, argv %r)" % (p.returncode, got))
    finally:
        shutil.rmtree(w, ignore_errors=True)
    spawner(src)


def spawner(src):
    """THE FINAL READ's L1: a test executable compiled with JAHSHAKA_BINARY=<the app's path> launches the app, so its row needs
    the app's target too (scripts/row_targets.py) - a toy build.ninja: test_x's object carries the define, test_y's does not."""
    sys.path.insert(0, os.path.join(src, "scripts"))
    import row_targets
    w = tempfile.mkdtemp(prefix="on-demand-spawn-")
    try:
        b = os.path.join(w, "build")
        os.makedirs(b)
        app = os.path.join(b, "bin", "App")
        open(os.path.join(b, "build.ninja"), "w").write(
            "rule CXX_COMPILER__t\n  command = c\nrule CXX_EXECUTABLE_LINKER__t\n  command = l\nrule phony\n"
            "build x.o: CXX_COMPILER__test_x_Debug /s/x.cpp\n  DEFINES = -DJAHSHAKA_BINARY=\\\"%s\\\" -DOTHER=1\n"
            "build y.o: CXX_COMPILER__test_y_Debug /s/y.cpp\n  DEFINES = -DOTHER=1\n"
            "build a.o: CXX_COMPILER__App_Debug /s/a.cpp\n  DEFINES = -DOTHER=1\n"
            "build tests/test_x: CXX_EXECUTABLE_LINKER__test_x_Debug x.o\n"
            "build tests/test_y: CXX_EXECUTABLE_LINKER__test_y_Debug y.o\n"
            "build bin/App: CXX_EXECUTABLE_LINKER__App_Debug a.o\n"
            "build test_x: phony tests/test_x\nbuild test_y: phony tests/test_y\nbuild App: phony bin/App\n"
            "build all: phony App test_x test_y\n" % app)
        nj = row_targets.Ninja(b)
        nj._parse(os.path.join(b, "build.ninja"))
        own = lambda exe: row_targets.own_targets({"command": [os.path.join(b, "tests", exe)], "properties": []}, nj)[0]  # noqa: E731
        x, y = own("test_x"), own("test_y")
        want(x == {"test_x", "App"} and y == {"test_y"}, "an executable compiled with JAHSHAKA_BINARY=<the app> needs the app's "
             "target with its own; one without the define needs its own alone (%s %s)" % (sorted(x), sorted(y)))
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
