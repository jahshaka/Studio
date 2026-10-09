#!/usr/bin/env python3
"""gate.verdict_* — THE JUDGE'S FOUR DOORS, the list's shape and the record's new fields (lane VERDICT-1 +
GATE-LOG-1; ONE_PICTURE_SPEC H1/H2; docs/TESTING_GATE.md §4). Toy run logs only (a PRIVATE run log,
JAH_RUN_LOG_DIR; a PRIVATE contention list, JAH_CONTENTION_FILE; a PRIVATE token dir, JAH_VRAM_DIR) — no GPU,
no display. One script, five rows:

  verdict_door      U1/U3 + the lead's addendum: real:<id> is CHECKED (`fixed` needs the row's PASS at the tip,
                    `pre-existing` a recorded solo red at the base; a ticket named alone clears nothing — a CRASH
                    never); contention: only on a LISTED row whose red shows a measured competitor, with 3/3 solo;
                    FAIL + text with 2/3 solo -> "solos below 3/3"; LOST + environmental -> refused; xid-read: only
                    when the record's journal was unreadable, its window covering the red; CRASH with an xid in the
                    record + contention: -> refused; NOADMIT / NOTRUN never cleared; the CLI prints
                    `VERDICT REFUSED <row>: <why>`
  noadmit_pool      U2: a toy pool whose every arm is NOADMIT records NOADMIT (and is re-queued as a never-ran
                    row); a mixed one records FAIL with its NOADMIT arms named
  carried_red       U4: lane X red at tip A, the lane rebased to tip B with no re-run -> refused (`OPEN RED carried
                    from <A>`); re-run green at B -> accepted; a verdict at A -> accepted; BATCH-GATE-1's `lanes`
                    list is read too; a schema-1 (historic) red is not carried (forward only)
  contention_shape  U5: an entry missing `date` or `recheck` -> the judge refuses to load the list (exit 2)
  log_schema2       C: a toy run's records carry schema 2, slot_wait_s, drain_s, hold_s, box.queue_depth, box.mem,
                    xid (from vram_tokens' supervise through kernel_xid, end to end); gate-report.py prints the
                    table on the toy log and, on the archive, the preflight's numbers (87 gates, 58 verdicts, 6
                    refusable, 13 NOADMIT pools, 44 carried)

Run: gate_verdict_test.py <case> <source-dir> <build-dir>
"""
import json
import os
import shlex
import subprocess
import sys
import tempfile
import time

RANGE = "51e9f2c49..3756b2f18"          # VIEWS-DEPTH-1 (ci_gate_check_test's): photon.view + the smoke pair
ROWS = ["api.contract", "app.startup_quiet", "photon.view"]
FAILURES = []


def check(ok, what):
    print(("  ok: " if ok else "  FAIL: ") + what)
    if not ok: FAILURES.append(what)


class Env:
    def __init__(self, source, build):
        self.source, self.build = source, build
        sys.path.insert(0, os.path.join(source, "scripts"))
        import gate_runlog, ci_gate_check, vram_tokens, kernel_xid
        self.rl, self.cgc, self.vt, self.kx = gate_runlog, ci_gate_check, vram_tokens, kernel_xid
        self.scratch = tempfile.mkdtemp(prefix="gate-verdict-")
        self.tool = os.path.join(source, "scripts", "ci-gate-check.sh")
        self.contention(None)
        os.environ["JAH_VRAM_DIR"] = os.path.join(self.scratch, "vram")
        self.tip = self.git("rev-parse", "3756b2f18")
        self.pin = self.git("rev-parse", "3756b2f18:irisgl")

    def git(self, *a):
        return subprocess.run(["git"] + list(a), cwd=self.source, capture_output=True, text=True).stdout.strip()

    def contention(self, suites):
        path = os.path.join(self.scratch, "contention-%d.json" % len(os.listdir(self.scratch)))
        json.dump({"suites": suites or {}}, open(path, "w"))
        os.environ["JAH_CONTENTION_FILE"] = path

    def fresh(self, listed=False):
        os.environ["JAH_RUN_LOG_DIR"] = tempfile.mkdtemp(dir=self.scratch)
        self.contention({"photon.view": {"reason": "the fixture's contention row", "date": "2026-10-09",
                                         "recheck": "never (a fixture)"}} if listed else None)

    def put(self, suites, verdict, ts, tip=None, retry=False, lane="lane-x", schema=2, **extra):
        t = tip or self.tip
        self.rl.append_records([dict({"schema": schema, "suite": s, "arm": None, "verdict": verdict, "ts": ts,
                                      "retry": retry, "lane": lane, "gating": True, "tier": "scoped",
                                      "tip": {"studio": t, "studio_dirty": False, "irisgl": self.pin,
                                              "irisgl_dirty": False}}, **extra) for s in suites], "scoped", t)

    def verdict(self, suite, text, ts, tip=None):
        t = tip or self.tip
        path = os.path.join(os.environ["JAH_RUN_LOG_DIR"], "x-verdict-%s.jsonl" % t[:9])
        with open(path, "a") as f:
            f.write(json.dumps({"schema": 2, "kind": "verdict", "suite": suite, "arm": None, "verdict": "VERDICT",
                                "text": text, "ts": ts, "tip": {"studio": t, "irisgl": self.pin,
                                                                "studio_dirty": False, "irisgl_dirty": False}}) + "\n")

    def run(self, *extra, rng=RANGE):
        p = subprocess.run([self.tool, rng, "--build", self.build] + list(extra), capture_output=True, text=True,
                           env=dict(os.environ))
        return p.returncode, p.stdout + p.stderr

    def judge(self, recs, listed=False, base=None):
        cont = {"photon.view": "x"} if listed else {}
        return self.cgc.judge(("photon.view", None), recs, cont, base_recs=base)


def rec(verdict, ts, retry=False, **kw):
    return dict({"suite": "photon.view", "arm": None, "verdict": verdict, "ts": ts, "retry": retry}, **kw)


def vrec(text, ts):
    return {"kind": "verdict", "suite": "photon.view", "arm": None, "verdict": "VERDICT", "text": text, "ts": ts}


def case_verdict_door(E):
    T = "2026-10-09T%s:00+02:00"
    red = rec("FAIL", T % "10:00")
    fixpass = rec("PASS", T % "10:30")
    base_solo = [rec("FAIL", T % "09:00", retry=True)]
    st, why = E.judge([red, vrec("real:VIEWS-XID-1", T % "11:00")])
    check(st == "red" and "names a ticket only" in why, "FAIL + real:<id> alone (a ticket named) -> refused (%s)" % why[:90])
    st, why = E.judge([red, vrec("real:VIEWS-XID-1 fixed in abc123", T % "11:00")])
    check(st == "red" and "no PASS record of the row at the tip" in why,
          "FAIL + real:<id> fixed, no PASS at the tip -> refused (%s)" % why[:90])
    st, why = E.judge([red, fixpass, vrec("real:VIEWS-XID-1 fixed in abc123", T % "11:00")])
    check(st == "green", "FAIL + real:<id> fixed + its row PASS at the tip after the red -> green (%s: %s)" % (st, why[:80]))
    st, why = E.judge([red, vrec("real:VIEWS-XID-1 pre-existing on d-build", T % "11:00")], base=[])
    check(st == "red" and "BASE" in why, "FAIL + real:<id> pre-existing, no solo red at the base -> refused (%s)" % why[:90])
    st, why = E.judge([red, vrec("real:VIEWS-XID-1 pre-existing on d-build", T % "11:00")], base=base_solo)
    check(st == "green", "...with the red reproduced by a recorded solo at the base -> green (%s)" % why[:80])
    st, why = E.judge([red, vrec("the reader looked at it and it is fine", T % "11:00")])
    check(st == "red" and why.startswith("VERDICT REFUSED") and "class token" in why,
          "FAIL + plain text -> VERDICT REFUSED, naming the class-token rule (%s)" % why[:110])
    solos = [rec(v, T % ("10:%02d" % (10 + i)), retry=True) for i, v in enumerate(("PASS", "FAIL", "PASS"))]
    st, why = E.judge([red] + solos + [vrec("real:VIEWS-XID-1 fixed", T % "11:00")])
    check(st == "red" and "solos below 3/3" in why, "FAIL + text, 2/3 solo -> red 'solos below 3/3' (%s)" % why[:100])
    st, why = E.judge([red] + solos[:2] + [vrec("contention: load 14 beside two gates", T % "11:00")], listed=True)
    check(st == "red" and "solos below 3/3" in why, "...listed or not: a contention-listed row too (%s)" % why[:90])
    ok3 = [rec("PASS", T % ("10:%02d" % (10 + i)), retry=True) for i in range(3)]
    busy = rec("FAIL", T % "10:00", box={"other_ctests": 2, "queue_depth": 0})
    st, why = E.judge([red] + ok3 + [vrec("contention: load 14 beside two gates", T % "11:00")])
    check(st == "red" and "not listed" in why, "contention: on an UNLISTED row -> refused, 3/3 or not (%s)" % why[:90])
    st, why = E.judge([red] + ok3 + [vrec("contention: load 14 beside two gates", T % "11:00")], listed=True)
    check(st == "red" and "MEASURED competitor" in why, "contention: on a listed row whose red shows no measured competitor "
          "-> refused (%s)" % why[:90])
    for name, r in (("box.other_ctests", busy), ("box.queue_depth", rec("FAIL", T % "10:00", box={"queue_depth": 1})),
                    ("drain_s", rec("FAIL", T % "10:00", drain_s=40.0))):
        st, why = E.judge([r] + ok3 + [vrec("contention: load 14 beside two gates", T % "11:00")], listed=True)
        check(st == "green" and "3/3" in why, "contention: + a listed row + %s in the red + 3/3 solo -> green (%s)"
              % (name, why[:80]))
    st, why = E.judge([busy, vrec("contention: load 14 beside two gates", T % "11:00")], listed=True)
    check(st == "red" and "3/3 solo" in why, "contention: with no solos -> refused (%s)" % why[:100])
    st, _ = E.judge([busy, vrec("contention: load 14 beside two gates", T % "10:05")] + ok3, listed=True)
    check(st == "green", "...and once its 3/3 solos run after it, the same verdict clears it (written before them)")
    lost = rec("LOST", T % "14:20", xid=None)
    st, why = E.judge([lost, vrec("environmental: a device loss beside four gates, no Xid", T % "15:00")])
    check(st == "red" and "never environmental" in why, "LOST + environmental -> refused, the sentence naming the rule (%s)"
          % why[:110])
    st, why = E.judge([lost, vrec("SPEED-CPU lead verdict: no Xid in dmesg", T % "15:00")])
    check(st == "red" and "xid-read" in why, "LOST + any other text -> refused (%s)" % why[:100])
    st, why = E.judge([lost, vrec("xid-read:2026-10-09T14:00..14:30 none", T % "15:00")])
    check(st == "red" and "journal was read" in why, "LOST + xid-read: when the record's journal WAS read -> refused "
          "(the Xid would be in the record) (%s)" % why[:100])
    lostu = rec("LOST", T % "14:20", xid=None, journal_unreadable=True)
    st, why = E.judge([lostu, vrec("xid-read:2026-10-09T14:00..14:30 none", T % "15:00")])
    check(st == "green" and "xid-read" in why, "LOST + journal_unreadable + xid-read:<a window covering it> -> green (%s)"
          % why[:90])
    st, why = E.judge([lostu, vrec("xid-read:2026-10-09T08:00..08:30 none", T % "15:00")])
    check(st == "red" and "does not cover" in why, "...a window that misses it -> refused (%s)" % why[:100])
    for cls in ("OOM", "CRASH"):
        r = rec(cls, T % "14:20")
        st, why = E.judge([r, vrec("real:BUDGET-7 a class under-counted", T % "15:00")])
        check(st == "red" and "ticket" in why, "%s + a ticket named alone -> refused (a CRASH is never cleared by a "
              "ticket alone) (%s)" % (cls, why[:70]))
        st, _ = E.judge([r, rec("PASS", T % "14:40"), vrec("real:BUDGET-7 fixed", T % "15:00")])
        check(st == "green", "%s + real:<id> fixed + a PASS at the tip -> green" % cls)
    xr = rec("CRASH", T % "14:20",
             xid={"pid": 4242, "window": "2026-10-09T14:10:00+02:00..2026-10-09T14:20:00+02:00", "lines": ["NVRM: Xid 109"]},
             box={"other_ctests": 3})
    st, why = E.judge([xr, vrec("contention: four gates on the card", T % "15:00")] + [
        rec("PASS", T % ("14:3%d" % i), retry=True) for i in range(3)], listed=True)
    check(st == "red" and "DEFECT by law" in why, "CRASH + an xid in the record + contention: (3/3 solo, listed, a "
          "competitor) -> refused (%s)" % why[:100])
    st, why = E.judge([xr, vrec("xid-read:2026-10-09T14:00..14:30 none", T % "15:00")])
    check(st == "red" and "real:<defect id>" in why, "...and xid-read: on it -> refused too (only real:<id>)")
    st, _ = E.judge([xr, rec("PASS", T % "14:40"), vrec("real:VIEWS-XID-1 fixed", T % "15:00")])
    check(st == "green", "...real:<id> fixed + a PASS at the tip -> green")
    st, why = E.judge([xr] + [rec("PASS", T % ("14:3%d" % i), retry=True) for i in range(3)], listed=True)
    check(st == "red" and "DEFECT" in why, "an xid red on a contention-listed row is not cleared by 3/3 solo (%s)" % why[:80])
    st, why = E.judge([rec("CRASH", T % "14:20")] + [rec("PASS", T % ("14:3%d" % i), retry=True) for i in range(3)],
                      listed=True)
    check(st == "red" and "never cleared by solos" in why, "a CRASH on a contention-listed row: no solo clearance (%s)"
          % why[:90])
    for na in ("NOADMIT", "NOTRUN"):
        st, _ = E.judge([rec(na, T % "10:00"), vrec("real:VIEWS-XID-1 fixed", T % "11:00")])
        check(st == "missing", "%s + any verdict -> never cleared (missing)" % na)
    # the CLI: a refused verdict is printed as `VERDICT REFUSED <row>: <why>` and the range stays refused
    E.fresh()
    E.put(ROWS[:2], "PASS", "2026-01-01T10:00:00")
    E.put(ROWS[2:], "FAIL", "2026-01-01T10:00:01")
    rc, out = E.run("--verdict", "photon.view=looked fine to the reader")
    check(rc == 1 and "VERDICT REFUSED photon.view:" in out, "the CLI prints VERDICT REFUSED <row>: <why> and "
          "refuses (%d)" % rc)
    rc, out = E.run("--verdict", "photon.view=real:FIXTURE-1 fixed")
    check(rc == 1 and "VERDICT REFUSED photon.view:" in out, "...real:<id> fixed with no PASS at the tip -> refused (%d)" % rc)
    E.put(ROWS[2:], "FAIL", "2026-01-01T09:00:00", tip=E.git("rev-parse", "51e9f2c49"), retry=True)
    rc, out = E.run("--verdict", "photon.view=real:FIXTURE-1 pre-existing on the base")
    check(rc == 0, "...real:<id> pre-existing with a recorded solo red at the RANGE'S BASE -> accepted (%d)" % rc)


def case_noadmit_pool(E):
    os.environ["JAH_RUN_LOG_DIR"] = d = tempfile.mkdtemp(dir=E.scratch)
    R = E.rl._Run("scoped", "verdict-test", 2, None, None, None, False, {}, False, None)
    R.sampler.stop()
    allna = "ARM toy.a NOADMIT 0\nARM toy.b NOADMIT 0\nARM toy.c NOADMIT 0\n"
    recs = R.records("pool.toy", "Failed", 1.0, time.time(), (0, 0, 0), allna, {})
    check(recs[0]["verdict"] == "NOADMIT" and all(r["verdict"] == "NOADMIT" for r in recs[1:]),
          "an all-NOADMIT pool records NOADMIT (never ran), never FAIL (%s)" % [r["verdict"] for r in recs])
    mixed = "ARM toy.a PASS 10\nARM toy.b NOADMIT 0\nARM toy.c NOADMIT 0\n"
    recs = R.records("pool.toy", "Failed", 1.0, time.time(), (0, 0, 0), mixed, {})
    check(recs[0]["verdict"] == "FAIL" and recs[0].get("noadmit_arms") == ["toy.b", "toy.c"]
          and "toy.b" in (recs[0].get("failLine") or ""),
          "a mixed pool records FAIL with its NOADMIT arms named (%s, %s)" % (recs[0]["verdict"], recs[0].get("noadmit_arms")))
    # the re-queue (GATE-COST-1 P5) treats it as a never-ran row: re-run once, recorded once, NOADMIT
    fake = os.path.join(E.scratch, "fake_pool_ctest.sh")
    open(fake, "w").write("#!/bin/sh\n"
                          "echo '      Start  1: pool.toy'\n"
                          "echo '1: ARM toy.a NOADMIT 0'\n"
                          "echo '1: ARM toy.b NOADMIT 0'\n"
                          "echo '1/1 Test  #1: pool.toy ..........***Failed    0.10 sec'\n")
    os.chmod(fake, 0o755)
    os.environ["JAH_GATE_REQUEUE"] = "1"
    try:
        rc = E.rl.run_ctest(fake, E.scratch, "scoped", "verdict-test", 1, labels={"pool.toy": ["app"]}, echo=False)
    finally:
        del os.environ["JAH_GATE_REQUEUE"]
    got = [json.loads(l) for f in os.listdir(d) for l in open(os.path.join(d, f))]
    row = [r for r in got if r["suite"] == "pool.toy" and r["arm"] is None]
    check(rc != 0 and len(row) == 1 and row[0]["verdict"] == "NOADMIT",
          "...re-queued as a never-ran row and recorded once, NOADMIT (rc %s, %s)" % (rc, [r["verdict"] for r in row]))


def case_carried_red(E):
    A = "a" * 40
    E.fresh()
    E.put(ROWS, "PASS", "2026-01-01T10:00:00")
    E.put(["test_engine"], "FAIL", "2026-01-01T09:00:00", tip=A)
    rc, out = E.run()
    check(rc == 1 and "OPEN RED carried from aaaaaaaaa: test_engine" in out,
          "lane X red at tip A, rebased to B with no re-run -> refused, OPEN RED carried (%d)" % rc)
    E.put(["test_engine"], "PASS", "2026-01-01T11:00:00", gating=True)
    rc, out = E.run()
    check(rc == 0 and "OPEN RED" not in out, "...re-run green at B -> accepted (%d)" % rc)
    E.fresh()
    E.put(ROWS, "PASS", "2026-01-01T10:00:00")
    E.put(["test_engine"], "FAIL", "2026-01-01T09:00:00", tip=A)
    E.put(["test_engine"], "FAIL", "2026-01-01T08:00:00", tip=E.git("rev-parse", "51e9f2c49"), retry=True)
    rc, out = E.run("--verdict", "test_engine=real:FIXTURE-1 pre-existing: the old tip's red, reproduced on the base")
    vfiles = [f for f in os.listdir(os.environ["JAH_RUN_LOG_DIR"]) if "-verdict-aaaaaaaaa" in f]
    check(rc == 0 and vfiles, "...a verdict at A (recorded AT A) -> accepted (%d, %s)" % (rc, vfiles))
    rc, out = E.run()
    check(rc == 0, "...which a later check reads without the flag (%d)" % rc)
    E.fresh()
    E.put(ROWS, "PASS", "2026-01-01T10:00:00")
    E.put(["test_engine"], "FAIL", "2026-01-01T09:00:00", tip=A)
    rc, out = E.run("--verdict", "test_engine=looked at it")
    check(rc == 1 and "VERDICT REFUSED test_engine" in out, "...a verdict at A that fails the door -> refused (%d)" % rc)
    E.fresh()
    E.put(ROWS, "PASS", "2026-01-01T10:00:00")
    E.put(["test_engine"], "FAIL", "2026-01-01T09:00:00", tip=A, lane=None, lanes=["lane-y", "lane-x"])
    rc, out = E.run()
    check(rc == 1 and "OPEN RED carried" in out, "BATCH-GATE-1's `lanes` list is read as well as `lane` (%d)" % rc)
    E.fresh()
    E.put(ROWS, "PASS", "2026-01-01T10:00:00")
    E.put(["test_engine"], "FAIL", "2026-01-01T09:00:00", tip=A, lane="another-lane")
    rc, _ = E.run()
    check(rc == 0, "another lane's red at another tip is not carried (%d)" % rc)
    E.fresh()
    E.put(ROWS, "PASS", "2026-01-01T10:00:00")
    E.put(["test_engine"], "FAIL", "2026-01-01T09:00:00", tip=A, schema=1)
    rc, _ = E.run()
    check(rc == 0, "a schema-1 (historic) red is not carried: forward only, gate-report.py --carried reports those (%d)"
          % rc)


def case_contention_shape(E):
    E.fresh()
    E.put(ROWS, "PASS", "2026-01-01T10:00:00")
    for miss in ("date", "recheck", "reason"):
        e = {"reason": "r", "date": "2026-10-09", "recheck": "after P1 lands: solo 3x"}
        del e[miss]
        E.contention({"photon.view": e})
        rc, out = E.run()
        check(rc == 2 and "without the shape" in out and miss in out,
              "an entry missing `%s` -> the judge refuses to load the list, exit 2 (%d)" % (miss, rc))
    E.contention({"photon.view": "a reason as a bare string (the old shape)"})
    rc, out = E.run()
    check(rc == 2 and "not an object" in out, "an entry in the old string shape -> refused (%d)" % rc)
    E.contention({"photon.view": {"reason": "r", "date": "last week", "recheck": "x"}})
    rc, out = E.run()
    check(rc == 2 and "YYYY-MM-DD" in out, "an undated entry (a date that is not YYYY-MM-DD) -> refused (%d)" % rc)
    E.contention({"photon.view": {"reason": "r", "date": "2026-10-09", "recheck": "after P1 lands: solo 3x"}})
    rc, out = E.run()
    check(rc == 0, "a list whose every entry has {reason, date, recheck} loads (%d)" % rc)
    live = os.path.join(E.rl.workspace_root(), "testing", "contention.json")
    if os.path.exists(live):
        os.environ["JAH_CONTENTION_FILE"] = live
        s, why = E.rl.contention_load()
        print("  (the workspace's live list %s: %s)" % (live, "loads, %d entries" % len(s) if s is not None else why[:160]))


def case_log_schema2(E):
    # U3 end to end: vram_tokens' supervise reads a (fake) journal through kernel_xid and prints the window
    journal = os.path.join(E.scratch, "journal.txt")
    open(journal, "w").close()
    env = dict(os.environ, JAH_KERNEL_JOURNAL=journal, JAH_VRAM_TOKENS="0")
    line = ('echo "$(date +%s).5 box kernel: NVRM: Xid (PCI:0000:01:00): 109, pid=$$, name=sh, channel 0x2" > ' + journal)
    p = subprocess.run([sys.executable, os.path.join(E.source, "scripts", "vram_tokens.py"), "admit", "1", "--label",
                        "toy.row", "--", "sh", "-c", line], capture_output=True, text=True, env=env)
    out = p.stdout + p.stderr
    xid, _ = E.rl.xid_of(out)
    check(p.returncode == 1 and "XID-WINDOW" in out and xid and xid["pid"] > 0 and xid["window"] and ".." in xid["window"]
          and "Xid" in xid["lines"][0], "supervise prints the Xid and its journal window; the record's xid is {pid, window, "
          "lines} (rc %d, %s)" % (p.returncode, xid))
    # a toy run: the slot's wait, a whole-card phase (drain, hold), a queue behind it, memory, an xid
    os.environ["JAH_RUN_LOG_DIR"] = d = tempfile.mkdtemp(dir=E.scratch)
    os.environ["JAH_VRAM_TOKENS"] = "2"
    os.environ["JAH_GATE_SLOT_HELD"] = str(os.getpid())
    os.environ["JAH_GATE_SLOT_WAIT_S"] = "12.5"
    tickets = [E.vt._take_ticket("toy holder")[1], E.vt._take_ticket("toy waiter")[1]]
    fake = os.path.join(E.scratch, "fake_ctest.sh")
    with open(fake, "w") as f:
        f.write("#!/bin/sh\n")
        f.write("echo '      Start  1: s.one'\n")
        f.write("echo '1/2 Test  #1: s.one ..........   Passed    0.10 sec'\n")
        f.write("echo '      Start  2: s.two'\n")
        for l in out.splitlines():
            if l.startswith("XID"): f.write("printf '%%s\\n' %s\n" % shlex.quote("2: " + l))
        f.write("echo '2/2 Test  #2: s.two ..........***Failed    0.10 sec'\n")
    os.chmod(fake, 0o755)
    try:
        E.rl.run_ctest(fake, E.scratch, "scoped", "verdict-test", 1, echo=False, whole_card=True)
    finally:
        for fd in tickets: os.close(fd)
        for k in ("JAH_VRAM_TOKENS", "JAH_GATE_SLOT_HELD", "JAH_GATE_SLOT_WAIT_S"): os.environ.pop(k, None)
    recs = {r["suite"]: r for f in os.listdir(d) for r in map(json.loads, open(os.path.join(d, f)))}
    one, two = recs.get("s.one", {}), recs.get("s.two", {})
    check(one.get("schema") == 2 and two.get("schema") == 2, "the records are schema 2")
    check(one.get("slot_wait_s") == 12.5, "slot_wait_s = the slot's wait (%s)" % one.get("slot_wait_s"))
    check(isinstance(one.get("drain_s"), float) and isinstance(one.get("hold_s"), float) and one["hold_s"] >= 0,
          "drain_s / hold_s of the whole-card phase (%s / %s)" % (one.get("drain_s"), one.get("hold_s")))
    check((one.get("box") or {}).get("queue_depth") == 1, "box.queue_depth = the gates waiting at the row's start (%s)"
          % (one.get("box") or {}).get("queue_depth"))
    mem = (one.get("box") or {}).get("mem") or {}
    check(set(mem) == {"psi10", "swap_used_mb", "builds"} and (not sys.platform.startswith("linux") or (
        isinstance(mem["psi10"], float) and isinstance(mem["builds"], int))), "box.mem = {psi10, swap_used_mb, builds} (%s)"
          % mem)
    check("xid" in one and one["xid"] is None and "journal_unreadable" not in one, "a row with no fault carries xid: null")
    R = E.rl._Run("scoped", "verdict-test", 1, None, None, None, False, {}, False, None)
    R.sampler.stop()
    ur = R.records("s.three", "Failed", 1.0, time.time(), (0, 0, 0), "vram: " + E.kx.FINDING + " (s.three)\nboom", {})
    check(ur[0].get("journal_unreadable") is True and ur[0]["xid"] is None,
          "a row whose journal was unreadable records journal_unreadable (the one case xid-read: may answer)")
    check((two.get("xid") or {}).get("pid") == xid["pid"] and (two.get("xid") or {}).get("window") == xid["window"],
          "a row whose output carries the Xid records it (%s)" % two.get("xid"))
    # gate-report.py: the table on the toy log, and the preflight's numbers on the archive
    rep = os.path.join(E.source, "scripts", "gate-report.py")
    p = subprocess.run([sys.executable, rep, d, "--ref", "HEAD"], capture_output=True, text=True)
    check(p.returncode == 0 and "TABLE gates" in p.stdout and "slot wait 0.0 h over 1 gate(s)" in p.stdout,
          "gate-report.py prints the weekly table on a toy log, reading its schema-2 fields (%d)" % p.returncode)
    ws = E.rl.workspace_root()
    dirs = [os.path.join(ws, "testing", "runs-archive"), os.path.join(ws, "testing", "runs")]
    if not os.path.isdir(dirs[0]) or not E.git("rev-parse", "--verify", "-q", "420486e63^{commit}"):
        print("  (no run-log archive at %s, or the preflight's d-build 420486e63 is not in this clone: the archive "
              "numbers are the Linux box's — not checked here)" % dirs[0])
        return
    frozen = os.path.join(E.source, "tests", "hygiene", "fixtures", "gate_report", "contention_preflight_2026-10-09.json")
    p = subprocess.run([sys.executable, rep] + dirs + ["--week", "2026-10-09T17:30", "--ref", "420486e63",
                                                       "--contention", frozen],
                       capture_output=True, text=True)
    last = (p.stdout.strip().splitlines() or [""])[-1]
    print("  " + last)
    want = "TABLE gates 87 | verdicts 58 | refusable(H2 solos<3/3) 6 |"
    check(p.returncode == 0 and last.startswith(want) and "NOADMIT pools 13 | carried 44" in last,
          "on the archive, the preflight's numbers: 87 gates, 58 verdicts, 6 refusable, 13 NOADMIT pools, 44 carried")


CASES = {"verdict_door": case_verdict_door, "noadmit_pool": case_noadmit_pool, "carried_red": case_carried_red,
         "contention_shape": case_contention_shape, "log_schema2": case_log_schema2}


def main(case, source, build):
    E = Env(source, build)
    if not E.tip:
        check(False, "the recorded tip 3756b2f18 is in this clone")
    else:
        CASES[case](E)
    subprocess.run(["rm", "-rf", E.scratch])
    if FAILURES:
        print("gate.%s: FAILED (%d)" % (case, len(FAILURES))); return 1
    print("gate.%s: PASSED" % case); return 0


if __name__ == "__main__":
    if len(sys.argv) < 4 or sys.argv[1] not in CASES:
        print(__doc__); sys.exit(2)
    sys.exit(main(sys.argv[1], os.path.abspath(sys.argv[2]), os.path.abspath(sys.argv[3])))
