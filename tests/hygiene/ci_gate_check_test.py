#!/usr/bin/env python3
"""gate.ci_check — THE MERGE REFUSAL, proved (MODULAR-GATE-1 T6; the flake law since TEST-SELECTOR-1).

scripts/ci-gate-check.sh <range> refuses a merge unless the run log at the range's tip answers every
row the range selects under THE FLAKE LAW (scripts/ci_gate_check.py): a contention-class red needs
3/3 solo PASS after it, any other red a recorded verdict (`--verdict "<row>=<text>"`, per row and
timestamped in the log, clearing only the reds before it),
and a row never run is refused. Proved on a recorded range (VIEWS-DEPTH-1, 51e9f2c49..3756b2f18:
photon.view then; since TESTING-SPEED-A a synthetic range on 51e9f2c49 reaching gi.speckle) against a PRIVATE run log (JAH_RUN_LOG_DIR) and a PRIVATE defect
registry (JAH_DEFECTS_FILE):

  nothing run -> refused; all green -> accepted; a verdict-less red -> refused, and ONE solo PASS
  does not clear it; the same red with a recorded verdict -> accepted (the verdict is in the log);
  a contention-class red with 2/3 solo -> refused, 3/3 -> accepted, a solo red among them -> refused;
  a green run from a DIRTY tree, at another tip, with a DIRTY irisgl or an irisgl that is not the
  tip's pin -> refused (F1); no defect registry -> unusable (exit 2); a range that MOVES THE FORK
  PIN needs the whole MERGE tier at its tip (§7b rule 4), whatever its scoped selection;
  and the run log samples the box at EACH SUITE'S START (L1), not once per gate.

Run: ci_gate_check_test.py <source-dir> <build-dir>
"""
import json
import os
import subprocess
import sys
import tempfile

RANGE = None          # 51e9f2c49..<synth(gi.speckle's source)> — set in main (TESTING-SPEED-A: the old range's row,
                      # photon.view, is DELETED by TESTING-SPEED-B batch 1; gi.speckle stays)
FORK_RANGE = "1775e79f0..684e0f7aa"     # contact-occlusion-1: a pin bump inside the lane
# TWO RED ROWS FOR THE REPEATED --verdict CASES (TESTING-CLEANUP-2B item 1): RANGE is a tests-only diff, so under
# TESTING_V3 §1.3.4 it selects gi.speckle alone (the product smoke pair no longer rides a tooling-only diff).
# This one-commit range (TESTING-CLEANUP-2 read 3 F1: two test scripts) selects exactly two rows, both of which run
# the file their own command names.
# TESTING-SPEED-A: the old pair's app.sky_swap_presented is DELETED by TESTING-SPEED-B batch 2 — the range is now a
# synthetic commit on b04d14f8d changing the scripts of two rows that stay, each running the file its command names
RANGE2 = None
ROWS2 = ["log.events", "log.perf"]
RANGE2_FILES = ["tests/log/e2e_log_events.js", "tests/log/e2e_log_perf.js"]
# THE DATA EDGE (RUNNER-A2B): both ranges touch a tests/**/CMakeLists.txt, which gate.tokens_by_needs READS (it lints every
# row's declarations) — so each scoped selection holds it beside the named rows. The fixtures record it green at both tips
# (a row the case is not about); the cases' reds stay on the rows they name.
DATA_EDGE_ROWS = []      # the synthetic ranges touch test sources/scripts only: no data edge, no tool path, no harness
FAILURES = []


def synth(source, parent, files, scratch, msg):
    """A commit object on `parent` whose tree carries each of `files` with today's text + one code line (an unreferenced
    object: no ref, no worktree moves) — what the cases' ranges are since the rows they named were deleted."""
    env = dict(os.environ, GIT_AUTHOR_NAME="jahshaka", GIT_AUTHOR_EMAIL="jahshaka@gmail.com",
               GIT_COMMITTER_NAME="jahshaka", GIT_COMMITTER_EMAIL="jahshaka@gmail.com",
               GIT_INDEX_FILE=os.path.join(scratch, "synth-%d.index" % len(os.listdir(scratch))))
    g = lambda *a, **k: subprocess.run(["git"] + list(a), cwd=source, capture_output=True, text=True, env=env, **k)
    g("read-tree", parent + "^{tree}")
    for f in files:
        text = open(os.path.join(source, f), errors="replace").read()
        text += ("\nvar tsaSpeedProbe = 1;\n" if f.endswith(".js") else "\nstatic int tsa_speed_probe = 1;\n")
        blob = g("hash-object", "-w", "--stdin", input=text).stdout.strip()
        g("update-index", "--add", "--cacheinfo", "100644,%s,%s" % (blob, f))
    tree = g("write-tree").stdout.strip()
    return g("commit-tree", tree, "-p", g("rev-parse", parent).stdout.strip(), "-m", "TESTING-SPEED-A " + msg).stdout.strip()


def check(ok, what):
    print(("  ok: " if ok else "  FAIL: ") + what)
    if not ok: FAILURES.append(what)


def main(source, build):
    tool = os.path.join(source, "scripts", "ci-gate-check.sh")
    # the rows RANGE selects (a tests-only diff, TESTING_V3 §1.3.4: gi.speckle alone — the smoke pair it used to
    # carry no longer rides, so records for it were noise)
    global RANGE, RANGE2
    rows = ["gi.speckle"]
    scratch = tempfile.mkdtemp(prefix="ci-check-")
    base = subprocess.run(["git", "rev-parse", "51e9f2c49"], cwd=source, capture_output=True, text=True).stdout.strip()
    base_pin = subprocess.run(["git", "rev-parse", "51e9f2c49:irisgl"], cwd=source, capture_output=True,
                              text=True).stdout.strip()
    tip = synth(source, "51e9f2c49", ["tests/gi/test_gi_speckle.cpp"], scratch, "the ci-check range (gi.speckle)")
    check(bool(tip), "the synthetic tip on 51e9f2c49 is made")
    if not tip: return 1
    RANGE = "51e9f2c49.." + tip
    RANGE2 = "b04d14f8d.." + synth(source, "b04d14f8d", RANGE2_FILES, scratch, "the two-row range (log.events + log.perf)")
    pin = subprocess.run(["git", "rev-parse", tip + ":irisgl"], cwd=source, capture_output=True, text=True).stdout.strip()
    sys.path.insert(0, os.path.join(source, "scripts"))
    import gate_runlog
    # THE DEFECT REGISTRY (TESTING_V3 §1.5): the fixtures' defects; `listed` adds gi.speckle's nondeterminism
    # entry (the contention class is the registry's open nondeterminism subset)
    def entry(i, rows, kind="defect"):
        return {"id": i, "rows": rows, "kind": kind, "cause": "the fixture's (this test only)", "state": "open",
                **({"enrolled": {"by": "lead", "rate": "fixture", "census": {"other_ctests": 2}, "date": "2026-10-09"}}
                   if kind == "nondeterminism" else {}),
                "first_seen": {"tip": "0" * 9, "pin": "0" * 9, "run": "20261009T120000-000000000"},
                "recheck": "2099-12-31", "found_by": "lane"}
    fixtures = [entry("FIXTURE-1", rows + ROWS2), entry("FIXTURE-2", rows + ROWS2),
                dict(entry("NOTREPRO-FIXTURE-1", [rows[0]], "nondeterminism"), uses=1, suspects=["ci-check"],
                     census={"other_ctests": 1}, first_seen={"tip": tip[:9], "pin": "0" * 9,
                                                             "run": "20261009T120000-" + tip[:9]})]
    fixtures[-1].pop("enrolled", None)
    listed = os.path.join(scratch, "defects-listed.json")
    unlisted = os.path.join(scratch, "defects.json")
    json.dump({"defects": fixtures + [entry("NONDET-FIXTURE-1", [rows[0]], "nondeterminism")]}, open(listed, "w"))
    json.dump({"defects": fixtures}, open(unlisted, "w"))

    def run(rng=None, *extra):
        # THE DOOR'S RULES LIVE AT THE PUSH (round 14: a merge arbitrates nothing — its own checks below, `merge_case`):
        # these checks ask the door in push mode unless they name a mode
        rng = rng or RANGE
        extra = list(extra) + ([] if "--mode" in extra else ["--mode", "push"])
        p = subprocess.run([tool, rng, "--build", build] + list(extra), capture_output=True, text=True,
                           env=dict(os.environ))
        return p.returncode, p.stdout

    def put(suites, verdict, ts, t=tip, dirty=False, retry=False, ig=pin, ig_dirty=False, box=None):
        gate_runlog.append_records([dict({"suite": s, "arm": None, "verdict": verdict, "ts": ts, "retry": retry,
                                          "tip": {"studio": t, "studio_dirty": dirty, "irisgl": ig,
                                                  "irisgl_dirty": ig_dirty,
                                                  "built": {"studio": t, "irisgl": ig, "dirty": False}}},
                                         **({"box": box} if box else {}))
                                    for s in suites], "scoped", t)
    busy = {"census": {"gpu_apps": [], "other_ctests": 2, "builds": 0, "psi10_mem": 0.0, "psi10_io": 0.0}}

    def fresh(contention, data_edge=True):
        d = tempfile.mkdtemp(dir=scratch)
        os.environ["JAH_RUN_LOG_DIR"] = d
        os.environ["JAH_DEFECTS_FILE"] = contention
        if data_edge:
            put(DATA_EDGE_ROWS, "PASS", "2026-01-01T09:30:00")
            put(DATA_EDGE_ROWS, "PASS", "2026-01-01T09:30:00", t=tip2, ig=pin2)
        return d

    tip2 = subprocess.run(["git", "rev-parse", RANGE2.split("..")[1]], cwd=source, capture_output=True,
                          text=True).stdout.strip()
    pin2 = subprocess.run(["git", "rev-parse", tip2 + ":irisgl"], cwd=source, capture_output=True, text=True).stdout.strip()
    # ---- the law, a row NOT in the contention class ---------------------------------------------
    fresh(unlisted, data_edge=False)
    rc, out = run()
    check(rc == 1 and "no record" in out and rows[0] in out,
          "nothing run -> refused, and says which rows (%d)" % rc)
    fresh(unlisted)
    put(rows, "FAIL", "2026-01-01T10:00:01")
    rc, out = run()
    check(rc == 1 and rows[0] in out and "needs a verdict" in out,
          "a verdict-less red -> refused, naming it and what it needs (%d)" % rc)
    put(rows, "PASS", "2026-01-01T10:05:00", retry=True)
    rc, out = run()
    check(rc == 1 and "needs a verdict" in out,
          "...and ONE solo PASS does not erase it (the audit's L2: latest-wins is gone) (%d)" % rc)
    # ---- A MERGE ARBITRATES NOTHING (round 14): this run's records; a red stands until the lead's named verdict -----
    # (no registry entry names the rows here: a row an open entry names is KNOWN at a merge, served, never run)
    empty = os.path.join(scratch, "defects-none.json")
    json.dump({"defects": []}, open(empty, "w"))
    fresh(empty, data_edge=False)
    put(rows, "FAIL", "2026-01-01T09:00:00")                       # an EARLIER fire's red at the same tip
    put(rows, "PASS", "2026-01-01T10:00:00")                       # this run: green
    rc, out = run(RANGE, "--mode", "merge", "--since", "2026-01-01T09:30:00")
    check(rc == 0, "round 14: a MERGE reads THIS run's records — an earlier fire's red at the same tip, this run green -> "
          "accepted (%d)" % rc)
    put(rows, "FAIL", "2026-01-01T10:30:00")                       # this run: red
    rc, out = run(RANGE, "--mode", "merge", "--since", "2026-01-01T10:15:00")
    check(rc == 1 and "the lead decides" in out, "round 14: a red of this run without the lead's verdict -> refused (%d)"
          % rc)
    rc, out = run(RANGE, "--mode", "merge", "--since", "2026-01-01T10:15:00", "--verdict",
                  "gi.speckle=the lead read the log: the fixture's red, read by the test")
    check(rc == 0, "round 14: ...with the lead's named verdict (--door-verdict -> --verdict) -> accepted, whatever its "
          "text (%d)" % rc)
    vfiles = [f for f in os.listdir(os.environ["JAH_RUN_LOG_DIR"]) if "-verdict-" in f]
    vrec = [json.loads(l) for f in vfiles for l in open(os.path.join(os.environ["JAH_RUN_LOG_DIR"], f))]
    check(len(vrec) == 1 and vrec[0]["suite"] == rows[0] and vrec[0]["kind"] == "verdict"
          and "read by the test" in vrec[0]["text"], "...and the verdict is a record in the run log (%s)" % vfiles)
    rc, out = run(RANGE, "--mode", "merge", "--since", "2026-01-01T10:15:00")
    check(rc == 0, "...which a later check reads without the flag (%d)" % rc)
    put(rows, "FAIL", "2099-01-01T10:00:00")
    rc, out = run(RANGE, "--mode", "merge")
    check(rc == 1 and "the lead decides" in out,
          "a red logged AFTER the verdict is not cleared by it (per row, timestamped) (%d)" % rc)
    rc, out = run(RANGE, "--mode", "push")
    check(rc == 1, "round 14: ...and the PUSH of a merge-accepted red (a verdict in prose) refuses: its proof rules stay "
          "(%d)" % rc)
    rc, out = run(RANGE, "--verdict", "api.contract=not red, so nothing to answer")
    check(rc == 1 and "no verdict recorded for api.contract" in out,
          "a verdict for a row that is not red is refused, said out loud (%d)" % rc)

    # ---- a REPEATED --verdict records every verdict (the form the runner builds) -------
    base2 = subprocess.run(["git", "rev-parse", RANGE2.split("..")[0]], cwd=source, capture_output=True,
                           text=True).stdout.strip()
    pin2, base2_pin = (subprocess.run(["git", "rev-parse", r + ":irisgl"], cwd=source, capture_output=True,
                                      text=True).stdout.strip() for r in (tip2, base2))
    fresh(empty)
    put(ROWS2, "FAIL", "2026-01-01T10:00:01", t=tip2, ig=pin2)
    rc, out = run(RANGE2, "--mode", "merge", "--verdict", "%s=the lead: first verdict" % ROWS2[0], "--verdict",
                  "%s=the lead: second verdict" % ROWS2[1])
    vrec = [json.loads(l) for f in os.listdir(os.environ["JAH_RUN_LOG_DIR"]) if "-verdict-" in f
            for l in open(os.path.join(os.environ["JAH_RUN_LOG_DIR"], f))]
    check(rc == 0 and sorted(r["suite"] for r in vrec) == sorted(ROWS2),
          "two repeated --verdict flags record BOTH verdicts and the lane is accepted (%d, %s)"
          % (rc, sorted(r["suite"] for r in vrec)))
    fresh(empty)
    put(ROWS2, "FAIL", "2026-01-01T10:00:01", t=tip2, ig=pin2)
    rc, out = run(RANGE2, "--mode", "merge", "--verdict", "%s=the lead: first verdict" % ROWS2[0],
                  "%s=the lead: second verdict" % ROWS2[1])
    check(rc == 0, "...and so does one --verdict with two pairs (%d)" % rc)

    # ---- a row that never ran (NOADMIT) is MISSING, which no verdict clears ------------------------
    fresh(unlisted)
    put(rows, "NOADMIT", "2026-01-01T10:00:01")
    rc, out = run(RANGE, "--verdict", "gi.speckle=it waited too long")
    check(rc == 1 and "no record" in out and "no verdict recorded for gi.speckle" in out,
          "a NOADMIT row is missing and a verdict cannot clear it (%d)" % rc)

    # ---- the law, a CONTENTION-CLASS row ----------------------------------------------------------
    fresh(listed)
    put(rows, "FAIL", "2026-01-01T10:00:01")
    for k in range(3): put(rows, "PASS", "2026-01-01T10:0%d:00" % (5 + k), retry=True)
    rc, out = run()
    check(rc == 1 and "competitor census" in out, "a contention-class red whose census shows no competition is not "
          "cleared by 3/3 solo (TESTING_V3 §1.4) (%d)" % rc)
    fresh(listed)
    put(rows, "FAIL", "2026-01-01T10:00:01", box=busy)
    put(rows, "PASS", "2026-01-01T10:05:00", retry=True)
    put(rows, "PASS", "2026-01-01T10:06:00", retry=True)
    rc, out = run()
    check(rc == 1 and "2/3" in out, "a contention-class red with 2/3 solo PASS -> refused (%d)" % rc)
    put(rows, "PASS", "2026-01-01T10:07:00", retry=True)
    rc, out = run()
    check(rc == 0 and "3/3 solo PASS" in out, "...3/3 -> accepted, and says why (%d)" % rc)
    fresh(listed)
    put(rows, "FAIL", "2026-01-01T10:00:01", box=busy)
    for k, v in enumerate(("PASS", "FAIL", "PASS")):
        put(rows, v, "2026-01-01T10:0%d:00" % (5 + k), retry=True)
    rc, out = run()
    check(rc == 1 and "SOLO retry went red" in out, "a contention-class red whose solo retries read 2/3 -> refused "
          "(not contention: a verdict) (%d)" % rc)

    # ---- the tree the records came from -----------------------------------------------------------
    for name, kw in (("a DIRTY tree", {"dirty": True}), ("another tip", {"t": "0" * 40}),
                     ("a DIRTY irisgl (uncommitted engine edits)", {"ig_dirty": True}),
                     ("an irisgl that is not the tip's pin", {"ig": "1" * 40})):
        fresh(unlisted)
        put(rows, "PASS", "2026-01-01T11:00:00", **kw)
        rc, out = run()
        check(rc == 1, "a green run from %s -> refused (%d)" % (name, rc))
    fresh(unlisted)
    put(rows, "PASS", "2026-01-01T12:00:00")
    rc, out = run()
    check(rc == 0, "every selected row green at the tip -> accepted (%d) %s" % (rc, out.strip().splitlines()[-2:]))

    # ---- no defect registry -----------------------------------------------------------------------
    fresh(os.path.join(scratch, "absent.json"))
    put(rows, "PASS", "2026-01-01T12:00:00")
    rc, out = run()
    check(rc == 2 and "defect registry" in out, "no defect registry -> unusable, exit 2, saying so (%d)" % rc)

    # ---- a fork pin bump needs the whole tier at the tip (§7b rule 4) -------------------------------
    ftip = subprocess.run(["git", "rev-parse", FORK_RANGE.split("..")[1]], cwd=source, capture_output=True,
                          text=True).stdout.strip()
    if ftip:
        fresh(unlisted)
        rc, out = run(FORK_RANGE)
        # TESTING-SPEED-A item 4: a pin move asks for the FORK DIFF'S COMPONENT reach (never the whole tier)
        check(rc == 1 and "the pin move: the fork diff's components" in out and "MERGE tier" not in out,
              "a range that moves the fork pin asks for its component reach's records, not the tier's (%d: %s)"
              % (rc, out.strip().splitlines()[0][:160] if out.strip() else ""))
    else:
        check(False, "the recorded fork range %s is in this clone" % FORK_RANGE)

    # ---- L1: the box is sampled at each suite's START ----------------------------------------------
    d = fresh(unlisted, data_edge=False)
    fake = os.path.join(scratch, "fake_ctest.sh")
    open(fake, "w").write("#!/bin/sh\n"
                          "echo '      Start  1: s.one'\n"
                          "echo '1/2 Test  #1: s.one ..........   Passed    0.10 sec'\n"
                          "echo '      Start  2: s.two'\n"
                          "echo '2/2 Test  #2: s.two ..........   Passed    0.10 sec'\n")
    os.chmod(fake, 0o755)
    calls = []
    real_oc, real_gc = gate_runlog.other_ctests, gate_runlog.gpu_clocks
    gate_runlog.other_ctests = lambda *own: calls.append(1) or len(calls)
    gate_runlog.gpu_clocks = lambda: {"state": "sampled-%d" % len(calls)}
    try:
        gate_runlog.run_ctest(fake, scratch, "scoped", "ci-check-test", 1, echo=False)
    finally:
        gate_runlog.other_ctests, gate_runlog.gpu_clocks = real_oc, real_gc
    recs = {}
    for f in os.listdir(d):
        for l in open(os.path.join(d, f)):
            r = json.loads(l); recs[r["suite"]] = r["box"]
    check(recs.get("s.one", {}).get("other_ctests") == 2 and recs.get("s.two", {}).get("other_ctests") == 3,
          "each suite's record carries the box sampled at ITS start (%s)"
          % {k: v.get("other_ctests") for k, v in recs.items()})

    # ---- FIX ROUND item 4: a batch carries its pools' arms (JAH_POOL_ARMS) --------------------------------------------
    sys.path.insert(0, os.path.join(source, "scripts"))
    import ci_gate_check as cgc_mod

    class _GS:
        GATE_JOBS = {"fixture": 8, "app": 4, "measuring": 1}

        @staticmethod
        def load_costs():
            return {}
    inv_ = {"pool.toy": {"labels": set(), "kind": "app", "pool": "toy", "arms": {"a1": 1, "a2": 1}},
            "x.fixture": {"labels": set(), "kind": "fixture"}}
    ps_ = cgc_mod.passes(_GS, inv_, ["pool.toy", "x.fixture"], [], {"pool.toy": ["a2"]})
    pa_ = {b["class"]: b["pool_arms"] for p_ in ps_ for b in p_["batches"]}
    check(pa_.get("app") == "toy.a2" and pa_.get("fixture") == "", "passes(): the app batch carries JAH_POOL_ARMS=toy.a2 "
          "(the one arm the change reaches), the fixture batch none (%s)" % pa_)

    # ---- FIX ROUND: THE RENDERS OUTSIDE THE HOMES, wiped after the run (gate_runlog._wipe_renders) ---------------------
    import time as _t
    wb = os.path.join(scratch, "wipe-build"); wt = os.path.join(wb, "tests", "gi")
    os.makedirs(os.path.join(wt, "e2e-home-x")); os.makedirs(os.path.join(wb, "tests", ".evidence"))
    old = os.path.join(wt, "reference.png"); open(old, "w").close(); os.utime(old, (1, 1))
    t0_ = _t.time() - 1
    for f in ("shot.png", "frame.ppm", "e2e-home-x/in_home.png"):
        open(os.path.join(wt, f), "w").close()
    msg = gate_runlog._wipe_renders(wb, t0_, "run-green", False)
    check(not os.path.exists(os.path.join(wt, "shot.png")) and not os.path.exists(os.path.join(wt, "frame.ppm"))
          and os.path.exists(old) and os.path.exists(os.path.join(wt, "e2e-home-x", "in_home.png")) and "deleted" in msg,
          "renders: an all-green run DELETES the images it wrote under build/tests/<dir>/ (not an older input, not a home's: "
          "fresh_home owns those) (%s)" % msg)
    open(os.path.join(wt, "shot.png"), "w").close()
    msg = gate_runlog._wipe_renders(wb, t0_, "run-red", True)
    check(os.path.isfile(os.path.join(wb, "tests", ".evidence", "output", "run-red", "renders", "gi", "shot.png"))
          and not os.path.exists(os.path.join(wt, "shot.png")) and os.path.exists(old),
          "renders: a run with a red MOVES them to .evidence/output/<run>/renders/<dir>/ (%s)" % msg)

    # ---- ROUND 15: a record of a row that names no data — THE EMPTY SET, never "unknown" ---------------------------
    import importlib.util as ilu
    sys.path.insert(0, os.path.join(source, "scripts"))
    spec = ilu.spec_from_file_location("cgc_r15", os.path.join(source, "scripts", "ci_gate_check.py"))
    cgc = ilu.module_from_spec(spec); spec.loader.exec_module(cgc)
    import gate_graph as gg_
    srv = cgc.Server.__new__(cgc.Server)
    srv.fork, srv.tip_fork = set(), None
    decl = {"hash": "d" * 64}
    srv.facts = {"x.row": {"declaration": decl, "data_hash": gg_.EMPTY_DATA_HASH, "data_why": None}}
    old = {"tier": "merge", "declaration": decl, "data_hash": None,
           "nulls": {"data_hash": "UNDECLARED: it names no data and its CMake declares none (DATA NONE ...)"}}
    check(srv.record_why_not(("x.row", None), old, None) is None,
          "round 15: an older record of a row naming no data (data_hash null, UNDECLARED) is the EMPTY set's — "
          "it serves while the row still names none")
    srv.facts["x.row"]["data_hash"] = "e" * 64
    check(srv.record_why_not(("x.row", None), old, None) == "its DATA changed since the record",
          "round 15: ...and the row naming data since -> its DATA changed (run)")
    cgc.UNCOMPARABLE.clear()
    srv.facts["x.row"].update(data_hash=None, data_why="tests/x is absent")
    why = srv.record_why_not(("x.row", None), dict(old, data_hash="f" * 64, nulls={}), None)
    check(why == "a data hash is unknown (never equal)" and cgc.UNCOMPARABLE.get("x.row") == "tests/x is absent",
          "round 15: a record that truly cannot be compared is counted ONCE in the plan's `uncomparable`, with its why")

    subprocess.run(["rm", "-rf", scratch])
    if FAILURES:
        print("gate.ci_check: FAILED (%d)" % len(FAILURES)); return 1
    print("gate.ci_check: PASSED"); return 0


if __name__ == "__main__":
    if len(sys.argv) < 3:
        print(__doc__); sys.exit(2)
    sys.exit(main(os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2])))
