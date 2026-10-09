#!/usr/bin/env python3
"""gate.verdict_* — THE JUDGE'S FOUR DOORS, the list's shape and the record's new fields (lane VERDICT-1 +
GATE-LOG-1; ONE_PICTURE_SPEC H1/H2; docs/TESTING_GATE.md §4). Toy run logs only (a PRIVATE run log,
JAH_RUN_LOG_DIR; a PRIVATE contention list, JAH_CONTENTION_FILE; a PRIVATE token dir, JAH_VRAM_DIR) — no GPU,
no display. One script, five rows:

  verdict_door      U1/U3: FAIL + real:<id> -> green; FAIL + plain text -> VERDICT REFUSED; FAIL + text with 2/3
                    solo -> "solos below 3/3"; contention: + 3/3 solo -> green, + no solos -> refused; LOST +
                    environmental -> refused; LOST + xid-read:<window covering it> -> green, a window that misses
                    it -> refused; CRASH with an xid in the record + contention: -> refused, + real:<id> -> green;
                    NOADMIT / NOTRUN never cleared; the CLI prints `VERDICT REFUSED <row>: <why>`
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

    def judge(self, recs, listed=False):
        cont = {"photon.view": "x"} if listed else {}
        return self.cgc.judge(("photon.view", None), recs, cont)


def rec(verdict, ts, retry=False, **kw):
    return dict({"suite": "photon.view", "arm": None, "verdict": verdict, "ts": ts, "retry": retry}, **kw)


def vrec(text, ts):
    return {"kind": "verdict", "suite": "photon.view", "arm": None, "verdict": "VERDICT", "text": text, "ts": ts}


def case_verdict_door(E):
    red = rec("FAIL", "2026-10-09T10:00:00+02:00")
    st, why = E.judge([red, vrec("real:VIEWS-XID-1 fixed in abc123", "2026-10-09T11:00:00+02:00")])
    check(st == "green", "FAIL + real:<defect id> -> green (%s: %s)" % (st, why[:80]))
    st, why = E.judge([red, vrec("the reader looked at it and it is fine", "2026-10-09T11:00:00+02:00")])
    check(st == "red" and why.startswith("VERDICT REFUSED") and "class token" in why,
          "FAIL + plain text -> VERDICT REFUSED, naming the class-token rule (%s)" % why[:110])
    solos = [rec(v, "2026-10-09T10:%02d:00+02:00" % (10 + i), retry=True) for i, v in enumerate(("PASS", "FAIL", "PASS"))]
    st, why = E.judge([red] + solos + [vrec("real:VIEWS-XID-1 a real defect", "2026-10-09T11:00:00+02:00")])
    check(st == "red" and "solos below 3/3" in why, "FAIL + text, 2/3 solo -> red 'solos below 3/3' (%s)" % why[:100])
    st, why = E.judge([red] + solos[:2] + [vrec("contention: load 14 beside two gates", "2026-10-09T11:00:00+02:00")],
                      listed=True)
    check(st == "red" and "solos below 3/3" in why, "...listed or not: a contention-listed row too (%s)" % why[:90])
    ok3 = [rec("PASS", "2026-10-09T10:%02d:00+02:00" % (10 + i), retry=True) for i in range(3)]
    st, why = E.judge([red] + ok3 + [vrec("contention: load 14 beside two gates", "2026-10-09T09:00:00+02:00"),
                                     vrec("contention: load 14 beside two gates", "2026-10-09T11:00:00+02:00")])
    check(st == "green" and "3/3" in why, "FAIL + contention: with 3/3 solo PASS after the red -> green (%s)" % why[:90])
    st, why = E.judge([red, vrec("contention: load 14 beside two gates", "2026-10-09T11:00:00+02:00")])
    check(st == "red" and "3/3 solo" in why, "FAIL + contention: with no solos -> refused (%s)" % why[:100])
    st, _ = E.judge([red, vrec("contention: load 14 beside two gates", "2026-10-09T10:05:00+02:00")] + ok3)
    check(st == "green", "...and once its 3/3 solos run after it, the same verdict clears it (written before them)")
    lost = rec("LOST", "2026-10-09T14:20:00+02:00")
    st, why = E.judge([lost, vrec("environmental: a device loss beside four gates, no Xid", "2026-10-09T15:00:00+02:00")])
    check(st == "red" and "never environmental" in why, "LOST + environmental -> refused, the sentence naming the rule (%s)"
          % why[:110])
    st, why = E.judge([lost, vrec("SPEED-CPU lead verdict: no Xid in dmesg", "2026-10-09T15:00:00+02:00")])
    check(st == "red" and "xid-read" in why, "LOST + any other text -> refused (%s)" % why[:100])
    st, why = E.judge([lost, vrec("xid-read:2026-10-09T14:00..14:30 none", "2026-10-09T15:00:00+02:00")])
    check(st == "green" and "xid-read" in why, "LOST + xid-read:<a window covering it> -> green (%s)" % why[:90])
    st, why = E.judge([lost, vrec("xid-read:2026-10-09T08:00..08:30 none", "2026-10-09T15:00:00+02:00")])
    check(st == "red" and "does not cover" in why, "LOST + xid-read:<a window that misses it> -> refused (%s)" % why[:100])
    for cls in ("OOM", "CRASH"):
        st, _ = E.judge([rec(cls, "2026-10-09T14:20:00+02:00"), vrec("real:BUDGET-7 a class under-counted",
                                                                       "2026-10-09T15:00:00+02:00")])
        check(st == "green", "%s + real:<id> -> green" % cls)
    xr = rec("CRASH", "2026-10-09T14:20:00+02:00",
             xid={"pid": 4242, "window": "2026-10-09T14:10:00+02:00..2026-10-09T14:20:00+02:00", "lines": ["NVRM: Xid 109"]})
    st, why = E.judge([xr, vrec("contention: four gates on the card", "2026-10-09T15:00:00+02:00")] + [
        rec("PASS", "2026-10-09T14:3%d:00+02:00" % i, retry=True) for i in range(3)], listed=True)
    check(st == "red" and "DEFECT by law" in why, "CRASH + an xid in the record + contention: (3/3 solo, listed) -> "
          "refused (%s)" % why[:100])
    st, why = E.judge([xr, vrec("xid-read:2026-10-09T14:00..14:30 none", "2026-10-09T15:00:00+02:00")])
    check(st == "red" and "real:<defect id>" in why, "...and xid-read: on it -> refused too (only real:<id>)")
    st, _ = E.judge([xr, vrec("real:VIEWS-XID-1", "2026-10-09T15:00:00+02:00")])
    check(st == "green", "...real:<id> on it -> green")
    st, why = E.judge([xr] + [rec("PASS", "2026-10-09T14:3%d:00+02:00" % i, retry=True) for i in range(3)], listed=True)
    check(st == "red" and "DEFECT" in why, "an xid red on a contention-listed row is not cleared by 3/3 solo (%s)" % why[:80])
    st, why = E.judge([rec("CRASH", "2026-10-09T14:20:00+02:00")] + [rec("PASS", "2026-10-09T14:3%d:00+02:00" % i,
                                                                         retry=True) for i in range(3)], listed=True)
    check(st == "red" and "never cleared by solos" in why, "a CRASH on a contention-listed row: no solo clearance (%s)"
          % why[:90])
    for na in ("NOADMIT", "NOTRUN"):
        st, _ = E.judge([rec(na, "2026-10-09T10:00:00+02:00"), vrec("real:VIEWS-XID-1", "2026-10-09T11:00:00+02:00")])
        check(st == "missing", "%s + any verdict -> never cleared (missing)" % na)
    # the CLI: a refused verdict is printed as `VERDICT REFUSED <row>: <why>` and the range stays refused
    E.fresh()
    E.put(ROWS[:2], "PASS", "2026-01-01T10:00:00")
    E.put(ROWS[2:], "FAIL", "2026-01-01T10:00:01")
    rc, out = E.run("--verdict", "photon.view=looked fine to the reader")
    check(rc == 1 and "VERDICT REFUSED photon.view:" in out, "the CLI prints VERDICT REFUSED <row>: <why> and "
          "refuses (%d)" % rc)
    rc, out = E.run("--verdict", "photon.view=real:FIXTURE-1 the fixture's red")
    check(rc == 0, "...a later real:<id> verdict on the same red is accepted (%d)" % rc)


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
    rc, out = E.run("--verdict", "test_engine=real:FIXTURE-1 the old tip's red, answered")
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
    check("xid" in one and one["xid"] is None, "a row with no fault carries xid: null")
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
