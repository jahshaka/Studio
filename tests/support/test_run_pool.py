#!/usr/bin/env python3
"""pool.runner — the pool runner's own test (lane SUITE-POOL-1; docs/TESTING.md §2).

Part 1, the REAL app (`--headless`, offscreen): five fixture arms (tests/scripting/
pool_selftest/) — a fresh realm and a closed project between arms, a thrown assertion and a
numeric completion value as FAIL, the pool going on after them, `JAH_POOL_ARMS` running a
subset and naming an unknown arm as a FAIL.
Part 2, the DRIVER against a stand-in app (a few lines of Python that speak the protocol):
the paths the real app cannot be asked to take — a crash (SIGSEGV) mid-arm, a hang past the
arm's budget, a lost baseline — each costs exactly its own arm and the pool continues.

Usage: test_run_pool.py <Jahshaka> <run_pool.py> <fixture dir>
"""
import glob, os, subprocess, sys, tempfile

failures = 0
def check(cond, what):
    global failures
    print(("ok: " if cond else "FAIL: ") + what)
    if not cond: failures += 1

def verdicts(out):
    """The ONE channel: the driver's `ARM <pool>.<arm> <verdict> <ms> [why]` lines (the app's own
    result lines are echoed as `arm-result`). Each arm must have exactly one."""
    v, seen = {}, {}
    for line in out.splitlines():
        p = line.split(" ", 4)
        if len(p) >= 4 and p[0] == "ARM":
            v[p[1]] = (p[2], p[4] if len(p) > 4 else "")
            seen[p[1]] = seen.get(p[1], 0) + 1
    verdicts.twice = sorted(a for a, n in seen.items() if n > 1)
    return v

def run(cmd, env=None):
    e = dict(os.environ); e.update(env or {})
    for f in glob.glob("pool-logs/*.log"): os.unlink(f)
    r = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=e, text=True, errors="replace")
    return r.returncode, r.stdout

def show(out):
    """The inner pools' output, quoted: its `ARM` lines are THIS row's fixtures, not arms of
    pool.runner, and the run log must not read them as such."""
    for line in out.splitlines(): print("| " + line)

def app_log():
    return "".join(open(f).read() for f in sorted(glob.glob("pool-logs/*.log")))

app, driver, fixtures = sys.argv[1:4]
arms = []
for n, f in (("a", "a_leaves_state.js"), ("b", "b_sees_baseline.js"), ("c", "c_throws.js"),
             ("d", "d_returns_count.js"), ("e", "e_after_failures.js")):
    arms += ["--arm", n, os.path.join(fixtures, f), "60"]
base = [sys.executable, driver, "--pool", "selftest", "--app", app, "--headless",
        "--baseline", os.path.join(fixtures, "baseline.js")]

# ---- part 1: the real app -------------------------------------------------------------
rc, out = run(base + arms, {"JAH_POOL_ARMS": ""})
out1 = out
show(out)
v = verdicts(out)
check(rc == 1, "the pool row fails when an arm fails (driver exit %d)" % rc)
check(v.get("selftest.a", ("",))[0] == "PASS", "arm a PASS")
check(v.get("selftest.b", ("",))[0] == "PASS",
      "arm b PASS: a fresh JavaScript realm and no project open after arm a")
check(v.get("selftest.c", ("", ""))[0] == "FAIL" and "the expected failure" in v["selftest.c"][1],
      "arm c FAIL, carrying its first failing assertion")
check(v.get("selftest.d", ("", ""))[0] == "FAIL" and "completion value 2" in v["selftest.d"][1],
      "arm d FAIL: a numeric completion value is the arm's failure count")
check(v.get("selftest.e", ("",))[0] == "PASS", "arm e PASS: the pool goes on after failed arms")
check("RESTART" not in out, "one process ran all five arms")
log = app_log()
check(log.count("pool-baseline-ran") == 5,
      "the pool's BASELINE script ran after every arm, the red ones too (%d of 5)" % log.count("pool-baseline-ran"))
check("the expected failure" in out and "ok: arm a created" not in out,
      "a red arm's own output reaches the row's output; a green arm's stays in pool-logs/")
check(not verdicts.twice, "every arm's verdict is ONE `ARM` line (twice: %s)" % verdicts.twice)

rc, out = run(base + arms, {"JAH_POOL_ARMS": "other.x,selftest.b,selftest.nope"})
show(out)
v = verdicts(out)
check(set(v) == {"selftest.b", "selftest.nope"}, "JAH_POOL_ARMS ran only the arms it named for this pool (%s)" % sorted(v))
check(v.get("selftest.b", ("",))[0] == "PASS", "...and arm b passes alone")
check(v.get("selftest.nope", ("",))[0] == "FAIL", "...and an arm the pool does not have is a FAIL, not a silent pass")
rc2, out2 = run(base + arms, {"JAH_POOL_ARMS": "other.x"})
check(len(verdicts(out2)) == 5 and rc2 == 1, "a JAH_POOL_ARMS that names other pools only runs every arm here")

# ---- part 2: the driver's crash / hang / lost-baseline paths ---------------------------
fake = tempfile.NamedTemporaryFile("w", suffix=".py", delete=False)
fake.write(r'''#!/usr/bin/env python3
import os, sys, time, signal
a = sys.argv; spec = a[a.index("--scripts") + 1]; pool = a[a.index("--pool") + 1]
for e in spec.split(","):
    n = e.split("=", 1)[0]
    print("ARM-BEGIN %s.%s" % (pool, n), flush=True)
    if n == "crash": os.kill(os.getpid(), signal.SIGSEGV)
    if n == "hang": time.sleep(120)
    if n == "chatty":
        while True: print("still going", flush=True); time.sleep(0.2)
    print("ARM %s.%s PASS 1" % (pool, n), flush=True)
    if n == "lost":
        print("POOL-BASELINE-LOST %s.%s a project is still open" % (pool, n), flush=True); sys.exit(1)
    if n == "diesafter": os.kill(os.getpid(), signal.SIGSEGV)
    if n == "hangsafter": time.sleep(120)
''')
fake.close()
os.chmod(fake.name, 0o755)
fargs = []
for n in ("p1", "crash", "p2", "hang", "p3", "lost", "p4", "chatty", "diesafter", "p5", "hangsafter", "p6"):
    fargs += ["--arm", n, "x.js", "3" if n in ("hang", "chatty") else "30"]
empty_journal = tempfile.NamedTemporaryFile("w", suffix=".journal", delete=False); empty_journal.close()
rc, out = run([sys.executable, driver, "--pool", "fake", "--app", fake.name, "--boot-budget", "4"] + fargs,
              {"JAH_POOL_ARMS": "", "JAH_KERNEL_JOURNAL": empty_journal.name})
os.unlink(fake.name)
show(out)
v = verdicts(out)
check(v.get("fake.crash", ("",))[0] == "CRASH" and "signal 11" in v["fake.crash"][1],
      "a process that dies mid-arm is that arm's CRASH, with the signal")
check(v.get("fake.hang", ("",))[0] == "TIMEOUT", "an arm past its budget is killed and is that arm's TIMEOUT")
check(v.get("fake.chatty", ("",))[0] == "TIMEOUT",
      "an arm that hangs PRINTING is killed at its budget too (the budget is read on every line)")
check(v.get("fake.lost", ("", ""))[0] == "CRASH" and "baseline lost" in v["fake.lost"][1],
      "a lost baseline after an arm's PASS is THAT arm's CRASH, never a silent restart")
check(v.get("fake.diesafter", ("", ""))[0] == "CRASH" and "after the arm" in v["fake.diesafter"][1],
      "a process that dies AFTER an arm's PASS (in the baseline) is that arm's CRASH")
check(v.get("fake.hangsafter", ("", ""))[0] == "CRASH" and "hung in the pool's baseline" in v["fake.hangsafter"][1],
      "a process that hangs after an arm's PASS is that arm's CRASH, killed at the boot budget")
check(all(v.get("fake." + n, ("",))[0] == "PASS" for n in ("p1", "p2", "p3", "p4", "p5", "p6")),
      "every other arm has its own PASS: each crash, hang or lost baseline costs one arm")
check(not verdicts.twice, "every arm's verdict is ONE `ARM` line (twice: %s)" % verdicts.twice)
check(out.count("POOL fake RESTART ") == 6, "every restart is a named line (%d)" % out.count("POOL fake RESTART "))
check(rc == 1 and "solo retry: JAH_POOL_ARMS=fake.crash,fake.hang,fake.lost" in out,
      "the row fails and prints the solo retry")

# ---- part 3: the pool's TIER and its MEM line (lane TEST-TIER-1) -------------------------
# The real app's headless boot (part 1) prints its footprint as `headless`: the driver's MEM line
# reads it as zero, at the document's tier.
check(any(l.startswith("MEM selftest gpuPoolUsed=0 textures=0 processMiB=0 tier=document")
          for l in out1.splitlines()), "a headless pool's MEM line: zero, at the document's tier")
tierapp = tempfile.NamedTemporaryFile("w", suffix=".py", delete=False)
tierapp.write(r'''#!/usr/bin/env python3
import sys
a = sys.argv; pool = a[a.index("--pool") + 1]
tier = a[a.index("--test-tier") + 1] if "--test-tier" in a else "document"
print("POOL-MEM %s gpuPoolUsed=%d textures=40 tier=%s" % (pool, 300 if tier == "low" else 1445, tier), flush=True)
for i, e in enumerate(a[a.index("--scripts") + 1].split(",")):
    n = e.split("=", 1)[0]
    print("ARM-BEGIN %s.%s" % (pool, n), flush=True)
    print("ARM %s.%s PASS 1" % (pool, n), flush=True)
    # the leak probe's point after the arm: a Low process CLIMBS 20 MB an arm, an Epic one is flat
    print("POOL-MEM %s.%s gpuPoolUsed=%d textures=40" % (pool, n, 300 + 20 * (i + 1) if tier == "low" else 1445), flush=True)
''')
tierapp.close()
os.chmod(tierapp.name, 0o755)
mems, outs = {}, {}
for t in ("low", "epic"):
    rc3, out3 = run([sys.executable, driver, "--pool", "tiered", "--app", tierapp.name, "--tier", t,
                     "--arm", "a1", "x.js", "30", "--arm", "a2", "x.js", "30", "--arm", "a3", "x.js", "30"],
                    {"JAH_POOL_ARMS": ""})
    show(out3)
    outs[t] = out3
    mems[t] = [l for l in out3.splitlines() if l.startswith("MEM tiered ")]
    check(rc3 == 0, "--tier %s: the pool runs" % t)
arm_mems = [l for l in outs["low"].splitlines() if l.startswith("MEM tiered.")]
check(arm_mems == ["MEM tiered.a1 gpuPoolUsed=320 textures=40", "MEM tiered.a2 gpuPoolUsed=340 textures=40",
                   "MEM tiered.a3 gpuPoolUsed=360 textures=40"],
      "the leak probe: one `MEM <pool>.<arm>` line after every arm (%s)" % arm_mems)
check(any(l.startswith("LEAK tiered +40 over 3 arms") for l in outs["low"].splitlines()),
      "a climb past the largest single step (20 MB) is a LEAK finding line")
check(not any(l.startswith("LEAK ") for l in outs["epic"].splitlines()),
      "a flat curve is no LEAK")
check("LEAK" not in "".join(l for l in outs["low"].splitlines() if l.startswith("POOL tiered:")) and
      "0 not PASS" in outs["low"], "a LEAK is a finding, never a red")
os.unlink(tierapp.name)
mems = {t: [l for l in v if not l.startswith("MEM tiered.")] for t, v in mems.items()}
check(len(mems["low"]) == 1 and mems["low"][0].startswith("MEM tiered gpuPoolUsed=300 textures=40 processMiB=")
      and mems["low"][0].endswith("tier=low"),
      "TIER low: every process gets --test-tier low, and the boot's MEM line is printed once (%s)" % mems["low"])
check(len(mems["epic"]) == 1 and mems["epic"][0].endswith("tier=document") and "gpuPoolUsed=1445" in mems["epic"][0],
      "TIER epic: no --test-tier is passed (the document's own tier) (%s)" % mems["epic"])
rc4, out4 = run([sys.executable, driver, "--pool", "x", "--app", "true", "--tier", "medium",
                 "--arm", "a", "x.js", "30"])
check(rc4 == 2 and "--tier" in out4, "a TIER the driver does not know is refused (exit %d)" % rc4)
# ...and the run log reads the field (scripts/gate_runlog.py, the `mem` of a pool's row).
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(driver)))), "scripts"))
import gate_runlog
m = gate_runlog._mem_of("\n".join(mems["low"] + ["MEM tiered gpuPoolUsed=310 textures=41 processMiB=? tier=low"]))
check(m is not None and m["gpuPoolUsedMB"] == 310 and m["tier"] == "low" and m["boots"] == 2,
      "the run log records a pool row's MEM: the largest over its boots (%s)" % m)
am = gate_runlog._arm_mems(outs["low"])
lk = gate_runlog._leaks_of(outs["low"])
check(am.get("tiered.a3") == {"gpuPoolUsedMB": 360, "texturesMB": 40} and lk == [{"riseMB": 40, "arms": 3}],
      "...each arm's MEM on the arm's record, the LEAK on the row's (%s, %s)" % (am, lk))
# ---- part 3: THE VRAM BUDGET and THE KERNEL'S WORD (lane GATE-ADMIT-1) --------------------
# A private token directory (never the box's /tmp/jah-vram): the stand-in app reports the token
# files IT holds open (inherited from the driver) and, in arm `xid`, writes a kernel-journal line
# naming its own pid — the fake journal the driver reads through JAH_KERNEL_JOURNAL.
import shutil, signal as _signal, time as _time
vdir = tempfile.mkdtemp(prefix="pool-vram-")
journal = os.path.join(vdir, "journal")
open(journal, "w").close()
fake = tempfile.NamedTemporaryFile("w", suffix=".py", delete=False)
fake.write(r'''#!/usr/bin/env python3
import os, sys, time, signal
a = sys.argv; spec = a[a.index("--scripts") + 1]; pool = a[a.index("--pool") + 1]
def held():
    out = []
    for fd in os.listdir("/proc/self/fd"):
        try: t = os.readlink("/proc/self/fd/" + fd)
        except OSError: continue
        if os.path.dirname(t) == os.environ["JAH_VRAM_DIR"] and os.path.basename(t).startswith("token."):
            out.append(int(t.rsplit(".", 1)[1]))
    return sorted(set(out))
for e in spec.split(","):
    n = e.split("=", 1)[0]
    print("ARM-BEGIN %s.%s" % (pool, n), flush=True)
    print("tokens-held %s held-env %s" % (held(), os.environ.get("JAH_VRAM_HELD")), flush=True)
    if n == "xid":
        time.sleep(0.3)     # the driver stamps ARM-BEGIN when it READS it; the fault comes later
        with open(os.environ["JAH_KERNEL_JOURNAL"], "a") as j:
            j.write("%.6f box kernel: NVRM: Xid (PCI:0000:01:00): 13, pid=%d, name=Jahshaka, Graphics Exception\n"
                    % (time.time(), os.getpid()))
        j = None
    if n == "crash": os.kill(os.getpid(), signal.SIGSEGV)
    print("ARM %s.%s PASS 1" % (pool, n), flush=True)
''')
fake.close()
os.chmod(fake.name, 0o755)
fargs = []
for n in ("t1", "xid", "t2", "crash", "t3"):
    fargs += ["--arm", n, "x.js", "30"]
admit = os.path.join(os.path.dirname(os.path.abspath(driver)), "..", "..", "scripts", "gpu-admit.sh")
venv = {"JAH_POOL_ARMS": "", "JAH_KERNEL_JOURNAL": journal, "JAH_VRAM_DIR": vdir, "JAH_VRAM_TOKENS": "12"}
e = dict(os.environ); e.update(venv); e.pop("JAH_VRAM_HELD", None)
# a blocker holds 11 of the 12 tokens: the pool (2 tokens) must WAIT, then run once it is killed
blocker = subprocess.Popen([admit, "11", "--label", "blocker", "--", "sleep", "300"], env=e,
                           stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
n = 0
while n < 200 and len([f for f in os.listdir(vdir) if f.startswith("token.")]) < 12:
    n += 1; _time.sleep(0.05)
for f in glob.glob("pool-logs/*.log"): os.unlink(f)
p = subprocess.Popen([sys.executable, driver, "--pool", "vram", "--app", fake.name, "--vram-tokens", "2",
                      "--boot-budget", "10"] + fargs, env=e, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                     text=True, errors="replace")
lines = []
for line in p.stdout:
    lines.append(line.rstrip("\n"))
    if line.startswith("vram: waiting for 2 tokens"):
        blocker.kill()      # SIGKILL: the kernel frees a killed holder's tokens
        blocker.wait()
rc = p.wait()
out = "\n".join(lines)
os.unlink(fake.name)
show(out)
v = verdicts(out)
log = app_log()
check("vram: waiting for 2 tokens, 1 free" in out,
      "a pool process that cannot get its tokens WAITS, with the one wait line (not a run on a full card)")
check("vram: admitted with 2 tokens 0,1" in out,
      "...and a KILLED holder's tokens are free again: admitted on the lowest two, 0 and 1")
check(log.count("tokens-held [0, 1] held-env 2") == 5,
      "every arm's app process holds exactly its 2 tokens, inherited (%d of 5 arms)" % log.count("tokens-held [0, 1] held-env 2"))
check(out.count("POOL vram RESTART ") == 1 and len(glob.glob("pool-logs/vram-process*.log")) == 2,
      "the crash restarted the process once, and the restart RE-TOOK the tokens (both processes held 0,1)")
check(v.get("vram.xid", ("", ""))[0] == "CRASH" and v["vram.xid"][1].startswith("xid 13"),
      "a kernel Xid from the pool's pid is the running arm's `CRASH xid 13` (%s)" % (v.get("vram.xid"),))
check(all(v.get("vram." + n, ("",))[0] == "PASS" for n in ("t1", "t2", "t3")),
      "the Xid is charged to the arm running at its second only (t1, t2, t3 PASS)")
check(v.get("vram.crash", ("",))[0] == "CRASH", "the crash arm is still its own CRASH")
check(not verdicts.twice, "every arm's verdict is ONE `ARM` line, the Xid override included (twice: %s)" % verdicts.twice)
held_after = subprocess.run([admit, "status"], env=e, stdout=subprocess.PIPE, text=True).stdout
check("vram: 0 of 12 tokens held" in held_after, "after the pool ends no token is held (the driver keeps none)")
# an unreadable journal is a RED, never a silent pass
okapp = os.path.join(vdir, "okapp.py")
with open(okapp, "w") as f:
    f.write("#!/usr/bin/env python3\nimport sys\na = sys.argv; p = a[a.index('--pool') + 1]\n"
            "print('ARM-BEGIN %s.a' % p, flush=True)\nprint('ARM %s.a PASS 1' % p, flush=True)\n")
os.chmod(okapp, 0o755)
rc3, out3 = run([sys.executable, driver, "--pool", "nojournal", "--app", okapp, "--arm", "a", "x.js", "30"],
                {"JAH_POOL_ARMS": "", "JAH_KERNEL_JOURNAL": os.path.join(vdir, "missing"), "JAH_VRAM_DIR": vdir})
check(rc3 == 0 and "FINDING: the kernel journal is unreadable" in out3 and "ARM nojournal.a PASS" in out3,
      "an unreadable kernel journal is a printed FINDING, not a red of the pool (devprocess.kernel_journal owns it)")
shutil.rmtree(vdir, ignore_errors=True)
os.unlink(empty_journal.name)

print("pool.runner: %s" % ("%d failure(s)" % failures if failures else "all ok"))
sys.exit(1 if failures else 0)
