#!/usr/bin/env python3
"""run_pool.py — THE POOL DRIVER (lane SUITE-POOL-1; jahshaka/docs/TESTING.md §"pools").

One ctest row = one POOL = one app process that runs N scripts ("arms") in turn
(`Jahshaka --scripts <list> --pool <name>`, src/app/cli/scriptrunner.cpp). The app
prints one line per arm; this driver owns what the app cannot:

  * A CRASH. An arm that printed `ARM-BEGIN <pool>.<arm>` and never its result died
    with the process: `CRASH`. A process that dies, hangs or loses its baseline
    (`POOL-BASELINE-LOST`) AFTER an arm's result — in the pool's baseline restore or its
    exit — is that arm's `CRASH` too: a result is final only once the baseline behind it
    held. Either way the driver starts a NEW process (`POOL <pool> RESTART <n>`) and
    continues from the NEXT arm — one crash costs one arm, never the pool.
  * A HANG. Every arm has a budget (6x its measured seconds, the row's TIMEOUT law),
    checked on every line and every silent second: an arm past it is killed and
    recorded `TIMEOUT`; a baseline past the boot budget is its arm's `CRASH`.
  * THE SUBSET. `--arms a,b` here, or `JAH_POOL_ARMS=<pool>.<arm>[,...]` in the
    environment (so `JAH_POOL_ARMS=gi_verbs.gi_status ctest -R '^pool\\.gi_verbs$'`
    is the solo retry of one arm, through the real row). A pool the variable does
    not name runs every arm; `<pool>` alone names the whole pool.
  * THE VERDICT, PER ARM, ON ONE CHANNEL: each arm's final `ARM <pool>.<arm>
    PASS|FAIL|CRASH|TIMEOUT <ms> [why]` line is printed exactly once, by this driver
    (the app's own result line is echoed as `arm-result …`, which no reader counts) —
    the run log (scripts/gate_runlog.py) reads these lines. The row fails iff an arm is
    not PASS, and a closing summary (`  <pool>.<arm>: <verdict> …`) repeats them for a
    reader of the output.

Usage:
  run_pool.py --pool <name> --app <Jahshaka> [--headless] [--arms a,b] [--baseline <js>]
              [--boot-budget <s>] --arm <name> <script> <budget-s> [--arm ...]
              [-- <extra app args>]

--baseline <js>: the pool's own baseline script, run by the app after EVERY arm, green
or red (`--pool-baseline`), before the runner's own (the project closed, the window
size put back, the deferred deletes delivered).
"""
import os
import queue
import re
import signal
import subprocess
import sys
import threading
import time

ARM_BEGIN = re.compile(r"^ARM-BEGIN (\S+)\.(\S+)\s*$")
ARM_END = re.compile(r"^ARM (\S+)\.(\S+) (PASS|FAIL) (\d+)(?: (.*))?$")
BASELINE_LOST = re.compile(r"^POOL-BASELINE-LOST (\S+)\.(\S+) (.*)$")


def usage(msg):
    sys.stderr.write("run_pool.py: %s\n" % msg)
    sys.exit(2)


def parse(argv):
    opt = {"pool": None, "app": None, "headless": False, "arms": None,
           "boot": 300.0, "list": [], "extra": [], "baseline": None}
    i = 0
    while i < len(argv):
        a = argv[i]
        if a == "--":
            opt["extra"] = argv[i + 1:]
            break
        if a == "--pool":
            opt["pool"] = argv[i + 1]; i += 2
        elif a == "--app":
            opt["app"] = argv[i + 1]; i += 2
        elif a == "--baseline":
            opt["baseline"] = argv[i + 1]; i += 2
        elif a == "--headless":
            opt["headless"] = True; i += 1
        elif a == "--arms":
            opt["arms"] = [x for x in argv[i + 1].split(",") if x]; i += 2
        elif a == "--boot-budget":
            opt["boot"] = float(argv[i + 1]); i += 2
        elif a == "--arm":
            if i + 3 >= len(argv):
                usage("--arm needs <name> <script> <budget-s>")
            opt["list"].append((argv[i + 1], argv[i + 2], float(argv[i + 3]))); i += 4
        else:
            usage("unknown argument '%s'" % a)
    if not opt["pool"] or not opt["app"] or not opt["list"]:
        usage("--pool, --app and at least one --arm are required")
    return opt


def selected(opt):
    """The arms to run: --arms, else JAH_POOL_ARMS's entries for this pool, else all."""
    pool = opt["pool"]
    names = [n for n, _, _ in opt["list"]]
    want = opt["arms"]
    if want is None:
        env = [t.strip() for t in os.environ.get("JAH_POOL_ARMS", "").split(",") if t.strip()]
        mine = [t for t in env if t == pool or t.startswith(pool + ".")]
        if mine and pool not in mine:
            want = [t[len(pool) + 1:] for t in mine]
    if want is None:
        return list(opt["list"]), []
    unknown = [w for w in want if w not in names]
    return [a for a in opt["list"] if a[0] in want], unknown


def child_setup():
    # The app dies with this driver (a ctest TIMEOUT kills the driver with a
    # signal nobody can catch; an orphaned Vulkan app would hold the GPU).
    if sys.platform.startswith("linux"):
        try:
            import ctypes
            ctypes.CDLL("libc.so.6", use_errno=True).prctl(1, signal.SIGKILL)  # PR_SET_PDEATHSIG
        except Exception:
            pass


def reader(stream, q):
    for raw in iter(stream.readline, b""):
        q.put(raw.decode("utf-8", "replace").rstrip("\n"))
    q.put(None)


def say(text):
    sys.stdout.write(text + "\n")
    sys.stdout.flush()


def main():
    opt = parse(sys.argv[1:])
    pool = opt["pool"]
    arms, unknown = selected(opt)
    budget = {n: b for n, _, b in opt["list"]}
    verdicts = {}   # arm -> (verdict, ms, reason)

    def settle(arm, verdict, ms, why=""):
        """THE ONE CHANNEL: an arm's verdict is printed exactly once, as an `ARM` line, when
        it is FINAL — after the pool's baseline held (or failed) behind it. The app's own
        `ARM` line is held until then (echoed as `arm-result`, which no reader counts)."""
        verdicts[arm] = (verdict, ms, why)
        say("ARM %s.%s %s %d%s" % (pool, arm, verdict, ms, (" " + why) if why else ""))

    for u in unknown:
        settle(u, "FAIL", 0, "no such arm in pool %s" % pool)
    order = [n for n, _, _ in arms] + unknown
    remaining = list(arms)
    process = 0
    restarts = 0

    while remaining:
        process += 1
        spec = ",".join("%s=%s" % (n, p) for n, p, _ in remaining)
        cmd = [opt["app"], "--scripts", spec, "--pool", pool]
        if opt["headless"]:
            cmd.append("--headless")
        if opt["baseline"]:
            cmd += ["--pool-baseline", opt["baseline"]]
        cmd += opt["extra"]
        say("pool: %s process %d — %d arm(s): %s" % (pool, process, len(remaining),
                                                     " ".join(n for n, _, _ in remaining)))
        started = time.monotonic()
        proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                preexec_fn=child_setup)
        q = queue.Queue()
        t = threading.Thread(target=reader, args=(proc.stdout, q), daemon=True)
        t.start()

        current = None          # the arm that began and has no result yet
        current_t0 = 0.0
        held = None             # (arm, verdict, ms, why): a result waiting for its baseline
        deadline = started + opt["boot"]
        killed_for = None       # "arm:<name>", "baseline:<name>" or "boot"
        began_any = False
        fails_here = 0
        lost = None
        while True:
            # THE BUDGET ON EVERY TURN, not only on a silent second: a hung arm that keeps
            # printing must time out too.
            if killed_for is None and time.monotonic() > deadline and proc.poll() is None:
                if current: killed_for = "arm:" + current
                elif held: killed_for = "baseline:" + held[0]
                else: killed_for = "boot"
                say("pool: %s — %s past its budget: killing the process" % (pool, killed_for))
                proc.kill()
            try:
                line = q.get(timeout=1.0)
            except queue.Empty:
                continue
            if line is None:
                break
            m = ARM_END.match(line)
            if m and m.group(1) == pool:
                say("arm-result " + line[len("ARM "):])
                held = (m.group(2), m.group(3), int(m.group(4)), m.group(5) or "")
                if m.group(3) != "PASS": fails_here += 1
                current = None
                # between two arms the app restores its baseline: the next ARM-BEGIN (or the
                # process's end) is due within the boot budget.
                deadline = time.monotonic() + opt["boot"]
                continue
            m = BASELINE_LOST.match(line)
            if m and m.group(1) == pool:
                say(line)
                lost = (m.group(2), m.group(3))
                continue
            say(line)
            m = ARM_BEGIN.match(line)
            if m and m.group(1) == pool:
                if held:            # the previous arm's baseline held: its result is final
                    settle(*held); held = None
                current = m.group(2)
                began_any = True
                current_t0 = time.monotonic()
                deadline = current_t0 + budget.get(current, opt["boot"])
                continue
        rc = proc.wait()
        t.join(timeout=5)
        how = ("signal %d" % -rc) if rc < 0 else ("exit %d" % rc)

        if current is not None:
            ms = int((time.monotonic() - current_t0) * 1000)
            if killed_for == "arm:" + current:
                settle(current, "TIMEOUT", ms, "past its %.0f s budget" % budget[current])
            else:
                settle(current, "CRASH", ms, "the process died in the arm (%s)" % how)
        if held:
            # THE BASELINE BEHIND THE LAST ARM: it must hold, and the process must end the way
            # its results say (exit = the number of failed arms). A death, a hang or a lost
            # baseline there is THIS arm's CRASH — never a silent restart.
            arm, v, ms, why = held
            if lost and lost[0] == arm:
                settle(arm, "CRASH", ms, "baseline lost after the arm: %s" % lost[1])
            elif killed_for == "baseline:" + arm:
                settle(arm, "CRASH", ms, "hung in the pool's baseline after the arm (killed)")
            elif rc < 0 or rc != min(fails_here, 255):
                settle(arm, "CRASH", ms, "the process died after the arm, in the pool's baseline "
                                         "or its exit (%s, %d failed arm(s))" % (how, fails_here))
            else:
                settle(arm, v, ms, why)
        remaining = [a for a in remaining if a[0] not in verdicts]
        if remaining and not began_any:
            # The process never started an arm: a boot failure. Restarting would fail the
            # same way; every remaining arm is named.
            for n, _, _ in remaining:
                settle(n, "CRASH", 0, "the pool's process never began an arm (%s)" % how)
            remaining = []
        elif remaining:
            restarts += 1
            say("POOL %s RESTART %d — the process ended with %d arm(s) left (%s); restarting from %s"
                % (pool, restarts, len(remaining), how, remaining[0][0]))

    say("")
    say("POOL %s VERDICTS (%d process(es), %d restart(s))" % (pool, process, restarts))
    bad = []
    for n in order:
        v, ms, why = verdicts.get(n, ("CRASH", 0, "no verdict"))
        say("  %s.%s: %s %d%s" % (pool, n, v, ms, (" " + why) if why else ""))
        if v != "PASS":
            bad.append(n)
    say("POOL %s: %d arm(s) — %d PASS, %d not PASS, %d restart(s)" % (
        pool, len(order), len(order) - len(bad), len(bad), restarts))
    if bad:
        say("solo retry: JAH_POOL_ARMS=%s ctest -R '^pool\\.%s$'" % (
            ",".join("%s.%s" % (pool, n) for n in bad), pool))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
