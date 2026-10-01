#!/usr/bin/env python3
"""gate.ci_check — THE MERGE REFUSAL, proved (MODULAR-GATE-1 T6; the flake law since TEST-SELECTOR-1).

scripts/ci-gate-check.sh <range> refuses a merge unless the run log at the range's tip answers every
row the range selects under THE FLAKE LAW (scripts/ci_gate_check.py): a contention-class red needs
3/3 solo PASS after it, any other red a recorded verdict (`--verdict "<row>=<text>"`, per row and
timestamped in the log, clearing only the reds before it),
and a row never run is refused. Proved on a recorded range (VIEWS-DEPTH-1, 51e9f2c49..3756b2f18:
photon.view + the smoke pair) against a PRIVATE run log (JAH_RUN_LOG_DIR) and a PRIVATE contention
list (JAH_CONTENTION_FILE):

  nothing run -> refused; all green -> accepted; a verdict-less red -> refused, and ONE solo PASS
  does not clear it; the same red with a recorded verdict -> accepted (the verdict is in the log);
  a contention-class red with 2/3 solo -> refused, 3/3 -> accepted, a solo red among them -> refused;
  a green run from a DIRTY tree, at another tip, with a DIRTY irisgl or an irisgl that is not the
  tip's pin -> refused (F1); no contention list -> unusable (exit 2); a range that MOVES THE FORK
  PIN needs the whole MERGE tier at its tip (§7b rule 4), whatever its scoped selection;
  and the run log samples the box at EACH SUITE'S START (L1), not once per gate.

Run: ci_gate_check_test.py <source-dir> <build-dir>
"""
import json
import os
import subprocess
import sys
import tempfile

RANGE = "51e9f2c49..3756b2f18"
FORK_RANGE = "1775e79f0..684e0f7aa"     # contact-occlusion-1: a pin bump inside the lane
FAILURES = []


def check(ok, what):
    print(("  ok: " if ok else "  FAIL: ") + what)
    if not ok: FAILURES.append(what)


def main(source, build):
    tool = os.path.join(source, "scripts", "ci-gate-check.sh")
    tip = subprocess.run(["git", "rev-parse", "3756b2f18"], cwd=source, capture_output=True, text=True).stdout.strip()
    check(bool(tip), "the recorded tip 3756b2f18 is in this clone")
    if not tip: return 1
    pin = subprocess.run(["git", "rev-parse", "3756b2f18:irisgl"], cwd=source, capture_output=True, text=True).stdout.strip()
    sys.path.insert(0, os.path.join(source, "scripts"))
    import gate_runlog
    scratch = tempfile.mkdtemp(prefix="ci-check-")
    listed = os.path.join(scratch, "contention.json")
    unlisted = os.path.join(scratch, "contention-empty.json")
    json.dump({"suites": {"photon.view": "the fixture's contention-class row (this test only)"}}, open(listed, "w"))
    json.dump({"suites": {}}, open(unlisted, "w"))

    def run(rng=RANGE, *extra):
        p = subprocess.run([tool, rng, "--build", build] + list(extra), capture_output=True, text=True,
                           env=dict(os.environ))
        return p.returncode, p.stdout

    def put(suites, verdict, ts, t=tip, dirty=False, retry=False, ig=pin, ig_dirty=False):
        gate_runlog.append_records([{"suite": s, "arm": None, "verdict": verdict, "ts": ts, "retry": retry,
                                     "tip": {"studio": t, "studio_dirty": dirty, "irisgl": ig,
                                             "irisgl_dirty": ig_dirty}} for s in suites], "scoped", t)

    rows = ["api.contract", "app.startup_quiet", "photon.view"]

    def fresh(contention):
        d = tempfile.mkdtemp(dir=scratch)
        os.environ["JAH_RUN_LOG_DIR"] = d
        os.environ["JAH_CONTENTION_FILE"] = contention
        return d

    # ---- the law, a row NOT in the contention class ---------------------------------------------
    fresh(unlisted)
    rc, out = run()
    check(rc == 1 and "no record" in out, "nothing run -> refused, and says which rows (%d)" % rc)
    put(rows[:2], "PASS", "2026-01-01T10:00:00")
    put(rows[2:], "FAIL", "2026-01-01T10:00:01")
    rc, out = run()
    check(rc == 1 and "photon.view" in out and "needs a recorded verdict" in out,
          "a verdict-less red -> refused, naming it and what it needs (%d)" % rc)
    put(rows[2:], "PASS", "2026-01-01T10:05:00", retry=True)
    rc, out = run()
    check(rc == 1 and "needs a recorded verdict" in out,
          "...and ONE solo PASS does not erase it (the audit's L2: latest-wins is gone) (%d)" % rc)
    rc, out = run(RANGE, "--verdict", "photon.view=the fixture's red, read by the test")
    check(rc == 0 and "recorded verdict" in out, "the same red with a recorded verdict -> accepted (%d)" % rc)
    vfiles = [f for f in os.listdir(os.environ["JAH_RUN_LOG_DIR"]) if "-verdict-" in f]
    vrec = [json.loads(l) for f in vfiles for l in open(os.path.join(os.environ["JAH_RUN_LOG_DIR"], f))]
    check(len(vrec) == 1 and vrec[0]["suite"] == "photon.view" and vrec[0]["kind"] == "verdict"
          and "read by the test" in vrec[0]["text"], "...and the verdict is a record in the run log (%s)" % vfiles)
    rc, out = run()
    check(rc == 0, "...which a later check reads without the flag (%d)" % rc)
    put(rows[2:], "FAIL", "2099-01-01T10:00:00")
    rc, out = run()
    check(rc == 1 and "needs a recorded verdict" in out,
          "a red logged AFTER the verdict is not cleared by it (D4: per row, timestamped) (%d)" % rc)
    rc, out = run(RANGE, "--verdict", "api.contract=not red, so nothing to answer")
    check(rc == 1 and "no verdict recorded for api.contract" in out,
          "a verdict for a row that is not red is refused, said out loud (%d)" % rc)

    # ---- a row that never ran (NOADMIT) is MISSING, which no verdict clears ------------------------
    fresh(unlisted)
    put(rows[:2], "PASS", "2026-01-01T10:00:00")
    put(rows[2:], "NOADMIT", "2026-01-01T10:00:01")
    rc, out = run(RANGE, "--verdict", "photon.view=it waited too long")
    check(rc == 1 and "no record" in out and "no verdict recorded for photon.view" in out,
          "a NOADMIT row is missing and a verdict cannot clear it (%d)" % rc)

    # ---- the law, a CONTENTION-CLASS row ----------------------------------------------------------
    fresh(listed)
    put(rows[:2], "PASS", "2026-01-01T10:00:00")
    put(rows[2:], "FAIL", "2026-01-01T10:00:01")
    put(rows[2:], "PASS", "2026-01-01T10:05:00", retry=True)
    put(rows[2:], "PASS", "2026-01-01T10:06:00", retry=True)
    rc, out = run()
    check(rc == 1 and "2/3" in out, "a contention-class red with 2/3 solo PASS -> refused (%d)" % rc)
    put(rows[2:], "PASS", "2026-01-01T10:07:00", retry=True)
    rc, out = run()
    check(rc == 0 and "3/3 solo PASS" in out, "...3/3 -> accepted, and says why (%d)" % rc)
    fresh(listed)
    put(rows[:2], "PASS", "2026-01-01T10:00:00")
    put(rows[2:], "FAIL", "2026-01-01T10:00:01")
    for k, v in enumerate(("PASS", "FAIL", "PASS")):
        put(rows[2:], v, "2026-01-01T10:0%d:00" % (5 + k), retry=True)
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
    check(rc == 0, "every selected row green at the tip -> accepted (%d)" % rc)

    # ---- no contention list -----------------------------------------------------------------------
    fresh(os.path.join(scratch, "absent.json"))
    put(rows, "PASS", "2026-01-01T12:00:00")
    rc, out = run()
    check(rc == 2 and "contention list" in out, "no contention list -> unusable, exit 2, saying so (%d)" % rc)

    # ---- a fork pin bump needs the whole tier at the tip (§7b rule 4) -------------------------------
    ftip = subprocess.run(["git", "rev-parse", FORK_RANGE.split("..")[1]], cwd=source, capture_output=True,
                          text=True).stdout.strip()
    if ftip:
        fresh(unlisted)
        rc, out = run(FORK_RANGE)
        check(rc == 1 and "the fork pin moved" in out,
              "a range that moves the fork pin asks for the MERGE tier's records (%d: %s)"
              % (rc, out.strip().splitlines()[0][:160] if out.strip() else ""))
    else:
        check(False, "the recorded fork range %s is in this clone" % FORK_RANGE)

    # ---- L1: the box is sampled at each suite's START ----------------------------------------------
    d = fresh(unlisted)
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

    subprocess.run(["rm", "-rf", scratch])
    if FAILURES:
        print("gate.ci_check: FAILED (%d)" % len(FAILURES)); return 1
    print("gate.ci_check: PASSED"); return 0


if __name__ == "__main__":
    if len(sys.argv) < 3:
        print(__doc__); sys.exit(2)
    sys.exit(main(os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2])))
