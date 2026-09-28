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
  * HUNKS: a real file with one edit applied IN MEMORY (the Fable read's negative cases: a
    header-only helper's callers, a struct a header-only helper names, a changed add_test COMMAND
    line) — each `must` set is what a past selector missed.
  * NM: without `nm` the selector refuses (H1).
  * ARMS: a pool row is selected arm by arm where a script or an API module selects, and whole
    where a directory or a group does (a synthetic pool of two real scripts).
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
import re
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


def arm_cases(gs, graph, build, inv0):
    """THE ARM IS THE UNIT (TESTING_V2 T2/T3): a pool row (`run_pool.py --pool <p> --arm <arm>
    <script> <budget> ...`, SUITE-POOL-1) is selected ARM BY ARM where a script or a module
    selects, and whole where a directory or a group does. Proved on a synthetic pool built from
    two real --script rows of this build, so the case holds before and after the pools land."""
    print("\narms (a synthetic pool of two real scripts):")
    world = [n for n, t in inv0.items() if t["app"] and t["script"] and not t["arms"]
             and t["script"].endswith(".js") and "world." in open(t["script"], errors="replace").read()]
    plain = [n for n, t in inv0.items() if t["app"] and t["script"] and not t["arms"] and t["script"].endswith(".js")
             and "world." not in open(t["script"], errors="replace").read()
             and "project." in open(t["script"], errors="replace").read()]
    check(bool(world) and bool(plain), "the build has a world-calling and a world-free --script row")
    if not (world and plain): return
    a_s, b_s = inv0[world[0]]["script"], inv0[plain[0]]["script"]
    inv = copy.deepcopy(inv0)
    inv["pool.synth"] = dict(copy.deepcopy(inv0[world[0]]), pool="synth", arms={"w": a_s, "p": b_s},
                             script=None, argv_files=set(), dir="synthpool", reldir="tests/synthpool",
                             cmd=["run_pool.py", "--pool", "synth", "--arm", "w", a_s, "30", "--arm", "p", b_s, "30"])
    S = gs.select(["src/scripting/modules/worldapi.cpp"], None, build, 4, graph=graph, inv=inv, quiet_graph=True)
    sub = S.arm_subsets().get("pool.synth")
    check(sub == ["w"], "a changed world API selects the world-calling arm alone (%s)" % sub)
    rel = os.path.relpath(b_s, gs.ROOT)
    if not rel.startswith(".."):
        S = gs.select([rel], None, build, 4, graph=graph, inv=copy.deepcopy(inv), quiet_graph=True)
        check(S.arm_subsets().get("pool.synth") == ["p"], "a touched arm script selects that arm (%s)"
              % S.arm_subsets().get("pool.synth"))
    S = gs.select(["irisgl/engine/media/Hlms/Atom/Any/800.Atom_piece_ps.any"], None, build, 4, graph=graph,
                  inv=copy.deepcopy(inv), quiet_graph=True)
    check("pool.synth" in S.selected and "pool.synth" not in S.arm_subsets(),
          "an engine rule's app group selects the pool whole (every arm)")


def gone_cases(gs, graph, build, inv0):
    """A ROW THAT IS GONE (SUITE-POOL-1): a registration hunk from before a row became a pool arm
    resolves to that ARM; one naming a suite or test target that exists nowhere any more resolves
    to NOTHING with a `retired:` reason — and neither rule may fire on a LIVE row or target."""
    print("\ngone rows (a row that became an arm; a retired one):")
    inv = copy.deepcopy(inv0)
    app_exe = gs.classify_rows(inv, graph, build)
    sel = gs.Selection(inv, graph, app_exe, gs.Revs(None), 4)
    pools = [(n, a, s_) for n, t in inv.items() for a, s_ in t["arms"].items()]
    check(bool(pools), "the build registers pool arms")
    if not pools: return
    row, arm, scr = pools[0]
    toks = lambda text: set(re.findall(r"[A-Za-z0-9_.+\-/${}]+", text))
    # POSITIVE: a gone row whose registration ran an arm's script → that arm, by the command
    # itself and by a properties line whose registration sits in the same file
    reg = "NAME gone.suite_x COMMAND Jahshaka --script %s" % scr
    got = sel.gone_row("add_test", reg, toks(reg))
    check(got == ("arm", ["%s::%s" % (row, arm)]), "a gone row that ran an arm's script → the arm (%s)" % (got,))
    whole = "add_test(%s)\nset_tests_properties(gone.suite_x PROPERTIES TIMEOUT 9)\n" % reg
    got = sel.gone_row("set_tests_properties", "gone.suite_x PROPERTIES TIMEOUT 9", toks("gone.suite_x"), whole)
    check(got and got[0] == "arm", "...and its set_tests_properties line, through its registration (%s)" % (got,))
    # POSITIVE: a suite and a target that exist nowhere → retired, with the reason
    got = sel.gone_row("add_test", "NAME gone.suite_y COMMAND test_gone_y", toks("gone.suite_y test_gone_y"))
    check(got and got[0] == "retired" and "gone.suite_y" in got[1], "a suite that exists nowhere → retired (%s)" % (got,))
    got = sel.gone_row("add_test", "NAME app.create_loop COMMAND test_create_loop", toks("app.create_loop"))
    check(got and got[0] == "retired" and "open.responsive" in got[1],
          "app.create_loop → retired, naming what carries its check (%s)" % (got,))
    if graph is not None:
        got = sel.gone_row("add_executable", "test_gone_z test_gone_z.cpp", toks("test_gone_z"))
        check(got and got[0] == "retired", "a test target that exists nowhere → retired (%s)" % (got,))
    # NEGATIVE: a LIVE row is never an arm or retired, even when its script is also an arm's
    live = [n for n, t in inv.items() if t["script"] and not t["arms"] and any(
            os.path.basename(t["script"]) == os.path.basename(s_) for _, _, s_ in pools)]
    target = live[0] if live else next(n for n, t in inv.items() if not t["arms"])
    text = "NAME %s COMMAND Jahshaka --script %s" % (target, inv[target]["script"] or "x.js")
    check(sel.gone_row("add_test", text, toks(text)) is None,
          "a LIVE row (%s) is neither an arm nor retired, whatever script it runs" % target)
    # NEGATIVE: a live target is not retired
    if graph is not None:
        tgt = next(iter(sorted(t for t in graph.targets if t.startswith("test_"))), None)
        if tgt:
            check(sel.gone_row("set_target_properties", "%s PROPERTIES X Y" % tgt, toks(tgt)) is None,
                  "a LIVE test target (%s) is not retired" % tgt)


def hunk_cases(gs, graph, build, inv0, cases):
    """SIMULATED HUNKS (the Fable read's negative cases): a real file of the tree with one edit
    applied in memory — the selector reads the two texts and git's -U0 diff of them, the tree is
    never written. Each `must` set is what the change reaches and a past version of the selector
    missed."""
    import tempfile
    print("\nhunks (simulated edits of real files; the tree is never written):")
    for c in cases.get("hunks", []):
        path = c["file"]
        a = open(os.path.join(gs.ROOT, path), errors="replace").read()
        check(c["old"] in a, "%s: the recorded text is still in %s" % (c["case"][:40], path))
        if c["old"] not in a: continue
        b = a.replace(c["old"], c["new"], 1)

        class FakeRevs(gs.Revs):
            def __init__(self, rng):
                super().__init__(rng); self.base, self.tip = "BASE", "TIP"
            def pair(self, p): return ("BASE", "TIP")
            def texts(self, p):
                if p == path: return a, b
                try: t = open(os.path.join(gs.ROOT, p), errors="replace").read()
                except OSError: t = ""
                return t, t
            def diff_u0(self, p):
                if p != path: return ""
                with tempfile.TemporaryDirectory() as d:
                    open(os.path.join(d, "a"), "w").write(a); open(os.path.join(d, "b"), "w").write(b)
                    return subprocess.run(["git", "diff", "--no-index", "-U0", "a", "b"], cwd=d,
                                          capture_output=True, text=True).stdout
        real = gs.Revs
        gs.Revs = FakeRevs
        try:
            S = gs.select([path], "BASE..TIP", build, 4, graph=graph, inv=copy.deepcopy(inv0), quiet_graph=True)
        finally:
            gs.Revs = real
        check(not S.fallback, "%s: no fallback (%s)" % (c["case"][:60], S.fallback[:1]))
        miss = [m for m in c["must"] if m not in S.selected and not S.full_tier]
        check(not miss, "%s: selects %s (%d rows; missing %s)" % (c["case"][:60], c["must"], len(S.selected), miss))


def runlog_cases(source):
    """THE RUN LOG'S VERDICT CLASSES (TESTING-DEBTS-1 T1) on fixed texts — FORK-OOM-1's two
    lines as the engine and the app print them: a red carrying the in-frame OOM line is OOM (the
    VRAM budget's class), a red carrying the loss line is LOST (a loss after an OOM stays LOST),
    a PASS keeps PASS whatever it logged, and a pool arm takes the class from its OWN lines."""
    print("run log (the budget verdicts on fixed texts):")
    sys.path.insert(0, os.path.join(source, "scripts"))
    import gate_runlog as rl
    oom = ("[2026.09.27-19.35.31.710][    9]engine: Warning: GPU out of memory (VK_ERROR_OUT_OF_DEVICE_MEMORY; "
           "the device is NOT lost): OGRE EXCEPTION(-2:RenderingAPIException): vkAllocateMemory failed for a "
           "67108864-byte pool (memory type 1, heap 0 of 17171480576 bytes, 1107296256 already held by Ogre)")
    lost = ("[2026.09.27-19.36.25.295][   18]engine: Error: Jahshaka: THE GPU DEVICE WAS LOST. The session cannot "
            "continue; the renderer does not recreate a lost device.")
    fatal = "FATAL: the graphics device was lost; ending the session."
    check(rl.budget_verdict("x\n" + oom + "\nFAIL: y")[0] == "OOM", "an in-frame OOM line -> OOM")
    check(rl.budget_verdict(lost)[0] == "LOST", "the engine's loss line -> LOST")
    check(rl.budget_verdict(fatal)[0] == "LOST", "the app's FATAL loss line -> LOST")
    check(rl.budget_verdict(oom + "\n" + lost)[0] == "LOST", "an OOM then a loss -> LOST")
    check(rl.budget_verdict("FAIL: ...and does not claim the device was lost")[0] is None,
          "an assertion naming 'the device was lost' is no loss")
    check(rl.budget_verdict("FAIL: 7 < 47")[0] is None, "an ordinary red keeps its class")
    check(rl.row_verdict("Failed", oom, [])[:2] == ("OOM", "Failed"), "a Failed row carrying it -> OOM (status kept)")
    check(rl.row_verdict("Exception: SegFault", lost, [])[0] == "LOST", "a crashed row carrying the loss -> LOST")
    check(rl.row_verdict("Passed", oom, [])[0] == "PASS", "a PASS that logged an OOM stays PASS")
    pool = "\n".join(["ARM-BEGIN p.a", "ARM-BEGIN p.b", "MEM p.a gpuPoolUsed=300 textures=40",
                      "---- p.a: its output (2 line(s)) ----", "| ARM-BEGIN p.a", "| " + oom, "ARM p.a FAIL 900",
                      "---- p.b: its output (1 line(s)) ----", "| FAIL: 3 < 4", "ARM p.b FAIL 800",
                      "ARM-BEGIN p.c", "| " + lost])
    arms = {a: v for a, v, _ in rl._suite_facts(pool)[2]}
    check(arms == {"p.a": "OOM", "p.b": "FAIL", "p.c": "LOST"}, "a pool's arms take the class from their own lines "
          "(%s)" % arms)


def nm_refusal(source, build):
    """H1: without `nm` the link graph cannot be read — the selector must REFUSE, never answer from
    empty symbol tables (an engine .cpp would select no compiled row, silently and persistently)."""
    import shutil
    import tempfile
    print("\nnm refusal (a PATH without nm):")
    with tempfile.TemporaryDirectory() as d:
        for tool in ("git", "ctest", "ninja", "cmake", sys.executable.split("/")[-1]):
            w = shutil.which(tool)
            if w: os.symlink(w, os.path.join(d, tool))
        p = subprocess.run([sys.executable, os.path.join(source, "scripts", "gate-scope.py"), "--files",
                            "irisgl/engine/src/OgreChain.cpp", "--build", build, "--json"],
                           cwd=source, capture_output=True, text=True, env=dict(os.environ, PATH=d))
        check(p.returncode != 0 and "nm" in p.stderr, "no nm -> the selector refuses with the reason (exit %d: %s)"
              % (p.returncode, p.stderr.strip().splitlines()[-1:] if p.stderr.strip() else ""))


def joint_case(gs, build, cases):
    """THE JOINT SUITES (T4): ATOM-CLUSTER-CUT and ATOM-OCCLUSION-1 both changed the id pass
    (OgreAtomIdPass.cpp, the cull's compute pieces). Merged together, the union must hold every
    real red of both lanes, and the joint rows must carry each lane's own id-pass guards."""
    print("\njoint (ATOM-CLUSTER-CUT + ATOM-OCCLUSION-1, the id pass):")
    lanes = {c["lane"]: c for c in cases["lanes"]}
    a, b = lanes["ATOM-CLUSTER-CUT"], lanes["ATOM-OCCLUSION-1"]
    J = gs.joint(a["range"], b["range"], build, 4)
    check("irisgl/engine/src/OgreAtomIdPass.cpp" in J["shared_paths"], "both touched the id pass (%d shared paths)"
          % len(J["shared_paths"]))
    for m in a["must"] + b["must"]:
        check(J["whole_tier"] or m in J["union"], "the union holds %s" % m)
    for m in ("atom.cluster_crack", "atom.occlusion_exact", "engine.atom_draw", "engine.atom_parity"):
        check(m in J["joint"], "the joint rows carry %s (%d joint of %d)" % (m, len(J["joint"]), len(J["union"])))


def main(source, build):
    if not os.path.isfile(os.path.join(build, "CTestTestfile.cmake")):
        print("gate_selection: %s is not a configured build dir" % build)
        return 2
    gs = load_tool(source)
    gg = gs.gate_graph
    runlog_cases(source)
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

    # every name a case asks for must be a row this build registers, or a pool's arm written
    # `<pool>.<arm>`: a suite that is renamed or becomes an arm is renamed in the cases file in
    # the same commit
    arm_rows = {f"{t['pool']}.{a}": r for r, t in inv0.items() for a in t["arms"]}
    for c in cases["lanes"] + cases["files"]:
        for n in c.get("must", []) + c.get("env", []) + c.get("must_not", []):
            check(n in inv0 or n in arm_rows, "%s: %s is a registered suite or arm" % (c.get("lane") or c.get("case"), n))

    def chosen(S, n):
        """A row selected, or an arm whose pool is selected whole or with that arm."""
        if n in inv0: return n in S.selected
        r = arm_rows.get(n)
        if not r or r not in S.selected: return False
        sub = S.arm_subsets().get(r)
        return sub is None or n.split(".", 1)[1] in sub

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
        env_hit = [e for e in c.get("env", []) if whole or chosen(S, e)]
        table.append((c["lane"], "TIER" if whole else len(gating), est))
        print("  %-42s %9s %10.0f  %d/%d   (%.1f s)" % (c["lane"][:42], "TIER" if whole else len(gating), est,
                                                     len(env_hit), len(c.get("env", [])), time.time() - t0))
        check(not S.fallback, "%s: no fallback (%s)" % (c["lane"], S.fallback[:2]))
        if c.get("whole_tier"):
            check(bool(S.full_tier), "%s: the MERGE tier by rule (%s)" % (c["lane"], S.full_tier[:1]))
        for m in c.get("must", []):
            check(whole or chosen(S, m), "%s: selects %s" % (c["lane"], m))
        if "max_rows" in c:
            check(not whole and len(gating) <= c["max_rows"],
                  "%s: stays at <= %d rows (%s)" % (c["lane"], c["max_rows"], "TIER" if whole else len(gating)))

    print("\nfiles (the audit's S1/S2 subjects, the graph's precision):")
    for c in cases["files"]:
        S = gs.select(c["files"], None, build, 4, graph=graph, inv=copy.deepcopy(inv0), quiet_graph=True)
        sel = set(S.selected)
        check(not S.fallback, "%s: no fallback" % c["case"])
        for m in c.get("must", []):
            check(chosen(S, m), "%s: %s selects %s" % (c["case"], c["files"][0], m))
        for m in c.get("must_not", []):
            check(not chosen(S, m), "%s: %s does NOT select %s (%s)" % (c["case"], c["files"][0], m,
                                                                         S.selected.get(m, "")))
    arm_cases(gs, graph, build, inv0)
    gone_cases(gs, graph, build, inv0)
    hunk_cases(gs, graph, build, inv0, cases)
    nm_refusal(source, build)
    joint_case(gs, build, cases)
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
