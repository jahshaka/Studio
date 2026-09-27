#!/usr/bin/env python3
"""source.testing_rules — THE TESTING RULES THAT CAN BE READ FROM THE TREE (MODULAR-GATE-1; T6;
PHOTON_ATOM_CONTRACT §7b "the rules are source.* lint suites").

A rule a lead enforces by reading diffs does not scale to a team; a rule a lint enforces does.
Five of §7b's rules are facts of the tree and the build's registration, so they are checked
here, each with its name in the failure line:

  R1 TIMING INSIDE THE LOCK. Every row that measures time — a `<suite>.timing` twin, a
     `.benchmark`, a row whose environment arms JAHSHAKA_TIMING_BARS — runs through scripts/gpu-exclusive.sh (the GPU-timing lock, TESTING_GATE §4).
     tests/CMakeLists.txt's configure check proves the list's members are registered through the
     lock; this proves the other direction: nothing that measures is outside the list.
  R2 EVERY NIGHTLY ROW IS PRICED. A `nightly` row has a cost in scripts/gate-times.txt (measured
     or estimated): the nightly tier's length is planned from it.
  R3 NO COPIED TIER. No tracked file other than scripts/gate-scope.py carries a `ctest ... -LE`
     label set: the tiers are printed by `gate-scope.py --merge-tier` / `--nightly-tier`, and a
     copy goes stale (rc-gate.sh's did: it lacked scale-target and still named benchmark).
  R4 NO RUN_SERIAL WITHOUT ITS VERDICT. A RUN_SERIAL setting in a tests CMakeLists carries its
     reason: a comment naming RUN_SERIAL within the 40 lines above it, or the row is one of the
     GPU-lock rows (the lock IS the verdict, TESTING_GATE §4).
  R5 EVERY ROW HAS A SUBJECT THAT SELECTS IT (the suites audit's S1/S2, closed for good). A
     compiled row is the build graph's (its own sources select it). An app row — a --script suite,
     a harness that spawns the app — must be reachable from a change to what it tests: its script
     calls an API module that has an api file, or its directory is named by an AREA_RULE (the app
     groups do not count: they are catch-alls, and S1's harnesses hid inside them). A lint or
     script row must run a file of the tree (touching it selects the row) or sit in a directory a
     rule names.

Run: testing_rules.py <source-dir> <build-dir>
"""
import glob
import importlib.util
import os
import re
import subprocess
import sys

FAILURES = []


def check(ok, what):
    print(("  ok: " if ok else "  FAIL: ") + what)
    if not ok:
        FAILURES.append(what)


LOCK = "gpu-exclusive.sh"
TIER_COPY = re.compile(r"ctest\b[^\n]*\s-LE\s+[\"']?\^?\(")


def measures(n, t):
    """A row that MEASURES: a `.timing` twin, a `.benchmark`, or one whose environment arms the
    timing bars (JAHSHAKA_TIMING_BARS=1, tests/support/timingbars.h). A fixture's setup row
    measures nothing; a soak (open.crash_soak) is quiet-box for its minutes, not a bar."""
    if t.get("fixture_setup"): return False
    return (n.endswith((".timing", ".benchmark")) or ".timing." in n
            or any("JAHSHAKA_TIMING_BARS=1" in e for e in t.get("env", [])))


def r1_bad(inv):
    return sorted(n for n, t in inv.items() if measures(n, t) and not any(c.endswith(LOCK) for c in t["cmd"]))


def r2_bad(inv, priced):
    return sorted(n for n, t in inv.items() if "nightly" in t["labels"] and n not in priced)


def r3_bad(name, text):
    return [f"{name}:{k}" for k, line in enumerate(text.splitlines(), 1)
            if TIER_COPY.search(line) and "gate-scope" not in line]


def r4_bad(name, text, gpu_rows):
    lines, bad = text.split("\n"), []
    for k, line in enumerate(lines):
        code = line.split("#", 1)[0]
        if not re.search(r"\bRUN_SERIAL\s+(TRUE|ON|1|YES)\b", code): continue
        above = lines[max(0, k - 40):k + 1]
        reasoned = any("#" in l and "RUN_SERIAL" in l.split("#", 1)[1] for l in above)
        row = None          # the row this setting belongs to: the nearest set_tests_properties(<name>
        for j in range(k, max(-1, k - 12), -1):
            m = re.search(r"set_tests_properties\(\s*([\w.\-]+)", lines[j])
            if m: row = m.group(1); break
        if not reasoned and row not in gpu_rows:
            bad.append("%s:%d (%s)" % (name, k + 1, row))
    return bad


def r5_bad(inv, rule_dirs, api_mods, tracked, script_modules, harness_mods):
    bad = []
    for n, t in sorted(inv.items()):
        if t["kind"] == "compiled": continue       # its own sources select it (the build graph)
        scripts = ({t["script"]} if t["script"] else set()) | set(t.get("arms", {}).values())
        mods = set(harness_mods.get(n, ()))
        for s_ in scripts: mods |= script_modules(s_)
        if t["kind"] == "app":
            ok = bool(mods & api_mods) or t["dir"] in rule_dirs
        else:
            ok = any(os.path.normpath(f) in tracked for f in t["argv_files"] | scripts) or t["dir"] in rule_dirs
        if not ok: bad.append(n)
    return bad


def the_rules_catch():
    """Each rule, fed one violation and its correction: a lint that cannot fail guards nothing."""
    print("the rules catch (synthetic inputs):")
    row = lambda cmd, **k: dict(dict(cmd=cmd, labels=set(), env=[], fixture_setup=False, kind="app",
                                     script=None, arms={}, argv_files=set(), dir="nowhere"), **k)
    check(r1_bad({"x.timing": row(["/b/x"])}) == ["x.timing"]
          and not r1_bad({"x.timing": row(["/s/scripts/gpu-exclusive.sh", "/b/x"])})
          and r1_bad({"y": row(["/b/y"], env=["JAHSHAKA_TIMING_BARS=1"])}) == ["y"]
          and not r1_bad({"y.timing.fresh_home": row(["/b/y"], fixture_setup=True)}),
          "R1 flags a measuring row outside the lock, not one inside it or a fixture's setup")
    check(r2_bad({"n": row([], labels={"nightly"})}, set()) == ["n"] and not r2_bad({"n": row([], labels={"nightly"})}, {"n"}),
          "R2 flags an unpriced nightly row")
    check(r3_bad("rc.sh", 'ctest -j4 -LE "^(nightly|photon-target)$"') == ["rc.sh:1"]
          and not r3_bad("rc.sh", 'TIER="$(python3 scripts/gate-scope.py --merge-tier)"'),
          "R3 flags a copied -LE tier, not the command that prints it")
    bare = 'add_test(NAME a.b COMMAND x)\nset_tests_properties(a.b PROPERTIES\n    RUN_SERIAL TRUE)'
    check(r4_bad("t", bare, set()) == ["t:3 (a.b)"] and not r4_bad("t", "# RUN_SERIAL: it compiles shaders\n" + bare, set())
          and not r4_bad("t", bare, {"a.b"}), "R4 flags a RUN_SERIAL with no verdict (a comment or the lock clears it)")
    check(r5_bad({"h": row(["/b/h"])}, set(), {"avatar"}, set(), lambda s: set(), {}) == ["h"]
          and not r5_bad({"h": row(["/b/h"])}, set(), {"avatar"}, set(), lambda s: set(), {"h": {"avatar"}}),
          "R5 flags an app row nothing selects (a harness's own verbs clear it)")


def main(source, build):
    spec = importlib.util.spec_from_file_location("gate_scope", os.path.join(source, "scripts", "gate-scope.py"))
    gs = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(gs)
    the_rules_catch()
    print("the tree:")
    inv = gs.load_inventory(build)
    graph = gs.gate_graph.NinjaGraph.load(build)
    if graph is not None and graph.deps_objects == 0:
        graph = None
    gs.classify_rows(inv, graph, build)

    bad = r1_bad(inv)
    check(not bad, "R1 timing inside the lock: every measuring row runs through %s %s" % (LOCK, bad[:8]))

    priced = set()
    for line in open(os.path.join(source, "scripts", "gate-times.txt")):
        f = line.split()
        if len(f) == 2 and not line.startswith("#"): priced.add(f[0])
    bad = r2_bad(inv, priced)
    check(not bad, "R2 every nightly row is priced in scripts/gate-times.txt %s" % bad[:8])

    files = subprocess.run(["git", "ls-files"], cwd=source, capture_output=True, text=True).stdout.split()
    bad = []
    for f in files:
        if f in ("scripts/gate-scope.py", "tests/hygiene/testing_rules.py") \
                or f.startswith(("irisgl/thirdparty", "thirdparty/")) \
                or not f.endswith((".sh", ".py", ".md", ".txt", ".cmake", ".yml", ".yaml", ".json")):
            continue
        try:
            bad += r3_bad(f, open(os.path.join(source, f), errors="replace").read())
        except (OSError, IsADirectoryError):
            continue
    check(not bad, "R3 no copied tier: no `ctest ... -LE (...)` outside scripts/gate-scope.py %s" % bad[:8])

    gpu_rows = {n for n, t in inv.items() if any(c.endswith(LOCK) for c in t["cmd"])}
    bad = []
    for cm in glob.glob(os.path.join(source, "tests", "**", "CMakeLists.txt"), recursive=True):
        bad += r4_bad(os.path.relpath(cm, source), open(cm, errors="replace").read(), gpu_rows)
    check(not bad, "R4 no RUN_SERIAL without its verdict comment (or the GPU lock) %s" % bad[:8])

    rule_dirs = set()
    for pat_, dirs, modules in gs.AREA_RULES:
        rule_dirs |= {d for d in dirs if not d.startswith("*")}
    tracked = {os.path.normpath(os.path.join(source, f)) for f in files}
    harness = gs.Selection(inv, graph, os.path.join(build, "bin", "Jahshaka"), gs.Revs(None), 4).mods
    bad = r5_bad(inv, rule_dirs, gs.api_modules(), tracked, gs.script_modules, harness)
    check(not bad, "R5 every app/lint row has a subject that selects it (%d without: %s)" % (len(bad), bad[:10]))

    if FAILURES:
        print("source.testing_rules: FAILED (%d)" % len(FAILURES))
        return 1
    print("source.testing_rules: PASSED")
    return 0


if __name__ == "__main__":
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(2)
    sys.exit(main(os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2])))
