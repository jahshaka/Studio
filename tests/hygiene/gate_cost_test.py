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
     AFTER the verdict line, inside the gate, on whole_card; the caller's JAH_POOL_ARMS survives a hold;
  7. P6's CLAIM: the per-row records equal the old junit path's, record for record (six rows);
  9. GATE-COST-2: a whole-card hold outside a gate takes the slot (and --solo with it); every NOADMIT try is
     a record (requeued: k), still never-ran to the refusal; a drain timeout prints and records the holders
     with their age; an abort is one `kind: abort` record and --resume re-runs a row it dropped red; the
     lints prune exactly irisgl/.gitmodules; a green row waits for journald before its Xid read;
  8. THE SLOT UNDER STRESS: a dead waiter's ticket is reaped; eight gates at once hold it one at a time;
     a SIGKILLed gate's ctest and rows die within 15 s and the slot is free; a killed ctest ends a gate.

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
add_test(NAME gpu.wait COMMAND sh -c "echo gpu.wait >> ${T}/order; while [ ! -e ${T}/go ]; do sleep 0.05; done; echo 'FAIL: the window it drew'; exit 1")
add_test(NAME gpu.noadmit_once COMMAND sh -c "if [ -e ${T}/na ]; then echo ok; else touch ${T}/na; echo 'NOADMIT vram: no admission for 2 tokens within 1 s (0 of 3 free at the last look) - gpu.noadmit_once'; exit 75; fi")
add_test(NAME gpu.noadmit_always COMMAND sh -c "echo 'NOADMIT vram: no admission for 2 tokens within 1 s (0 of 3 free at the last look) - gpu.noadmit_always'; exit 75")
add_test(NAME time.card COMMAND sh -c "python3 ${VT} status > ${T}/card.time; python3 ${VT} admit 1 -- true 2>> ${T}/card.time; echo ARMS=\$JAH_POOL_ARMS >> ${T}/card.time")
set_tests_properties(time.card PROPERTIES LABELS "timing" FIXTURES_REQUIRED timeHome)
add_test(NAME time.card.home COMMAND sh -c "echo setup")
set_tests_properties(time.card.home PROPERTIES FIXTURES_SETUP timeHome)
add_test(NAME tgt.card COMMAND sh -c "python3 ${VT} status > ${T}/card.tgt; python3 ${VT} admit 1 -- true 2>> ${T}/card.tgt")
set_tests_properties(tgt.card PROPERTIES LABELS "photon-target")
add_test(NAME id.env COMMAND sh -c "echo FOO=\$FOO; echo 'vram: admitted with 2 tokens 0,1 after 1.5 s'; echo 'target: 3 (bar 2)'")
set_tests_properties(id.env PROPERTIES ENVIRONMENT "FOO=1;BAR=two")
add_test(NAME id.envmod COMMAND sh -c "echo D=\${DISPLAY:-none}; echo 'gpu-lock: waited 2.5 s'")
set_tests_properties(id.envmod PROPERTIES ENVIRONMENT_MODIFICATION "DISPLAY=unset:;JAH_X=set:1")
add_test(NAME id.big COMMAND python3 -c "import sys; [print('line %d ' % i + 'x' * 90) for i in range(1000)]; print('target: 7 (bar 9)')")
add_test(NAME id.red COMMAND sh -c "echo 'ok: a'; echo 'FAIL: the first assertion'; echo 'FAIL: the second'; exit 1")
add_test(NAME id.stderr COMMAND sh -c "echo out; echo 'GPU out of memory (VK_ERROR_OUT_OF_DEVICE_MEMORY; the device is NOT lost)' >&2; exit 1")
add_test(NAME id.arms COMMAND sh -c "echo 'ARM-BEGIN p.one'; echo 'ARM p.one PASS 120 ms'; echo 'ARM-BEGIN p.two'; echo 'MEM p.two gpuPoolUsed=10 textures=2'; echo 'ARM p.two FAIL 50 ms why'; echo 'ARM-BEGIN p.three'; echo 'MEM p gpuPoolUsed=100 textures=20 processMiB=300 tier=high'; exit 1")
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
    recs = [(r["verdict"], r.get("requeued")) for r in records() if r["suite"] == "gpu.noadmit_once"]
    check(rc == 0 and recs == [("NOADMIT", 0), ("PASS", 1)] and "re-queued 1 row" in out,
          "a row with no admission is re-queued at the end of the run; EVERY try is a record — the held one NOADMIT "
          "requeued 0, the passing one requeued 1 (rc %r, %r)" % (rc, recs))
    reset()
    rc, out = run("^(gpu\\.a|gpu\\.noadmit_always)$")
    recs = [(r["verdict"], r.get("requeued")) for r in records() if r["suite"] == "gpu.noadmit_always"]
    check(rc != 0 and recs == [("NOADMIT", 0), ("NOADMIT", 1), ("NOADMIT", 2)] and out.count("re-queued 1 row") == 2,
          "a row that never gets one is tried 1 + JAH_GATE_REQUEUE (2) times, one NOADMIT record per try, the run red "
          "(rc %r, %r)" % (rc, recs))
    import ci_gate_check as cgc
    st_, why_ = cgc.judge(("gpu.noadmit_always", None), [r for r in records() if r["suite"] == "gpu.noadmit_always"], {})
    check(st_ == "missing", "...and the refusal still reads them as never-ran (%s: %s)" % (st_, why_))

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
    open(os.path.join(state, "go"), "w").close()
    run("^gpu\\.wait$")                     # an older record of gpu.wait at this tip (red: it fails by design)
    os.unlink(os.path.join(state, "go"))
    open(os.path.join(state, "order"), "w").close()
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
    os.environ["JAH_DISPLAY_POLL_S"] = "60"     # only the row-end check sees the death: gpu.wait ENDS after it
    env = dict(os.environ, DISPLAY=":77")
    res = {}
    t = threading.Thread(target=lambda: res.update(r=run("^(lint\\.one|gpu\\.a|gpu\\.wait|gpu\\.slow)$", jobs=3, env=env)))
    t.start()
    deadline = time.time() + 40
    # the death comes once gpu.wait and gpu.slow run and gpu.a's record is written (a loaded box delays it)
    while not ({"gpu.wait", "gpu.slow"} <= set(order()) and any(r["suite"] == "gpu.a" for r in records())) \
            and time.time() < deadline:
        time.sleep(0.2)
    fake_x.kill(); fake_x.wait()
    open(os.path.join(state, "go"), "w").close()       # gpu.wait ends RED after the display died
    t.join(90)
    os.environ.pop("TOY_SLOW"); os.environ.pop("JAH_DISPLAY_POLL_S")
    rc, out = res.get("r", (None, ""))
    got = {r["suite"] for r in records() if not r.get("kind")}
    check(rc == rl.DISPLAY_LOST and "GATE ABORTED" in out and "is gone" in out and rl.ABORTED,
          "the X server dies under a run -> it stops, DISPLAY_LOST, the abort line says why (rc %r)" % rc)
    runs_ = [r for r in records() if r["suite"] == "gpu.wait" and not r.get("kind")]
    check(len(runs_) == 1 and {"lint.one", "gpu.a"} <= got and "gpu.slow" not in got,
          "rows that ended before the death are recorded, the rows that ended after it are not (%s)" % sorted(got))
    ab = [r for r in records() if r.get("kind") == "abort"]
    check(len(ab) == 1 and ab[0]["suite"] == "@abort" and "is gone" in ab[0].get("why", "")
          and [d_["suite"] for d_ in ab[0].get("dropped", [])] == ["gpu.wait"] and ab[0].get("droppedRed") == ["gpu.wait"]
          and ab[0]["dropped"][0].get("failLine") == "FAIL: the window it drew" and ab[0].get("inFlight") == ["gpu.slow"],
          "the abort is ONE `kind: abort` record: the row that ended RED after the death with its status and FAIL "
          "line, the row still running (%r)" % ({k: ab[0].get(k) for k in ("dropped", "inFlight")} if ab else None))
    done_ = rl.recorded_rows(clean)
    check("gpu.wait" not in done_ and "gpu.a" in done_,
          "--resume re-runs the row the abort dropped RED although it has an older record at the tip; the others stay done")
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

    # ---- 6. the records are the junit path's, byte for byte (P6's claim) ------------------------------
    print("6. the per-row records equal the old junit path's")
    reset()
    rx = "^id\\.(env|envmod|big|red|stderr|arms)$"
    rc, out = run(rx, jobs=3)
    new = {(r["suite"], r.get("arm")): r for r in records()}
    junit = os.path.join(scratch, "j.xml")
    subprocess.run(f"ctest -j3 -R '{rx}' --output-junit {junit} --test-output-size-passed 262144 "
                   f"--test-output-size-failed 262144", cwd=tb, shell=True, capture_output=True)
    import xml.etree.ElementTree as ET
    outs = {tc.get("name"): (tc.find("system-out").text or "") if tc.find("system-out") is not None else ""
            for tc in ET.parse(junit).getroot().iter("testcase")}
    R = rl._Run("scoped", "gate-cost-test", 3, None, None, None, False, LABELS, False, None)
    R.sampler.stop()
    skip = ("ts", "run", "box", "seconds", "wallSeconds", "retries")
    same, n = True, 0
    for name in sorted({k[0] for k in new}):
        row = new[(name, None)]
        old = R.records(name, row["status"], row["seconds"], 0, (0, 0, 0), outs.get(name, ""), {})
        for o in old:
            nr = new.get((name, o.get("arm")))
            n += 1
            strip = lambda d: {k: v for k, v in (d or {}).items() if k not in skip}
            if strip(o) != strip(nr):
                same = False
                print("    differs: %s %s\n      junit: %s\n      rows:  %s" % (name, o.get("arm"), strip(o), strip(nr)))
    check(same and n >= 9 and len(new) == n, "the per-row records equal the junit path's, record for record (%d records: "
          "env, env-modification, big output, red, stderr, arms; ts/run/box/seconds aside)" % n)

    # ---- 7. the slot under stress: a dead waiter, a race, a killed gate -----------------------------
    print("7. the slot: reaping, the race, a killed gate")
    holder = subprocess.Popen([sys.executable, vt, "gate", "--label", "H", "--", "sleep", "30"],
                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(0.8)
    waiter = subprocess.Popen([sys.executable, vt, "gate", "--label", "W", "--", "true"],
                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(0.8)
    before = [t[1] for t in vt_mod.gate_queue()]
    waiter.kill(); waiter.wait()
    qdir = os.path.join(os.environ["JAH_VRAM_DIR"], "gate-queue")
    after = [t[1] for t in vt_mod.gate_queue()]
    check(waiter.pid in before and waiter.pid not in after and not any(n.endswith(".%d" % waiter.pid) for n in os.listdir(qdir)),
          "a dead waiter's ticket is reaped by the next reader (queue %s -> %s)" % (before, after))
    holder.terminate(); holder.wait()
    # F3: eight gates asked at once — never two inside the critical section
    crit = os.path.join(state, "crit")
    racers = [subprocess.Popen([sys.executable, vt, "gate", "--label", "R%d" % i, "--", "sh", "-c",
                                f"mkdir {crit} 2>/dev/null || echo DOUBLE >> {state}/race; sleep 0.2; rmdir {crit}"],
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL) for i in range(8)]
    for r_ in racers: r_.wait(timeout=60)
    check(not os.path.exists(os.path.join(state, "race")),
          "eight gates asking at once hold the slot one at a time (the ticket is renamed in under the counter's lock)")
    # F2: a gate SIGKILLed mid-run — its ctest and rows die within 15 s, the slot is free
    reset()
    os.environ["TOY_SLOW"] = "60"
    gk = subprocess.Popen([sys.executable, os.path.join(scripts, "gate_runlog.py"), "run", "--tier", "scoped",
                           "--lane", "GK", "--build", tb, "--", "ctest -j1 -R '^gpu\\.slow$'"], cwd=source,
                          stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    deadline = time.time() + 40
    while "gpu.slow" not in order() and time.time() < deadline:
        time.sleep(0.2)
    tree = rl._descendants(gk.pid)
    gk.kill(); gk.wait()
    t_kill = time.time()
    def running(p_):
        try:                                     # a process may vanish between any two reads
            return open(f"/proc/{p_}/stat").read().rsplit(")", 1)[1].split()[0] != "Z"
        except (OSError, IndexError):
            return False
    while time.time() - t_kill < 20 and any(running(p_) for p_ in tree):
        time.sleep(0.5)
    left = [p_ for p_ in tree if running(p_)]
    os.environ.pop("TOY_SLOW")
    stt = subprocess.run([sys.executable, vt, "status"], capture_output=True, text=True).stdout
    check(tree and not left and time.time() - t_kill < 20 and "nobody holds it" in stt,
          "a SIGKILLed gate: its ctest and its rows (%d processes) die within %.0f s and the slot is free"
          % (len(tree), time.time() - t_kill))

    # ---- 8. P2/P9: the whole card once per phase ---------------------------------------------------
    print("8. the whole card")
    reset()
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):      # the serial phase as rc-gate starts it: -L, a fixture pulled in
        rc = rl.run_ctest("ctest -j1 --timeout 60 --output-on-failure -L '^timing$'", tb, "scoped", "gate-cost-test", 1,
                          labels=LABELS, env=dict(os.environ, JAH_POOL_ARMS="p.x"))
    out = buf.getvalue()
    card = open(os.path.join(state, "card.time")).read()
    check(rc == 0 and "3 of 3 tokens held" in card and "already admitted by the parent" in card
          and "whole card (3 tokens) held for the phase" in out and "time.card.home" in out and "ARMS=p.x" in card,
          "a timing phase (`-L ^timing$`, its unlabelled fixture pulled in) holds every token once; its row runs "
          "nested on them, with the caller's JAH_POOL_ARMS (F5)")
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
    mine = lambda: [t for t in vt_mod.gate_queue() if t[1] == os.getpid()]
    seen = []
    g.gate_runlog.run_ctest = lambda *a, **k: (seen.append(bool(mine())), 0)[1]
    code, out = gs_main(["--solo", "gi.chain_face", "--times", "2", "--build", build, "--lane", "gate-cost-test"])
    check(code == 0 and seen == [True, True] and "gate-slot: taken" in out and not mine(),
          "--solo (a whole-card hold) takes the slot for its batch and gives it back (%r)" % seen)
    g.gate_runlog.run_ctest = lambda *a, **k: (calls.append(dict(k, tier=a[2], held=bool(mine()))), 0)[1]
    code, out = gs_main(files + ["--run"])
    gating = [c for c in calls if c["tier"] != "target"]
    tgt = [c for c in calls if c["tier"] == "target"]
    check(code == 0 and out.count("gate-slot: taken") == 1 and calls and all(c["held"] for c in calls),
          "gate-scope --run takes the slot once and holds it through every phase (%d call(s))" % len(calls))
    if tgt:
        check(tgt[-1].get("whole_card") is True and out.index("GATE VERDICT") < out.index("target tests (label"),
              "the target step runs AFTER the verdict line, inside the gate, on the whole card")
    else:
        check(gating and "GATE VERDICT" in out, "(no target row selected by this path: the verdict printed, %d phase(s))"
              % len(gating))
    calls.clear()
    g.gate_runlog.run_ctest = fake_run
    code, out = gs_main(files + ["--resume"])
    check(code == 0 and calls and all(c.get("exclude") == {"gi.chain_face"} for c in calls),
          "--resume runs the gate with the tip's recorded rows excluded from every phase (%r)"
          % [(c["tier"], c.get("exclude")) for c in calls])
    calls.clear()
    g.gate_runlog.run_ctest = lambda *a, **k: (calls.append(1), rl.DISPLAY_LOST)[1]
    code, out = gs_main(files + ["--run"])
    check(code == rl.DISPLAY_LOST and "GATE VERDICT: ABORTED" in out and len(calls) == 1,
          "a run that lost its display ends the gate there with `GATE VERDICT: ABORTED` (exit %r)" % code)
    calls.clear()
    g.gate_runlog.run_ctest = lambda *a, **k: (calls.append(1), 137)[1]
    code, out = gs_main(files + ["--run"])
    check(code == 137 and "GATE ABORTED: ctest was killed" in out and len(calls) == 1,
          "a ctest killed under the gate (137) ends it there: no timing phase, no target step (F4; exit %r)" % code)
    g.gate_runlog.run_ctest, g.gate_runlog.fork_pin_problem, g.gate_runlog.recorded_rows = real
    os.environ.pop("JAH_GATE_SLOT_HELD", None)

    # ---- 9. GATE-COST-2: whole-card holds in the slot, the drain timeout, the prune list, the ingest wait --
    print("9. whole-card holds, the drain timeout, the prune list, the Xid ingest")
    os.environ.pop("JAH_GATE_SLOT_HELD", None)
    # the gate-scope runs above took the slot in THIS process and never gave it back (main() owns it to its
    # exit): close those tickets, so this process stops being the slot's holder
    for fd_ in os.listdir("/proc/self/fd"):
        try:
            if "/gate-queue/" in os.readlink(f"/proc/self/fd/{fd_}"): os.close(int(fd_))
        except OSError:
            pass
    vt_mod.gate_queue()                          # reaps them
    hand = subprocess.Popen([sys.executable, "-c", "import sys, time; sys.path.insert(0, sys.argv[1]); import vram_tokens as v;"
                             " fds, env = v.hold_card('hand phase'); print('HELD', env.get('JAH_GATE_SLOT_HELD'),"
                             " env.get('JAH_VRAM_HELD'), flush=True); time.sleep(3); v.release(fds)", scripts],
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    first = hand.stdout.readline()
    while first and not first.startswith("HELD"):
        first = hand.stdout.readline()
    q = vt_mod.gate_queue()
    other = subprocess.Popen([sys.executable, vt, "gate", "--label", "G9", "--", "true"], stdout=subprocess.PIPE,
                             stderr=subprocess.STDOUT, text=True)
    time.sleep(1.5)
    waiting = other.poll() is None
    hand.wait(timeout=30)
    o9 = other.communicate(timeout=30)[0]
    check(first.split()[1:3] == [str(hand.pid), "3"] and any(t_[1] == hand.pid for t_ in q) and waiting
          and "queued at position" in o9 and "holding it for" in o9 and other.returncode == 0,
          "a whole-card hold outside a gate takes the slot (%r); a gate asking meanwhile queues behind it, the "
          "holder's age shown, and runs after it" % first.strip())
    reset()
    blocker = subprocess.Popen([sys.executable, vt, "admit", "1", "--label", "blocker", "--", "sleep", "20"],
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(1.0)
    os.environ["JAH_VRAM_PHASE_WAIT"] = "1"
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        rc = rl.run_ctest("ctest -j1 --timeout 60 --output-on-failure -L '^timing$'", tb, "scoped", "gate-cost-test", 1,
                          labels=LABELS)
    os.environ.pop("JAH_VRAM_PHASE_WAIT")
    blocker.terminate(); blocker.wait()
    dt = [r for r in records() if r.get("kind") == "drain-timeout"]
    check(rc == 0 and len(dt) == 1 and dt[0]["suite"] == "@drain-timeout" and any("blocker" in h and " for " in h
                                                                                for h in dt[0].get("holders", []))
          and "HELD by" in buf.getvalue(),
          "a drain past JAH_VRAM_PHASE_WAIT prints the holders with their age and writes a `kind: drain-timeout` "
          "record naming them (%r)" % (dt[0].get("holders") if dt else None))
    for q_ in ("longest", "load-reds", "trend", "times"):
        p_ = subprocess.run([sys.executable, os.path.join(scripts, "gate_runlog.py"), q_], capture_output=True, text=True)
        check(p_.returncode == 0, "gate_runlog.py %s reads a log holding run records (rc %d)" % (q_, p_.returncode))
    lib = os.path.join(source, "tests", "hygiene", "first_party.sh")
    gm = subprocess.run(["git", "config", "-f", os.path.join(source, "irisgl", ".gitmodules"), "--get-regexp",
                         r"\.path$"], capture_output=True, text=True).stdout.split()[1::2]
    pruned = subprocess.run(["bash", "-c", f". {lib}; submodule_paths irisgl"], cwd=source, capture_output=True,
                            text=True).stdout.split()
    walked = subprocess.run(["bash", "-c", f". {lib}; first_party_paths irisgl"], cwd=source, capture_output=True,
                            text=True).stdout.split()
    intree = [d_ for d_ in ("irisgl/thirdparty/meshoptimizer-clusterlod", "irisgl/thirdparty/assimp-patches")
              if os.path.isdir(os.path.join(source, d_))]
    check(gm and sorted(pruned) == sorted("irisgl/" + x for x in gm)
          and not any(w == p_ or w.startswith(p_ + "/") for w in walked for p_ in pruned)
          and all(d_ in walked for d_ in intree),
          "the lints prune exactly irisgl/.gitmodules' paths (%d) and walk the in-tree vendored dirs (%s)"
          % (len(gm), ", ".join(os.path.basename(d_) for d_ in intree)))
    env_x = {k: v for k, v in os.environ.items() if k != "JAH_KERNEL_JOURNAL"}
    t0 = time.monotonic()
    p_ = subprocess.run([sys.executable, vt, "admit", "1", "--", "true"], env=env_x, capture_output=True, text=True)
    check(p_.returncode == 0 and time.monotonic() - t0 >= 1.0,
          "a GREEN row waits for journald's ingest before its Xid read (%.2f s)" % (time.monotonic() - t0))

    shutil.rmtree(scratch, ignore_errors=True)
    if FAILURES:
        print("gate.cost: FAILED (%d)" % len(FAILURES)); return 1
    print("gate.cost: PASSED"); return 0


if __name__ == "__main__":
    if len(sys.argv) < 3:
        print(__doc__); sys.exit(2)
    sys.exit(main(os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2])))
