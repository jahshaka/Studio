#!/usr/bin/env python3
"""gate.verdict_* — THE JUDGE'S FOUR DOORS, the defect registry's shape and the record's new fields (lane VERDICT-1 +
GATE-LOG-1; ONE_PICTURE_SPEC H1/H2; docs/TESTING_GATE.md §4). Toy run logs only (a PRIVATE run log,
JAH_RUN_LOG_DIR; a PRIVATE defect registry, JAH_DEFECTS_FILE; a PRIVATE token dir, JAH_VRAM_DIR) — no GPU,
no display. One script, five rows:

  verdict_door      THE ONE JUDGE (TESTING-SPEED-A round 27): per key at the tip, green passes; a red is KNOWN by an
                    open entry of the run's build kind (any kind of entry, `nondeterminism` included) or the lead's
                    --verdict (free text) and passes, its id / text printed; any other red refuses; a row that never
                    ran is NEVER-RAN [OWED] and refuses; nothing is carried from another sha; an Xid is printed and
                    changes nothing; a merge reads THIS run's records (--since); --ship judges the sanitizer runs by kind
  noadmit_pool      U2: a toy pool whose every arm is NOADMIT records NOADMIT (and is re-queued as a never-ran
                    row); a mixed one records FAIL with its NOADMIT arms named
  contention_shape  U5 / TESTING_V3 §1.5: a malformed registry entry (a missing field, a bad kind/state/date/found_by,
                    a bad build kind, a duplicate id) is QUARANTINED (copied to defects.quarantine/ with a .why,
                    printed) and the rest loads; an unreadable registry file -> exit 2
  log_schema2       C: a toy run's records carry schema 2, slot_wait_s, drain_s, hold_s, box.queue_depth, box.mem,
                    xid (from vram_tokens' supervise through kernel_xid, end to end); gate-report.py prints the
                    table on the toy log and, on the archive, the preflight's numbers (87 gates, 13 NOADMIT pools)

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


def rec(verdict, ts, retry=False, **kw):
    return dict({"suite": SPEED_ROW, "arm": None, "verdict": verdict, "ts": ts, "retry": retry}, **kw)


def vrec(text, ts):
    return {"kind": "verdict", "suite": SPEED_ROW, "arm": None, "verdict": "VERDICT", "text": text, "ts": ts}


def case_verdict_door(E):
    """THE ONE JUDGE (TESTING-SPEED-A round 27): green passes; a red is KNOWN by an open entry of the run's build kind or
    the lead's verdict, else refused; never-ran is owed; nothing carried from another sha; one judge at every moment."""
    T = "2026-01-01T%s:00"
    base = E.git("rev-parse", "51e9f2c49")
    E.fresh()
    E.put([SPEED_ROW], "PASS", T % "10:00")
    rc, out = E.run()
    check(rc == 0 and "ACCEPTED" in out, "a PASS at the tip -> accepted (%d)" % rc)
    # a red with no entry and no verdict: refused, the row named — the lead decides
    E.fresh()
    E.defects = E.registry([])
    E.put([SPEED_ROW], "FAIL", T % "10:00", failLine="FAIL: the fixture's red")
    rc, out = E.run()
    check(rc == 1 and "RED gi.speckle: FAIL — FAIL: the fixture's red" in out and "the lead decides" in out,
          "a red with no registry entry and no verdict -> REFUSED, the row and its fail line named (%d)" % rc)
    # KNOWN by an open entry naming the row — the same at a merge, a push and a stage close (`--mode` is ignored)
    E.fresh()
    E.put([SPEED_ROW], "FAIL", T % "10:00")
    for m in ([], ["--mode", "push"], ["--mode", "stage-close"], ["--mode", "merge"]):
        rc, out = E.run(*m)
        check(rc == 0 and "KNOWN gi.speckle: KNOWN RED: real:FIXTURE-1" in out,
              "a red an OPEN entry names -> KNOWN, the entry printed, ACCEPTED %s(%d)" % (" ".join(m) + " " if m else "", rc))
    # BY BUILD KIND: an entry measured under ASan never answers a Debug run; the asan run's judge reads it
    E.defects = E.registry([dict(x, build="asan") for x in FIXTURE_DEFECTS])
    rc, out = E.run()
    check(rc == 1 and "RED gi.speckle" in out, "...an ASAN entry naming the row at a Debug run -> REFUSED (%d)" % rc)
    rc, out = E.run("--build-kind", "asan")
    check(rc == 0 and "KNOWN gi.speckle: KNOWN RED: real:FIXTURE-1" in out,
          "...the same red judged as an asan run (--build-kind asan) -> KNOWN by its entry (%d)" % rc)
    # A `nondeterminism` entry is an ordinary open entry: KNOWN (no class, no solos, no census)
    for ent in (NONDET, defect("NOTREPRO-1", [SPEED_ROW], "nondeterminism", uses=1, suspects=["lane-x"],
                               census={"other_ctests": 1})):
        E.defects = E.registry([ent])
        rc, out = E.run()
        check(rc == 0 and ("KNOWN gi.speckle: KNOWN RED: real:%s" % ent["id"]) in out,
              "a nondeterminism entry (%s) behaves as any entry: KNOWN, no 3/3, no census (%d)"
              % ("enrolled" if "enrolled" in ent else "single-use", rc))
    # THE LEAD'S VERDICT (free text; `runner.sh merge|push --door-verdict`): recorded at the tip, the red passes
    E.fresh()
    E.defects = E.registry([])
    E.put([SPEED_ROW], "FAIL", T % "10:00")
    rc, out = E.run("--verdict", "gi.speckle=the lead read the log: the fixture's red")
    vfiles = [f for f in os.listdir(os.environ["JAH_RUN_LOG_DIR"]) if "-verdict-" in f]
    check(rc == 0 and "KNOWN gi.speckle: the lead's verdict: the lead read the log" in out and vfiles,
          "--verdict \"<row>=<text>\" -> recorded at the tip, the red KNOWN with the text, ACCEPTED (%d, %s)" % (rc, vfiles))
    rc, out = E.run()
    check(rc == 0, "...a later judge reads the recorded verdict without the flag (%d)" % rc)
    E.put([SPEED_ROW], "FAIL", "2099-01-01T10:00:00")
    rc, out = E.run()
    check(rc == 1 and "RED gi.speckle" in out, "...a red logged AFTER the verdict is not answered by it (%d)" % rc)
    rc, out = E.run("--verdict", "api.contract=not red")
    check("no verdict recorded for api.contract" in out, "a verdict for a row that is not red is said, not recorded")
    # NEVER-RAN: no run at the tip and none served -> [OWED], refused; a verdict answers a red only
    E.fresh()
    E.defects = E.registry([])
    E.put([SPEED_ROW], "NOADMIT", T % "10:00")
    rc, out = E.run("--verdict", "gi.speckle=it waited too long")
    check(rc == 1 and "NEVER-RAN [OWED]" in out and "NEVER RAN" in out and "no verdict recorded for gi.speckle" in out,
          "a row that never ran (NOADMIT) -> NEVER-RAN [OWED], refused; no verdict answers it (%d)" % rc)
    # NOTHING IS CARRIED FROM ANOTHER SHA: a red at the base and at a lane's other tip, green at the tip -> accepted
    E.fresh()
    E.defects = E.registry([])
    twin = speed_commit(E.source, E.scratch, text="static int tsa_twin_probe = 2;\n", msg="a twin")
    E.put([SPEED_ROW], "FAIL", T % "09:00", tip=base)
    E.put([SPEED_ROW], "FAIL", T % "09:10", tip=twin)
    E.put([SPEED_ROW], "PASS", T % "10:00")
    rc, out = E.run("--lane", "lane-x")
    check(rc == 0 and "carried" not in out, "a red at the base and at the lane's other tip, green at the tip -> ACCEPTED, "
          "nothing carried (%d)" % rc)
    # AN XID on a red is printed on its line (never environmental) and changes nothing else
    E.fresh()
    E.defects = E.registry([])
    E.put([SPEED_ROW], "LOST", T % "10:00", xid={"pid": 4242, "codes": [79]})
    rc, out = E.run()
    check(rc == 1 and "[Xid" in out and "never environmental" in out, "a LOST with an Xid and no entry -> refused, the Xid "
          "printed on its line (%d)" % rc)
    E.defects = E.registry(FIXTURE_DEFECTS)
    rc, out = E.run()
    check(rc == 0 and "[Xid" in out, "...the same with an open entry naming the row -> KNOWN, the Xid still printed (%d)" % rc)
    # A MERGE READS THIS RUN'S RECORDS (--since: the merge run's start)
    E.fresh()
    E.defects = E.registry([])
    E.put([SPEED_ROW], "FAIL", T % "09:00")
    E.put([SPEED_ROW], "PASS", T % "10:00")
    rc, out = E.run("--since", T % "09:30")
    check(rc == 0, "a merge: an earlier fire's red at the same tip, this run green -> ACCEPTED (%d)" % rc)
    E.put([SPEED_ROW], "FAIL", T % "10:30")
    rc, out = E.run("--since", T % "10:15")
    check(rc == 1 and "RED gi.speckle" in out, "...a red of this run without a verdict -> REFUSED (%d)" % rc)
    # THE SANITIZER RUNS BY KIND (--ship's arms): (row, lsan) judged by the asan entries, (row, tsan) by the tsan ones
    d = {e["id"]: e for e in [dict(defect("LSAN-1", [SPEED_ROW]), build="asan")]}
    kr = lambda kind: E.cgc.KnownRegistry(E.cgc.defects_of_kind(d, kind))
    lsan = [rec("FAIL", T % "10:00", arm="lsan")]
    check(E.cgc.state_of((SPEED_ROW, "lsan"), lsan, kr("asan"))[0] == "known"
          and E.cgc.state_of((SPEED_ROW, "lsan"), lsan, kr("debug"))[0] == "red"
          and E.cgc.SANITIZER_ARMS == {"lsan": "asan", "tsan": "tsan"},
          "--ship: an lsan red is KNOWN by an asan entry, red by the debug ones (each arm by its kind)")
    # THE SELECT: a row an open entry names is SERVED as known, never run; a stage-close row the brief names is set apart
    E.fresh()
    rc_, out_ = E.run("--plan")
    try:
        pl = json.loads(out_.strip().splitlines()[-1])
    except (ValueError, IndexError):
        pl = {"served": {}, "run": {}}
    check((pl["served"].get(SPEED_ROW) or {}).get("state") == "known" and SPEED_ROW not in pl["run"],
          "the select serves a row an open entry names as KNOWN, never runs it (%s)"
          % ((pl["served"].get(SPEED_ROW) or {}).get("why") or "")[:80])
    inv = E.rl.row_facts(E.build)[0] if hasattr(E.rl, "row_facts") else {}
    sc = next((n for n, f in sorted(inv.items()) if "stage-close" in (f.get("labels") or [])), None)
    if sc:
        rc_, out_ = E.run("--plan", "--named", sc)
        try:
            pl = json.loads(out_.strip().splitlines()[-1])
        except (ValueError, IndexError):
            pl = {}
        check(sc in (pl.get("stage_close") or []) and sc not in (pl.get("run") or {}),
              "a STAGE-CLOSE row the brief names (%s) never runs in a merge: listed apart" % sc)
    # THE DELETED RULES ARE GONE: no door, no census, no class, no 3/3
    gone = [n for n in ("door", "judge", "_real", "census_of", "Prover", "solo_index", "ancestor_base_proof",
                        "lane_records", "SOLO_NEEDED", "HARD") if hasattr(E.cgc, n)]
    gone += [n for n in ("contention_of", "contention_list", "competitors") if hasattr(E.rl, n)]
    check(not gone, "the deleted rules are gone from ci_gate_check / gate_runlog (%s)" % gone)

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


def case_contention_shape(E):
    """THE REGISTRY'S SHAPE (TESTING_V3 §1.5) — a malformed entry is QUARANTINED, the rest loads (one bad file never
    disables the judge)."""
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
                                                                                      uses=1)),
              ("a build kind outside debug|asan|tsan", "build", defect("SHAPE-1", ["x"], build="msan"))]
    for name, word, bad in cases:
        shutil.rmtree(q(), ignore_errors=True)
        E.registry([bad, good])
        rc, out = E.run()
        files = os.listdir(q()) if os.path.isdir(q()) else []
        check(rc == 0 and "REGISTRY:" in out and "quarantined" in out and word in out and any(f.endswith(".why") for f in files),
              "%s -> QUARANTINED (printed, a .why sidecar), the rest loads and the judge works (%d, %s)" % (name, rc, files))
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
    p = subprocess.run([sys.executable, rep, d], capture_output=True, text=True)
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
    p = subprocess.run([sys.executable, rep, d2, "--week", "2026-10-09T17:30"], capture_output=True, text=True)
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
    p = subprocess.run([sys.executable, rep] + dirs + ["--week", "2026-10-09T17:30"], capture_output=True, text=True)
    last = (p.stdout.strip().splitlines() or [""])[-1]
    print("  " + last)
    check(p.returncode == 0 and last.startswith("TABLE gates 87 | NOADMIT pools 13"),
          "on the archive, the preflight's numbers: 87 gates, 13 NOADMIT pools")


CASES = {"verdict_door": case_verdict_door, "noadmit_pool": case_noadmit_pool,
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
