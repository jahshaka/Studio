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
  carried_red       U4 (only a lane's OWN records, `lanes == [lane]`): lane X red at tip A, the lane rebased to tip B with no re-run -> refused (`OPEN RED carried
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

RANGE = None          # 51e9f2c49..<speed_commit> — set by Env (the row: SPEED_ROW, which stays; see speed_commit)
ROWS = ["gi.speckle"]
# THE DATA EDGE (RUNNER-A2B): RANGE touches tests/gi/CMakeLists.txt, which gate.tokens_by_needs READS (it lints every row's
# declarations) — the scoped selection holds it (every range here touches that file or descends from one that did). Each
# run records it green at the range's tip first (no case is about it); the cases' reds stay on the rows they name.
# TESTING-SPEED-A item 5: RANGE touches tests/support/ (the test harness, a tool path): the smoke pair and the tools'
# rows that read it ride with it — recorded green at the range's tip like the data edge
DATA_EDGE_ROWS = []      # the speed range touches one test source: no data edge, no tool path, no harness
FAILURES = []

# THE RANGE'S ROW, ONE THAT STAYS (TESTING-SPEED-A, for TESTING-SPEED-B's batches: gi.speckle — the VIEWS-DEPTH-1 range's
# row — is DELETED by batch 1): the range is 51e9f2c49..<a synthetic commit on it that adds a code line to gi.speckle's own
# source> — an unreferenced object, no ref, no worktree moved; it reaches gi.speckle alone
SPEED_ROW = "gi.speckle"
SPEED_PATH = "tests/gi/test_gi_speckle.cpp"


def speed_commit(source, scratch, parent="51e9f2c49", text="static int tsa_speed_probe = 1;\n", msg="the speed range"):
    """A commit object on `parent` whose tree carries SPEED_PATH = `text` (a code line: gi.speckle's source moved)."""
    env = dict(os.environ, GIT_AUTHOR_NAME="jahshaka", GIT_AUTHOR_EMAIL="jahshaka@gmail.com",
               GIT_COMMITTER_NAME="jahshaka", GIT_COMMITTER_EMAIL="jahshaka@gmail.com",
               GIT_INDEX_FILE=os.path.join(scratch, "speed-%d.index" % len(os.listdir(scratch))))
    g = lambda *a, **k: subprocess.run(["git"] + list(a), cwd=source, capture_output=True, text=True, env=env, **k)
    blob = g("hash-object", "-w", "--stdin", input=text).stdout.strip()
    g("read-tree", parent + "^{tree}")
    g("update-index", "--add", "--cacheinfo", "100644,%s,%s" % (blob, SPEED_PATH))
    tree = g("write-tree").stdout.strip()
    return g("commit-tree", tree, "-p", g("rev-parse", parent).stdout.strip(), "-m", "TESTING-SPEED-A " + msg).stdout.strip()



ENROLLED = {"enrolled": {"by": "lead", "rate": "3 in 200 (fixture)", "census": {"other_ctests": 2}, "date": "2026-10-09"}}


def defect(i, rows, kind="defect", **kw):
    if kind == "nondeterminism" and "uses" not in kw and "enrolled" not in kw:
        kw = dict(ENROLLED, **kw)
    return dict({"id": i, "rows": rows, "kind": kind, "cause": "the fixture's (this test only)", "state": "open",
                 "first_seen": {"tip": "0" * 9, "pin": "0" * 9, "run": "20261009T120000-000000000"},
                 "recheck": "2099-12-31", "found_by": "lane"}, **kw)


FIXTURE_DEFECTS = [defect("FIXTURE-1", ROWS + ["test_engine"]), defect("VIEWS-XID-1", [SPEED_ROW]),
                   defect("BUDGET-7", [SPEED_ROW]), defect("RETIRED-1", [SPEED_ROW], state="retired"),
                   defect("OTHER-ROW-1", ["api.contract"]), defect("NONDET-SOLO-1", [SPEED_ROW], "nondeterminism",
                                                                    state="retired")]
NONDET = defect("NONDET-FIXTURE-1", [SPEED_ROW], "nondeterminism")
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
        global RANGE
        self.tip = speed_commit(source, self.scratch)
        self.pin = self.git("rev-parse", self.tip + ":irisgl")
        RANGE = "51e9f2c49.." + self.tip

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

    def run(self, *extra, rng=None):
        rng = rng or RANGE
        self.put(DATA_EDGE_ROWS, "PASS", "2026-01-01T00:30:00", tip=self.git("rev-parse", rng.split("..")[1]))
        p = subprocess.run([self.tool, rng, "--build", self.build] + list(extra), capture_output=True, text=True,
                           env=dict(os.environ))
        return p.returncode, p.stdout + p.stderr

    def judge(self, recs, listed=False, base=None, defects=None):
        d = defects or {e["id"]: e for e in FIXTURE_DEFECTS + ([NONDET] if listed else [])}
        # a stub history for the toy shas: every different sha descends and reaches (the real Prover is tested apart)
        return self.cgc.judge((SPEED_ROW, None), recs, d, base_recs=base,
                              proves=lambda a, b, k: bool(a and b and a != b))


def rec(verdict, ts, retry=False, **kw):
    return dict({"suite": SPEED_ROW, "arm": None, "verdict": verdict, "ts": ts, "retry": retry}, **kw)


def vrec(text, ts):
    return {"kind": "verdict", "suite": SPEED_ROW, "arm": None, "verdict": "VERDICT", "text": text, "ts": ts}


def case_verdict_door(E):
    registry_id_cases(E)
    batch_range_cases(E)
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
    # differed only by the tree root (rc-merge-c2 vs rc-base-d4991c754). RED ON 314770a52: "not the SAME red".
    W_ = "/home/jahshaka/Developer/jahshaka/.claude/worktrees/"
    msg = "FAIL: the data root and the working directory must be inside %s/build-linux/tests/scripting/e2e-home-clip_ref"
    st, why = E.judge([rec("FAIL", T % "10:00", failLine=msg % (W_ + "rc-merge-c2")), vrec("real:VIEWS-XID-1", T % "11:00")],
                      base=[rec("FAIL", T % "09:00", retry=True, failLine=msg % ("/mnt/work/Developer/jahshaka/.claude/worktrees/"
                                                                                      "rc-base-d4991c754"))])
    check(st == "known", "the same failure under two tree roots (rc-merge-c2 / rc-base-<sha>, two spellings of the root) "
          "is the SAME red -> KNOWN RED (%s: %s)" % (st, why[:80]))
    st, why = E.judge([rec("FAIL", T % "10:00", failLine=msg % (W_ + "rc-merge-c2")), vrec("real:VIEWS-XID-1", T % "11:00")],
                      base=[rec("FAIL", T % "09:00", retry=True,
                                failLine=(msg % (W_ + "rc-base-d4991c754")).replace("clip_ref", "tex_ref"))])
    check(st == "red" and "not the SAME red" in why, "...a different file INSIDE the tree stays a different red (%s)" % why[:70])
    check(E.cgc.masked("open /tmp/jah-lead/rc-C2.state: /home/a/.local/share/Jahshaka/x.db") ==
          E.cgc.masked("open /tmp/jah-rc9/rc-C2.state: /home/u/.local/share/Jahshaka/x.db"),
          "/tmp/jah-* and a data root are one token each (%s)" % E.cgc.masked("open /tmp/jah-lead/rc-C2.state"))
    # FIX ROUND 3 F3: A ROW NOT RUN BECAUSE ITS PARENT FAILED takes the parent's state (one verdict covers both), never
    # MISSING; a parent still missing leaves it missing; a parent outside the judged set is judged for it. RED ON
    # 9eb5343e2: no resolver — the inspector stayed MISSING, which no verdict clears.
    nr_ = {("insp", None): [{"suite": "insp", "verdict": "NOTRUN", "notRunBecause": ["par"], "ts": T % "10:05"}]}
    resolve_ = getattr(E.cgc, "resolve_not_run", None)
    check(resolve_ is not None, "ci_gate_check has the Not Run resolver")
    resolve_ = resolve_ or (lambda *a: None)          # absent (a base before F3): the cases below read red, not a crash
    for pst_, want in (("red", "red"), ("known", "known"), ("missing", "missing"), ("nondet", "missing"),
                       ("green", "missing")):
        out_ = {("insp", None): ("missing", "never run", "t"), ("par", None): (pst_, "why", "t")}
        resolve_(out_, nr_, lambda pk: ("red", "x"), "t")
        check(out_[("insp", None)][0] == want and (want == "missing" or "its parent par" in out_[("insp", None)][1]),
              "an inspector Not Run because its parent is %s -> %s (%s)" % (pst_, want, out_[("insp", None)][1][:70]))
    out_ = {("insp", None): ("missing", "never run", "t")}
    resolve_(out_, nr_, lambda pk: ("known", "judged for it"), "t")
    check(out_[("insp", None)][0] == "known", "...a parent outside the judged set is judged for it (%s)" % out_[("insp", None)][0])
    # FIX ROUND 4 item 1: a green / nondet parent leaves the inspector MISSING (its assertion never ran — rows above), and
    # only the NEWEST record overall counts: an older parent-Not-Run beside a newer bare Not Run resolves nothing.
    # RED ON b77ab5abc: nondet -> nondet, green -> green, and the older record resolved.
    nr2 = {("insp", None): [nr_[("insp", None)][0], {"suite": "insp", "verdict": "NOTRUN", "ts": T % "10:09"}]}
    out_ = {("insp", None): ("missing", "never run", "t"), ("par", None): ("red", "why", "t")}
    resolve_(out_, nr2, lambda pk: ("red", "x"), "t")
    check(out_[("insp", None)][0] == "missing", "...an older parent-Not-Run under a newer bare Not Run resolves nothing (%s)"
          % out_[("insp", None)][0])
    # FIX ROUND 3 F2: masked() never over-masks. RED ON 9eb5343e2 (the reader's measurements): two in-tree homes read as
    # one red, the owner's real data root read as a test home, and the unanchored prefix ate `a=` / `FAIL:`.
    mk, Wt = E.cgc.masked, "/home/jahshaka/Developer/jahshaka/.claude/worktrees/rc-merge-c2"
    check(mk(Wt + "/build-linux/tests/scripting/e2e-home-clip_ref/.local/share/Jahshaka/lib.db missing")
          != mk(Wt + "/build-linux/tests/scripting/e2e-home-tex_ref/.local/share/Jahshaka/lib.db missing"),
          "two in-tree homes stay two reds (%s)" % mk(Wt + "/b/e2e-home-clip_ref/.local/share/Jahshaka/lib.db"))
    check(mk("wrote /home/jahshaka/.local/share/Jahshaka/lib.db") != mk("wrote " + Wt + "/b/tests/x/home/.local/share/Jahshaka/lib.db"),
          "the owner's real data root never reads as a test home (%s)" % mk("wrote /home/jahshaka/.local/share/Jahshaka/lib.db"))
    check(mk("wrote /home/jahshaka/.local/share/Jahshaka/lib.db") != mk("wrote /tmp/tmp.Xy12/.local/share/Jahshaka/lib.db"),
          "fix round 4 item 4: a user's data root and a /tmp home are different tokens (%s / %s)"
          % (mk("/home/u/.local/share/Jahshaka/x"), mk("/tmp/t/.local/share/Jahshaka/x")))
    check(mk("cmp a=" + Wt + "/x.png") != mk("cmp b=" + Wt + "/x.png") and mk("FAIL:" + Wt + "/a.png") != mk("WARN:" + Wt + "/a.png"),
          "a mask starts where a path starts: `a=`/`b=` and `FAIL:`/`WARN:` stay (%s)" % mk("cmp a=" + Wt + "/x.png"))
    st, why = E.judge([red, vrec("real:VIEWS-XID-1", T % "11:00")], base=base_solo)
    check(st == "known" and "KNOWN RED" in why, "real:<registered id> + the red reproduced by a solo at the base -> KNOWN "
          "RED (%s: %s)" % (st, why[:70]))
    nd = {e["id"]: e for e in FIXTURE_DEFECTS + [NONDET]}
    ok3 = [rec("PASS", T % ("10:%02d" % (10 + i)), retry=True) for i in range(3)]
    st, why = E.judge([red] + ok3 + [vrec("real:NONDET-FIXTURE-1 enrolled", T % "11:00")], defects=nd)
    check(st == "nondet" and "3/3" in why, "real:<an enrolled nondeterminism id> + 3/3 solo -> cleared as nondeterminism "
          "(%s)" % why[:80])
    tip = E.tip
    nr = defect("NOTREPRO-1", [SPEED_ROW], "nondeterminism", uses=1, suspects=["lane-x"], census={"other_ctests": 1},
                first_seen={"tip": tip[:9], "pin": "0" * 9, "run": "20261009T120000-" + tip[:9]})
    one = {"NOTREPRO-1": nr}
    st, why = E.cgc.judge((SPEED_ROW, None), [red] + ok3 + [vrec("real:NOTREPRO-1", T % "11:00")], one, tip_sha=tip)
    check(st == "nondet", "a SINGLE-USE NOT REPRODUCED entry clears its own merge's red by 3/3 (%s)" % why[:70])
    st, why = E.cgc.judge((SPEED_ROW, None), [red] + ok3 + [vrec("real:NOTREPRO-1", T % "11:00")], one, tip_sha=tip,
                          mode="push")
    check(st == "red" and "never a push" in why, "F4: ...never a push, even at the sha that registered it (%s)" % why[:70])
    st, why = E.cgc.judge((SPEED_ROW, None), [red] + ok3 + [vrec("real:NOTREPRO-1", T % "11:00")], one, tip_sha="b" * 40)
    check(st == "red" and "SINGLE-USE" in why, "...and never a red at another tip (a later merge, a push) (%s)" % why[:80])
    check(not E.rl.contention_of(one), "...and it is never the contention class (only an ENROLLED entry is)")
    old = {"OLD-1": defect("OLD-1", [SPEED_ROW], recheck="2026-01-01")}
    st, why = E.cgc.judge((SPEED_ROW, None), [red, fixpass, vrec("real:OLD-1", T % "11:00")], old)
    check(st == "red" and "past its recheck" in why, "an entry past its recheck DATE is refused until re-verdicted (%s)"
          % why[:80])
    oldnd = {"OLD-2": defect("OLD-2", [SPEED_ROW], "nondeterminism", recheck="2026-01-01")}
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
    # TESTING-CLEANUP-2C (the lead's item 4): THE SOLO RULE IS FOR A CLAIM OF NONDETERMINISM. A registered DEFECT whose
    # solos are 3/3 RED at the candidate and whose base solos show the same red is KNOWN RED — batch C2's attribution
    # recorded exactly that for five rows and the door refused all five ("solos below 3/3 (0/3)") before reading the
    # base. RED ON 314770a52: refused 'solos below 3/3'. Mixed solos (2/3 above) stay refused; a nondeterminism claim
    # (contention:, or a real:<nondeterminism entry>) with 0/3 green solos stays refused.
    fl = "FAIL: the data root must be inside /home/jahshaka/Developer/jahshaka/.claude/worktrees/rc-merge-c2/x"
    red3 = [rec("FAIL", T % ("10:%02d" % (10 + i)), retry=True, failLine=fl) for i in range(3)]
    base3 = [rec("FAIL", T % ("09:%02d" % (10 + i)), retry=True, failLine=fl.replace("rc-merge-c2", "rc-base-d4991c754"))
             for i in range(3)]
    st, why = E.judge([rec("FAIL", T % "10:00", failLine=fl)] + red3 + [vrec("real:VIEWS-XID-1", T % "11:00")], base=base3)
    check(st == "known", "a real:<defect> with 3/3 RED solos at the candidate and the same red 3/3 at the base -> KNOWN RED, "
          "the red solos its evidence (%s: %s)" % (st, why[:90]))
    # FIX ROUND 3 F1: EVERY pending red must be the base's red, and the base's solos must ALL be red. RED ON 9eb5343e2
    # (the reader's probes F/G/C): a CRASH or another assertion among the candidate's solos passed as KNOWN RED (only
    # the newest red was compared), and a base with 1 red + 2 green solos made a KNOWN RED.
    crash_mid = [red3[0], rec("CRASH", T % "10:11", retry=True, status="Exception: SegFault"), red3[2]]
    st, why = E.judge([rec("FAIL", T % "10:00", failLine=fl)] + crash_mid + [vrec("real:VIEWS-XID-1", T % "11:00")], base=base3)
    check(st == "red" and "not every pending red" in why, "(F) a CRASH among the solos (not the newest) -> not KNOWN RED "
          "(%s)" % why[:90])
    other_mid = [red3[0], rec("FAIL", T % "10:11", retry=True, failLine="FAIL: another assertion 3"), red3[2]]
    st, why = E.judge([rec("FAIL", T % "10:00", failLine=fl)] + other_mid + [vrec("real:VIEWS-XID-1", T % "11:00")], base=base3)
    check(st == "red" and "not every pending red" in why, "(G) another assertion among the solos -> not KNOWN RED (%s)" % why[:90])
    st, why = E.judge([rec("FAIL", T % "10:00", failLine=fl)] + red3 + [vrec("real:VIEWS-XID-1", T % "11:00")],
                      base=[base3[0], rec("PASS", T % "09:11", retry=True), rec("PASS", T % "09:12", retry=True)])
    check(st == "red" and "nondeterminism on the base" in why, "(C) a base with 1 red + 2 green solos is nondeterminism "
          "on the base, not a KNOWN RED (%s)" % why[:90])
    # FIX ROUND 4 item 6: a base whose solos flip class (FAIL, CRASH, FAIL) is not one known red — all-CRASH candidate solos
    # do not pass on its one CRASH (RED ON b77ab5abc: KNOWN RED); and the count names each pending red once
    crash3 = [rec("CRASH", T % ("10:%02d" % (10 + i)), retry=True, status="Exception: SegFault") for i in range(3)]
    flip = [base3[0], rec("CRASH", T % "09:11", retry=True, status="Exception: SegFault"), base3[2]]
    st, why = E.judge([rec("FAIL", T % "10:00", failLine=fl)] + crash3 + [vrec("real:VIEWS-XID-1", T % "11:00")], base=flip)
    check(st == "red" and "flip class" in why, "(J) a base flipping FAIL/CRASH is not one KNOWN RED (%s)" % why[:80])
    st, why = E.judge([rec("FAIL", T % "10:00", failLine=fl)] + red3 + [vrec("real:VIEWS-XID-1", T % "11:00")], base=base3)
    check(st == "known" and "the 4 pending red(s)" in why, "(K) one gate red + 3 red solos count as 4 pending reds (%s)"
          % why[-90:])
    st, why = E.judge([rec("FAIL", T % "10:00", failLine=fl)] + red3 + [vrec("real:VIEWS-XID-1", T % "11:00")])
    check(st == "red" and "solos below" not in why, "...with no base record it is refused by the KNOWN RED door, not the "
          "solo rule (%s)" % why[:90])
    st, why = E.judge([red] + red3 + [vrec("contention: load 14 beside two gates", T % "11:00")], listed=True)
    check(st == "red" and "solos below 3/3" in why, "a contention: claim with 0/3 green solos -> refused 'solos below "
          "3/3' (%s)" % why[:80])
    st, why = E.judge([red] + red3 + [vrec("real:NONDET-FIXTURE-1", T % "11:00")], listed=True)
    check(st == "red" and "3/3 solo PASS" in why, "a real:<nondeterminism entry> with 0/3 green solos -> refused (%s)"
          % why[:90])
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
    E.put(ROWS, "FAIL", "2026-01-01T10:00:01")
    rc, out = E.run("--verdict", "gi.speckle=looked fine to the reader")
    check(rc == 1 and "VERDICT REFUSED gi.speckle:" in out, "the CLI prints VERDICT REFUSED <row>: <why> and "
          "refuses (%d)" % rc)
    rc, out = E.run("--verdict", "gi.speckle=real:FIXTURE-1")
    check(rc == 1 and "VERDICT REFUSED gi.speckle:" in out, "...real:<id> with no proof -> refused (%d)" % rc)
    E.put(ROWS, "FAIL", "2026-01-01T09:00:00", tip=E.git("rev-parse", "51e9f2c49"), retry=True)
    rc, out = E.run()
    check(rc == 0 and "KNOWN RED gi.speckle" in out, "...the red reproduced by a recorded solo at the RANGE'S BASE -> "
          "KNOWN RED: the merge passes it (%d)" % rc)
    for mode in ("push", "stage-close"):
        rc, out = E.run("--mode", mode)
        check(rc == 1 and "KNOWN RED" in out, "...and a %s refuses it: never push on a red (%d)" % (mode, rc))
    # F4: a row cleared as NONDETERMINISM must be 3/3 solo green AT THE CANDIDATE for a push (patched judge: every
    # row "nondet", no solo at the tip)
    E.fresh()
    E.put(ROWS + DATA_EDGE_ROWS, "PASS", "2026-01-01T10:00:00")     # the in-process check: no E.run() records the data edge
    real_judge = E.cgc.judge
    E.cgc.judge = lambda *a, **k: ("nondet", "cleared as nondeterminism (the patched fixture)")
    try:
        ok_m, _ = E.cgc.check(RANGE, E.build, mode="merge")
        ok_p, why_p = E.cgc.check(RANGE, E.build, mode="push")
        for k in range(3): E.put(ROWS + DATA_EDGE_ROWS, "PASS", "2026-01-01T11:0%d:00" % k, retry=True)
        ok_p3, _ = E.cgc.check(RANGE, E.build, mode="push")
    finally:
        E.cgc.judge = real_judge
    check(ok_m and not ok_p and any("not 3/3 solo green at the candidate" in w for w in why_p) and ok_p3,
          "F4: a nondeterminism-cleared row passes a merge, is re-asked 3/3 at the push candidate (refused with 0/0, "
          "accepted with 3/3) (%s, %s, %s)" % (ok_m, ok_p, ok_p3))
    # ROUND 3 F-A: AN EMPTY COMMIT proves nothing. C1 = a lane commit (51e9f2c49's child carrying 3756b2f18's tree), X =
    # an EMPTY commit on it (`git commit --allow-empty`, made here with commit-tree: objects only, no ref moves): red at
    # C1, green at X -> refused (before: X..C1 read as unreadable -> ALL -> "the fix reached it" -> green, no verdict)
    tree = E.git("rev-parse", E.tip + "^{tree}")
    env = dict(os.environ, GIT_AUTHOR_NAME="jahshaka", GIT_AUTHOR_EMAIL="jahshaka@gmail.com",
               GIT_COMMITTER_NAME="jahshaka", GIT_COMMITTER_EMAIL="jahshaka@gmail.com")
    c1 = subprocess.run(["git", "commit-tree", tree, "-p", E.git("rev-parse", "51e9f2c49"), "-m", "verdict test C1"],
                        cwd=E.source, capture_output=True, text=True, env=env).stdout.strip()
    x = subprocess.run(["git", "commit-tree", tree, "-p", c1, "-m", "verdict test: an empty commit"], cwd=E.source,
                       capture_output=True, text=True, env=env).stdout.strip()
    E.fresh()
    E.put(ROWS, "PASS", "2026-01-01T09:00:00", tip=c1)
    E.put(ROWS, "FAIL", "2026-01-01T09:30:00", tip=c1)
    E.put(ROWS, "PASS", "2026-01-01T10:00:00", tip=x)
    rc, out = E.run(rng="51e9f2c49..%s" % x)
    check(bool(c1 and x) and rc == 1 and SPEED_ROW in out and "OPEN red at %s" % c1[:9] in out,
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
    # TESTING-SPEED-A item 1: a fallback is ITS DIRECTORY's subsystem (a root file: nothing more) on BOTH sides, never ALL
    check(R.between(c1, y, unreadable=set()) == set() and R.between(c1, y) == set(),
          "a FALLBACK range reaches its directory's subsystem (a root file: nothing) when proving AND re-using — never ALL")
    prover4 = E.cgc.Prover(gs, E.build, lambda: R)
    st, why = E.cgc.judge((SPEED_ROW, None), [rec("FAIL", T % "09:30", tip={"studio": c1}),
                                                  rec("PASS", T % "10:00", tip={"studio": y}),
                                                  vrec("real:FIXTURE-1", T % "11:00")],
                          {e["id"]: e for e in FIXTURE_DEFECTS}, proves=prover4)
    check(bool(y) and st == "red" and "REACHES" in why, "ROUND 4: red at C1, green at Y (one stray file, the selector "
          "falls back) + real:<id> -> refused (%s)" % why[:80])
    # N5: a lane-tool PASS (tier `lane`) never answers a row for the judge
    E.fresh()
    # (the speed range has one row: no other row recorded green beside it)
    E.put(ROWS, "PASS", "2026-01-01T10:00:00", tier="lane")
    rc, out = E.run()
    check(rc == 1 and "no record" in out, "a lane-tool PASS (tier lane) does not answer a row the gate never ran (%d)" % rc)
    # the abort's droppedRed: 3/3 solo, whatever else answered it
    E.fresh()
    E.put(ROWS, "PASS", "2026-01-01T10:00:00")
    E.rl.append_records([{"schema": 2, "kind": "abort", "suite": "(abort)", "arm": None, "verdict": "ABORT",
                          "ts": "2026-01-01T10:30:00", "droppedRed": [SPEED_ROW],
                          "tip": {"studio": E.tip, "studio_dirty": False, "irisgl": E.pin, "irisgl_dirty": False}}],
                        "scoped", E.tip)
    E.put(ROWS, "PASS", "2026-01-01T10:40:00")
    rc, out = E.run()
    check(rc == 1 and "DROPPED RED" in out, "a row the abort dropped RED stays red after a quiet PASS (%d)" % rc)
    for k in range(3): E.put(ROWS, "PASS", "2026-01-01T10:5%d:00" % k, retry=True)
    rc, out = E.run()
    check(rc == 0, "...until 3/3 solo PASS after the abort (%d)" % rc)
    # overrides: the push refuses a candidate whose records carry any
    E.fresh()
    E.put(ROWS, "PASS", "2026-01-01T10:00:00", overrides=["JAH_GATE_SLOT=0"])
    rc, out = E.run()
    rc2, out2 = E.run("--mode", "push")
    check(rc == 0 and rc2 == 1 and "OVERRIDES" in out2, "a merge reads overrides; a push refuses a candidate carrying "
          "any (%d, %d)" % (rc, rc2))


def batch_range_cases(E):
    """TESTING-CLEANUP-2D. (1) THE BATCH'S VERDICT COVERS THE BATCH'S RANGE: a KNOWN RED accepted at the tip is accepted
    at an earlier commit of the range whose solo reds carry no verdict record (the tip's verdicts are in scope there);
    a red of ANOTHER signature at that commit is still refused. RED ON 30f4ad2d0: 'an OPEN red at <c1> ... needs a
    verdict through the door'. (2) THE BASE'S PROOF TRAVELS WHILE NOTHING REACHES THE ROW: a registered red with no
    solos at the base is KNOWN when the nearest ancestor A of the base has 3/3 red solos of the same signature and
    A..base does not reach the row (the why names A); refused when A's solos are not all red, when A..base reaches the
    row, when A is not an ancestor, and when A..base is tree-equal (unreadable: ALL). RED ON 30f4ad2d0: refused, 'neither
    a PASS ... nor the same red reproduced by a recorded solo at the BASE'."""
    T = "2026-10-09T%s:00+02:00"
    FL = "FAIL: the depth pre-pass drew 12 px off"
    env = dict(os.environ, GIT_AUTHOR_NAME="jahshaka", GIT_AUTHOR_EMAIL="jahshaka@gmail.com",
               GIT_COMMITTER_NAME="jahshaka", GIT_COMMITTER_EMAIL="jahshaka@gmail.com")

    def commit(tree, parent, msg):
        return subprocess.run(["git", "commit-tree", tree, "-p", parent, "-m", msg], cwd=E.source, capture_output=True,
                              text=True, env=env).stdout.strip()

    def tree_plus(base_tree, path, text):
        idx = os.path.join(E.scratch, "bt-%d.index" % len(os.listdir(E.scratch)))
        genv = dict(env, GIT_INDEX_FILE=idx)
        blob = subprocess.run(["git", "hash-object", "-w", "--stdin"], cwd=E.source, input=text, capture_output=True,
                              text=True).stdout.strip()
        subprocess.run(["git", "read-tree", base_tree], cwd=E.source, env=genv, check=True)
        subprocess.run(["git", "update-index", "--add", "--cacheinfo", "100644,%s,%s" % (blob, path)], cwd=E.source,
                       env=genv, check=True)
        return subprocess.run(["git", "write-tree"], cwd=E.source, env=genv, capture_output=True, text=True).stdout.strip()

    base = E.git("rev-parse", "51e9f2c49")
    tip_tree = E.git("rev-parse", E.tip + "^{tree}")

    def solos(tip, t0, verdicts=("FAIL", "FAIL", "FAIL"), fl=FL, batch=None):
        for i, v in enumerate(verdicts):
            extra = {"failLine": fl} if v == "FAIL" else ({"status": "Exception: SegFault"} if v == "CRASH" else {})
            # an attribution's solo is never a lane's own record (it never carries, U4): `lanes: [attribution:<lane>]`
            E.put([SPEED_ROW], v, T % ("%s:%02d" % (t0, i)), tip=tip, retry=True,
                  **({"lane": "attribution:" + batch} if batch else {}), **extra)

    # ---- (1) the batch's verdict covers the batch: C1 = a lane commit, X = an empty commit on it (C1..X reaches nothing)
    c1 = commit(tip_tree, base, "batch-range test C1")
    x = commit(tip_tree, c1, "batch-range test X (empty)")
    for variant in ("same", "crash"):
        E.fresh()
        solos(base, "09")
        E.put([SPEED_ROW], "FAIL", T % "10:00", tip=c1, failLine=FL)
        solos(c1, "10", ("FAIL", "CRASH", "FAIL") if variant == "crash" else ("FAIL", "FAIL", "FAIL"))
        E.put([SPEED_ROW], "FAIL", T % "11:00", tip=x, failLine=FL)
        solos(x, "11")
        E.verdict(SPEED_ROW, "real:VIEWS-XID-1", T % "12:00", tip=x)
        rc, out = E.run(rng="%s..%s" % (base, x))
        if variant == "same":
            check(bool(c1 and x) and rc == 0 and "KNOWN RED gi.speckle" in out and "OPEN red at" not in out,
                  "(1) a KNOWN RED at the tip is KNOWN at the batch's earlier commit C1 too: the tip's verdict covers C1's "
                  "3/3 red solos (rc %d; %s)" % (rc, [l for l in out.splitlines() if SPEED_ROW in l][:1]))
        else:
            check(rc == 1 and "OPEN red at %s" % c1[:9] in out and "not every pending red" in out,
                  "(1) ...but a CRASH among C1's solos (another signature) is still refused there (rc %d)" % rc)
    # a verdict recorded at C1 itself (an older lane delivery) keeps working
    E.fresh()
    solos(base, "09")
    E.put([SPEED_ROW], "FAIL", T % "10:00", tip=c1, failLine=FL)
    solos(c1, "10")
    E.verdict(SPEED_ROW, "real:VIEWS-XID-1", T % "10:30", tip=c1)
    E.put([SPEED_ROW], "PASS", T % "11:00", tip=x)
    rc, out = E.run(rng="%s..%s" % (base, x))
    check(rc == 0, "(1) a verdict recorded at the earlier commit itself still answers its red (rc %d)" % rc)

    # read 1 I1 (a): a tip verdict stamped BEFORE C1's red clears nothing there — C1's reds after it stay open
    E.fresh()
    solos(base, "09")
    E.put([SPEED_ROW], "FAIL", T % "09:40", tip=x, failLine=FL)
    for i in range(3):
        E.put([SPEED_ROW], "FAIL", T % ("09:4%d" % (i + 1)), tip=x, retry=True, failLine=FL)
    E.verdict(SPEED_ROW, "real:VIEWS-XID-1", T % "09:50", tip=x)
    E.put([SPEED_ROW], "FAIL", T % "10:00", tip=c1, failLine=FL)
    solos(c1, "10")
    rc, out = E.run(rng="%s..%s" % (base, x))
    check(rc == 1 and "OPEN red at %s" % c1[:9] in out,
          "(1, read 1 I1a) a tip verdict stamped BEFORE C1's red clears nothing at C1 (rc %d)" % rc)
    # read 1 I1 (b): a verdict at C1 does not answer the TIP's reds (only the tip's verdicts reach down, never up)
    E.fresh()
    solos(base, "09")
    E.put([SPEED_ROW], "FAIL", T % "10:00", tip=c1, failLine=FL)
    solos(c1, "10")
    # (round 7: the tip's red is ANOTHER failure than the base's — the same red would be a registered known red the
    # registry answers with no verdict at all; this case asks whether C1's verdict reaches UP to a red it does not name)
    E.put([SPEED_ROW], "FAIL", T % "11:00", tip=x, failLine=FL + " (another assertion)")
    E.verdict(SPEED_ROW, "real:VIEWS-XID-1", T % "12:00", tip=c1)
    rc, out = E.run(rng="%s..%s" % (base, x))
    check(rc == 1 and "row gi.speckle <- %s (the tip): red" % x[:9] in out,
          "(1, read 1 I1b) a verdict recorded at C1 does not answer the tip's reds (rc %d)" % rc)

    # ---- (2) the base's proof travels: A = 51e9f2c49 (the solos), B = A + one docs file (the base), Tt = the VIEWS-DEPTH-1
    # tree + the same docs file on B (B..Tt selects gi.speckle; A..B is docs-only: reaches no product row)
    a_tree = E.git("rev-parse", "51e9f2c49^{tree}")
    b = commit(tree_plus(a_tree, "docs/batch_range_test_note.md", "a note\n"), base, "batch-range test B (docs only)")
    tt = commit(tree_plus(tip_tree, "docs/batch_range_test_note.md", "a note\n"), b, "batch-range test T")
    b0 = commit(a_tree, base, "batch-range test B0 (tree-equal)")
    t0 = commit(tip_tree, b0, "batch-range test T0")
    # Br = A + gi.speckle's own script (A..Br reaches the row); Tr = the VIEWS-DEPTH-1 tree on Br
    br = commit(tree_plus(a_tree, SPEED_PATH, "static int tsa_reaches = 1;\n"), base, "batch-range test Br")
    tr = commit(tip_tree, br, "batch-range test Tr")
    other = commit(a_tree, E.tip, "batch-range test: not an ancestor of B")

    def item2(base_sha, tip_sha, a_sha, verdicts=("FAIL", "FAIL", "FAIL")):
        E.fresh()
        solos(a_sha, "08", verdicts, batch="t2d")
        E.put([SPEED_ROW], "FAIL", T % "11:00", tip=tip_sha, failLine=FL)
        E.verdict(SPEED_ROW, "real:VIEWS-XID-1", T % "12:00", tip=tip_sha)
        return E.run(rng="%s..%s" % (base_sha, tip_sha))

    rc, out = item2(b, tt, base)
    check(bool(b and tt) and rc == 0 and "KNOWN RED gi.speckle" in out and "the base's proof is %s's solos" % base[:9] in out,
          "(2) no solo at the base B; A = B's parent has 3/3 red of the signature and A..B is docs-only -> KNOWN, the why "
          "names A (rc %d; %s)" % (rc, [l for l in out.splitlines() if "base proof" in l or "KNOWN RED photon" in l][:2]))
    rc, out = item2(b, tt, base, ("FAIL", "PASS", "FAIL"))
    check(rc == 1 and "nondeterminism, never a base proof" in out,
          "(2) refused when A's solos are not all red (rc %d)" % rc)
    rc, out = item2(b0, t0, base)
    check(bool(b0 and t0) and rc == 1 and "reaches the row (or cannot be read)" in out,
          "(2) refused when A..base is TREE-EQUAL (the selector cannot read it: ALL, the proof does not travel) (rc %d)" % rc)
    rc, out = item2(br, tr, base)
    check(bool(br and tr) and rc == 1 and "reaches the row (or cannot be read)" in out,
          "(2) refused when A..base REACHES the row (gi.speckle's own script changes between A and the base) (rc %d)" % rc)
    rc, out = item2(b, tt, other)
    check(bool(other) and rc == 1 and "KNOWN RED gi.speckle" not in out,
          "(2) refused when the sha with the solos is NOT an ancestor of the base (rc %d)" % rc)
    # the helper alone: the nearest ancestor is the one candidate (one hop) — an all-red OLDER ancestor behind a mixed
    # nearer one proves nothing; an unreadable range (ALL) refuses
    S = {"near": [rec("FAIL", T % "09:00", retry=True), rec("PASS", T % "09:01", retry=True)],
         "far": [rec("FAIL", T % "08:00", retry=True)] * 3}
    dist = {"near": 1, "far": 5}
    got, a, why = E.cgc.ancestor_base_proof((SPEED_ROW, None), "B", S, lambda a_, b_: True,
                                            lambda a_, b_: dist[a_], lambda a_, b_, k: False)
    check(got == [] and a is None and "near" in why, "(2) one hop: the nearest ancestor decides, never an older one behind it")
    got, a, why = E.cgc.ancestor_base_proof((SPEED_ROW, None), "B", {"far": S["far"]}, lambda a_, b_: True,
                                            lambda a_, b_: 5, lambda a_, b_, k: E.cgc.Reach.reaches(E.cgc.Reach.ALL, k))
    check(got == [] and "reaches the row" in why, "(2) an unreadable range (Reach.ALL) refuses")
    got, a, why = E.cgc.ancestor_base_proof((SPEED_ROW, None), "B", {"far": S["far"]}, lambda a_, b_: True,
                                            lambda a_, b_: 5, lambda a_, b_, k: False)
    check(len(got) == 3 and a == "far" and all(r.get("_proofFrom") == "far" for r in got) and "far's solos" in why,
          "(2) the proof found: A's solos tagged with A, the why names A")
    # read 1 I3: an all-green A, or an A that never ran it, is named for what it is (never "nondeterminism")
    _, _, why = E.cgc.ancestor_base_proof((SPEED_ROW, None), "B", {"g": [rec("PASS", T % "08:00", retry=True)] * 3},
                                          lambda a_, b_: True, lambda a_, b_: 1, lambda a_, b_, k: False)
    check("green: no red to prove" in why and "nondeterminism" not in why,
          "(2, read 1 I3) an all-green A: 'no red to prove' (%s)" % why)
    # read 2 I2: an attribution at A1 that never got admission (NOADMIT / NOTRUN only) is no run — it does not hide the
    # older A2's 3/3 red (RED ON 8e57285b6: A1 was the one hop and refused "never ran it")
    got, a, why2 = E.cgc.ancestor_base_proof((SPEED_ROW, None), "B",
                                             {"A1": [rec("NOADMIT", T % "08:30", retry=True), rec("NOTRUN", T % "08:31", retry=True)],
                                              "A2": [rec("FAIL", T % "08:00", retry=True)] * 3},
                                             lambda a_, b_: True, lambda a_, b_: {"A1": 1, "A2": 2}[a_], lambda a_, b_, k: False)
    check(a == "A2" and len(got) == 3, "(2, read 2 I2) an ancestor that never ran the row does not hide an older ancestor's "
          "3/3 red (%s: %s)" % (a, why2))
    # read 1 I2 (the lead's rule): the base holds a gate PASS of the row -> the proof does NOT travel (a red at the
    # candidate is a regression); and a GREEN row never reads or prints the base proof
    E.fresh()
    solos(base, "08", batch="t2d")
    E.put([SPEED_ROW], "PASS", T % "09:00", tip=b)
    E.put([SPEED_ROW], "FAIL", T % "11:00", tip=tt, failLine=FL)
    E.verdict(SPEED_ROW, "real:VIEWS-XID-1", T % "12:00", tip=tt)
    rc, out = E.run(rng="%s..%s" % (b, tt))
    check(rc == 1 and "base proof for" not in out and "KNOWN RED gi.speckle" not in out,
          "(2, read 1 I2) a base with a gate PASS of the row is its own evidence: the proof does not travel (rc %d)" % rc)
    # read 2 I1: a base holding only NOADMIT / NOTRUN records of the row has NO run of it — the proof still travels
    E.fresh()
    solos(base, "08", batch="t2d")
    E.put([SPEED_ROW], "NOADMIT", T % "09:00", tip=b)
    E.put([SPEED_ROW], "NOTRUN", T % "09:01", tip=b, retry=True)
    E.put([SPEED_ROW], "FAIL", T % "11:00", tip=tt, failLine=FL)
    E.verdict(SPEED_ROW, "real:VIEWS-XID-1", T % "12:00", tip=tt)
    rc, out = E.run(rng="%s..%s" % (b, tt))
    check(rc == 0 and "the base's proof is %s's solos" % base[:9] in out,
          "(2, read 2 I1) a base with only NOADMIT / NOTRUN records of the row falls through to the ancestor (rc %d)" % rc)
    E.fresh()
    solos(base, "08", batch="t2d")
    E.put([SPEED_ROW], "PASS", T % "11:00", tip=tt)
    rc, out = E.run(rng="%s..%s" % (b, tt))
    check(rc == 0 and "base proof for" not in out, "(2, read 1 I3) a green row never reads nor prints a base proof (rc %d)" % rc)
    # read 1 F1: A STAGE-CLOSE ROW THE SELECTOR SET ASIDE IS STILL REACHED — A..base touching an app source (every app
    # row's dependency, not gi.chain_converge_scenes' own subject) reaches that stage-close row for the travel and for
    # serving (Reach.between with the serving's fork rows — RUNNER-A2 deleted Reach.of, the old re-use arm); the proving side is unchanged. RED ON f980d8ced: not reached (the set-aside row was invisible)
    g30 = E.git("rev-parse", "30f4ad2d0")
    dep = commit(tree_plus(E.git("rev-parse", "30f4ad2d0^{tree}"), "src/app/crashhandler.cpp",
                           E.git("show", "30f4ad2d0:src/app/crashhandler.cpp") + "\n// a dependency-only change\n"), g30,
                 "batch-range test: an app dependency change")
    gs = E.cgc.load_gs()
    R = E.cgc.Reach(gs, E.build, dep, "fork")
    key = ("gi.chain_converge_scenes", None)
    trav = R.between(g30, dep, unreadable=E.cgc.Reach.ALL)
    check(bool(dep) and E.cgc.Reach.reaches(trav, key) and E.cgc.Reach.reaches(R.between(g30, dep, unreadable=E.cgc.Reach.ALL, fork_rows=set()), key),
          "(2, read 1 F1) a dependency-only change to a stage-close row between A and the base refuses the travel, and the "
          "re-use arm does not re-use across it (%s)" % (key in (trav or set()) if trav is not E.cgc.Reach.ALL else "ALL"))
    # read 2 F1: THE PROVING SIDE counts the set-aside row too — a fix of gi.chain_converge_scenes landing outside its
    # test dir (an app source) PROVES it (the Prover reaches it across `dep`); the empty-set side of a fallback stays.
    # RED ON 8e57285b6: not reached -> "an OPEN red at c that c..tip does not reach" at a push / stage close
    prover2 = E.cgc.Prover(gs, E.build, lambda: R)
    check(bool(dep) and prover2(g30, dep, key) and E.cgc.Reach.reaches(R.between(g30, dep, unreadable=set()), key),
          "(2, read 2 F1) the Prover reaches the set-aside stage-close row across the same dependency-only change")


def registry_id_cases(E):
    """TESTING-CLEANUP-2B item 14. (1) `real:<id>` is any token and the REGISTRY is the grammar: an id the attribution
    registered with lowercase, dots and no number reaches the registry lookup. RED ON BASE (b0a3b1f3c): the pattern
    ([A-Z…]-<n>) matched nothing in `real:A2-defect-scripting.e2e.clip_ref`, so the verdict was prose and refused.
    (The batch attribution's own registration — gate-scope.py `_register` — is deleted with the batch layer, RUNNER-A7B:
    the runner's merge registers through `lane.sh defect`.)"""
    m = E.cgc.DEFECT_ID.search("real:A2-defect-scripting.e2e.clip_ref the fixture's red")
    check(m and m.group(1) == "A2-defect-scripting.e2e.clip_ref", "real:<any token> is parsed whole (%s)" % (m and m.group(1)))
    ok, why, _ = E.cgc._real(m, "scripting.e2e.clip_ref", [], [], {}, [], [])
    check(not ok and "not in the defect registry" in why, "...and LOOKED UP in the registry: unknown there, refused there")
    reg_e = {"A2-defect-scripting.e2e.clip_ref": {"id": "A2-defect-scripting.e2e.clip_ref", "rows": ["other.row"],
                                                  "state": "open", "recheck": "2099-12-31"}}
    ok, why, _ = E.cgc._real(m, "scripting.e2e.clip_ref", [], [], reg_e, [], [])
    check(not ok and "registered for ['other.row']" in why, "...a registered id reaches the entry's own checks (%s)" % why[:70])


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
    E.put([SPEED_ROW], "FAIL", "2026-01-01T09:00:00", tip=t0)
    rc, out = E.run()
    check(rc == 0 and "OPEN RED" not in out, "a carried red at an ANCESTOR whose range to the tip reaches the row is "
          "answered by the tip's later green (%d)" % rc)
    # ROUND 2: a lane tip that DESCENDS from the checked tip (a5ab3a057 descends from 3756b2f18) holds a red the tip's
    # older code never had to answer — the tip's PASS is pre-bug and clears nothing
    E.fresh()
    E.put(ROWS + ["test_engine"], "PASS", "2026-01-01T11:00:00")
    E.put(["test_engine"], "FAIL", "2026-01-01T09:00:00",
          tip=speed_commit(E.source, E.scratch, parent=E.tip, msg="a lane tip that descends from the checked tip"))
    rc, out = E.run()
    check(rc == 1 and "OPEN RED carried" in out, "a red at a lane tip that DESCENDS from the checked tip is not cleared by "
          "the tip's (pre-bug) PASS (%d)" % rc)
    E.fresh()
    E.put(ROWS, "PASS", "2026-01-01T10:00:00")
    E.put(["test_engine"], "FAIL", "2026-01-01T09:00:00", tip=A)
    E.put(["test_engine"], "FAIL", "2026-01-01T08:00:00", tip=E.git("rev-parse", "51e9f2c49"), retry=True)
    rc, out = E.run("--verdict", "test_engine=real:FIXTURE-1 the old tip's red, reproduced on the base")
    vfiles = [f for f in os.listdir(os.environ["JAH_RUN_LOG_DIR"]) if "-verdict-aaaaaaaaa" in f]
    # (round 8: the base's solo reproduces A's red of a registered row, so the REGISTRY answers it and drops the carried red
    # before any verdict is needed — the lead's verdict there is then not recorded: nothing is red to answer)
    check(rc == 0 and (vfiles or "OPEN RED" not in out), "...a verdict at A (recorded AT A) -> accepted, or the registry "
          "answered the row first (round 8) (%d, %s)" % (rc, vfiles))
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
    for name, kw in (("a record of several lanes (lanes [lane-y, lane-x])", {"lanes": ["lane-y", "lane-x"]}),):
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
    # RUNNER-FIX-4 item 10 (the judge crashed on VOXEL-READOUT-1's fourth fire): a POOL row carried with one of its ARMS
    # (keys (row, None) and (row, "pool.x")) — sorted() compared None with a str; the judge must order them and refuse
    E.fresh()
    E.put(ROWS, "PASS", "2026-01-01T10:00:00")
    E.put(["test_engine"], "FAIL", "2026-01-01T09:00:00", tip=A)
    E.put(["test_engine"], "FAIL", "2026-01-01T09:00:00", tip=A, arm="pool.x")
    rc, out = E.run()
    check(rc == 1 and "Traceback" not in out and out.count("OPEN RED carried from aaaaaaaaa: test_engine") >= 2,
          "item 10: a row carried with one of its arms -> both OPEN REDs listed, the row before its arm, no crash (%d%s)"
          % (rc, "; " + out.strip().splitlines()[-1][:120] if "Traceback" in out else ""))
    # RUNNER-FIX-4 item 14: a row OUTSIDE THE MERGE TIER by this build's labels (a photon-target row: red by design) recorded
    # red under tier `lane` at the lane's other tip is NEVER carried to a merge (the tier the record names does not decide)
    inv = E.cgc.load_gs().load_inventory(E.build)
    tgt_row = next((n for n, t in sorted(inv.items()) if "photon-target" in t["labels"]), None)
    check(bool(tgt_row), "item 14: this build has a photon-target row (%s)" % tgt_row)
    if tgt_row:
        E.fresh()
        E.put(ROWS, "PASS", "2026-01-01T10:00:00")
        E.put([tgt_row], "FAIL", "2026-01-01T09:00:00", tip=A, tier="lane")
        rc, out = E.run()
        check(rc == 0 and "OPEN RED" not in out, "item 14: %s (photon-target) red under tier lane at the lane's other tip is "
              "not carried to the merge (%d)" % (tgt_row, rc))
    # ...and the runner's own `kind: attribution` record of the lane (verdict None) is NO RUN: never a red, never carried
    E.fresh()
    E.put(ROWS, "PASS", "2026-01-01T10:00:00")
    E.put(["test_engine"], None, "2026-01-01T09:00:00", tip=A, kind="attribution", **{"class": "NOT REPRODUCED"})
    rc, out = E.run()
    check(rc == 0 and "OPEN RED" not in out and "None and not" not in out,
          "item 10: the runner's `kind: attribution` record of the lane is no run: not carried as a red (%d)" % rc)


    # ROUND 5 (2): A RED AT A NON-ANCESTOR LANE TIP (the pre-rebase tip, off the candidate's history): carried only while the
    # TREE diff from that tip to the candidate does NOT reach the row; when it reaches it, the candidate's own record decides
    pre = speed_commit(E.source, E.scratch, text="static int tsa_pre_rebase = 2;\n", msg="the pre-rebase tip")
    E.fresh()
    E.put(ROWS, "PASS", "2026-01-01T10:00:00")
    E.put([SPEED_ROW], "FAIL", "2026-01-01T09:00:00", tip=pre)
    rc, out = E.run()
    check(rc == 0 and "OPEN RED" not in out, "round 5: a red at the PRE-REBASE tip whose tree diff to the candidate REACHES "
          "the row (its source changed) -> the candidate's own green decides: not carried (%d%s)"
          % (rc, "; " + [l for l in out.splitlines() if "OPEN RED" in l][0][:120] if "OPEN RED" in out else ""))
    twin = speed_commit(E.source, E.scratch, msg="a rebase twin with the SAME tree")
    E.fresh()
    E.put(ROWS, "PASS", "2026-01-01T10:00:00")
    E.put([SPEED_ROW], "FAIL", "2026-01-01T09:00:00", tip=twin)
    rc, out = E.run()
    check(rc == 1 and "OPEN RED carried from %s" % twin[:9] in out, "round 5: ...a red at a non-ancestor twin whose tree "
          "diff does NOT reach the row (the same tree) is the same code's red: carried (%d)" % rc)
    # ROUND 5 (1): A RED REGISTERED BY DESIGN AT ITS LANE'S DELIVERY is a door proof at that lane's candidate (the delivered
    # tip or a descendant), until its recheck; at any other candidate it proves nothing
    for at, want_ok, what, reg in ((E.tip[:9], True, "the delivered tip itself", "lane-x"),
                                   (pre[:9], False, "another lane's candidate that does not descend from the delivered tip",
                                    "lane-z")):
        bd = defect("BYDESIGN-9", [SPEED_ROW], first_seen={"tip": at, "pin": "0" * 9, "run": "20261012T232430-" + at},
                    notes=["2026-10-12T23:24:30+02:00 registered at %s's delivery: an acceptance row RED BY DESIGN" % reg])
        E.fresh()
        E.defects = E.registry(FIXTURE_DEFECTS + [bd])
        E.put(ROWS, "FAIL", "2026-01-01T10:00:00")
        rc, out = E.run("--verdict", "%s=real:BYDESIGN-9 red by design at this lane's delivery" % SPEED_ROW)
        if want_ok:
            check(rc == 0 and ("RED BY DESIGN — registered at %s's delivery" % reg) in out,
                  "round 5: real:<id> of an entry registered BY DESIGN at the lane's delivery clears the row at %s, "
                  "printed as such (%d)" % (what, rc))
        else:
            check(rc == 1 and "proves nothing here" in out, "round 5: ...and proves nothing at %s (%d)" % (what, rc))

    # ROUND 6: THE REGISTERING LANE'S CANDIDATE, WHATEVER ITS TIP AFTER THE TOOL'S REBASE — the entry was registered at
    # lane-x's delivery at `twin` (now rebased away: the candidate E.tip does not descend from it); judged for lane-x, its red
    # at the new tip is cleared by real:<id> and its red at the old tip is never carried
    bd = defect("BYDESIGN-6", [SPEED_ROW], first_seen={"tip": twin[:9], "pin": "0" * 9, "run": "20261012T232430-" + twin[:9]},
                notes=["2026-10-12T23:24:30+02:00 registered at lane-x's delivery: an acceptance row RED BY DESIGN"])
    E.fresh()
    E.defects = E.registry(FIXTURE_DEFECTS + [bd])
    E.put([SPEED_ROW], "FAIL", "2026-01-01T09:00:00", tip=twin)   # a twin: its tree diff reaches nothing (round 5 would carry it)
    E.put(ROWS, "FAIL", "2026-01-01T10:00:00")
    rc, out = E.run("--lane", "lane-x", "--verdict", "%s=real:BYDESIGN-6 red by design at this lane's delivery" % SPEED_ROW)
    check(rc == 0 and (("the candidate is lane-x's" in out and "rebases keep the proof" in out)
                       or "KNOWN RED: real:BYDESIGN-6 RED BY DESIGN" in out) and "OPEN RED" not in out,
          "round 6: a by-design red of the REGISTERING lane, its delivery's tip rebased away -> cleared at the new tip by the "
          "lane, the reason printed; the old tip's red not carried (%d%s)"
          % (rc, "; " + [l for l in out.splitlines() if "REFUSED" in l or "OPEN RED" in l][0][:140]
                  if ("REFUSED" in out or "OPEN RED" in out) else ""))

    # ROUND 7: THE REGISTRY IS THE MERGE'S PROOF — no hand verdict: a by-design row of the lane and a base-reproduced registered
    # defect are SERVED AS KNOWN RED by the select (never run) and ACCEPTED by the merge's judge; a push still refuses them
    def plan_of(*extra):
        rc_, out_ = E.run("--plan", *extra)
        try:
            return json.loads(out_.strip().splitlines()[-1])
        except (ValueError, IndexError):
            return {"served": {}, "run": {}, "err": out_[-300:]}
    bd7 = defect("BYDESIGN-7", [SPEED_ROW], notes=["2026-10-13T00:00:00+02:00 registered at lane-x's delivery: an acceptance "
                                                   "row RED BY DESIGN"])
    E.fresh()
    E.defects = E.registry([x for x in FIXTURE_DEFECTS if SPEED_ROW not in x["rows"]] + [bd7])
    pl = plan_of("--lane", "lane-x")
    check((pl["served"].get(SPEED_ROW) or {}).get("state") == "known" and SPEED_ROW not in pl["run"],
          "round 7 (1): the select SERVES a by-design row of the lane AS KNOWN RED — never run (%s)"
          % ((pl["served"].get(SPEED_ROW) or {}).get("why") or pl.get("err") or "")[:100])
    E.put(ROWS, "FAIL", "2026-01-01T10:00:00")
    rc, out = E.run("--lane", "lane-x")
    check(rc == 0 and "KNOWN RED" in out and "RED BY DESIGN" in out,
          "round 7 (2): the merge's judge reads the registry: the by-design red ACCEPTED with no hand verdict (%d)" % rc)
    rc, out = E.run("--lane", "lane-x", "--mode", "push")
    check(rc == 1, "round 7: ...a push still refuses it (%d)" % rc)
    rc, out = E.run("--lane", "lane-z")
    check(rc == 1, "round 7: ...and another lane's merge is not answered by lane-x's by-design entry (%d)" % rc)
    E.fresh()          # FIXTURE-1 (kind defect) names the row; the base's solos reproduce the red
    E.put(ROWS, "FAIL", "2026-01-01T09:00:00", tip=E.git("rev-parse", "51e9f2c49"), retry=True)
    E.put(ROWS, "FAIL", "2026-01-01T09:05:00", tip=E.git("rev-parse", "51e9f2c49"), retry=True)
    pl = plan_of()
    check((pl["served"].get(SPEED_ROW) or {}).get("state") == "known" and SPEED_ROW not in pl["run"],
          "round 7 (1): the select SERVES a registered defect the base's solos reproduce AS KNOWN RED (%s)"
          % ((pl["served"].get(SPEED_ROW) or {}).get("why") or pl.get("err") or "")[:100])
    E.put(ROWS, "FAIL", "2026-01-01T10:00:00")
    rc, out = E.run()
    check(rc == 0 and "KNOWN RED" in out and "no hand verdict needed" in out,
          "round 7 (2): ...and the merge's judge accepts that red with no hand verdict (%d)" % rc)

    # ROUND 8: THE REGISTRY ANSWERS THE ROW, NOT ONE RECORD — the lane's earlier tip (a same-tree twin: round 5 alone would
    # carry its red) holds a red of a base-reproduced registered row (FIXTURE-1, the base's solos red): the candidate's red
    # is a KNOWN RED by the registry and the earlier tip's red is dropped with it — the lane lands with no hand step
    E.fresh()
    E.put(ROWS, "FAIL", "2026-01-01T09:00:00", tip=E.git("rev-parse", "51e9f2c49"), retry=True)
    E.put([SPEED_ROW], "FAIL", "2026-01-01T09:30:00", tip=twin)
    E.put(ROWS, "FAIL", "2026-01-01T10:00:00")
    rc, out = E.run()
    check(rc == 0 and "KNOWN RED" in out and "OPEN RED" not in out,
          "round 8: an earlier tip's red of a row the registry answers (base-reproduced) is not carried: the lane lands "
          "with no hand verdict (%d%s)" % (rc, "; " + [l for l in out.splitlines() if "OPEN RED" in l][0][:120]
                                          if "OPEN RED" in out else ""))

def case_contention_shape(E):
    """THE REGISTRY'S SHAPE (TESTING_V3 §1.5) — a malformed entry is QUARANTINED, the rest loads (one bad file never
    disables the door); the contention class is the enrolled nondeterminism subset."""
    import shutil
    E.fresh()
    E.put(ROWS[:2], "PASS", "2026-01-01T10:00:00")
    E.put(ROWS, "FAIL", "2026-01-01T10:00:01")
    E.put(ROWS, "FAIL", "2026-01-01T09:00:00", tip=E.git("rev-parse", "51e9f2c49"), retry=True)   # KNOWN on the base
    good = defect("GOOD-1", [SPEED_ROW])
    q = lambda: E.rl.defects_quarantine_dir()
    cases = [("missing `%s`" % m, m, {k: v for k, v in defect("SHAPE-1", [SPEED_ROW]).items() if k != m})
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
        rc, out = E.run("--verdict", "gi.speckle=real:GOOD-1")
        files = os.listdir(q()) if os.path.isdir(q()) else []
        check(rc == 0 and "REGISTRY:" in out and "quarantined" in out and word in out and any(f.endswith(".why") for f in files),
              "%s -> QUARANTINED (printed, a .why sidecar), the rest loads and the door works (%d, %s)" % (name, rc, files))
    shutil.rmtree(q(), ignore_errors=True)
    E.registry([defect("DUP-1", ["x"]), defect("DUP-1", ["y"]), good])
    d, _ = E.rl.defects_load(log=None)
    check(d is not None and "DUP-1" in d and d["DUP-1"]["rows"] == ["x"] and "GOOD-1" in d,
          "a duplicate id: the second is quarantined, the first and the rest load")
    # RUNNER-A1: ONE REGISTRY — a defects.pending/ directory beside it is no source any more (its reader is deleted):
    # an entry there is never loaded; a malformed `sightings` on an entry is quarantined like any malformed field
    shutil.rmtree(q(), ignore_errors=True)
    E.registry([good, NONDET, dict(defect("SIGHT-1", ["api.contract"]), sightings="not a list")])
    pend = os.path.join(os.path.dirname(os.environ["JAH_DEFECTS_FILE"]), "defects.pending")
    os.makedirs(pend, exist_ok=True)
    json.dump(defect("PENDING-1", ["api.contract"]), open(os.path.join(pend, "PENDING-1.json"), "w"))
    import io
    buf = io.StringIO()
    d, _ = E.rl.defects_load(log=buf)
    check(d is not None and "PENDING-1" not in d and "GOOD-1" in d and "SIGHT-1" not in d
          and not hasattr(E.rl, "defects_pending_dir"),
          "one registry: a stray defects.pending/ entry is never read (the reader is gone); a malformed `sightings` is "
          "quarantined (%s)" % sorted(d or {}))
    check(any("SIGHT-1" in f for f, _ in E.rl.defects_quarantined()) and "REGISTRY: " in buf.getvalue(),
          "...the bad entry COPIED to defects.quarantine/ with its .why, printed (%s)" % E.rl.defects_quarantined())
    shutil.rmtree(pend); shutil.rmtree(q(), ignore_errors=True)
    path = os.path.join(E.scratch, "broken.json")
    open(path, "w").write("{ not json")
    os.environ["JAH_DEFECTS_FILE"] = path
    rc, out = E.run()
    check(rc == 2 and "defect registry" in out, "the registry FILE itself unreadable -> the judge is unusable, exit 2 (%d)" % rc)
    E.registry([defect("SHAPE-1", [SPEED_ROW], state={"fixed": {"tip": "abc"}}), NONDET])
    cl = E.rl.contention_list()
    check(list(cl or {}) == [SPEED_ROW] and "NONDET-FIXTURE-1" in cl[SPEED_ROW],
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
    check(one.get("schema") == 3 and two.get("schema") == 3, "the records are schema 3 (RUNNER-A1: config, declaration, data_hash, nulls)")
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
    mw = re.search(r"summed gate wall \(every non-solo run, rc tiers included\): ([\d.]+) h", p.stdout)
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
