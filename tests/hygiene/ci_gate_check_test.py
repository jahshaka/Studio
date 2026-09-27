#!/usr/bin/env python3
"""gate.ci_check — THE MERGE REFUSAL, proved (MODULAR-GATE-1, T6).

scripts/ci-gate-check.sh <range> refuses a merge unless the run log holds a green record at the
range's tip for every row the range selects. Proved on a recorded range (VIEWS-DEPTH-1,
51e9f2c49..3756b2f18: photon.view + the smoke pair) against a private run log
(JAH_RUN_LOG_DIR), six cases: nothing run -> refused; all green -> accepted; one red ->
refused; a red then a green solo retry -> accepted (the flake protocol; both stay in the log);
a green run from a DIRTY tree -> refused; a green run at another tip -> refused.

Run: ci_gate_check_test.py <source-dir> <build-dir>
"""
import os
import subprocess
import sys
import tempfile

RANGE = "51e9f2c49..3756b2f18"
FAILURES = []


def check(ok, what):
    print(("  ok: " if ok else "  FAIL: ") + what)
    if not ok: FAILURES.append(what)


def main(source, build):
    tool = os.path.join(source, "scripts", "ci-gate-check.sh")
    tip = subprocess.run(["git", "rev-parse", "3756b2f18"], cwd=source, capture_output=True, text=True).stdout.strip()
    check(bool(tip), "the recorded tip 3756b2f18 is in this clone")
    if not tip: return 1
    sys.path.insert(0, os.path.join(source, "scripts"))
    with tempfile.TemporaryDirectory() as d:
        os.environ["JAH_RUN_LOG_DIR"] = d
        import gate_runlog

        def run():
            p = subprocess.run([tool, RANGE, "--build", build], capture_output=True, text=True, env=dict(os.environ))
            return p.returncode, p.stdout

        def put(suites, verdict, ts, t=tip, dirty=False, retry=False):
            gate_runlog.append_records([{"suite": s, "arm": None, "verdict": verdict, "ts": ts, "retry": retry,
                                         "tip": {"studio": t, "studio_dirty": dirty}} for s in suites], "scoped", t)

        rows = ["api.contract", "app.startup_quiet", "photon.view"]
        rc, out = run()
        check(rc == 1 and "no record" in out, "nothing run -> refused, and says which rows (%d)" % rc)
        put(rows[:2], "PASS", "2026-01-01T10:00:00")
        put(rows[2:], "FAIL", "2026-01-01T10:00:01")
        rc, out = run()
        check(rc == 1 and "red" in out and "photon.view" in out, "one red -> refused, naming it (%d)" % rc)
        put(rows[2:], "PASS", "2026-01-01T10:05:00", retry=True)
        rc, out = run()
        check(rc == 0 and "green" in out, "the red then a green solo retry -> accepted (%d)" % rc)
    for name, kw in (("a DIRTY tree", {"dirty": True}), ("another tip", {"t": "0" * 40})):
        with tempfile.TemporaryDirectory() as d:
            os.environ["JAH_RUN_LOG_DIR"] = d
            put(rows, "PASS", "2026-01-01T11:00:00", **kw)
            rc, out = run()
            check(rc == 1, "a green run from %s -> refused (%d)" % (name, rc))
    with tempfile.TemporaryDirectory() as d:
        os.environ["JAH_RUN_LOG_DIR"] = d
        put(rows, "PASS", "2026-01-01T12:00:00")
        rc, out = run()
        check(rc == 0, "every selected row green at the tip -> accepted (%d)" % rc)
    if FAILURES:
        print("gate.ci_check: FAILED (%d)" % len(FAILURES)); return 1
    print("gate.ci_check: PASSED"); return 0


if __name__ == "__main__":
    if len(sys.argv) < 3:
        print(__doc__); sys.exit(2)
    sys.exit(main(os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2])))
