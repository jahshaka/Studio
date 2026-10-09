#!/usr/bin/env python3
"""gate.fix_round — THE FIX ROUND RE-USES WHAT IT CANNOT REACH, and the targets never decide a gate
(lane GATE-SPEED-1; docs/TESTING_GATE.md §3b made mechanical, the gate-speed audit's R1/S1/L1-L3).

The merge refusal (scripts/ci_gate_check.py) used to key every record on the EXACT tip, so a one-line
fix re-ran the lane's whole selection (45.9 % of all suite-time since 09-28). It now takes, for a row
with no record at the tip, the newest record of that row at an earlier commit A of the lane — when
the scoped selection of A..tip does not reach the row and the fork pin did not move. Proved on
RECORDED lanes of this clone against a PRIVATE run log (JAH_RUN_LOG_DIR) and a PRIVATE contention
list (JAH_CONTENTION_FILE):

  1. D11-LIBRARY-SCALE's review fix 4 (5ba5abcce: src/ui/controls/librarymodel.cpp only) after its
     fix 3 (1e21df220): the lane's rows green at 1e21df220 and the fix's own selection green at
     5ba5abcce -> ACCEPTED, the untouched rows re-used from 1e21df220, each row naming its commit;
  2. the same, with ONE row the fix reaches missing at 5ba5abcce -> REFUSED, naming it (a green at
     1e21df220 does not stand in for a row the fix reaches);
  3. CONTACT-OCCLUSION-1's 1775e79f0 -> 684e0f7aa moves the FORK PIN: every row green at 1775e79f0 and
     nothing at 684e0f7aa -> REFUSED, "no re-use across" (a pin change invalidates every earlier record);
  4. a row green at 0dba35f9e and RED at the later 1e21df220 (neither reached by the fix) -> REFUSED: the
     later red blocks the older green; `--verdict` records its answer AT 1e21df220 -> accepted;
  5. THE TARGETS NEVER SET THE EXIT CODE: gate-scope's `--run` returns the gating phases' code; the
     target step runs AFTER the verdict line, inside the gate (GATE-COST-1 P9: the gate's display, the
     whole card) and its red does not change the exit; `--no-targets` skips it; `--targets-only` runs
     the targets under the run log's `target` tier and exits 0 when they fail;
  and the run log's three fixes: L1 other_ctests excludes the gate's OWN process tree, L2 an
  admission's `vram: admitted … after <s> s` is the row's tokenWaitS (and leaves its seconds), L3 a
  red row records its first FAIL line.

Run: gate_fix_round_test.py <source-dir> <build-dir>
"""
import importlib.util
import io
import json
import os
import shutil
import subprocess
import sys
import tempfile
import contextlib

FAILURES = []

BASE, T0, A, B = "a859f31ed", "0dba35f9e", "1e21df220", "5ba5abcce"      # D11-LIBRARY-SCALE
F_BASE, F_A, F_B = "dd0ef3776", "1775e79f0", "684e0f7aa"                    # CONTACT-OCCLUSION-1


def check(ok, what):
    print(("  ok: " if ok else "  FAIL: ") + what)
    if not ok: FAILURES.append(what)


def main(source, build):
    scripts = os.path.join(source, "scripts")
    sys.path.insert(0, scripts)
    import gate_runlog
    import ci_gate_check as cgc
    gs = cgc.load_gs()
    tool = os.path.join(scripts, "ci-gate-check.sh")
    sha = lambda r: subprocess.run(["git", "rev-parse", r], cwd=source, capture_output=True, text=True).stdout.strip()
    revs = {r: sha(r) for r in (BASE, T0, A, B, F_BASE, F_A, F_B)}
    missing = [r for r, s in revs.items() if not s]
    check(not missing, "the recorded lanes' commits are in this clone (%s)" % missing)
    if missing: return 1
    pin = lambda c: sha(f"{c}:irisgl")
    scratch = tempfile.mkdtemp(prefix="gate-fix-round-")
    contention = os.path.join(scratch, "contention.json")
    json.dump({"suites": {}}, open(contention, "w"))
    os.environ["JAH_CONTENTION_FILE"] = contention

    import copy
    import gate_graph
    graph = gate_graph.NinjaGraph.load(build)
    inv0 = gs.load_inventory(build)

    def sel(rng):
        return gs.select(gs.touched_paths(rng), rng, build, gs.GATE_JOBS, graph=graph, inv=copy.deepcopy(inv0),
                         quiet_graph=True)

    def need(rng):
        return cgc.needed_rows(gs, sel(rng))[0]

    def fresh():
        d = tempfile.mkdtemp(dir=scratch)
        os.environ["JAH_RUN_LOG_DIR"] = d
        return d

    def put(keys, verdict, ts, at):
        s = revs.get(at, at)
        gate_runlog.append_records([{"suite": k[0], "arm": k[1], "verdict": verdict, "ts": ts, "retry": False,
                                     "tip": {"studio": s, "studio_dirty": False, "irisgl": pin(s),
                                             "irisgl_dirty": False}} for k in keys], "scoped", s)

    def run(rng, *extra):
        p = subprocess.run([tool, rng, "--build", build] + list(extra), capture_output=True, text=True,
                           env=dict(os.environ))
        return p.returncode, p.stdout

    lane, fix = f"{BASE}..{B}", f"{A}..{B}"
    lane_need, fix_need = need(lane), need(fix)
    fix_keys = set(fix_need)
    print(f"  (fixture: {lane} needs {len(lane_need)} rows, the fix {fix} {len(fix_need)})")
    check(0 < len(fix_need) < len(lane_need), "the UI-only fix selects fewer rows than its lane (%d < %d)"
          % (len(fix_need), len(lane_need)))

    # ---- 1. a fix round touching only Studio UI re-uses the ancestor's records -----------------------
    fresh()
    put(lane_need, "PASS", "2026-01-01T10:00:00", A)            # the lane's gate at its fix-3 tip
    put(fix_need, "PASS", "2026-01-01T11:00:00", B)             # the fix round: A..B --run
    rc, out = run(lane)
    reused = len(lane_need) - len([k for k in lane_need if k in fix_keys])
    check(rc == 0, "the fix round's selection at the tip + the lane's earlier records -> ACCEPTED (%d)" % rc)
    check(f"{reused} re-used from {revs[A][:9]}" in out,
          "...%d untouched rows re-used from %s, said in the summary" % (reused, A))
    row_lines = [l for l in out.splitlines() if l.startswith("ci-gate-check: row ")]
    check(len(row_lines) == len(lane_need) and all(" <- " in l for l in row_lines),
          "...and EVERY row prints the commit its record came from (%d lines)" % len(row_lines))
    row_keys = sorted(k for k in fix_keys if k[1] is None)
    some_fix = row_keys[0]
    check(any(l.startswith(f"ci-gate-check: row {some_fix[0]} <- {revs[B][:9]} (the tip)") for l in row_lines),
          "...a row the fix reaches is answered at the tip (%s)" % some_fix[0])

    # ---- 2. a row the fix reaches is demanded at the tip ---------------------------------------------
    fresh()
    dropped = row_keys[0]
    put(lane_need, "PASS", "2026-01-01T10:00:00", A)
    put([k for k in fix_need if k != dropped], "PASS", "2026-01-01T11:00:00", B)
    rc, out = run(lane)
    check(rc == 1 and "no record" in out and dropped[0] in out and f"{A[:9]}..{B[:9]} reaches it" in out,
          "a row the fix reaches, green only at %s -> REFUSED, naming %s (%d)" % (A, dropped[0], rc))

    # ---- 3. a fork pin change invalidates everything -------------------------------------------------
    fresh()
    f_need = need(f"{F_BASE}..{F_B}")
    check(sha(f"{F_A}:irisgl") != sha(f"{F_B}:irisgl"), "the fork fixture's two commits pin different engines")
    put(f_need, "PASS", "2026-01-01T10:00:00", F_A)
    rc, out = run(f"{F_BASE}..{F_B}")
    check(rc == 1 and "the fork pin moved" in out and "no re-use across" in out,
          "every row green at %s, the pin moved in %s..%s -> REFUSED, no re-use across it (%d)" % (F_A, F_A, F_B, rc))

    # ---- 4. a later red without a verdict blocks reuse -----------------------------------------------
    reach_t0 = sel(f"{T0}..{B}")
    t0_keys = {(n, None) for n in reach_t0.selected}
    cand = [k for k in lane_need if k[1] is None and k not in t0_keys and k not in fix_keys]
    check(bool(cand), "fixture: a row of the lane that %s..%s does not reach (%d)" % (T0, B, len(cand)))
    if cand:
        k = cand[0]
        fresh()
        put(lane_need, "PASS", "2026-01-01T09:00:00", T0)                 # the lane's gate at T0
        put([x for x in lane_need if x != k], "PASS", "2026-01-01T10:00:00", A)  # a later round at A...
        put([k], "FAIL", "2026-01-01T10:00:01", A)                         # ...where k went red
        put(fix_need, "PASS", "2026-01-01T11:00:00", B)
        rc, out = run(lane)
        check(rc == 1 and f"red at {revs[A][:9]}" in out and k[0] in out,
              "%s green at %s, RED at the later %s, nothing at the tip -> REFUSED (%d)" % (k[0], T0, A, rc))
        rc, out = run(lane, "--verdict", f"{k[0]}=real:FIXTURE-1 the fixture's red, answered where it happened")
        vfiles = [f for f in os.listdir(os.environ["JAH_RUN_LOG_DIR"]) if "-verdict-" in f]
        if rc != 0:
            print("\n".join(l for l in out.splitlines() if "REFUSED" in l)[:2000])
        check(rc == 0 and any(revs[A][:9] in f for f in vfiles),
              "...a verdict is recorded AT %s (where the red happened) and the lane is accepted (%d, %s)"
              % (A, rc, vfiles))

    # ---- 4b. THE FLAKE LAW ACROSS COMMITS (the lead's read, F1): a green at the tip does not answer an
    # earlier red the fix does not reach; it does answer one the fix reaches ------------------------
    if cand:
        k = cand[0]
        fresh()
        put([x for x in lane_need if x not in (k, some_fix)], "PASS", "2026-01-01T10:00:00", A)
        put([k, some_fix], "FAIL", "2026-01-01T10:00:01", A)
        put(lane_need, "PASS", "2026-01-01T11:00:00", B)                 # everything re-run green at the tip
        rc, out = run(lane)
        check(rc == 1 and f"OPEN red at {revs[A][:9]}" in out and k[0] in out
              and not any(l.startswith(f"ci-gate-check: RED {some_fix[0]}:") for l in out.splitlines()),
              "%s red at %s (the fix does not reach it), green at the tip -> REFUSED; %s, which the fix reaches, "
              "is answered by the tip (%d)" % (k[0], A, some_fix[0], rc))
        rc, out = run(lane, "--verdict", f"{k[0]}=real:FIXTURE-1 read: the fixture's red")
        check(rc == 0, "...its verdict (recorded at %s) answers it (%d)" % (A, rc))

    # ---- 5. the targets never set the exit code ------------------------------------------------------
    spec = importlib.util.spec_from_file_location("gate_scope_t", os.path.join(scripts, "gate-scope.py"))
    g = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(g)
    calls = []

    def fake_run(cmd, cwd, tier, lane_, jobs, reasons=None, gating=None, **kw):
        is_target = "chain_face_target" in cmd.replace("\\", "")
        calls.append((tier, is_target))
        return 8 if is_target else 0

    real_rc, real_fpp = g.gate_runlog.run_ctest, g.gate_runlog.fork_pin_problem
    g.gate_runlog.run_ctest = fake_run
    g.gate_runlog.fork_pin_problem = lambda *a, **k: None
    old_vram = os.environ.get("JAH_VRAM_DIR")
    os.environ["JAH_VRAM_DIR"] = os.path.join(scratch, "vram")     # the slot and the card: a private queue
    held = os.environ.pop("JAH_GATE_SLOT_HELD", None)

    def gs_main(argv):
        sys.argv = ["gate-scope.py"] + argv
        buf = io.StringIO()
        code = None
        with contextlib.redirect_stdout(buf):
            try:
                g.main()
            except SystemExit as e:
                code = e.code
        return code, buf.getvalue()

    files = ["--files", "tests/gi/test_gi_chain_face.cpp", "--build", build, "--lane", "gate-fix-round-test"]
    code, out = gs_main(files + ["--run"])
    gating = [c for c in calls if c[0] != "target"]
    check(code == 0 and gating and not any(t for _, t in gating) and calls[-1] == ("target", True),
          "--run: the gating phases' exit (0) is the gate's, though the target step after them read 8 (%r, %r)"
          % (code, calls))
    check("GATE VERDICT: GREEN" in out and out.index("GATE VERDICT") < out.index("target tests exited 8"),
          "...the verdict is printed BEFORE the target step runs")
    calls.clear()
    code, out = gs_main(files + ["--run", "--no-targets"])
    check(code == 0 and calls and not any(c[0] == "target" for c in calls),
          "--no-targets: the target step does not run (%r)" % code)
    calls.clear()
    code, out = gs_main(files + ["--run", "--targets-only"])
    check(code in (0, None) and calls == [("target", True)] and "exited 8" in out,
          "--targets-only: the targets run alone under the run log's `target` tier, read 8, and exit 0 (%r, %r)"
          % (code, calls))
    g.gate_runlog.run_ctest, g.gate_runlog.fork_pin_problem = real_rc, real_fpp
    os.environ.pop("JAH_GATE_SLOT_HELD", None)
    if held is not None: os.environ["JAH_GATE_SLOT_HELD"] = held
    if old_vram is None: os.environ.pop("JAH_VRAM_DIR", None)
    else: os.environ["JAH_VRAM_DIR"] = old_vram

    # ---- L1 / L2 / L3: the run log -------------------------------------------------------------------
    d = fresh()
    fake = os.path.join(scratch, "fake_ctest.py")
    open(fake, "w").write(
        "#!/usr/bin/env python3\n"
        "import sys\n"
        "assert '-V' in sys.argv\n"     # the runner reads every row's own lines from ctest -V (GATE-COST-1 P6)
        "print('      Start  1: s.one'); print('1: Test command: /bin/true'); print('1: Test timeout computed to be: 60')\n"
        "print('1: vram: admitted with 2 tokens 3,4 after 4.0 s — s.one'); print('1: ok: fine')\n"
        "print('1/2 Test  #1: s.one ..........   Passed   10.00 sec')\n"
        "print('      Start  2: s.two'); print('2: ok: a'); print('2: FAILED: 2 check(s)')\n"
        "print('2: FAIL: the first assertion'); print('2: FAIL: the second')\n"
        "print('2/2 Test  #2: s.two ..........***Failed    5.00 sec')\n")
    os.chmod(fake, 0o755)
    gate_runlog.run_ctest(fake, scratch, "scoped", "gate-fix-round-test", 1, echo=False)
    recs = {}
    for f in os.listdir(d):
        for l in open(os.path.join(d, f)):
            r = json.loads(l); recs[r["suite"]] = r
    one, two = recs.get("s.one", {}), recs.get("s.two", {})
    check(one.get("tokenWaitS") == 4.0 and one.get("seconds") == 6.0 and one.get("wallSeconds") == 10.0,
          "L2: the admission's wait is tokenWaitS and leaves the row's seconds (%r)"
          % {k: one.get(k) for k in ("tokenWaitS", "seconds", "wallSeconds")})
    check(two.get("failLine") == "FAIL: the first assertion" and "failLine" not in one,
          "L3: a red row records its FIRST FAIL line, a green one none (%r)" % two.get("failLine"))
    check(gate_runlog.fail_line("[log] script: Error: x.js:73 FAILED after 9 ms\nfile:///a/b/x.js:73: Error: assert failed: "
                                "idle: budget") == "x.js:73: Error: assert failed: idle: budget",
          "L3: a --script suite's red records its runner's assertion line")
    # L1: a ctest under this process (or under the given root) is ours; an orphan in another tree is not
    child = subprocess.Popen(["sleep", "30"])
    orphan = subprocess.run(["sh", "-c", "setsid sleep 30 > /dev/null 2>&1 & echo $!"], capture_output=True,
                            text=True).stdout.strip()
    real_run = gate_runlog.subprocess.run

    def fake_pgrep(args, **kw):
        if args[:2] == ["pgrep", "-x"]:
            return subprocess.CompletedProcess(args, 0, f"{child.pid}\n{orphan}\n", "")
        return real_run(args, **kw)
    gate_runlog.subprocess.run = fake_pgrep
    try:
        n_own = gate_runlog.other_ctests()
    finally:
        gate_runlog.subprocess.run = real_run
        child.kill(); child.wait()
        if orphan.isdigit():
            try: os.kill(int(orphan), 9)
            except OSError: pass
    check(n_own == 1, "L1: other_ctests counts the orphan and NOT this process's own child (%r)" % n_own)

    shutil.rmtree(scratch, ignore_errors=True)
    if FAILURES:
        print("gate.fix_round: FAILED (%d)" % len(FAILURES)); return 1
    print("gate.fix_round: PASSED"); return 0


if __name__ == "__main__":
    if len(sys.argv) < 3:
        print(__doc__); sys.exit(2)
    sys.exit(main(os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2])))
