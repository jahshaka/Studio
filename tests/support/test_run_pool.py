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
import os, subprocess, sys, tempfile

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
    r = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=e, text=True, errors="replace")
    return r.returncode, r.stdout

app, driver, fixtures = sys.argv[1:4]
arms = []
for n, f in (("a", "a_leaves_state.js"), ("b", "b_sees_baseline.js"), ("c", "c_throws.js"),
             ("d", "d_returns_count.js"), ("e", "e_after_failures.js")):
    arms += ["--arm", n, os.path.join(fixtures, f), "60"]
base = [sys.executable, driver, "--pool", "selftest", "--app", app, "--headless",
        "--baseline", os.path.join(fixtures, "baseline.js")]

# ---- part 1: the real app -------------------------------------------------------------
rc, out = run(base + arms, {"JAH_POOL_ARMS": ""})
print(out)
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
check(out.count("pool: selftest process") == 1, "one process ran all five arms")
check(out.count("pool-baseline-ran") == 5,
      "the pool's BASELINE script ran after every arm, the red ones too (%d of 5)" % out.count("pool-baseline-ran"))
check(not verdicts.twice, "every arm's verdict is ONE `ARM` line (twice: %s)" % verdicts.twice)

rc, out = run(base + arms, {"JAH_POOL_ARMS": "other.x,selftest.b,selftest.nope"})
print(out)
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
rc, out = run([sys.executable, driver, "--pool", "fake", "--app", fake.name, "--boot-budget", "4"] + fargs,
              {"JAH_POOL_ARMS": ""})
os.unlink(fake.name)
print(out)
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

print("pool.runner: %s" % ("%d failure(s)" % failures if failures else "all ok"))
sys.exit(1 if failures else 0)
