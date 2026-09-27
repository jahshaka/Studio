#!/usr/bin/env python3
"""run_pool.py — THE POOL DRIVER (lane SUITE-POOL-1; jahshaka/docs/TESTING.md §"pools").

One ctest row = one POOL = one app process that runs N scripts ("arms") in turn
(`Jahshaka --scripts <list> --pool <name>`, src/app/cli/scriptrunner.cpp). The app
prints one line per arm; this driver owns what the app cannot:

  * A CRASH. An arm that printed `ARM-BEGIN <pool>.<arm>` and never its `ARM` line
    died with the process: the driver records `CRASH`, starts a NEW process and
    continues from the NEXT arm — one crash costs one arm, never the pool.
  * A HANG. Every arm has a budget (6x its measured seconds, the row's TIMEOUT law):
    an arm past it is killed and recorded `TIMEOUT`, and the pool continues.
  * THE SUBSET. `--arms a,b` here, or `JAH_POOL_ARMS=<pool>.<arm>[,...]` in the
    environment (so `JAH_POOL_ARMS=gi_verbs.gi_status ctest -R '^pool\\.gi_verbs$'`
    is the solo retry of one arm, through the real row). A pool the variable does
    not name runs every arm; `<pool>` alone names the whole pool.
  * THE VERDICT, PER ARM. The row fails iff an arm is not PASS, and the last lines
    of the output name every arm's verdict.

Usage:
  run_pool.py --pool <name> --app <Jahshaka> [--headless] [--arms a,b]
              [--boot-budget <s>] --arm <name> <script> <budget-s> [--arm ...]
              [-- <extra app args>]

JAH_POOL_RECORD=<file>: one JSON line per arm is APPENDED there (pool, arm, verdict,
ms, process, reason) — the hook the gate's run log reads (MODULAR-GATE-1).
"""
import json
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
           "boot": 300.0, "list": [], "extra": []}
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
    verdicts = {}   # arm -> (verdict, ms, reason, process#)
    for u in unknown:
        verdicts[u] = ("FAIL", 0, "no such arm in pool %s" % pool, 0)
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
        cmd += opt["extra"]
        say("pool: %s process %d — %d arm(s): %s" % (pool, process, len(remaining),
                                                     " ".join(n for n, _, _ in remaining)))
        started = time.monotonic()
        proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                preexec_fn=child_setup)
        q = queue.Queue()
        t = threading.Thread(target=reader, args=(proc.stdout, q), daemon=True)
        t.start()

        current = None          # the arm that began and has no verdict yet
        current_t0 = 0.0
        deadline = started + opt["boot"]
        killed_for = None
        began_any = False
        while True:
            try:
                line = q.get(timeout=1.0)
            except queue.Empty:
                line = ""
                if time.monotonic() > deadline and proc.poll() is None:
                    killed_for = current
                    say("pool: %s — %s past its budget (%.0f s): killing the process" % (
                        pool, ("arm " + current) if current else "the boot",
                        budget.get(current, opt["boot"])))
                    proc.kill()
                continue
            if line is None:
                break
            say(line)
            m = ARM_BEGIN.match(line)
            if m and m.group(1) == pool:
                current = m.group(2)
                began_any = True
                current_t0 = time.monotonic()
                deadline = current_t0 + budget.get(current, opt["boot"])
                continue
            m = ARM_END.match(line)
            if m and m.group(1) == pool:
                verdicts[m.group(2)] = (m.group(3), int(m.group(4)), m.group(5) or "", process)
                current = None
                # between two arms the app re-begins its baseline: the next
                # ARM-BEGIN is due within the boot budget.
                deadline = time.monotonic() + opt["boot"]
                continue
        rc = proc.wait()
        t.join(timeout=5)

        if current is not None and current not in verdicts:
            ms = int((time.monotonic() - current_t0) * 1000)
            if killed_for == current:
                verdicts[current] = ("TIMEOUT", ms, "past its %.0f s budget" % budget[current], process)
            else:
                how = ("signal %d" % -rc) if rc < 0 else ("exit %d" % rc)
                verdicts[current] = ("CRASH", ms, "the process died (%s)" % how, process)
        remaining = [a for a in remaining if a[0] not in verdicts]
        if remaining and not began_any:
            # The process never started an arm: a boot failure, not an arm's.
            # Restarting would fail the same way; every remaining arm is named.
            how = ("signal %d" % -rc) if rc < 0 else ("exit %d" % rc)
            if killed_for is None and rc == 0:
                how = "exit 0 with arms left"
            for n, _, _ in remaining:
                verdicts[n] = ("CRASH", 0, "the pool's process never began an arm (%s)" % how, process)
            remaining = []
        elif remaining:
            restarts += 1
            say("pool: %s — the process ended with %d arm(s) left (rc %d); restarting from %s" % (
                pool, len(remaining), rc, remaining[0][0]))

    record = os.environ.get("JAH_POOL_RECORD")
    say("")
    say("POOL %s VERDICTS (%d process(es))" % (pool, process))
    bad = []
    for n in order:
        v, ms, why, proc_no = verdicts.get(n, ("CRASH", 0, "no verdict", 0))
        say("ARM %s.%s %s %d%s" % (pool, n, v, ms, (" " + why) if why else ""))
        if v != "PASS":
            bad.append(n)
        if record:
            with open(record, "a") as f:
                f.write(json.dumps({"pool": pool, "arm": n, "verdict": v, "ms": ms,
                                    "process": proc_no, "reason": why}) + "\n")
    say("POOL %s: %d arm(s) — %d PASS, %d not PASS, %d restart(s)" % (
        pool, len(order), len(order) - len(bad), len(bad), restarts))
    if bad:
        say("solo retry: JAH_POOL_ARMS=%s ctest -R '^pool\\.%s$'" % (
            ",".join("%s.%s" % (pool, n) for n in bad), pool))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
