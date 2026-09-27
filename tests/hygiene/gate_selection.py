#!/usr/bin/env python3
"""gate.selection — THE SELECTOR'S REPLAY (lane MODULAR-GATE-1; TESTING_V2 T3).

scripts/gate-scope.py decides what a lane runs, so a defect in it is invisible by
construction: the wrong answer is a SHORTER gate, and a shorter gate passes. The selector now
chooses by reach — the build graph, the symbols a hunk touches, the app's families — and this
suite is the guard the contract names (PHOTON_ATOM_CONTRACT §7b: "a suite the selector missed
that a full tier reds is a SELECTOR defect, fixed there and added to its replay cases").

WHAT IT REPLAYS (tests/hygiene/gate_selection_cases.json):
  * LANES: each recorded lane's diff, re-selected against THIS build's graph and inventory. Every
    suite the lane's gates found red with a REAL verdict, and every named suite of the lane, must
    be in the selection (or the selection must be the whole tier). The reds judged environmental
    are printed — selected or not — and never required: a contention red is not the change's.
    A case that says `whole_tier` must select the MERGE tier by rule (a fork pin bump); a case
    with `max_rows` must stay under it (the precision the lane is there to prove).
  * FILES: the suites audit's S1/S2 subjects, and the graph's precision (a compiled row whose
    executable does not contain the file is NOT selected).
  * IDENTIFIERS: the symbol reader on fixed texts — a comment-only hunk touches nothing; a
    member's modified default selects its struct; an added member selects only itself; a body
    line selects its function; a parameter changed on a declaration's second line selects the
    function.
And no LANE case may FALL BACK: the fallback is for a path no rule, no symbol and no graph owner
covers, and every recorded lane had an owner for every path.

The replay needs the recorded shas (Studio and irisgl) and a BUILT build dir (ninja's deps log);
a missing sha or an unbuilt dir is a failure, never a skip.

Run: gate_selection.py <source-dir> <build-dir>
"""
import copy
import importlib.util
import json
import os
import subprocess
import sys
import time

FAILURES = []


def check(ok, what):
    print(("  ok: " if ok else "  FAIL: ") + what)
    if not ok:
        FAILURES.append(what)


def load_tool(source):
    path = os.path.join(source, "scripts", "gate-scope.py")
    spec = importlib.util.spec_from_file_location("gate_scope", path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def have(repo, sha):
    return subprocess.run(["git", "cat-file", "-e", sha + "^{commit}"], cwd=repo,
                          capture_output=True).returncode == 0


def identifier_cases(gg):
    print("identifiers (the symbol reader on fixed texts):")
    a = ("struct FogDesc {\n    float density = 0.01f; // metres\n    bool enabled = false;\n};\n"
         "inline float fogOpacity(const FogDesc &d, float metres) {\n    return d.density * metres;\n}\n"
         "void setFog(const FogDesc &desc,\n            int layer);\n")
    ids, n = gg.cxx_changed_identifiers(a, a.replace("// metres", "// in metres, per the sky"))
    check(n == 0 and not ids, "a comment-only hunk touches nothing (%s)" % sorted(ids))
    ids, _ = gg.cxx_changed_identifiers(a, a.replace("0.01f", "0.02f"))
    check(ids == {"FogDesc", "density"}, "a member's modified default selects the member and its struct (%s)" % sorted(ids))
    ids, _ = gg.cxx_changed_identifiers(a, a.replace("    bool enabled = false;\n",
                                                     "    bool enabled = false;\n    float heightFalloff = 1.0f;\n"))
    check(ids == {"heightFalloff"}, "an ADDED member selects itself only (%s)" % sorted(ids))
    ids, _ = gg.cxx_changed_identifiers(a, a.replace("d.density * metres", "d.density * metres * 0.5f"))
    check(ids == {"fogOpacity"}, "a body line selects its function (%s)" % sorted(ids))
    ids, _ = gg.cxx_changed_identifiers(a, a.replace("int layer);", "int layer, bool now);"))
    check(ids == {"setFog"}, "a parameter changed on a declaration's second line selects the function (%s)" % sorted(ids))


def main(source, build):
    if not os.path.isfile(os.path.join(build, "CTestTestfile.cmake")):
        print("gate_selection: %s is not a configured build dir" % build)
        return 2
    gs = load_tool(source)
    gg = gs.gate_graph
    identifier_cases(gg)

    graph = gg.NinjaGraph.load(build)
    check(graph is not None and graph.deps_objects > 0,
          "the build dir is BUILT (ninja's deps log holds %d objects): the replay needs the graph"
          % (graph.deps_objects if graph else 0))
    if FAILURES:
        return 1
    cases = json.load(open(os.path.join(source, "tests", "hygiene", "gate_selection_cases.json")))
    inv0 = gs.load_inventory(build)
    costs = gs.load_costs()
    tier_rows = [n for n, t in inv0.items() if not (t["labels"] & (gs.NIGHTLY_LABELS | gs.TARGET_LABELS))]
    tier_s = sum(costs.get(n, 10.0) for n in tier_rows)

    # every name a case asks for must be a suite this build registers: a renamed suite (or one
    # that became a pool's arm) is renamed in the cases file in the same commit
    for c in cases["lanes"] + cases["files"]:
        for n in c.get("must", []) + c.get("env", []) + c.get("must_not", []):
            check(n in inv0, "%s: %s is a registered suite" % (c.get("lane") or c.get("case"), n))

    print("\nlanes (recorded diffs re-selected against this build):")
    print("  %-42s %9s %10s  %s" % ("lane", "rows", "est. s", "env reds selected"))
    table = []
    for c in cases["lanes"]:
        base, tip = c["range"].split("..")
        missing = [s for s in (base, tip) if not have(source, s)]
        for s in (base, tip):
            if s in missing: continue
            try:
                ig = subprocess.run(["git", "rev-parse", s + ":irisgl"], cwd=source, capture_output=True,
                                    text=True).stdout.strip()
                if ig and not have(os.path.join(source, "irisgl"), ig): missing.append("irisgl " + ig[:9])
            except OSError:
                pass
        if missing:
            check(False, "%s: the recorded shas are in this clone (missing %s — fetch d-build and irisgl's o3de)"
                  % (c["lane"], missing))
            continue
        t0 = time.time()
        paths = gs.touched_paths(c["range"])
        S = gs.select(paths, c["range"], build, 4, graph=graph, inv=copy.deepcopy(inv0), quiet_graph=True)
        whole = bool(S.fallback or S.full_tier)
        sel = set(S.selected)
        gating = [n for n in sel if not (inv0[n]["labels"] & (gs.SCOPE_EXCLUDED_LABELS | gs.TARGET_LABELS))]
        est = tier_s if whole else sum(costs.get(n, 10.0) for n in gating)
        env_hit = [e for e in c.get("env", []) if whole or e in sel]
        table.append((c["lane"], "TIER" if whole else len(gating), est))
        print("  %-42s %9s %10.0f  %d/%d   (%.1f s)" % (c["lane"][:42], "TIER" if whole else len(gating), est,
                                                     len(env_hit), len(c.get("env", [])), time.time() - t0))
        check(not S.fallback, "%s: no fallback (%s)" % (c["lane"], S.fallback[:2]))
        if c.get("whole_tier"):
            check(bool(S.full_tier), "%s: the MERGE tier by rule (%s)" % (c["lane"], S.full_tier[:1]))
        for m in c.get("must", []):
            check(whole or m in sel, "%s: selects %s" % (c["lane"], m))
        if "max_rows" in c:
            check(not whole and len(gating) <= c["max_rows"],
                  "%s: stays at <= %d rows (%s)" % (c["lane"], c["max_rows"], "TIER" if whole else len(gating)))

    print("\nfiles (the audit's S1/S2 subjects, the graph's precision):")
    for c in cases["files"]:
        S = gs.select(c["files"], None, build, 4, graph=graph, inv=copy.deepcopy(inv0), quiet_graph=True)
        sel = set(S.selected)
        check(not S.fallback, "%s: no fallback" % c["case"])
        for m in c.get("must", []):
            check(m in sel, "%s: %s selects %s" % (c["case"], c["files"][0], m))
        for m in c.get("must_not", []):
            check(m not in sel, "%s: %s does NOT select %s (%s)" % (c["case"], c["files"][0], m,
                                                                     S.selected.get(m, "")))
    print("\n  the MERGE tier: %d rows, ~%.0f suite-seconds" % (len(tier_rows), tier_s))
    if FAILURES:
        print("gate.selection: FAILED (%d)" % len(FAILURES))
        return 1
    print("gate.selection: PASSED")
    return 0


if __name__ == "__main__":
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(2)
    sys.exit(main(os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2])))
