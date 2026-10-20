#!/usr/bin/env python3
"""gate.selection — THE SELECTOR'S REPLAY (lane MODULAR-GATE-1; TESTING_V2 T3).

scripts/gate-scope.py decides what a spec's test and merge run, so a defect in it is invisible by construction: the
wrong answer is a SHORTER gate, and a shorter gate passes. A reach a later run proves wrong is fixed in the selector and
added here. A seconds-long guard (TESTING-SIMPLE: six recorded diffs, within a CPU row's 10 s):

  * LANES: recorded diffs re-selected against THIS build's graph and inventory (tests/hygiene/gate_selection_cases.json):
    every named row must be in the selection; `whole_tier`: a fork diff this checkout cannot read reaches every row;
    `fork_reach`: a pin bump is selected by the fork diff's reach; `must_families`, `must_picture`, `none_of`,
    `no_whole_pool`: BLOOM-1's precision (the post chain reaches the picture set and no gi row); no case may fall back
    (`rows_at_tip_only`: a range's new registrations fall to their test directory here, never the whole inventory).
  * HUNKS: a real file with one edit applied IN MEMORY — each `must` set is what a past selector missed.
  * ARMS: a pool row is selected arm by arm where a script or an API module selects (a synthetic pool of two scripts).
  * GONE ROWS, TOOLING-ONLY DIFFS, IDENTIFIERS: the gone-row reader, a diff with no product path, the symbol reader.
The replay needs the recorded shas (Studio and irisgl) and a BUILT build dir (ninja's deps log); a missing sha or an
unbuilt dir is a failure, never a skip.

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


_INV_BLOB = {}


def fresh_inv(inv0):
    """A private copy of the inventory for one selection (select() classifies its rows in place): unpickled from one
    pickle of it — copy.deepcopy of the 830-row inventory cost ~0.08 s a selection, 2.5 s of this row's price."""
    import pickle
    k = id(inv0)
    if k not in _INV_BLOB:
        _INV_BLOB[k] = pickle.dumps(inv0, protocol=pickle.HIGHEST_PROTOCOL)
    return pickle.loads(_INV_BLOB[k])


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
    inv = fresh_inv(inv0)
    inv["pool.synth"] = dict(copy.deepcopy(inv0[world[0]]), pool="synth", arms={"w": a_s, "p": b_s},
                             script=None, argv_files=set(), dir="synthpool", reldir="tests/synthpool",
                             cmd=["run_pool.py", "--pool", "synth", "--arm", "w", a_s, "30", "--arm", "p", b_s, "30"])
    S = gs.select(["src/scripting/modules/worldapi.cpp"], None, build, graph=graph, inv=inv, quiet_graph=True)
    sub = S.arm_subsets().get("pool.synth")
    check(sub == ["w"], "a changed world API selects the world-calling arm alone (%s)" % sub)
    rel = os.path.relpath(b_s, gs.ROOT)
    if not rel.startswith(".."):
        S = gs.select([rel], None, build, graph=graph, inv=copy.deepcopy(inv), quiet_graph=True)
        check(S.arm_subsets().get("pool.synth") == ["p"], "a touched arm script selects that arm (%s)"
              % S.arm_subsets().get("pool.synth"))


def gone_cases(gs, graph, build, inv0):
    """A ROW THAT IS GONE (SUITE-POOL-1): a registration hunk from before a row became a pool arm
    resolves to that ARM; one naming a suite or test target that exists nowhere any more resolves
    to NOTHING with a `retired:` reason — and neither rule may fire on a LIVE row or target."""
    print("\ngone rows (a row that became an arm; a retired one):")
    inv = fresh_inv(inv0)
    app_exe = gs.classify_rows(inv, graph, build)
    sel = gs.Selection(inv, graph, app_exe, gs.Revs(None))
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
            S = gs.select([path], "BASE..TIP", build, graph=graph, inv=fresh_inv(inv0), quiet_graph=True)
        finally:
            gs.Revs = real
        check(not S.fallback, "%s: no fallback (%s)" % (c["case"][:60], S.fallback[:1]))
        # a hunk is read by what it names: never the whole inventory unless the case says so (TOOLING-2: a `must` list
        # passed by a whole-inventory reach proved nothing)
        if c.get("require_tier"):
            # a change the reader CANNOT resolve stays the whole tier with its reason (TOOLING-2's reader rounds: a fallback
            # to "its directory" reached nothing for irisgl/CMakeLists.txt; PARENT_SCOPE inside a function read as nothing)
            check(bool(S.full_tier), "%s: the WHOLE TIER, its reason printed (%s)" % (c["case"][:60], S.full_tier[:1]))
        else:
            check(not S.full_tier or c.get("whole_tier"), "%s: no whole-inventory reach (%s)" % (c["case"][:60], S.full_tier[:1]))
        miss = [m for m in c["must"] if m not in S.selected and not S.full_tier]
        check(not miss, "%s: selects %s (%d rows; missing %s)" % (c["case"][:60], c["must"], len(S.selected), miss))
        if c.get("why_has"):
            # what the reach was READ through, when the rows cannot tell (a reach the engine target already spans)
            why = " ".join(r for _, r in S.rationale)
            check(all(w in why for w in c["why_has"]), "%s: read through %s" % (c["case"][:60], c["why_has"]))
        extra = [m for m in c.get("must_not", []) if m in S.selected]
        check(not extra, "%s: does NOT select %s (%s)" % (c["case"][:60], c.get("must_not", []), extra))


def tooling_cases(gs, graph, build, inv0):
    """TESTING_V3 §1.3.4 (TESTING-CLEANUP-2B item 1): A TOOLING-ONLY DIFF SELECTS NO PRODUCT ROW. A diff with no
    product path (gate-scope.py NON_PRODUCT is the list of the paths that are NOT; everything else is) selects the hygiene + tooling rows, the runner's own self-test and the rows whose declaration
    or command changed — never an app or compiled product row, never the smoke pair; ONE product path makes it a
    product diff and the selection is the whole union again.
    RED ON BASE (b0a3b1f3c): no such rule — a tooling script alone selected 9 product rows, a test source alone added
    the product smoke pair."""
    print("\ntooling-only diffs (TESTING_V3 §1.3.4):")
    def sel(files):
        return gs.select(files, None, build, graph=graph, inv=fresh_inv(inv0), quiet_graph=True)
    def product(S):
        return sorted(n for n in S.selected if S.inv[n]["kind"] in ("app", "compiled")
                      and not (S.inv[n]["labels"] & {"hygiene", "tooling"}))
    hyg = {n for n, t in inv0.items() if "hygiene" in t["labels"]}
    S = sel(["scripts/gate-scope.py"])
    check(S.tooling_only and not product(S) and hyg <= set(S.selected) and not S.fallback and not S.full_tier,
          "scripts/gate-scope.py alone is TOOLING-ONLY: the hygiene label (%d rows), no product row (%s)"
          % (len(hyg), product(S)[:6]))
    lints = {n for n in inv0 if n.startswith("source.") and not (inv0[n]["labels"] & gs.NEVER_SELECTED)}
    S = sel(["tests/gi/test_gi_cliff.cpp"])
    check(S.tooling_only and set(S.selected) - lints == {"gi.cliff"},
          "a test source alone selects its row and the lints, nothing else — no smoke pair, no other tooling row (%s)"
          % sorted(set(S.selected) - lints)[:6])
    # THE LINTS RIDE EVERY DIFF (TOOLING-2): R2 and the row budget among them, for a one-line test change too
    check(lints and lints <= set(S.selected) and {"source.testing_rules", "source.row_budget"} <= set(S.selected),
          "a one-line test change selects every source.* lint (%d), source.testing_rules and source.row_budget among them"
          % len(lints))
    S = sel(["tests/meshbake/CMakeLists.txt", "scripts/gate_runlog.py"])
    mb = {n for n, t in inv0.items() if t["dir"] == "meshbake"}
    check(S.tooling_only and mb <= set(S.selected) and "app.startup_quiet" not in S.selected,
          "a test registration + a runner script: the declared rows (%d meshbake) + the hygiene label, no smoke pair"
          % len(mb))
    alone = set(sel(["src/services/vrworld.cpp"]).selected)
    S = sel(["scripts/gate-scope.py", "src/services/vrworld.cpp"])
    check(not S.tooling_only and alone <= set(S.selected) and "app.startup_quiet" not in alone
          and "ui.vr_panel" in S.selected and "source.gate_scope_rules" in S.selected
          and not any("hygiene" in S.inv[n]["labels"] for n in alone if not n.startswith("source.")),
          "ONE product path makes it a product diff: the union (vrworld.cpp's %d rows + the tool's hygiene/tooling rows); "
          "the product path alone selects no tools' row and no smoke pair (TESTING-SPEED-A item 5)" % len(alone))
    S = sel(["tests/support/fresh_home.sh"])
    check("app.startup_quiet" in S.selected and "api.contract" in S.selected,
          "a change to the test harness (tests/support/) selects the smoke pair (%s)" % sorted(S.selected)[:4])
    check(gs.product_paths(["irisgl/docs/OGRE_BUILD.md", "docs/TESTING.md", "scripts/x.py", "tests/hygiene/y.py",
                            "irisgl/README.md"]) == []
          and len(gs.product_paths(["irisgl/engine/src/OgreGi.cpp", "app/content/x.obj", "CMakeLists.txt",
                                    "cmake/IncludeOgre.cmake", "scenes/Tornado.zip", "thirdparty/qlementine/x.h",
                                    "irisgl/thirdparty/ogre-next", ".gitmodules", "newtopdir/tool.cpp"])) == 9,
          "the rule fails CLOSED: the sources, content, vendored code, build files AND a new top-level directory "
          "nobody classified are product; docs, scripts and tests are not")


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

    # every name a case asks for must be a row this build registers, or a pool's arm written
    # `<pool>.<arm>`: a suite that is renamed or becomes an arm is renamed in the cases file in
    # the same commit
    arm_rows = {f"{t['pool']}.{a}": r for r, t in inv0.items() for a in t["arms"]}
    for c in cases["lanes"]:
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
    print("  %-42s %9s  %s" % ("lane", "rows", "env reds selected"))
    table = []
    classified = None
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
        S = gs.select(paths, c["range"], build, graph=graph, inv=fresh_inv(inv0), quiet_graph=True)
        # a replay's own new registrations fall to their test directory here (rows_at_tip_only): that is no tier
        whole = bool(S.full_tier or (S.fallback and not c.get("rows_at_tip_only")))
        sel = set(S.selected)
        # A FIXTURE SETUP is not a suite (a home wipe, ~10 ms; ctest pulls it in with the row that
        # requires it), so it is not a gating row either — PROJECT-NAMES-1 gave 50 project-creating
        # rows a fresh-home fixture, and counting those would move every max_rows by its wipes.
        gating = [n for n in sel if not inv0[n].get("fixture_setup")]
        env_hit = [e for e in c.get("env", []) if whole or chosen(S, e)]
        table.append((c["lane"], "TIER" if whole else len(gating)))
        print("  %-42s %9s  %d/%d   (%.1f s)" % (c["lane"][:42], "TIER" if whole else len(gating),
                                                     len(env_hit), len(c.get("env", [])), time.time() - t0))
        if c.get("rows_at_tip_only"):
            # a REPLAY against this build: the range's NEW rows are registered at its tip, not here — their registration
            # commands resolve to nothing in this build and fall to their test directory (never the tier), said
            check(all(f.startswith("tests/") for f in S.fallback),
                  "%s: only the range's new test registrations are unresolved here (%s)" % (c["lane"], S.fallback[:2]))
        else:
            check(not S.fallback, "%s: no fallback (%s)" % (c["lane"], S.fallback[:2]))
        if c.get("whole_tier"):
            # THE CASE'S PREMISE IS THIS CHECKOUT'S (a fork diff it CANNOT read): a clone that holds the rebased-away commit
            # reads it, and the selector's rule for a readable pin move is its component reach — asked as such, said
            fb = S.fork_bump or {}
            fork = os.path.join(source, "irisgl", "thirdparty", "ogre-next")
            readable = bool(fb.get("old") and fb.get("new")) and all(
                subprocess.run(["git", "-C", fork, "cat-file", "-e", x + "^{commit}"], capture_output=True).returncode == 0
                for x in (fb["old"], fb["new"]))
            if readable and not S.full_tier and fb.get("files") is not None:
                check(True, "%s: this checkout's fork clone READS the diff (%s..%s): selected by its component reach (the "
                      "case's tier rule is for a clone that cannot)" % (c["lane"], fb["old"][:9], fb["new"][:9]))
            else:
                check(bool(S.full_tier), "%s: the MERGE tier by rule (%s)" % (c["lane"], S.full_tier[:1]))
        if c.get("fork_reach"):
            # T2: a pin bump selects by the fork diff's reach, never the tier (that runs once, at the merge)
            check(bool(S.fork_bump) and not S.full_tier and (S.fork_bump or {}).get("files") is not None,
                  "%s: the fork pin selects by its diff's reach, not the tier (%s)"
                  % (c["lane"], S.full_tier[:1] or len((S.fork_bump or {}).get("files") or [])))
        for m in c.get("must", []):
            # A HYGIENE ROW IS THE LANE RUN'S (TESTING-SPEED-A item 5): a product change no longer selects the tools'
            # rows — the builder's self-audit runs the whole hygiene label at the lane's tip (records), so a lint red the
            # lane's gate once found is caught there, said
            if not (whole or chosen(S, m)) and m in inv0 and "hygiene" in inv0[m]["labels"]:
                print("  (%s: %s is a hygiene row — the self-audit's hygiene label runs it at the lane's tip)" % (c["lane"], m))
                continue
            check(whole or chosen(S, m), "%s: selects %s" % (c["lane"], m))
        # THE TESTING-SPEED-A REPLAYS: the families, the picture set, the rows that must NOT ride, no pool whole
        fam = lambda n: {inv0[n]["dir"], n.split(".", 1)[0]}
        for f_ in c.get("must_families", []):
            want = sorted(n for n in inv0 if f_ in fam(n) and not (inv0[n]["labels"] & (gs.NEVER_SELECTED
                          | {"hygiene", "tooling"})) and not inv0[n].get("fixture_setup"))
            miss = [n for n in want if n not in sel]
            check(bool(want) and not miss, "%s: selects every kept %s row (%d; missing %s)" % (c["lane"], f_, len(want),
                                                                                             miss[:5]))
        if c.get("must_picture"):
            pic = sorted(n for n, t in inv0.items() if t.get("picture"))
            arms = sorted("%s.%s" % (t["pool"], a) for n, t in inv0.items() for a in t.get("picture_arms") or ())
            miss = [n for n in pic if n not in sel] + [a for a in arms if not chosen(S, a)]
            check(bool(pic) and not miss, "%s: selects THE PICTURE SET (%d rows, %d screenshot arms; missing %s)"
                  % (c["lane"], len(pic), len(arms), miss[:5]))
        for f_ in c.get("none_of", []):
            bad = sorted(n for n in sel if f_ in fam(n))
            check(not bad, "%s: selects NO %s row (%s)" % (c["lane"], f_, bad[:6]))
        if c.get("no_whole_pool"):
            wp = sorted(n for n in sel if inv0[n].get("pool") and n not in S.arm_subsets())
            check(not wp, "%s: no pool selected WHOLE — only its screenshot arms (%s)" % (c["lane"], wp[:4]))
        if "max_rows" in c:
            check(not whole and len(gating) <= c["max_rows"],
                  "%s: stays at <= %d rows (%s)" % (c["lane"], c["max_rows"], "TIER" if whole else len(gating)))
        if "derived_max_rows" in c:
            # THE BAR DERIVED FROM THE GRAPH (TOOLING-6 item 2): a reach that IS an edge of the graph (every executable
            # that links <links>, and so the physics world) grows with every row registered on that edge — a literal
            # max_rows moved by hand four times in one night, a merge red each time (§2640). The bar is the recorded
            # selection moved by what the graph and the inventory say changed since: the rows that link the target and
            # the source.* lints (which ride every diff); the record itself is asserted ONCE here, at its own numbers.
            dm = c["derived_max_rows"]
            at = dm["at"]
            lib = graph.target_reach(dm["links"]) or set()
            if classified is None:      # a row's executables are the selector's own reading of it (classify_rows)
                classified = fresh_inv(inv0)
                gs.classify_rows(classified, graph, build)
            linking = sorted(n for n in classified if not inv0[n].get("fixture_setup") and classified[n]["exes"] & lib)
            lints = [n for n in inv0 if n.startswith("source.") and not (inv0[n]["labels"] & gs.NEVER_SELECTED)]
            bar = at["rows"] + (len(linking) - at["linking"]) + (len(lints) - at["lints"])
            check(bool(lib) and bool(linking), "%s: the graph names the executables that link %s (%d) and their rows (%d)"
                  % (c["lane"], dm["links"], len(lib), len(linking)))
            check(not whole and len(gating) <= bar,
                  "%s: stays at <= %d rows, the bar derived (recorded %d at %d linking rows + %d lints; now %d + %d) (%s)"
                  % (c["lane"], bar, at["rows"], at["linking"], at["lints"], len(linking), len(lints),
                     "TIER" if whole else len(gating)))
            # THE EDGE ITSELF (a selector that drops it selects FEWER rows, and a bar alone passes that): every compiled
            # row on it is selected — the app rows ride the physics TUs' area rule by their modules, not the graph
            miss = [n for n in linking if classified[n]["kind"] != "app" and n not in sel]
            check(not miss, "%s: selects every compiled row that links %s (%d; missing %s)"
                  % (c["lane"], dm["links"], sum(1 for n in linking if classified[n]["kind"] != "app"), miss[:5]))

    tooling_cases(gs, graph, build, inv0)
    arm_cases(gs, graph, build, inv0)
    gone_cases(gs, graph, build, inv0)
    hunk_cases(gs, graph, build, inv0, cases)
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
