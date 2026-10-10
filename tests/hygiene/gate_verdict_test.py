#!/usr/bin/env python3
"""gate.verdict_* — THE JUDGE'S FOUR DOORS, the defect registry's shape and the record's new fields (lane VERDICT-1 +
GATE-LOG-1; ONE_PICTURE_SPEC H1/H2; docs/TESTING_GATE.md §4). Toy run logs only (a PRIVATE run log,
JAH_RUN_LOG_DIR; a PRIVATE defect registry, JAH_DEFECTS_FILE; a PRIVATE token dir, JAH_VRAM_DIR) — no GPU,
no display. One script, five rows:

  verdict_door      U1/U3, TESTING_V3 §1.4: real:<id> must be REGISTERED for the row and PROVED (a PASS at a later sha that reaches the row;
                    the red reproduced by a solo at the base = KNOWN RED: a merge passes, a push / stage close
                    refuses; a nondeterminism id clears by 3/3); contention: only for an open nondeterminism row
                    whose red's COMPETITOR CENSUS shows competition (never the gate's own queue or drain), with 3/3;
                    solos below 3/3 red; 'environmental' / ENOSPC / a dead display never verdicts; xid-read: only
                    when the journal was unreadable; an xid in the record takes real: only; NOADMIT / NOTRUN never
                    cleared; the CLI prints `VERDICT REFUSED <row>: <why>`
  noadmit_pool      U2: a toy pool whose every arm is NOADMIT records NOADMIT (and is re-queued as a never-ran
                    row); a mixed one records FAIL with its NOADMIT arms named
  carried_red       U4 (only a lane's OWN records, `lanes == [lane]`, no batch tag): lane X red at tip A, the lane rebased to tip B with no re-run -> refused (`OPEN RED carried
                    from <A>`); re-run green at B -> accepted; a verdict at A -> accepted; BATCH-GATE-1's `lanes`
                    list is read too; a schema-1 (historic) red is not carried (forward only)
  contention_shape  U5 / TESTING_V3 §1.5: a malformed registry entry or pending file (a missing field, a bad
                    kind/state/date/found_by, unreadable, a duplicate id) is QUARANTINED (moved / copied to
                    defects.quarantine/ with a .why, printed) and the rest loads; an unreadable registry file ->
                    exit 2; pending entries ingested; the contention class is the enrolled nondeterminism subset
  log_schema2       C: a toy run's records carry schema 2, slot_wait_s, drain_s, hold_s, box.queue_depth, box.mem,
                    xid (from vram_tokens' supervise through kernel_xid, end to end); gate-report.py prints the
                    table on the toy log and, on the archive, the preflight's numbers (87 gates, 58 verdicts, 6
                    refusable, 13 NOADMIT pools, 44 carried)

Run: gate_verdict_test.py <case> <source-dir> <build-dir>
"""
import json
import os
import re
import shlex
import subprocess
import sys
import tempfile
import time

RANGE = "51e9f2c49..3756b2f18"          # VIEWS-DEPTH-1 (ci_gate_check_test's): photon.view + the smoke pair
ROWS = ["api.contract", "app.startup_quiet", "photon.view"]
FAILURES = []


ENROLLED = {"enrolled": {"by": "lead", "rate": "3 in 200 (fixture)", "census": {"other_ctests": 2}, "date": "2026-10-09"}}


def defect(i, rows, kind="defect", **kw):
    if kind == "nondeterminism" and "uses" not in kw and "enrolled" not in kw:
        kw = dict(ENROLLED, **kw)
    return dict({"id": i, "rows": rows, "kind": kind, "cause": "the fixture's (this test only)", "state": "open",
                 "first_seen": {"tip": "0" * 9, "pin": "0" * 9, "run": "20261009T120000-000000000"},
                 "recheck": "2099-12-31", "found_by": "lane"}, **kw)


FIXTURE_DEFECTS = [defect("FIXTURE-1", ROWS + ["test_engine"]), defect("VIEWS-XID-1", ["photon.view"]),
                   defect("BUDGET-7", ["photon.view"]), defect("RETIRED-1", ["photon.view"], state="retired"),
                   defect("OTHER-ROW-1", ["api.contract"]), defect("NONDET-SOLO-1", ["photon.view"], "nondeterminism",
                                                                    state="retired")]
NONDET = defect("NONDET-FIXTURE-1", ["photon.view"], "nondeterminism")
BUSY = {"census": {"gpu_apps": [], "gpu_competitors": [], "other_ctests": 2, "builds": 0, "psi10_mem": 0.0, "psi10_io": 0.0}}


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

    def registry(self, entries):
        """A PRIVATE defect registry (TESTING_V3 §1.5), JAH_DEFECTS_FILE."""
        path = os.path.join(self.scratch, "defects-%d.json" % len(os.listdir(self.scratch)))
        json.dump({"defects": entries}, open(path, "w"))
        os.environ["JAH_DEFECTS_FILE"] = path
        d, _ = self.rl.defects_load()
        return d

    def contention(self, ignored=None):
        self.defects = self.registry(FIXTURE_DEFECTS)

    def fresh(self, listed=False):
        os.environ["JAH_RUN_LOG_DIR"] = tempfile.mkdtemp(dir=self.scratch)
        self.defects = self.registry(FIXTURE_DEFECTS + ([NONDET] if listed else []))

    def put(self, suites, verdict, ts, tip=None, retry=False, lane="lane-x", schema=2, tier="scoped", **extra):
        t = tip or self.tip
        ig = self.pin if t == self.tip else self.git("rev-parse", "%s:irisgl" % t)
        self.rl.append_records([dict({"schema": schema, "suite": s, "arm": None, "verdict": verdict, "ts": ts,
                                      "retry": retry, "lanes": [lane] if lane else [], "gating": True, "tier": tier,
                                      "tip": {"studio": t, "studio_dirty": False, "irisgl": ig,
                                              "irisgl_dirty": False,
                                              # GATE-COST-2 #8/F4: a run carries what it was built from (no stamp: stale)
                                              "built": {"studio": t, "irisgl": ig, "dirty": False}}}, **extra)
                                for s in suites], "scoped", t)

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

    def judge(self, recs, listed=False, base=None, defects=None):
        d = defects or {e["id"]: e for e in FIXTURE_DEFECTS + ([NONDET] if listed else [])}
        # a stub history for the toy shas: every different sha descends and reaches (the real Prover is tested apart)
        return self.cgc.judge(("photon.view", None), recs, d, base_recs=base,
                              proves=lambda a, b, k: bool(a and b and a != b))


def rec(verdict, ts, retry=False, **kw):
    return dict({"suite": "photon.view", "arm": None, "verdict": verdict, "ts": ts, "retry": retry}, **kw)


def vrec(text, ts):
    return {"kind": "verdict", "suite": "photon.view", "arm": None, "verdict": "VERDICT", "text": text, "ts": ts}


def case_verdict_door(E):
    registry_id_cases(E)
    T = "2026-10-09T%s:00+02:00"
    RED_SHA, FIX_SHA = {"studio": "a" * 40}, {"studio": "f" * 40}
    red = rec("FAIL", T % "10:00", tip=RED_SHA)
    fixpass = rec("PASS", T % "10:30", tip=FIX_SHA)             # the fix: a PASS at a LATER sha than the red
    base_solo = [rec("FAIL", T % "09:00", retry=True)]
    # ROUND 2 F1: THE PROVER — the proving PASS is at a sha DESCENDED from the red's whose range REACHES the row (real
    # history: a docs-only commit reaches nothing; this lane's first commit reaches the gate rows)
    gs = E.cgc.load_gs()
    prover = E.cgc.Prover(gs, E.build, lambda R=[]: R[0] if R else (R.append(E.cgc.Reach(gs, E.build, E.tip, "")) or R[0]))
    docs, docs_p = E.git("rev-parse", "c37fed3b7"), E.git("rev-parse", "c37fed3b7^")
    lane0, lane1 = E.git("rev-parse", "a5ab3a057"), E.git("rev-parse", "0e7ee6eb6")
    check(not prover(docs_p, docs, ("api.contract", None)), "the empty/docs-only commit case: a PASS at a commit whose "
          "change touches no row proves no fix (c37fed3b7, docs/TESTING.md)")
    check(prover(lane0, lane1, ("gate.ci_check", None)), "a PASS at a descendant whose change reaches the row proves it")
    check(not prover(lane1, lane0, ("gate.ci_check", None)), "a PASS at an ANCESTOR of the red (pre-bug code) proves nothing")
    check(not prover(lane0, lane0, ("gate.ci_check", None)), "a PASS at the red's own sha proves nothing")
    st, why = E.cgc.judge(("api.contract", None), [rec("FAIL", T % "10:00", suite="api.contract", tip={"studio": docs_p}),
                                                   rec("PASS", T % "10:30", suite="api.contract", tip={"studio": docs}),
                                                   vrec("real:OTHER-ROW-1", T % "11:00")],
                          {e["id"]: e for e in FIXTURE_DEFECTS}, proves=prover)
    check(st == "red" and "REACHES" in why, "real:<id> + a green re-run at a docs-only commit after the red -> refused "
          "(%s)" % why[:90])
    # real:<id> — a REGISTERED fact, proved
    st, why = E.judge([red, vrec("real:NOT-REGISTERED-9 fixed", T % "11:00")])
    check(st == "red" and "not in the defect registry" in why, "real:<id> not in the registry -> refused (%s)" % why[:90])
    st, why = E.judge([red, fixpass, vrec("real:OTHER-ROW-1 fixed", T % "11:00")])
    check(st == "red" and "registered for" in why, "real:<id> registered for another row -> refused (%s)" % why[:90])
    st, why = E.judge([red, fixpass, vrec("real:RETIRED-1 fixed", T % "11:00")])
    check(st == "red" and "RETIRED" in why, "real:<a retired id> -> refused (%s)" % why[:80])
    st, why = E.judge([red, vrec("real:VIEWS-XID-1 fixed in abc123", T % "11:00")])
    check(st == "red" and "naming a ticket clears nothing" in why,
          "real:<registered id> with no proof (no PASS at a later sha that reaches the row, no red on the base) -> refused (%s)" % why[:90])
    st, why = E.judge([red, rec("PASS", T % "10:30", tip=RED_SHA), vrec("real:VIEWS-XID-1", T % "11:00")])
    check(st == "red" and "REACHES" in why, "F1: real:<registered id> + a PASS at the red's OWN sha -> refused (the "
          "same code passing once proves no fix) (%s)" % why[:80])
    st, why = E.judge([red, fixpass, vrec("real:VIEWS-XID-1", T % "11:00")])
    check(st == "green", "real:<registered id> + its row PASS at a LATER sha than the red -> green (%s: %s)" % (st, why[:70]))
    st, why = E.judge([rec("CRASH", T % "10:00", status="Exception: SegFault"), vrec("real:VIEWS-XID-1", T % "11:00")],
                      base=[rec("CRASH", T % "09:00", retry=True, status="Exception: Child aborted")])
    check(st == "red" and "not the SAME red" in why, "KNOWN RED with no failLine also needs the same status text "
          "(SegFault vs abort) (%s)" % why[:80])
    st, why = E.judge([rec("FAIL", T % "10:00", failLine="FAIL: crash at frame 12"),
                       vrec("real:VIEWS-XID-1", T % "11:00")], base=[rec("CRASH", T % "09:00", retry=True)])
    check(st == "red" and "not the SAME red" in why, "KNOWN RED needs the SAME verdict class on the base: a base CRASH "
          "does not make a lane FAIL known (%s)" % why[:90])
    st, why = E.judge([rec("FAIL", T % "10:00", failLine="FAIL: thumbnail 12 px off"), vrec("real:VIEWS-XID-1", T % "11:00")],
                      base=[rec("FAIL", T % "09:00", retry=True, failLine="FAIL: the panel has 3 rows")])
    check(st == "red" and "not the SAME red" in why, "...and the same masked failLine: another assertion is not known (%s)"
          % why[:80])
    st, why = E.judge([rec("FAIL", T % "10:00", failLine="FAIL: thumbnail 12 px off"), vrec("real:VIEWS-XID-1", T % "11:00")],
                      base=[rec("FAIL", T % "09:00", retry=True, failLine="FAIL: thumbnail 14 px off")])
    check(st == "known", "...the same class and failLine (numbers masked) on the base -> KNOWN RED (%s)" % why[:70])
    # TESTING-CLEANUP-2C item 1: THE TREE ROOT IS NOT THE FAILURE. Batch C2 refused three KNOWN RED verdicts whose lines
    # differed only by the tree root (rc-batch-C2 vs rc-base-d4991c754). RED ON 314770a52: "not the SAME red".
    W_ = "/home/jahshaka/Developer/jahshaka/.claude/worktrees/"
    msg = "FAIL: the data root and the working directory must be inside %s/build-linux/tests/scripting/e2e-home-clip_ref"
    st, why = E.judge([rec("FAIL", T % "10:00", failLine=msg % (W_ + "rc-batch-C2")), vrec("real:VIEWS-XID-1", T % "11:00")],
                      base=[rec("FAIL", T % "09:00", retry=True, failLine=msg % ("/mnt/work/Developer/jahshaka/.claude/worktrees/"
                                                                                      "rc-base-d4991c754"))])
    check(st == "known", "the same failure under two tree roots (rc-batch-C2 / rc-base-<sha>, two spellings of the root) "
          "is the SAME red -> KNOWN RED (%s: %s)" % (st, why[:80]))
    st, why = E.judge([rec("FAIL", T % "10:00", failLine=msg % (W_ + "rc-batch-C2")), vrec("real:VIEWS-XID-1", T % "11:00")],
                      base=[rec("FAIL", T % "09:00", retry=True,
                                failLine=(msg % (W_ + "rc-base-d4991c754")).replace("clip_ref", "tex_ref"))])
    check(st == "red" and "not the SAME red" in why, "...a different file INSIDE the tree stays a different red (%s)" % why[:70])
    check(E.cgc.masked("open /tmp/jah-lead/rc-C2.state: /tmp/a/.local/share/Jahshaka/x.db") ==
          E.cgc.masked("open /tmp/jah-rc9/rc-C2.state: /home/u/.local/share/Jahshaka/x.db"),
          "/tmp/jah-* and a data root are one token each (%s)" % E.cgc.masked("open /tmp/jah-lead/rc-C2.state"))
    st, why = E.judge([red, vrec("real:VIEWS-XID-1", T % "11:00")], base=base_solo)
    check(st == "known" and "KNOWN RED" in why, "real:<registered id> + the red reproduced by a solo at the base -> KNOWN "
          "RED (%s: %s)" % (st, why[:70]))
    nd = {e["id"]: e for e in FIXTURE_DEFECTS + [NONDET]}
    ok3 = [rec("PASS", T % ("10:%02d" % (10 + i)), retry=True) for i in range(3)]
    st, why = E.judge([red] + ok3 + [vrec("real:NONDET-FIXTURE-1 enrolled", T % "11:00")], defects=nd)
    check(st == "nondet" and "3/3" in why, "real:<an enrolled nondeterminism id> + 3/3 solo -> cleared as nondeterminism "
          "(%s)" % why[:80])
    tip = E.tip
    nr = defect("NOTREPRO-1", ["photon.view"], "nondeterminism", uses=1, suspects=["lane-x"], census={"other_ctests": 1},
                first_seen={"tip": tip[:9], "pin": "0" * 9, "run": "20261009T120000-" + tip[:9]})
    one = {"NOTREPRO-1": nr}
    st, why = E.cgc.judge(("photon.view", None), [red] + ok3 + [vrec("real:NOTREPRO-1", T % "11:00")], one, tip_sha=tip)
    check(st == "nondet", "a SINGLE-USE NOT REPRODUCED entry clears its own merge's red by 3/3 (%s)" % why[:70])
    st, why = E.cgc.judge(("photon.view", None), [red] + ok3 + [vrec("real:NOTREPRO-1", T % "11:00")], one, tip_sha=tip,
                          mode="push")
    check(st == "red" and "never a push" in why, "F4: ...never a push, even at the sha that registered it (%s)" % why[:70])
    st, why = E.cgc.judge(("photon.view", None), [red] + ok3 + [vrec("real:NOTREPRO-1", T % "11:00")], one, tip_sha="b" * 40)
    check(st == "red" and "SINGLE-USE" in why, "...and never a red at another tip (a later merge, a push) (%s)" % why[:80])
    check(not E.rl.contention_of(one), "...and it is never the contention class (only an ENROLLED entry is)")
    old = {"OLD-1": defect("OLD-1", ["photon.view"], recheck="2026-01-01")}
    st, why = E.cgc.judge(("photon.view", None), [red, fixpass, vrec("real:OLD-1", T % "11:00")], old)
    check(st == "red" and "past its recheck" in why, "an entry past its recheck DATE is refused until re-verdicted (%s)"
          % why[:80])
    oldnd = {"OLD-2": defect("OLD-2", ["photon.view"], "nondeterminism", recheck="2026-01-01")}
    check(not E.rl.contention_of(oldnd), "...and a nondeterminism entry past its recheck leaves the contention class")
    st, why = E.judge([red, vrec("the reader looked at it and it is fine", T % "11:00")])
    check(st == "red" and why.startswith("VERDICT REFUSED") and "nothing accepts prose" in why,
          "plain text -> VERDICT REFUSED, nothing accepts prose (%s)" % why[:110])
    for word in ("environmental: beside four gates", "ENOSPC on /tmp", "the display died under it"):
        st, why = E.judge([red, fixpass, vrec("real:VIEWS-XID-1 — " + word, T % "11:00")])
        check(st == "red" and "never verdicts" in why, "'%s' is never a verdict, even beside a token (%s)" % (word, why[:60]))
    solos = [rec(v, T % ("10:%02d" % (10 + i)), retry=True) for i, v in enumerate(("PASS", "FAIL", "PASS"))]
    st, why = E.judge([red, fixpass] + solos + [vrec("real:VIEWS-XID-1", T % "11:00")])
    check(st == "red" and "solos below 3/3" in why, "FAIL + a token, 2/3 solo -> red 'solos below 3/3' (%s)" % why[:100])
    # contention: — a nondeterminism row, a competitor census, 3/3
    busy = rec("FAIL", T % "10:00", box=BUSY)
    st, why = E.judge([busy] + ok3 + [vrec("contention: load 14 beside two gates", T % "11:00")])
    check(st == "red" and "has none" in why, "contention: on a row with no nondeterminism entry -> refused (%s)" % why[:90])
    st, why = E.judge([red] + ok3 + [vrec("contention: load 14 beside two gates", T % "11:00")], listed=True)
    check(st == "red" and "COMPETITOR CENSUS" in why, "contention: on a listed row whose red has no census -> refused (%s)"
          % why[:80])
    own = rec("FAIL", T % "10:00", box={"queue_depth": 3, "census": {"gpu_apps": [], "other_ctests": 0, "builds": 0,
                                                                   "psi10_mem": 0.0, "psi10_io": 0.0}}, drain_s=40.0)
    st, why = E.judge([own] + ok3 + [vrec("contention: the queue and the drain", T % "11:00")], listed=True)
    check(st == "red" and "COMPETITOR CENSUS" in why, "...the gate's own queue and drain are never competition (%s)" % why[:70])
    desk = rec("FAIL", T % "10:00", box={"census": {"gpu_apps": [{"pid": 9, "name": "chrome", "mib": 200, "ours": False}],
                                                  "gpu_competitors": [], "other_ctests": 0, "builds": 0,
                                                  "psi10_mem": 0.0, "psi10_io": 0.0}})
    st, why = E.judge([desk] + ok3 + [vrec("contention: the desktop", T % "11:00")], listed=True)
    check(st == "red" and "COMPETITOR CENSUS" in why, "F3: the idle desktop's GPU clients (in the baseline) are never "
          "competition (%s)" % why[:60])
    for name, c in (("a GPU process outside the gate", {"gpu_competitors": [{"pid": 7, "name": "Jahshaka", "mib": 2800}]}),
                    ("sibling ctests", {"other_ctests": 2}), ("a build", {"builds": 3}), ("memory pressure", {"psi10_mem": 22.0}),
                    ("IO pressure", {"psi10_io": 40.0})):
        r = rec("FAIL", T % "10:00", box={"census": dict({"gpu_apps": [], "other_ctests": 0, "builds": 0, "psi10_mem": 0.0,
                                                          "psi10_io": 0.0}, **c)})
        st, why = E.judge([r] + ok3 + [vrec("contention: measured", T % "11:00")], listed=True)
        check(st == "nondet" and "3/3" in why, "contention: + a listed row + %s in the census + 3/3 solo -> cleared as "
              "nondeterminism" % name)
    st, why = E.judge([busy, vrec("contention: load 14 beside two gates", T % "11:00")], listed=True)
    check(st == "red" and "3/3 solo" in why, "contention: with no solos -> refused (%s)" % why[:100])
    st, _ = E.judge([busy, vrec("contention: load 14 beside two gates", T % "10:05")] + ok3, listed=True)
    check(st == "nondet", "...and once its 3/3 solos run after it, the same verdict clears it (written before them)")
    # LOST / OOM / CRASH
    lost = rec("LOST", T % "14:20", xid=None)
    st, why = E.judge([lost, vrec("environmental: a device loss beside four gates, no Xid", T % "15:00")])
    check(st == "red" and "never verdicts" in why, "LOST + environmental -> refused (%s)" % why[:100])
    st, why = E.judge([lost, vrec("contention: four gates", T % "15:00")] + [rec("PASS", T % ("14:3%d" % i), retry=True)
                                                                         for i in range(3)], listed=True)
    check(st == "red" and "never contention" in why, "LOST + contention: -> refused (%s)" % why[:90])
    st, why = E.judge([lost, vrec("xid-read:2026-10-09T14:00..14:30 none", T % "15:00")])
    check(st == "red" and "journal was read" in why, "LOST + xid-read: when the record's journal WAS read -> refused (%s)"
          % why[:90])
    lostu = rec("LOST", T % "14:20", xid=None, journal_unreadable=True)
    st, why = E.judge([lostu, vrec("xid-read:2026-10-09T14:00..14:30 none", T % "15:00")])
    check(st == "green" and "xid-read" in why, "LOST + journal_unreadable + xid-read:<a window covering it> -> green (%s)"
          % why[:90])
    st, why = E.judge([lostu, vrec("xid-read:2026-10-09T08:00..08:30 none", T % "15:00")])
    check(st == "red" and "does not cover" in why, "...a window that misses it -> refused (%s)" % why[:100])
    for cls in ("OOM", "CRASH"):
        r = rec(cls, T % "14:20")
        st, why = E.judge([r, vrec("real:BUDGET-7", T % "15:00")])
        check(st == "red" and "naming a ticket" in why, "%s + a registered id with no proof -> refused (%s)" % (cls, why[:60]))
        st, _ = E.judge([r, rec("PASS", T % "14:40"), vrec("real:BUDGET-7", T % "15:00")])
        check(st == "red", "F1: %s + real:<id> + one re-run PASS at the SAME sha -> refused" % cls)
        st, _ = E.judge([dict(r, tip=RED_SHA), rec("PASS", T % "14:40", tip=FIX_SHA), vrec("real:BUDGET-7", T % "15:00")])
        check(st == "green", "%s + real:<registered id> + a PASS at a later sha -> green" % cls)
        st, why = E.judge([r] + [rec("PASS", T % ("14:3%d" % i), retry=True) for i in range(3)]
                          + [vrec("real:NONDET-FIXTURE-1", T % "15:00")], listed=True)
        check(st == "red" and "kind: defect" in why, "F2: %s + real:<a nondeterminism id> + 3/3 -> refused (only a defect "
              "entry) (%s)" % (cls, why[:70]))
    xr = rec("CRASH", T % "14:20", box=BUSY,
             xid={"pid": 4242, "window": "2026-10-09T14:10:00+02:00..2026-10-09T14:20:00+02:00", "lines": ["NVRM: Xid 109"]})
    xok3 = [rec("PASS", T % ("14:3%d" % i), retry=True) for i in range(3)]
    st, why = E.judge([xr, vrec("contention: four gates on the card", T % "15:00")] + xok3, listed=True)
    check(st == "red" and "DEFECT by law" in why, "CRASH + an xid in the record + contention: -> refused (%s)" % why[:80])
    st, why = E.judge([xr, vrec("xid-read:2026-10-09T14:00..14:30 none", T % "15:00")])
    check(st == "red" and "real:<registered id>" in why, "...and xid-read: on it -> refused too (only real:<id>)")
    st, _ = E.judge([xr, rec("PASS", T % "14:40"), vrec("real:VIEWS-XID-1", T % "15:00")])
    check(st == "red", "F1: ...real:<id> + one gate re-run PASS at the Xid's own sha -> refused (the merge read's hole)")
    st, _ = E.judge([dict(xr, tip=RED_SHA), rec("PASS", T % "14:40", tip=FIX_SHA), vrec("real:VIEWS-XID-1", T % "15:00")])
    check(st == "green", "...real:<registered id> + a PASS at a later sha -> green")
    st, why = E.judge([xr] + xok3 + [vrec("real:NONDET-FIXTURE-1", T % "15:00")], listed=True)
    check(st == "red" and "kind: defect" in why, "F2: an Xid red + real:<a nondeterminism id> + 3/3 -> refused (%s)" % why[:70])
    st, why = E.judge([xr] + xok3, listed=True)
    check(st == "red" and "DEFECT" in why, "an xid red on a contention row is not cleared by 3/3 solo (%s)" % why[:80])
    st, why = E.judge([rec("CRASH", T % "14:20", box=BUSY)] + xok3, listed=True)
    check(st == "red" and "never cleared by solos" in why, "a CRASH on a contention row: no solo clearance (%s)" % why[:80])
    for na in ("NOADMIT", "NOTRUN"):
        st, _ = E.judge([rec(na, T % "10:00"), vrec("real:VIEWS-XID-1", T % "11:00")])
        check(st == "missing", "%s + any verdict -> never cleared (missing)" % na)
    # the CLI: VERDICT REFUSED printed; a KNOWN RED passes a merge and refuses a push
    E.fresh()
    E.put(ROWS[:2], "PASS", "2026-01-01T10:00:00")
    E.put(ROWS[2:], "FAIL", "2026-01-01T10:00:01")
    rc, out = E.run("--verdict", "photon.view=looked fine to the reader")
    check(rc == 1 and "VERDICT REFUSED photon.view:" in out, "the CLI prints VERDICT REFUSED <row>: <why> and "
          "refuses (%d)" % rc)
    rc, out = E.run("--verdict", "photon.view=real:FIXTURE-1")
    check(rc == 1 and "VERDICT REFUSED photon.view:" in out, "...real:<id> with no proof -> refused (%d)" % rc)
    E.put(ROWS[2:], "FAIL", "2026-01-01T09:00:00", tip=E.git("rev-parse", "51e9f2c49"), retry=True)
    rc, out = E.run()
    check(rc == 0 and "KNOWN RED photon.view" in out, "...the red reproduced by a recorded solo at the RANGE'S BASE -> "
          "KNOWN RED: the merge passes it (%d)" % rc)
    for mode in ("push", "stage-close"):
        rc, out = E.run("--mode", mode)
        check(rc == 1 and "KNOWN RED" in out, "...and a %s refuses it: never push on a red (%d)" % (mode, rc))
    # F4: a row cleared as NONDETERMINISM must be 3/3 solo green AT THE CANDIDATE for a push (patched judge: every
    # row "nondet", no solo at the tip)
    E.fresh()
    E.put(ROWS, "PASS", "2026-01-01T10:00:00")
    real_judge = E.cgc.judge
    E.cgc.judge = lambda *a, **k: ("nondet", "cleared as nondeterminism (the patched fixture)")
    try:
        ok_m, _ = E.cgc.check(RANGE, E.build, mode="merge")
        ok_p, why_p = E.cgc.check(RANGE, E.build, mode="push")
        for k in range(3): E.put(ROWS, "PASS", "2026-01-01T11:0%d:00" % k, retry=True)
        ok_p3, _ = E.cgc.check(RANGE, E.build, mode="push")
    finally:
        E.cgc.judge = real_judge
    check(ok_m and not ok_p and any("not 3/3 solo green at the candidate" in w for w in why_p) and ok_p3,
          "F4: a nondeterminism-cleared row passes a merge, is re-asked 3/3 at the push candidate (refused with 0/0, "
          "accepted with 3/3) (%s, %s, %s)" % (ok_m, ok_p, ok_p3))
    # ROUND 3 F-A: AN EMPTY COMMIT proves nothing. C1 = a lane commit (51e9f2c49's child carrying 3756b2f18's tree), X =
    # an EMPTY commit on it (`git commit --allow-empty`, made here with commit-tree: objects only, no ref moves): red at
    # C1, green at X -> refused (before: X..C1 read as unreadable -> ALL -> "the fix reached it" -> green, no verdict)
    tree = E.git("rev-parse", "3756b2f18^{tree}")
    env = dict(os.environ, GIT_AUTHOR_NAME="jahshaka", GIT_AUTHOR_EMAIL="jahshaka@gmail.com",
               GIT_COMMITTER_NAME="jahshaka", GIT_COMMITTER_EMAIL="jahshaka@gmail.com")
    c1 = subprocess.run(["git", "commit-tree", tree, "-p", E.git("rev-parse", "51e9f2c49"), "-m", "verdict test C1"],
                        cwd=E.source, capture_output=True, text=True, env=env).stdout.strip()
    x = subprocess.run(["git", "commit-tree", tree, "-p", c1, "-m", "verdict test: an empty commit"], cwd=E.source,
                       capture_output=True, text=True, env=env).stdout.strip()
    E.fresh()
    E.put(ROWS, "PASS", "2026-01-01T09:00:00", tip=c1)
    E.put(ROWS[2:], "FAIL", "2026-01-01T09:30:00", tip=c1)
    E.put(ROWS, "PASS", "2026-01-01T10:00:00", tip=x)
    rc, out = E.run(rng="51e9f2c49..%s" % x)
    check(bool(c1 and x) and rc == 1 and "photon.view" in out and "OPEN red at %s" % c1[:9] in out,
          "F-A: red at C1, a green re-run at an EMPTY commit X on it -> refused, no verdict passes it silently (%d)" % rc)
    # ROUND 4: ONE STRAY FILE proves nothing either — Y = C1 + a root file no rule owns (the selector FALLS BACK): red at
    # C1, green at Y -> refused (before: a fallback read as ALL on the proving side)
    idx = os.path.join(E.scratch, "stray.index")
    genv = dict(env, GIT_INDEX_FILE=idx)
    blob = subprocess.run(["git", "hash-object", "-w", "--stdin"], cwd=E.source, input="stray\n", capture_output=True,
                          text=True).stdout.strip()
    subprocess.run(["git", "read-tree", tree], cwd=E.source, env=genv, check=True)
    subprocess.run(["git", "update-index", "--add", "--cacheinfo", "100644,%s,stray_verdict_test.bin" % blob],
                   cwd=E.source, env=genv, check=True)
    tree2 = subprocess.run(["git", "write-tree"], cwd=E.source, env=genv, capture_output=True, text=True).stdout.strip()
    y = subprocess.run(["git", "commit-tree", tree2, "-p", c1, "-m", "verdict test: one stray file"], cwd=E.source,
                       capture_output=True, text=True, env=env).stdout.strip()
    gs = E.cgc.load_gs()
    R = E.cgc.Reach(gs, E.build, y, "")
    check(R.between(c1, y, unreadable=set()) == set() and R.between(c1, y) is E.cgc.Reach.ALL,
          "a FALLBACK range reaches nothing when proving, everything when re-using")
    prover4 = E.cgc.Prover(gs, E.build, lambda: R)
    st, why = E.cgc.judge(("photon.view", None), [rec("FAIL", T % "09:30", tip={"studio": c1}),
                                                  rec("PASS", T % "10:00", tip={"studio": y}),
                                                  vrec("real:FIXTURE-1", T % "11:00")],
                          {e["id"]: e for e in FIXTURE_DEFECTS}, proves=prover4)
    check(bool(y) and st == "red" and "REACHES" in why, "ROUND 4: red at C1, green at Y (one stray file, the selector "
          "falls back) + real:<id> -> refused (%s)" % why[:80])
    # N5: a lane-tool PASS (tier `lane`) never answers a row for the judge
    E.fresh()
    E.put(ROWS[:2], "PASS", "2026-01-01T10:00:00")
    E.put(ROWS[2:], "PASS", "2026-01-01T10:00:00", tier="lane")
    rc, out = E.run()
    check(rc == 1 and "no record" in out, "a lane-tool PASS (tier lane) does not answer a row the gate never ran (%d)" % rc)
    # the abort's droppedRed: 3/3 solo, whatever else answered it
    E.fresh()
    E.put(ROWS, "PASS", "2026-01-01T10:00:00")
    E.rl.append_records([{"schema": 2, "kind": "abort", "suite": "(abort)", "arm": None, "verdict": "ABORT",
                          "ts": "2026-01-01T10:30:00", "droppedRed": ["photon.view"],
                          "tip": {"studio": E.tip, "studio_dirty": False, "irisgl": E.pin, "irisgl_dirty": False}}],
                        "scoped", E.tip)
    E.put(ROWS[2:], "PASS", "2026-01-01T10:40:00")
    rc, out = E.run()
    check(rc == 1 and "DROPPED RED" in out, "a row the abort dropped RED stays red after a quiet PASS (%d)" % rc)
    for k in range(3): E.put(ROWS[2:], "PASS", "2026-01-01T10:5%d:00" % k, retry=True)
    rc, out = E.run()
    check(rc == 0, "...until 3/3 solo PASS after the abort (%d)" % rc)
    # overrides: the push refuses a candidate whose records carry any
    E.fresh()
    E.put(ROWS, "PASS", "2026-01-01T10:00:00", overrides=["JAH_GATE_SLOT=0"])
    rc, out = E.run()
    rc2, out2 = E.run("--mode", "push")
    check(rc == 0 and rc2 == 1 and "OVERRIDES" in out2, "a merge reads overrides; a push refuses a candidate carrying "
          "any (%d, %d)" % (rc, rc2))


def registry_id_cases(E):
    """TESTING-CLEANUP-2B item 14. (1) `real:<id>` is any token and the REGISTRY is the grammar: an id the attribution
    registered with lowercase, dots and no number reaches the registry lookup. RED ON BASE (b0a3b1f3c): the pattern
    ([A-Z…]-<n>) matched nothing in `real:A2-defect-scripting.e2e.clip_ref`, so the verdict was prose and refused.
    (2) The attribution's ids are ONE scheme, <BATCH>-<ROW-UPPER>-<n>, and an OPEN entry of the same kind for the row is
    REUSED (the new sighting a recheck), never a second entry. RED ON BASE: `B2-defect-<row>` beside `A2-defect-<row>`."""
    m = E.cgc.DEFECT_ID.search("real:A2-defect-scripting.e2e.clip_ref the fixture's red")
    check(m and m.group(1) == "A2-defect-scripting.e2e.clip_ref", "real:<any token> is parsed whole (%s)" % (m and m.group(1)))
    ok, why, _ = E.cgc._real(m, "scripting.e2e.clip_ref", [], [], {}, [], [])
    check(not ok and "not in the defect registry" in why, "...and LOOKED UP in the registry: unknown there, refused there")
    reg_e = {"A2-defect-scripting.e2e.clip_ref": {"id": "A2-defect-scripting.e2e.clip_ref", "rows": ["other.row"],
                                                  "state": "open", "recheck": "2099-12-31"}}
    ok, why, _ = E.cgc._real(m, "scripting.e2e.clip_ref", [], [], reg_e, [], [])
    check(not ok and "registered for ['other.row']" in why, "...a registered id reaches the entry's own checks (%s)" % why[:70])
    gs = E.cgc.load_gs()
    d = tempfile.mkdtemp(dir=E.scratch)
    old = {k: os.environ.get(k) for k in ("JAH_DEFECTS_FILE", "JAH_DEFECTS_PENDING_DIR")}
    os.environ["JAH_DEFECTS_FILE"] = os.path.join(d, "defects.json")
    os.environ.pop("JAH_DEFECTS_PENDING_DIR", None)
    json.dump({"defects": []}, open(os.environ["JAH_DEFECTS_FILE"], "w"))
    try:
        rec_ = {"tip": {"fork": "f" * 40}, "run": "20261010T100000-" + "a" * 9}
        p1 = gs._register("A2", "defect", "scripting.e2e.clip_ref", "a" * 40, rec_, "red on d-build")
        b1 = open(p1, "rb").read()
        p2 = gs._register("B2", "defect", "scripting.e2e.clip_ref", "b" * 40, dict(rec_, run="20261010T120000-" + "b" * 9),
                          "red on d-build again")
        p3 = gs._register("B2", "defect", "pool.editor_view", "b" * 40, rec_, "another row")
        pend = os.path.join(d, "defects.pending")
        files = sorted(os.listdir(pend))
        rc1 = json.load(open(p2))
        check(os.path.basename(p1) == "A2-SCRIPTING-E2E-CLIP-REF-1.json"
              and os.path.basename(p2) == "A2-SCRIPTING-E2E-CLIP-REF-1.recheck.json"
              and files == ["A2-SCRIPTING-E2E-CLIP-REF-1.json", "A2-SCRIPTING-E2E-CLIP-REF-1.recheck.json",
                            "B2-POOL-EDITOR-VIEW-1.json"]
              and [r["batch"] for r in rc1.get("sightings", [])] == ["B2"] and os.path.basename(p3).startswith("B2-POOL"),
              "the attribution's ids are <BATCH>-<ROW-UPPER>-<n>, and an open entry for the row is reused — the new "
              "sighting a RECHECK FILE, never a second entry (%s)" % files)
        # TESTING-CLEANUP-2B item 15 (2): REUSE NEVER REWRITES AN ENTRY (the pending entry and testing/defects.json are
        # both tracked; a sighting that moved `recheck` forward stood in for the re-verdict the door demands). RED ON
        # 74740b0de: the entry was re-serialised with rechecks[] and a later recheck.
        check(open(p1, "rb").read() == b1, "...the reused PENDING entry's bytes are untouched (its recheck the lead's)")
        got, why = E.rl.defects_load()
        check(got is not None and "A2-SCRIPTING-E2E-CLIP-REF-1" in got
              and [x["batch"] for x in got["A2-SCRIPTING-E2E-CLIP-REF-1"].get("sightings", [])] == ["B2"]
              and not any(k.endswith(".recheck") for k in got) and not os.path.isdir(os.path.join(d, "defects.quarantine")),
              "...the reader loads the entry with the recheck file's sightings attached, never the file as an entry (%s)" % why)
        # registry-sourced reuse: an OPEN entry in defects.json PAST its recheck — the sighting is a recheck file, the
        # tracked registry's bytes unchanged, and the entry is still past its date (the door still wants the re-verdict)
        regd = {"defects": [
            {"id": "SC1-OLD-ROW-1", "rows": ["old.row"], "kind": "defect", "cause": "c", "state": "open",
             "found_by": "gate", "first_seen": {"tip": "e" * 40, "pin": "f" * 40, "run": "20260101T000000-" + "e" * 9}, "recheck": "2026-01-01",
             "expires": "2026-01-01"},
            {"id": "SC1-FIXED-ROW-1", "rows": ["fixed.row"], "kind": "defect", "cause": "c",
             "state": {"fixed": {"tip": "f" * 40}}, "found_by": "gate", "first_seen": {"tip": "e" * 40, "pin": "f" * 40, "run": "20260101T000000-" + "e" * 9},
             "recheck": "2099-01-01", "expires": "2099-01-01"},
            {"id": "SC1-RET-ROW-1", "rows": ["ret.row"], "kind": "defect", "cause": "c", "state": "retired",
             "found_by": "gate", "first_seen": {"tip": "e" * 40, "pin": "f" * 40, "run": "20260101T000000-" + "e" * 9}, "recheck": "2099-01-01",
             "expires": "2099-01-01"}]}
        json.dump(regd, open(os.environ["JAH_DEFECTS_FILE"], "w"), indent=1)
        rb = open(os.environ["JAH_DEFECTS_FILE"], "rb").read()
        p4 = gs._register("C3", "defect", "old.row", "c" * 40, rec_, "seen again")
        check(os.path.basename(p4) == "SC1-OLD-ROW-1.recheck.json"
              and open(os.environ["JAH_DEFECTS_FILE"], "rb").read() == rb,
              "a registry-sourced reuse writes <id>.recheck.json and leaves the tracked defects.json byte-identical (%s)"
              % os.path.basename(p4))
        got, _ = E.rl.defects_load()
        check(got and E.rl.recheck_past(got["SC1-OLD-ROW-1"]) and len(got["SC1-OLD-ROW-1"].get("sightings", [])) == 1,
              "...and the entry stays PAST its recheck: a sighting never stands in for the lead's re-verdict")
        p5 = gs._register("C3", "defect", "old.row", "c" * 40, rec_, "and again")
        check(p5 == p4 and len(json.load(open(p5))["sightings"]) == 2, "a second sighting APPENDS to the same recheck file")
        # fixed / retired: never reused — a new sighting is a NEW finding
        p6 = gs._register("C3", "defect", "fixed.row", "c" * 40, rec_, "back")
        p7 = gs._register("C3", "defect", "ret.row", "c" * 40, rec_, "back")
        check(os.path.basename(p6) == "C3-FIXED-ROW-1.json" and os.path.basename(p7) == "C3-RET-ROW-1.json",
              "a FIXED or RETIRED entry is never reused: a new entry each (%s, %s)"
              % (os.path.basename(p6), os.path.basename(p7)))
        # the -2 increment: the -1 of the stem exists but is not reusable (another kind) -> -2
        p8 = gs._register("C3", "combination", "fixed.row", "c" * 40, rec_, "a combination")
        check(os.path.basename(p8) == "C3-FIXED-ROW-2.json", "the next free number of the stem: -2 (%s)" % os.path.basename(p8))
        # NOT REPRODUCED is never reused: two sightings, two single-use entries
        n1 = gs._register("C3", "nondeterminism", "nd.row", "c" * 40, rec_, "flake", suspects=["l1"])
        n2 = gs._register("C3", "nondeterminism", "nd.row", "c" * 40, rec_, "flake", suspects=["l1"])
        check(os.path.basename(n1) == "C3-ND-ROW-1.json" and os.path.basename(n2) == "C3-ND-ROW-2.json"
              and json.load(open(n2)).get("uses") == 1,
              "a NOT REPRODUCED entry is never reused (single-use): %s, %s" % (os.path.basename(n1), os.path.basename(n2)))
        # (4) THE NUMBERING LOCK: eight registrations of one new row at once (eight processes) take eight
        # DIFFERENT numbers... of one entry: the first registers it, the other seven are its recheck sightings
        code = ("import importlib.util,sys; sp=importlib.util.spec_from_file_location('gsx', sys.argv[1]); "
                "g=importlib.util.module_from_spec(sp); sp.loader.exec_module(g); "
                "print(g._register('D4', sys.argv[2], 'race.row', 'd'*40, {}, 'race'))")
        gsp = os.path.join(E.source, "scripts", "gate-scope.py")
        ps = [subprocess.Popen([sys.executable, "-c", code, gsp, "defect"], stdout=subprocess.PIPE, text=True)
              for _ in range(8)]
        outs = [os.path.basename(p_.communicate()[0].strip()) for p_ in ps]
        rcd = json.load(open(os.path.join(pend, "D4-RACE-ROW-1.recheck.json")))
        check(sorted(outs).count("D4-RACE-ROW-1.json") == 1 and outs.count("D4-RACE-ROW-1.recheck.json") == 7
              and len(rcd["sightings"]) == 7 and not os.path.exists(os.path.join(pend, "D4-RACE-ROW-2.json")),
              "eight registrations at once: ONE entry and seven sightings, no second number, no lost write (%s)"
              % sorted(set(outs)))
        ps = [subprocess.Popen([sys.executable, "-c", code, gsp, "nondeterminism"], stdout=subprocess.PIPE, text=True)
              for _ in range(6)]
        outs = sorted(os.path.basename(p_.communicate()[0].strip()) for p_ in ps)
        check(outs == ["D4-RACE-ROW-%d.json" % k for k in (2, 3, 4, 5, 6, 7)],
              "six single-use registrations at once take six DIFFERENT numbers (%s)" % outs)
    finally:
        for k, v in old.items():
            if v is None: os.environ.pop(k, None)
            else: os.environ[k] = v


def case_noadmit_pool(E):
    os.environ["JAH_RUN_LOG_DIR"] = d = tempfile.mkdtemp(dir=E.scratch)
    R = E.rl._Run("scoped", "verdict-test", 2, None, None, None, False, {}, False, None)
    R.sampler.stop()
    allna = "ARM toy.a NOADMIT 0\nARM toy.b NOADMIT 0\nARM toy.c NOADMIT 0\n"
    recs = R.records("pool.toy", "Failed", 1.0, time.time(), (0, 0, 0), allna, {})
    check(recs[0]["verdict"] == "NOADMIT" and all(r["verdict"] == "NOADMIT" for r in recs[1:]),
          "an all-NOADMIT pool records NOADMIT (never ran), never FAIL (%s)" % [r["verdict"] for r in recs])
    # TESTING-CLEANUP-2B item 2: no row's TIMEOUT covers the admission wait any more, so a row whose TIMEOUT ends
    # while it is STILL WAITING for its tokens never ran: NOADMIT, never a TIMEOUT charged to its code; a row that
    # was admitted and then ran out of time stays TIMEOUT. RED ON BASE (b0a3b1f3c): the first read TIMEOUT.
    waiting = "vram: waiting for 2 tokens, 0 free — toy.row:app\n"
    r0 = R.records("toy.row", "Timeout", 120.0, time.time(), (0, 0, 0), waiting, {})[0]
    check(r0["verdict"] == "NOADMIT" and "never ran" in r0["status"],
          "a row whose TIMEOUT ended its admission wait records NOADMIT (%s: %s)" % (r0["verdict"], r0["status"][:80]))
    r1 = R.records("toy.row", "Timeout", 120.0, time.time(), (0, 0, 0),
                   waiting + "vram: admitted with 2 tokens 0,1 after 30.0 s — toy.row:app\nrunning\n", {})[0]
    check(r1["verdict"] == "TIMEOUT" and r1.get("tokenWaitS") == 30.0,
          "...an admitted row that then ran out of time is its own TIMEOUT, the wait recorded (%s, %s)"
          % (r1["verdict"], r1.get("tokenWaitS")))
    mixed = "ARM toy.a PASS 10\nARM toy.b NOADMIT 0\nARM toy.c NOADMIT 0\n"
    recs = R.records("pool.toy", "Failed", 1.0, time.time(), (0, 0, 0), mixed, {})
    check(recs[0]["verdict"] == "NOADMIT" and recs[0].get("noadmit_arms") == ["toy.b", "toy.c"],
          "a pool whose only non-PASS arms never ran is NOADMIT, its never-ran arms named (TESTING-CLEANUP-2B F-C1; "
          "was FAIL) (%s, %s)" % (recs[0]["verdict"], recs[0].get("noadmit_arms")))
    realmix = "ARM toy.a FAIL 10\nARM toy.b NOADMIT 0\n"
    recs = R.records("pool.toy", "Failed", 1.0, time.time(), (0, 0, 0), realmix, {})
    check(recs[0]["verdict"] == "FAIL" and recs[0].get("noadmit_arms") == ["toy.b"],
          "a pool with a REAL red beside a NOADMIT arm stays FAIL, its NOADMIT arm named (%s, %s)"
          % (recs[0]["verdict"], recs[0].get("noadmit_arms")))
    # the re-queue (GATE-COST-1 P5) treats it as a never-ran row: re-run once; EVERY try is a record (GATE-COST-2 #2:
    # requeued 0 and 1), each NOADMIT — never-ran to the judge
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
    check(rc != 0 and [(r["verdict"], r.get("requeued")) for r in row] == [("NOADMIT", 0), ("NOADMIT", 1)],
          "...re-queued as a never-ran row, one NOADMIT record per try (rc %s, %s)"
          % (rc, [(r["verdict"], r.get("requeued")) for r in row]))
    # TESTING-CLEANUP-2B delta F-C1: a pool whose ONLY non-PASS arms never ran is re-queued like a row (red on base:
    # recorded FAIL once, never re-run); a pool with a real FAIL beside a NOADMIT arm stays the red it is
    for f_ in os.listdir(d): os.unlink(os.path.join(d, f_))
    for name, second, want in (("pool.part", "PASS 9", [("NOADMIT", 0), ("NOADMIT", 1)]),     # final try: MISSING
                               ("pool.real", "FAIL 9", [("FAIL", None)])):
        open(fake, "w").write("#!/bin/sh\n"
                              "echo '      Start  1: %s'\n"
                              "echo '1: ARM toy.a %s'\n"
                              "echo '1: ARM toy.b NOADMIT 0'\n"
                              "echo '1/1 Test  #1: %s ..........***Failed    0.10 sec'\n" % (name, second, name))
        os.environ["JAH_GATE_REQUEUE"] = "1"
        try:
            E.rl.run_ctest(fake, E.scratch, "scoped", "verdict-test", 1, labels={name: ["app"]}, echo=False)
        finally:
            del os.environ["JAH_GATE_REQUEUE"]
        got = [json.loads(l) for f in os.listdir(d) for l in open(os.path.join(d, f))]
        row = [(r["verdict"], r.get("requeued")) for r in got if r["suite"] == name and r["arm"] is None]
        check(row == want, "%s (toy.a %s, toy.b NOADMIT): %s (%s)" % (name, second.split()[0], want, row))


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
    check(rc == 1 and "OPEN RED carried" in out, "ROUND 3 (U4): ...a green re-run at B does NOT answer a red at a "
          "REBASED-AWAY tip (A is not B's ancestor: no proof the fix reached it) (%d)" % rc)
    # ...but at a tip that DESCENDS from the red's and whose range REACHES the row, the re-run answers it
    t0 = E.git("rev-parse", "51e9f2c49^")
    E.fresh()
    E.put(ROWS, "PASS", "2026-01-01T10:00:00")
    E.put(["photon.view"], "FAIL", "2026-01-01T09:00:00", tip=t0)
    rc, out = E.run()
    check(rc == 0 and "OPEN RED" not in out, "a carried red at an ANCESTOR whose range to the tip reaches the row is "
          "answered by the tip's later green (%d)" % rc)
    # ROUND 2: a lane tip that DESCENDS from the checked tip (a5ab3a057 descends from 3756b2f18) holds a red the tip's
    # older code never had to answer — the tip's PASS is pre-bug and clears nothing
    E.fresh()
    E.put(ROWS + ["test_engine"], "PASS", "2026-01-01T11:00:00")
    E.put(["test_engine"], "FAIL", "2026-01-01T09:00:00", tip=E.git("rev-parse", "a5ab3a057"))
    rc, out = E.run()
    check(rc == 1 and "OPEN RED carried" in out, "a red at a lane tip that DESCENDS from the checked tip is not cleared by "
          "the tip's (pre-bug) PASS (%d)" % rc)
    E.fresh()
    E.put(ROWS, "PASS", "2026-01-01T10:00:00")
    E.put(["test_engine"], "FAIL", "2026-01-01T09:00:00", tip=A)
    E.put(["test_engine"], "FAIL", "2026-01-01T08:00:00", tip=E.git("rev-parse", "51e9f2c49"), retry=True)
    rc, out = E.run("--verdict", "test_engine=real:FIXTURE-1 the old tip's red, reproduced on the base")
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
    E.put(["test_engine"], "FAIL", "2026-01-01T09:00:00", tip=A, lanes=["lane-x"])
    rc, out = E.run()
    check(rc == 1 and "OPEN RED carried" in out, "`lanes == [lane-x]` carries (%d)"
          % rc)
    for name, kw in (("a batch's record (lanes [lane-y, lane-x])", {"lanes": ["lane-y", "lane-x"]}),
                     ("a record with a `batch` tag", {"lanes": ["lane-x"], "batch": "batch-a"})):
        E.fresh()
        E.put(ROWS, "PASS", "2026-01-01T10:00:00")
        E.put(["test_engine"], "FAIL", "2026-01-01T09:00:00", tip=A, lane=None, **kw)
        rc, out = E.run()
        check(rc == 0 and "OPEN RED" not in out, "%s never carries to a lane (%d)" % (name, rc))
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
    """THE REGISTRY'S SHAPE (TESTING_V3 §1.5) — a malformed entry is QUARANTINED, the rest loads (one bad file never
    disables the door); the contention class is the enrolled nondeterminism subset."""
    import shutil
    E.fresh()
    E.put(ROWS[:2], "PASS", "2026-01-01T10:00:00")
    E.put(ROWS[2:], "FAIL", "2026-01-01T10:00:01")
    E.put(ROWS[2:], "FAIL", "2026-01-01T09:00:00", tip=E.git("rev-parse", "51e9f2c49"), retry=True)   # KNOWN on the base
    good = defect("GOOD-1", ["photon.view"])
    q = lambda: E.rl.defects_quarantine_dir()
    cases = [("missing `%s`" % m, m, {k: v for k, v in defect("SHAPE-1", ["photon.view"]).items() if k != m})
             for m in ("first_seen", "recheck", "kind", "found_by", "rows", "cause", "state")]
    cases += [("a kind outside the five", "kind", defect("SHAPE-1", ["x"], "flake")),
              ("a state outside open|fixed{tip}|retired", "state", defect("SHAPE-1", ["x"], state="closed")),
              ("first_seen.run without a date", "first_seen", defect("SHAPE-1", ["x"], first_seen={"tip": "a", "pin": "b",
                                                                                                    "run": "last week"})),
              ("found_by outside read|gate|owner|lane", "found_by", defect("SHAPE-1", ["x"], found_by="agent")),
              ("a recheck that is not a DATE", "recheck", defect("SHAPE-1", ["x"], recheck="after P1 lands")),
              ("a nondeterminism entry neither single-use nor enrolled", "enrolled",
               defect("SHAPE-1", ["x"], "nondeterminism", enrolled=None)),
              ("a NOT REPRODUCED entry without suspects / census", "suspects", defect("SHAPE-1", ["x"], "nondeterminism",
                                                                                      uses=1))]
    for name, word, bad in cases:
        shutil.rmtree(q(), ignore_errors=True)
        E.registry([bad, good])
        rc, out = E.run("--verdict", "photon.view=real:GOOD-1")
        files = os.listdir(q()) if os.path.isdir(q()) else []
        check(rc == 0 and "REGISTRY:" in out and "quarantined" in out and word in out and any(f.endswith(".why") for f in files),
              "%s -> QUARANTINED (printed, a .why sidecar), the rest loads and the door works (%d, %s)" % (name, rc, files))
    shutil.rmtree(q(), ignore_errors=True)
    E.registry([defect("DUP-1", ["x"]), defect("DUP-1", ["y"]), good])
    d, _ = E.rl.defects_load(log=None)
    check(d is not None and "DUP-1" in d and d["DUP-1"]["rows"] == ["x"] and "GOOD-1" in d,
          "a duplicate id: the second is quarantined, the first and the rest load")
    # BATCH-GATE-1's pending entries (testing/defects.pending/<id>.json): ingested; a bad one MOVED to quarantine
    shutil.rmtree(q(), ignore_errors=True)
    E.registry([good, NONDET])
    pend = E.rl.defects_pending_dir()
    os.makedirs(pend, exist_ok=True)
    json.dump(defect("PENDING-1", ["api.contract"]), open(os.path.join(pend, "PENDING-1.json"), "w"))
    json.dump(defect("PENDING-2", ["api.contract"], kind="flake"), open(os.path.join(pend, "PENDING-2.json"), "w"))
    open(os.path.join(pend, "PENDING-3.json"), "w").write("{ not json")
    json.dump(defect("GOOD-1", ["api.contract"]), open(os.path.join(pend, "PENDING-4.json"), "w"))
    import io
    buf = io.StringIO()
    d, _ = E.rl.defects_load(log=buf)
    left = sorted(os.listdir(pend))
    qd = sorted(os.listdir(q()))
    check(d is not None and "PENDING-1" in d and "GOOD-1" in d and d["GOOD-1"]["rows"] == ["photon.view"]
          and "PENDING-2" not in d, "a good pending entry is ingested; a bad one, an unreadable one and a colliding id are "
          "not (%s)" % sorted(d or {}))
    check(left == ["PENDING-1.json"] and {"PENDING-2.json", "PENDING-3.json", "PENDING-4.json"} <= set(qd)
          and "REGISTRY: " in buf.getvalue() and "FINDING" in buf.getvalue(),
          "...the bad pending files are MOVED to defects.quarantine/ with their .why, printed, and a FINDING line (%s / %s)"
          % (left, qd))
    check(len(E.rl.defects_quarantined()) == 3, "defects_quarantined() lists them for the lead's status (%d)"
          % len(E.rl.defects_quarantined()))
    shutil.rmtree(pend); shutil.rmtree(q(), ignore_errors=True)
    path = os.path.join(E.scratch, "broken.json")
    open(path, "w").write("{ not json")
    os.environ["JAH_DEFECTS_FILE"] = path
    rc, out = E.run()
    check(rc == 2 and "defect registry" in out, "the registry FILE itself unreadable -> the judge is unusable, exit 2 (%d)" % rc)
    E.registry([defect("SHAPE-1", ["photon.view"], state={"fixed": {"tip": "abc"}}), NONDET])
    cl = E.rl.contention_list()
    check(list(cl or {}) == ["photon.view"] and "NONDET-FIXTURE-1" in cl["photon.view"],
          "the contention class = the enrolled open nondeterminism entries (%s)" % cl)
    live = os.path.join(E.rl.workspace_root(), "testing")
    for f in ("contention.json", "defects.json"):
        print("  (the workspace's testing/%s: %s)" % (f, "present" if os.path.exists(os.path.join(live, f)) else "absent"))


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
    cen = (one.get("box") or {}).get("census") or {}
    check(set(cen) == {"gpu_apps", "gpu_competitors", "baseline", "other_ctests", "builds", "psi10_mem", "psi10_io"}
          and (not sys.platform.startswith("linux") or isinstance(cen["builds"], int)),
          "box.census = the competitor census {gpu_apps, gpu_competitors (ours / off the idle baseline / over the floor), "
          "baseline, other_ctests, builds (outside the gate), psi10_mem, psi10_io} (%s)"
          % {k: (v if k != "gpu_apps" else (len(v) if v is not None else None)) for k, v in cen.items()})
    # ROUND 2: whose GPU process and whose build — our trees by path, an unreadable exe never counted, a gate in the
    # MAIN tree still sees every sibling worktree's build
    m = E.rl.main_tree()
    check(E.rl.ours_exe(os.path.join(m, ".claude", "worktrees", "x", "build-linux", "bin", "Jahshaka"))
          and E.rl.ours_exe(os.path.join(m, "build-linux", "tests", "test_engine"))
          and not E.rl.ours_exe(os.path.join(os.path.expanduser("~"), "jahshaka", "bin", "tool"))
          and not E.rl.ours_exe(os.path.join(os.path.dirname(m), "x", "jahshaka", "chrome")),
          "ours = a binary under <main>/.claude/worktrees/ or <main>/build-linux, never a '/jahshaka/' string match")
    real_root, real_apps = E.rl.ROOT, E.rl.gpu_apps
    try:
        E.rl.ROOT = m
        check(E.rl._own_tree(os.path.join(m, "build-linux")) and not E.rl._own_tree(
            os.path.join(m, ".claude", "worktrees", "lane-x", "build-linux")),
              "a gate in the MAIN tree excludes its own builds, never a sibling worktree's")
        E.rl.gpu_apps = lambda *a: [{"pid": 1, "name": "gone", "exe": None, "mib": 900, "ours": False, "unknown": True}]
        c = E.rl.census(None, {"psi10": 0.0, "builds": 0}, 0)
        check(c["gpu_competitors"] == [], "an exe that cannot be read is recorded `unknown` and never counted (%s)"
              % c["gpu_competitors"])
    finally:
        E.rl.ROOT, E.rl.gpu_apps = real_root, real_apps
    check("xid" in one and one["xid"] is None and "journal_unreadable" not in one, "a row with no fault carries xid: null")
    R = E.rl._Run("scoped", "verdict-test", 1, None, None, None, False, {}, False, None)
    R.sampler.stop()
    ur = R.records("s.three", "Failed", 1.0, time.time(), (0, 0, 0), "vram: " + E.kx.FINDING + " (s.three)\nboom", {})
    check(ur[0].get("journal_unreadable") is True and ur[0]["xid"] is None,
          "a row whose journal was unreadable records journal_unreadable (the one case xid-read: may answer)")
    # TESTING-CLEANUP-2B item 8 (H8c's run-log flag): a row whose app took the shutdown watchdog's _Exit(86) carries
    # forced_exit: 86 and the line's why — green or red; a row that ended in order carries no such field.
    # RED ON BASE (b0a3b1f3c): the record had no forced_exit field (KeyError-free None: the check below read None).
    wd = ("shutdown watchdog: background workers still running 5 s after exit — forcing process exit (code 86)")
    funnel = "[2026.10.10-02.11.13.448][    0]app: Error: " + wd       # the log funnel's stderr mirror of the same exit
    one = R.records("s.five", "Passed", 1.0, time.time(), (0, 0, 0), "PASS: ok\n" + funnel + "\n" + wd, {})[0]
    check(one.get("forced_exit") == 86 and "forced_exits" not in one,
          "ONE forced exit printed twice (the funnel's mirror + the plain line) counts once (fix round E; red: 2)")
    fx = R.records("s.four", "Passed", 1.0, time.time(), (0, 0, 0),
                   "PASS: ok\n" + funnel + "\n" + wd + "\n| " + funnel + "\n| " + wd, {})[0]
    check(fx.get("forced_exit") == 86 and "background workers" in (fx.get("forced_exit_why") or "")
          and fx.get("forced_exits") == 2 and fx.get("verdict") == "PASS",
          "a green row whose app forced its exit records forced_exit 86, the why and the count (%s %s %s)"
          % (fx.get("forced_exit"), fx.get("forced_exit_why"), fx.get("forced_exits")))
    check("forced_exit" not in ur[0], "a row whose app ended in order records no forced_exit")
    check((two.get("xid") or {}).get("pid") == xid["pid"] and (two.get("xid") or {}).get("window") == xid["window"],
          "a row whose output carries the Xid records it (%s)" % two.get("xid"))
    # gate-report.py: the table on the toy log, and the preflight's numbers on the archive
    rep = os.path.join(E.source, "scripts", "gate-report.py")
    p = subprocess.run([sys.executable, rep, d, "--ref", "HEAD"], capture_output=True, text=True)
    check(p.returncode == 0 and "TABLE gates" in p.stdout and "slot wait 0.0 h over 1 gate(s)" in p.stdout,
          "gate-report.py prints the weekly table on a toy log, reading its schema-2 fields (%d)" % p.returncode)
    check("== 9. forced exits" in p.stdout and "| forced exits " in p.stdout,
          "gate-report.py counts the forced exits (section 9 and the TABLE line)")
    # the stack read's F2: a stage close (tier `stage-close`) is counted in section 7, and a lane tool's own run
    # (tier `lane`, GATE-COST-2) never enters the gate wall
    d2 = tempfile.mkdtemp(dir=E.scratch)
    def rw(run, tier, ts, secs, suite):
        with open(os.path.join(d2, "2026-10-09-%s-%s.jsonl" % (tier, E.tip[:9])), "a") as f:
            f.write(json.dumps({"schema": 2, "run": run, "tier": tier, "suite": suite, "arm": None, "verdict": "PASS",
                                "ts": ts, "seconds": secs, "retry": False, "lanes": ["x"], "box": {"other_ctests": 0},
                                "tip": {"studio": E.tip, "irisgl": E.pin}}) + "\n")
    rw("sc1", "stage-close", "2026-10-09T10:00:00+02:00", 60, "row.a")
    rw("sc1", "stage-close", "2026-10-09T11:00:00+02:00", 60, "row.b")
    rw("ln1", "lane", "2026-10-09T12:00:00+02:00", 7200, "row.c")
    p = subprocess.run([sys.executable, rep, d2, "--week", "2026-10-09T17:30", "--ref", "HEAD"], capture_output=True,
                       text=True)
    m7 = re.search(r"stage runs (\d+)\s+rows (\d+)", p.stdout)
    mw = re.search(r"summed gate wall \(every non-retry run, rc tiers included\): ([\d.]+) h", p.stdout)
    check(p.returncode == 0 and m7 and m7.groups() == ("1", "2") and mw and float(mw.group(1)) < 1.5,
          "gate-report counts a stage-close batch in section 7 (%s) and keeps a lane-tier run out of the gate wall (%s h)"
          % (m7.groups() if m7 else None, mw.group(1) if mw else None))
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
    try:
        if not E.tip:
            check(False, "the recorded tip 3756b2f18 is in this clone")
        else:
            CASES[case](E)
    finally:            # a case that CRASHES leaves no scratch in /tmp either (three leaked on 2026-10-10)
        subprocess.run(["rm", "-rf", E.scratch])
    if FAILURES:
        print("gate.%s: FAILED (%d)" % (case, len(FAILURES))); return 1
    print("gate.%s: PASSED" % case); return 0


if __name__ == "__main__":
    if len(sys.argv) < 4 or sys.argv[1] not in CASES:
        print(__doc__); sys.exit(2)
    sys.exit(main(sys.argv[1], os.path.abspath(sys.argv[2]), os.path.abspath(sys.argv[3])))
