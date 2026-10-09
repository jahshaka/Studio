#!/usr/bin/env python3
"""gate.cost — HOW GATES SHARE THE BOX (lane GATE-COST-1; SPECS/audits/GATE_COST_2026-10-09.md P1 P2
P5 P6 P8 P9; docs/TESTING_GATE.md §4b/§4c). Every case drives the REAL code (scripts/vram_tokens.py,
scripts/gate_runlog.py, scripts/gate-scope.py) against a toy ctest project configured here, a PRIVATE
token directory (JAH_VRAM_DIR), a PRIVATE run log (JAH_RUN_LOG_DIR) and a fake X server — never the
box's own queue, log or displays:

  1. P1 ONE GATE AT A TIME: a gate holds the slot; a second gate (`gate_runlog.py run`, what rc-gate
     runs) prints its queue position and runs only after the first is gone; a third waits behind both
     (FIFO); `--solo` and a plain admission never take the slot; gate-scope's --run takes it once and
     hands its fd to every phase;
  2. P5 NOADMIT RE-QUEUED IN-RUN: a row that got no admission is re-run at the end of the same run and
     recorded ONCE (its passing run); a row that never gets one is recorded once, as NOADMIT, after
     its JAH_GATE_REQUEUE tries — and the run is red;
  3. P6 PER-ROW RECORDS + --resume: a ctest killed mid-run leaves every row it finished in the run log;
     a re-run with the recorded rows excluded runs exactly the rest; gate-scope --resume hands the
     tip's recorded rows to every phase;
  4. P6 DEAD DISPLAY: the X server dies under a run -> the run stops (DISPLAY_LOST), the abort line
     says why, rows that ended after it are NOT recorded; gate-scope prints `GATE VERDICT: ABORTED`;
  5. P8 THE CPU PHASE FIRST: the `hygiene` row ends before any GPU row starts;
  6. P2/P9 THE WHOLE CARD ONCE PER PHASE: a timing phase and the target step hold every token once and
     their rows run nested on it (`already admitted by the parent`); gate-scope runs the target step
     AFTER the verdict line, inside the gate, with the slot's fd and whole_card.

Run: gate_cost_test.py <source-dir> <build-dir>   (the build dir is not read; ctest is found on PATH)
"""
import contextlib
import importlib.util
import io
import json
import os
import shutil
import signal
import subprocess
import sys
import tempfile
import threading
import time

FAILURES = []


def check(ok, what):
    print(("  ok: " if ok else "  FAIL: ") + what)
    if not ok: FAILURES.append(what)


TOY = r'''
cmake_minimum_required(VERSION 3.20)
project(toy NONE)
enable_testing()
set(T "$ENV{TOYDIR}")
add_test(NAME lint.one COMMAND sh -c "sleep 1; echo lint.one >> ${T}/order")
set_tests_properties(lint.one PROPERTIES LABELS "hygiene")
add_test(NAME gpu.a COMMAND sh -c "echo gpu.a >> ${T}/order; echo ok")
add_test(NAME gpu.b COMMAND sh -c "echo gpu.b >> ${T}/order; echo ok")
add_test(NAME gpu.slow COMMAND sh -c "echo gpu.slow >> ${T}/order; sleep \${TOY_SLOW:-0}; echo ok")
add_test(NAME gpu.noadmit_once COMMAND sh -c "if [ -e ${T}/na ]; then echo ok; else touch ${T}/na; echo 'NOADMIT vram: no admission for 2 tokens within 1 s (0 of 3 free at the last look) - gpu.noadmit_once'; exit 75; fi")
add_test(NAME gpu.noadmit_always COMMAND sh -c "echo 'NOADMIT vram: no admission for 2 tokens within 1 s (0 of 3 free at the last look) - gpu.noadmit_always'; exit 75")
add_test(NAME time.card COMMAND sh -c "python3 ${VT} status > ${T}/card.time; python3 ${VT} admit 1 -- true 2>> ${T}/card.time")
set_tests_properties(time.card PROPERTIES LABELS "timing" FIXTURES_REQUIRED timeHome)
add_test(NAME time.card.home COMMAND sh -c "echo setup")
set_tests_properties(time.card.home PROPERTIES FIXTURES_SETUP timeHome)
add_test(NAME tgt.card COMMAND sh -c "python3 ${VT} status > ${T}/card.tgt; python3 ${VT} admit 1 -- true 2>> ${T}/card.tgt")
set_tests_properties(tgt.card PROPERTIES LABELS "photon-target")
'''
LABELS = {"lint.one": ["hygiene"], "time.card": ["timing"], "tgt.card": ["photon-target"]}


def main(source, build):
    scripts = os.path.join(source, "scripts")
    sys.path.insert(0, scripts)
    # this row runs INSIDE a gate: none of that gate's state may leak into the simulated ones
    for k in ("JAH_GATE_SLOT_HELD", "JAH_VRAM_HELD", "JAH_VRAM_ALL", "JAH_GATE_SLOT", "DISPLAY", "JAH_POOL_ARMS"):
        os.environ.pop(k, None)
    scratch = tempfile.mkdtemp(prefix="gate-cost-test-")
    toy, tb, state = (os.path.join(scratch, d) for d in ("src", "b", "state"))
    for d in (toy, state):
        os.makedirs(d)
    vt = os.path.join(scripts, "vram_tokens.py")
    os.environ.update(JAH_VRAM_DIR=os.path.join(scratch, "vram"), JAH_VRAM_TOKENS="3", JAH_VRAM_WAIT="5",
                      JAH_RUN_LOG_DIR=os.path.join(scratch, "runs"), TOYDIR=state, JAH_GATE_REQUEUE="2")
    open(os.path.join(toy, "CMakeLists.txt"), "w").write(TOY.replace("${VT}", vt))
    r = subprocess.run(["cmake", "-S", toy, "-B", tb], capture_output=True, text=True)
    if r.returncode != 0:
        print(r.stdout[-2000:], r.stderr[-2000:])
        check(False, "the toy ctest project configures"); return 1
    import gate_runlog as rl
    import vram_tokens as vt_mod
    clean = {"studio": "c" * 40, "irisgl": "d" * 40, "fork": "e" * 40, "studio_dirty": False, "irisgl_dirty": False}
    rl.tree_shas = lambda: dict(clean)                   # a clean tree, whatever this checkout's state

    def reset():
        shutil.rmtree(os.environ["JAH_RUN_LOG_DIR"], ignore_errors=True)
        for f in os.listdir(state):
            os.unlink(os.path.join(state, f))

    def records():
        out, d = [], os.environ["JAH_RUN_LOG_DIR"]
        for f in sorted(os.listdir(d)) if os.path.isdir(d) else []:
            out += [json.loads(l) for l in open(os.path.join(d, f))]
        return out

    def order():
        try: return open(os.path.join(state, "order")).read().split()
        except OSError: return []

    def run(rx, jobs=1, **kw):
        cmd = f"ctest -j{jobs} --timeout 60 --output-on-failure --no-tests=error -R '{rx}'"
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            rc = rl.run_ctest(cmd, tb, "scoped", "gate-cost-test", jobs, labels=LABELS, **kw)
        return rc, buf.getvalue()

    # ---- 1. P1: one gate at a time, FIFO, no bound ------------------------------------------------
    print("1. the gate slot")
    reset()
    t0 = time.time()
    g1 = subprocess.Popen([sys.executable, vt, "gate", "--label", "G1", "--", "sh", "-c",
                           f"sleep 3; date +%s.%N > {state}/g1.end"], stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT, text=True)
    time.sleep(0.8)
    g2 = subprocess.Popen([sys.executable, os.path.join(scripts, "gate_runlog.py"), "run", "--tier", "scoped",
                           "--lane", "G2", "--build", tb, "--", f"ctest -j1 --output-on-failure -R '^gpu\\.a$'"],
                          cwd=source, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                          env=dict(os.environ, JAH_RUN_LOG_DIR=os.path.join(scratch, "runs-g2")))
    time.sleep(0.8)
    g3 = subprocess.Popen([sys.executable, vt, "gate", "--label", "G3", "--", "sh", "-c",
                           f"date +%s.%N > {state}/g3.start"], stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT, text=True)
    time.sleep(0.8)
    st = subprocess.run([sys.executable, vt, "status"], capture_output=True, text=True).stdout
    o1, o2, o3 = (p.communicate(timeout=120)[0] for p in (g1, g2, g3))
    g1_end = float(open(os.path.join(state, "g1.end")).read())
    g3_start = float(open(os.path.join(state, "g3.start")).read())
    g2_recs = [json.loads(l) for f in os.listdir(os.path.join(scratch, "runs-g2"))
               for l in open(os.path.join(scratch, "runs-g2", f))] if os.path.isdir(os.path.join(scratch, "runs-g2")) else []
    check("gate-slot: taken" in o1 and "HELD by" in st and "G1" in st.split("HELD by", 1)[1].splitlines()[0],
          "the first gate takes the slot and `status` names it the holder")
    check("queued at position 1" in o2 and "behind" in o2 and "G1" in o2.split("queued at position 1", 1)[1].splitlines()[0],
          "the second gate (gate_runlog.py run) prints its position (1) and whom it waits behind")
    check("queued at position 2" in o3, "the third prints position 2 (FIFO: behind the holder and the second)")
    check(g2.returncode == 0 and len(g2_recs) == 1 and g2_recs[0]["verdict"] == "PASS",
          "the second gate runs its row after the wait (rc %r, %d record(s))" % (g2.returncode, len(g2_recs)))
    ts2 = max((time.mktime(time.strptime(r["ts"][:19], "%Y-%m-%dT%H:%M:%S")) for r in g2_recs), default=0)
    check(ts2 + 1 >= int(g1_end) and g3_start >= g1_end, "nothing ran before the holder was gone "
          "(holder ended %.1f s in; the third started %.1f s in)" % (g1_end - t0, g3_start - t0))
    check(time.time() - t0 < 60, "the queue has no 900 s bound and no stall (%.0f s)" % (time.time() - t0))
    # small things never take it
    holder = subprocess.Popen([sys.executable, vt, "gate", "--label", "H", "--", "sleep", "20"],
                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(0.8)
    p = subprocess.run([sys.executable, vt, "admit", "1", "--", "true"], capture_output=True, text=True, timeout=30)
    check(p.returncode == 0 and "gate-slot" not in p.stderr, "a plain admission (a hand run) never takes the slot")
    os.environ["JAH_GATE_SLOT_HELD"] = "1"
    check(vt_mod.gate_slot("nested", log=io.StringIO()) is None, "a process already inside a gate never queues again")
    os.environ.pop("JAH_GATE_SLOT_HELD")
    holder.terminate(); holder.wait()

    # ---- 2. P5: NOADMIT re-queued in the same run, recorded once ---------------------------------
    print("2. NOADMIT re-queued in-run")
    reset()
    rc, out = run("^(gpu\\.a|gpu\\.noadmit_once)$")
    recs = [r for r in records() if r["suite"] == "gpu.noadmit_once"]
    check(rc == 0 and len(recs) == 1 and recs[0]["verdict"] == "PASS" and "re-queued 1 row" in out,
          "a row with no admission is re-queued at the end of the run and recorded ONCE, its passing run "
          "(rc %r, %r)" % (rc, [r["verdict"] for r in recs]))
    reset()
    rc, out = run("^(gpu\\.a|gpu\\.noadmit_always)$")
    recs = [r for r in records() if r["suite"] == "gpu.noadmit_always"]
    check(rc != 0 and len(recs) == 1 and recs[0]["verdict"] == "NOADMIT" and out.count("re-queued 1 row") == 2,
          "a row that never gets one is tried 1 + JAH_GATE_REQUEUE (2) times and recorded once, NOADMIT, the run red "
          "(rc %r, %r, %d re-queue(s))" % (rc, [r["verdict"] for r in recs], out.count("re-queued 1 row")))

    # ---- 3. P6: a killed ctest keeps what it finished; the rest runs on resume --------------------
    print("3. per-row records and resume")
    reset()
    os.environ["TOY_SLOW"] = "40"
    res = {}
    t = threading.Thread(target=lambda: res.update(r=run("^(lint\\.one|gpu\\.a|gpu\\.b|gpu\\.slow)$")))
    t.start()
    deadline = time.time() + 40
    while "gpu.slow" not in order() and time.time() < deadline:
        time.sleep(0.2)
    me = os.getpid()
    ctests = [p for p in rl._descendants(me)
              if open(f"/proc/{p}/cmdline", "rb").read().split(b"\0")[0].endswith(b"ctest")]
    for p in ctests + [p for c in ctests for p in rl._descendants(c)]:
        try: os.kill(p, signal.SIGKILL)
        except OSError: pass
    t.join(60)
    os.environ.pop("TOY_SLOW")
    finished = [n for n in order() if n != "gpu.slow"]
    got = {r["suite"] for r in records()}
    check(ctests and "gpu.slow" not in got and set(finished) <= got and "lint.one" in got,
          "a ctest killed mid-run leaves every row it finished in the run log (%s) and none it did not" % sorted(got))
    check("ctest was killed" in res.get("r", (0, ""))[1], "...and says so, naming --resume")
    done = rl.recorded_rows(clean)
    open(os.path.join(state, "order"), "w").close()
    rc, out = run("^(lint\\.one|gpu\\.a|gpu\\.b|gpu\\.slow)$", exclude=done)
    again = order()
    got2 = [r["suite"] for r in records()]
    check(rc == 0 and set(again) == {"lint.one", "gpu.a", "gpu.b", "gpu.slow"} - done and
          sorted(got2) == sorted(set(got2)) and set(got2) == {"lint.one", "gpu.a", "gpu.b", "gpu.slow"},
          "the re-run with the recorded rows excluded runs exactly the rest (%s); every row has one record"
          % sorted(again))

    # ---- 4. P6: a dead display stops the run --------------------------------------------------------
    print("4. a dead display")
    reset()
    xroot = os.path.join(scratch, "x11")
    os.makedirs(os.path.join(xroot, ".X11-unix"))
    sock = os.path.join(xroot, ".X11-unix", "X77")
    fake_x = subprocess.Popen([sys.executable, "-c",
                               "import socket,time,sys\ns=socket.socket(socket.AF_UNIX)\ns.bind(sys.argv[1])\n"
                               "s.listen(16)\nwhile True: time.sleep(1)", sock])
    for _ in range(50):
        if os.path.exists(sock): break
        time.sleep(0.1)
    open(os.path.join(xroot, ".X77-lock"), "w").write("%10d\n" % fake_x.pid)
    os.environ["JAH_X11_ROOT"] = xroot
    os.environ["TOY_SLOW"] = "30"
    env = dict(os.environ, DISPLAY=":77")
    res = {}
    t = threading.Thread(target=lambda: res.update(r=run("^(lint\\.one|gpu\\.a|gpu\\.slow)$", env=env)))
    t.start()
    deadline = time.time() + 40
    while "gpu.slow" not in order() and time.time() < deadline:
        time.sleep(0.2)
    fake_x.kill(); fake_x.wait()
    t.join(60)
    os.environ.pop("TOY_SLOW")
    rc, out = res.get("r", (None, ""))
    got = {r["suite"] for r in records()}
    check(rc == rl.DISPLAY_LOST and "GATE ABORTED" in out and "is gone" in out and rl.ABORTED,
          "the X server dies under a run -> it stops, DISPLAY_LOST, the abort line says why (rc %r)" % rc)
    check("gpu.slow" not in got and {"lint.one", "gpu.a"} <= got,
          "rows that ended before the death are recorded, the row that ended after it is not (%s)" % sorted(got))
    rc, out = run("^gpu\\.a$", env=env)
    check(rc == rl.DISPLAY_LOST and "before its first row" in out, "a run on a display already dead refuses to start")
    os.environ.pop("JAH_X11_ROOT")

    # ---- 5. P8: the CPU phase first -----------------------------------------------------------------
    print("5. the CPU phase")
    reset()
    rc, out = run("^(lint\\.one|gpu\\.a|gpu\\.b)$", jobs=3)
    o = order()
    check(rc == 0 and o[:1] == ["lint.one"] and "the CPU phase: 1" in out and out.index("the CPU phase") < out.index("the GPU phase"),
          "the hygiene row runs (and ends) in its own phase before any GPU row starts, at -j3 (%s)" % o)

    # ---- 6. P2/P9: the whole card once per phase ---------------------------------------------------
    print("6. the whole card")
    reset()
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):      # the serial phase as rc-gate starts it: -L, a fixture pulled in
        rc = rl.run_ctest("ctest -j1 --timeout 60 --output-on-failure -L '^timing$'", tb, "scoped", "gate-cost-test", 1,
                          labels=LABELS)
    out = buf.getvalue()
    card = open(os.path.join(state, "card.time")).read()
    check(rc == 0 and "3 of 3 tokens held" in card and "already admitted by the parent" in card
          and "whole card (3 tokens) held for the phase" in out and "time.card.home" in out,
          "a timing phase (`-L ^timing$`, its unlabelled fixture pulled in) holds every token once; its row runs "
          "nested on them")
    rc, out = run("^tgt\\.card$", whole_card=True)
    card = open(os.path.join(state, "card.tgt")).read()
    check(rc == 0 and "3 of 3 tokens held" in card and "already admitted by the parent" in card,
          "the target step (whole_card) holds the card for its rows")
    stt = subprocess.run([sys.executable, vt, "status"], capture_output=True, text=True).stdout
    check("0 of 3 tokens held" in stt and stt.count(" free") == 3,
          "...and gives every token back after the phase (and the slot's line never reads as a free token)")
    # gate-scope's wiring: the slot once, its fd to every phase, the target step after the verdict
    spec = importlib.util.spec_from_file_location("gate_scope_c", os.path.join(scripts, "gate-scope.py"))
    g = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(g)
    calls = []

    def fake_run(cmd, cwd, tier, lane_, jobs, **kw):
        calls.append(dict(kw, tier=tier, cmd=cmd))
        return 0
    real = (g.gate_runlog.run_ctest, g.gate_runlog.fork_pin_problem, g.gate_runlog.recorded_rows)
    g.gate_runlog.run_ctest = fake_run
    g.gate_runlog.fork_pin_problem = lambda *a, **k: None
    g.gate_runlog.recorded_rows = lambda *a: {"gi.chain_face"}

    def gs_main(argv):
        sys.argv = ["gate-scope.py"] + argv
        buf, code = io.StringIO(), None
        with contextlib.redirect_stdout(buf):
            try: g.main()
            except SystemExit as e: code = e.code
        return code, buf.getvalue()
    files = ["--files", "tests/gi/test_gi_chain_face.cpp", "--build", build, "--lane", "gate-cost-test"]
    code, out = gs_main(files + ["--run"])
    gating = [c for c in calls if c["tier"] != "target"]
    tgt = [c for c in calls if c["tier"] == "target"]
    fd_sets = {tuple(c.get("fds") or ()) for c in calls}
    check(code == 0 and "gate-slot: taken" in out and len(fd_sets) == 1 and all(fd_sets.pop()),
          "gate-scope --run takes the slot once and hands its fd to every phase (%d call(s))" % len(calls))
    if tgt:
        check(tgt[-1].get("whole_card") is True and out.index("GATE VERDICT") < out.index("target tests (label"),
              "the target step runs AFTER the verdict line, inside the gate, on the whole card")
    else:
        check(gating and "GATE VERDICT" in out, "(no target row selected by this path: the verdict printed, %d phase(s))"
              % len(gating))
    calls.clear()
    code, out = gs_main(files + ["--resume"])
    check(code == 0 and calls and all(c.get("exclude") == {"gi.chain_face"} for c in calls),
          "--resume runs the gate with the tip's recorded rows excluded from every phase (%r)"
          % [(c["tier"], c.get("exclude")) for c in calls])
    calls.clear()
    g.gate_runlog.run_ctest = lambda *a, **k: (calls.append(1), rl.DISPLAY_LOST)[1]
    code, out = gs_main(files + ["--run"])
    check(code == rl.DISPLAY_LOST and "GATE VERDICT: ABORTED" in out and len(calls) == 1,
          "a run that lost its display ends the gate there with `GATE VERDICT: ABORTED` (exit %r)" % code)
    g.gate_runlog.run_ctest, g.gate_runlog.fork_pin_problem, g.gate_runlog.recorded_rows = real
    os.environ.pop("JAH_GATE_SLOT_HELD", None)

    shutil.rmtree(scratch, ignore_errors=True)
    if FAILURES:
        print("gate.cost: FAILED (%d)" % len(FAILURES)); return 1
    print("gate.cost: PASSED"); return 0


if __name__ == "__main__":
    if len(sys.argv) < 3:
        print(__doc__); sys.exit(2)
    sys.exit(main(os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2])))
