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
  * THE TIER (TEST-TIER-1). `--tier low` starts every process with `--test-tier low`
    (every scene it binds on the Low World Mode, the window 1280x720 — services/testtier.h);
    `--tier epic` (the default) passes nothing, so each scene keeps its own tier — the
    pixel pools. At every boot the app prints `POOL-MEM <pool> gpuPoolUsed=<MB>
    textures=<MB> tier=<t>` (app.memoryStats / app.textureMemory); this driver prints it as
    `MEM <pool> gpuPoolUsed=<MB> textures=<MB> processMiB=<MiB> tier=<t>`, the last figure
    the process's own nvidia-smi line, and the run log (scripts/gate_runlog.py) records it on
    the pool's row.
  * THE LEAK PROBE. After every arm's baseline the app prints `POOL-MEM <pool>.<arm>
    gpuPoolUsed=<MB> textures=<MB>`; this driver prints it as `MEM <pool>.<arm> ...` (the run log
    records it on the arm) and, per process, reads the arm-to-arm curve (curve_findings,
    TESTING-DEBTS-1): the ONE-TIME STEP (the largest rise, >= 16 MB and larger than all the
    others together — a cache or a sample's textures, allocated once) is the FINDING line
    `STEP <pool>.<arm> +<MB>`; a MONOTONIC CLIMB (>= 3 consecutive rising arms, the step
    excluded) is `LEAK <pool> +<MB> over <n> arms`. And an arm whose figure passes 3x its
    process's boot is `OVER <pool>.<arm> gpuPoolUsed=<MB> tier=<t> boot=<MB>` (a pixel claim
    wrongly in a Low pool). All three are findings, never a red (the first runs decide).
  * THE VRAM BUDGET (lane GATE-ADMIT-1; docs/TESTING_GATE.md §4b). `--vram-tokens <k>`: before
    every app process the driver takes k box-wide VRAM tokens (scripts/vram_tokens.py — the
    same flock files as scripts/gpu-admit.sh), all or nothing, waiting (one `vram: waiting …`
    line) rather than letting the card over-fill; the app inherits them and they are released
    when it ends, so a restart re-takes them and the driver holds none between processes. The
    wait is BEFORE the process starts, outside every arm's budget; a wait past its bound makes
    every remaining arm `NOADMIT` (the helper's line as why), never a run on a full card.
  * THE KERNEL'S WORD (GPU_LOSS_AUDIT_2026-09-27 B: a device loss is not always a red arm). After
    each process ends, the driver reads the kernel journal since its launch (`journalctl -k`,
    readable by the adm group, never sudo) and a `NVRM: Xid` line from THAT process's pid turns
    the arm that was running at the fault's second (or the baseline behind it — the arm before)
    into `CRASH xid <n>`. An unreadable journal is printed as a FINDING, never a red of every
    pool: the one row that reds for it is devprocess.kernel_journal. macOS has no NVRM log. JAH_KERNEL_JOURNAL=<file> reads a file
    of `journalctl -o short-unix` lines instead (the runner's own test).
  * THE VERDICT, PER ARM, ON ONE CHANNEL: each arm's final `ARM <pool>.<arm>
    PASS|FAIL|CRASH|TIMEOUT <ms> [why]` line is printed exactly once, by this driver
    (the app's own result line is echoed as `arm-result …`, which no reader counts) —
    the run log (scripts/gate_runlog.py) reads these lines. The row fails iff an arm is
    not PASS, and a closing summary (`  <pool>.<arm>: <verdict> …`) repeats them for a
    reader of the output.

Usage:
  run_pool.py --pool <name> --app <Jahshaka> [--headless] [--arms a,b] [--baseline <js>]
              [--boot-budget <s>] [--tier low|epic] [--vram-tokens <k>] --arm <name> <script> <budget-s> [--arm ...]
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

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "scripts"))
import vram_tokens  # noqa: E402  (scripts/vram_tokens.py — THE box-wide VRAM budget)
import kernel_xid   # noqa: E402  (scripts/kernel_xid.py — THE one reader of the kernel's GPU faults)

ARM_BEGIN = re.compile(r"^ARM-BEGIN (\S+)\.(\S+)\s*$")
ARM_END = re.compile(r"^ARM (\S+)\.(\S+) (PASS|FAIL) (\d+)(?: (.*))?$")
BASELINE_LOST = re.compile(r"^POOL-BASELINE-LOST (\S+)\.(\S+) (.*)$")
POOL_MEM = re.compile(r"^POOL-MEM (\S+) (.*?) tier=(\S+)\s*$")
ARM_MEM = re.compile(r"^POOL-MEM (\S+)\.(\S+) gpuPoolUsed=(\d+) textures=(\d+)\s*$")
TIERS = ("low", "epic")


def usage(msg):
    sys.stderr.write("run_pool.py: %s\n" % msg)
    sys.exit(2)


def parse(argv):
    opt = {"pool": None, "app": None, "headless": False, "arms": None,
           "boot": 300.0, "list": [], "extra": [], "baseline": None, "tier": "epic", "vram": 0}
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
        elif a == "--tier":
            opt["tier"] = argv[i + 1]; i += 2
            if opt["tier"] not in TIERS:
                usage("--tier: '%s' is not one of %s" % (opt["tier"], ", ".join(TIERS)))
        elif a == "--headless":
            opt["headless"] = True; i += 1
        elif a == "--arms":
            opt["arms"] = [x for x in argv[i + 1].split(",") if x]; i += 2
        elif a == "--vram-tokens":
            opt["vram"] = int(argv[i + 1]); i += 2
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


def process_mib(pid):
    """The process's own VRAM as nvidia-smi reports it (MiB), or None (no GPU, no tool, or a
    process the driver does not list — a headless one)."""
    try:
        out = subprocess.run(["nvidia-smi", "--query-compute-apps=pid,used_memory",
                              "--format=csv,noheader,nounits"], capture_output=True, text=True,
                             timeout=10).stdout
    except (OSError, subprocess.SubprocessError):
        return None
    for row in out.splitlines():
        parts = [x.strip() for x in row.split(",")]
        if len(parts) == 2 and parts[0] == str(pid):
            try: return int(parts[1])
            except ValueError: return None
    return None


# THE STEP'S FLOOR: a single arm-to-arm rise below it is drift, never a one-time step.
STEP_MIN_MB = 16
# THE OVER FACTOR: an arm whose figure passes its process's boot figure by this factor holds a
# working set its tier does not explain (a pixel claim in a Low pool: Low boots ~0.3 GB).
OVER_FACTOR = 3


def curve_findings(curve):
    """The leak probe over one process (TESTING-DEBTS-1 T2): `curve` is [(arm, gpuPoolUsed MB)] in
    arm order (the boot's figure is NOT a point: boot -> first arm is that arm's working set).
    Returns (step, leak):
      step = (arm, MB) — THE ONE-TIME STEP: the largest arm-to-arm rise, when it is at least
             STEP_MIN_MB and larger than every other rise together (a cache or a sample's
             textures allocated once); a FINDING, not a leak.
      leak = (rise MB, n) — a MONOTONIC CLIMB: the longest run of n >= 3 consecutive rising
             arm-to-arm steps, the step excluded (it breaks a run); rise = the run's sum.
    Either may be None."""
    steps = [(curve[i][0], curve[i][1] - curve[i - 1][1]) for i in range(1, len(curve))]
    step = None
    rises = [(a, d) for a, d in steps if d > 0]
    if rises:
        big = max(rises, key=lambda x: x[1])
        if big[1] >= STEP_MIN_MB and big[1] > sum(d for _, d in rises) - big[1]:
            step = big
    best, run = None, []
    for a, d in steps + [(None, 0)]:
        if d > 0 and not (step and a == step[0]):
            run.append(d)
            continue
        if len(run) >= 3 and (best is None or len(run) > best[1] or
                              (len(run) == best[1] and sum(run) > best[0])):
            best = (sum(run), len(run))
        run = []
    return step, best


def say(text):
    sys.stdout.write(text + "\n")
    sys.stdout.flush()


def main():
    opt = parse(sys.argv[1:])
    pool = opt["pool"]
    arms, unknown = selected(opt)
    budget = {n: b for n, _, b in opt["list"]}
    verdicts = {}   # arm -> (verdict, ms, reason)

    # THE OUTPUT IS TRIAGE, NOT A LOG: the app's own lines go to one file per process
    # (pool-logs/, in the row's working directory, which fresh_home wipes every run) and reach
    # ctest's output only for an arm that did NOT pass (or a process that never began one). A
    # pool's whole output is megabytes, and ctest truncates what it keeps of a passing row —
    # the run log's `ARM` lines were cut away with it (measured: 21 of 29 doc arms recorded).
    os.makedirs("pool-logs", exist_ok=True)
    pending = {}    # arm -> its buffered lines
    boot = []       # lines of the current process before its first arm

    batch = []      # this process's settled arms, printed once its end and the kernel are read
    journal_bad = []

    def settle(arm, verdict, ms, why="", now=False):
        """THE ONE CHANNEL: an arm's verdict is printed exactly once, as an `ARM` line, when
        it is FINAL — after the pool's baseline held (or failed) behind it AND the kernel's
        journal for its process was read (an Xid from the process's pid overrides it). The
        app's own `ARM` line is not repeated (it is `arm-result …` in the per-process log)."""
        if not now:
            batch.append([arm, verdict, ms, why])
            return
        verdicts[arm] = (verdict, ms, why)
        lines = pending.pop(arm, [])
        if verdict != "PASS" and lines:
            say("---- %s.%s: its output (%d line(s)) ----" % (pool, arm, len(lines)))
            for l in lines: say("| " + l)
        say("ARM %s.%s %s %d%s" % (pool, arm, verdict, ms, (" " + why) if why else ""))

    def flush(pid, since, begins):
        """The process ended: read the kernel's word for its pid, then print its arms."""
        if pid is not None and batch:
            if not os.environ.get("JAH_KERNEL_JOURNAL") and sys.platform.startswith("linux"):
                # WHY ONE SECOND IS ENOUGH: the Xid is logged by the kernel AT THE FAULT, and the
                # faulting process outlives it by seconds (the fence wait until VK_ERROR_DEVICE_LOST
                # surfaces measured 10-11 s, GPU_LOSS_AUDIT B; a driver kill comes later still), so
                # by the process's exit the line is already in the kernel ring; what is left is
                # journald's ingest of /dev/kmsg, milliseconds on this box. /dev/kmsg itself is not
                # readable here (dmesg_restrict=1), so the journal is the only reader.
                time.sleep(1.0)
            xids = kernel_xid.kernel_xids(since, {pid})
            if xids is None and not journal_bad:
                journal_bad.append(pid)
                # A FINDING, NOT A RED (one hygiene row owns it: devprocess.kernel_journal) — an
                # unreadable journal is the box's configuration, never the pool's arms.
                say("pool: %s — %s" % (pool, kernel_xid.FINDING))
            for t, n, xp, _line in xids or []:
                arm = None
                for bt, name in begins:
                    if bt <= t: arm = name
                if arm is None: arm = batch[0][0]
                for e in batch:
                    if e[0] == arm:
                        e[1] = "CRASH"
                        e[3] = "xid %d (the kernel's GPU fault from pid %d at %s)%s" % (
                            n, xp, time.strftime("%H:%M:%S", time.localtime(t)),
                            ("; " + e[3]) if e[3] else "")
        for arm, v, ms, why in batch:
            settle(arm, v, ms, why, now=True)
        del batch[:]

    for u in unknown:
        settle(u, "FAIL", 0, "no such arm in pool %s" % pool, now=True)
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
        if opt["tier"] != "epic":
            cmd += ["--test-tier", opt["tier"]]
        if opt["baseline"]:
            cmd += ["--pool-baseline", opt["baseline"]]
        cmd += opt["extra"]
        say("pool: %s process %d — %d arm(s): %s" % (pool, process, len(remaining),
                                                     " ".join(n for n, _, _ in remaining)))
        logname = os.path.join("pool-logs", "%s-process%d.log" % (pool, process))
        plog = open(logname, "w")
        say("pool: %s process %d output -> %s" % (pool, process, os.path.abspath(logname)))
        boot = []
        # THE VRAM BUDGET: this process's tokens, taken BEFORE it starts (outside every budget)
        # and inherited by it; released below when it has ended. A restart re-takes them.
        try:
            tokens = vram_tokens.acquire(opt["vram"], "pool.%s process %d" % (pool, process),
                                         log=sys.stdout)
        except vram_tokens.AdmitTimeout as e:
            plog.close()
            say("pool: %s — %s" % (pool, e))
            for n, _, _ in remaining:
                settle(n, "NOADMIT", 0, "never ran: %s" % e, now=True)
            remaining = []
            break
        env = dict(os.environ)
        if tokens:
            env["JAH_VRAM_HELD"] = str(len(tokens))
        started = time.monotonic()
        since = time.time()
        begins = []             # (epoch, arm) of every ARM-BEGIN in this process
        proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                preexec_fn=child_setup, pass_fds=tokens, env=env)
        q = queue.Queue()
        t = threading.Thread(target=reader, args=(proc.stdout, q), daemon=True)
        t.start()

        current = None          # the arm that began and has no result yet
        current_t0 = 0.0
        held = None             # (arm, verdict, ms, why): a result waiting for its baseline
        deadline = started + opt["boot"]
        killed_for = None       # "arm:<name>", "baseline:<name>" or "boot"
        began_any = False
        curve = []              # the leak probe: [(arm, gpuPoolUsed MB)] after each arm's baseline
        boot_mb, boot_tier = None, None
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
            plog.write(line + "\n")
            if current is not None: pending.setdefault(current, []).append(line)
            elif held: pending.setdefault(held[0], []).append(line)
            else: boot.append(line)
            m = ARM_END.match(line)
            if m and m.group(1) == pool:
                held = (m.group(2), m.group(3), int(m.group(4)), m.group(5) or "")
                if m.group(3) != "PASS": fails_here += 1
                current = None
                # between two arms the app restores its baseline: the next ARM-BEGIN (or the
                # process's end) is due within the boot budget.
                deadline = time.monotonic() + opt["boot"]
                continue
            m = ARM_MEM.match(line)
            if m and m.group(1) == pool:
                curve.append((m.group(2), int(m.group(3))))
                say("MEM %s.%s gpuPoolUsed=%s textures=%s" % (pool, m.group(2), m.group(3), m.group(4)))
                # T3: an arm past OVER_FACTOR x its process's boot holds what its tier does not
                # explain — a FINDING on the arm (a pixel claim wrongly in a Low pool), never a red
                if boot_mb and int(m.group(3)) > OVER_FACTOR * boot_mb:
                    say("OVER %s.%s gpuPoolUsed=%s tier=%s boot=%d" % (pool, m.group(2), m.group(3),
                                                                       boot_tier, boot_mb))
                continue
            m = POOL_MEM.match(line)
            if m and m.group(1) == pool:
                bm = re.match(r"gpuPoolUsed=(\d+)", m.group(2))
                boot_mb = int(bm.group(1)) if bm else None
                boot_tier = m.group(3)
                mib = process_mib(proc.pid) if not opt["headless"] else 0
                body = m.group(2)
                if body == "headless": body = "gpuPoolUsed=0 textures=0"
                say("MEM %s %s processMiB=%s tier=%s" % (pool, body,
                                                         "?" if mib is None else mib, m.group(3)))
                continue
            m = BASELINE_LOST.match(line)
            if m and m.group(1) == pool:
                lost = (m.group(2), m.group(3))
                continue
            m = ARM_BEGIN.match(line)
            if m and m.group(1) == pool:
                if held:            # the previous arm's baseline held: its result is final
                    settle(*held); held = None
                current = m.group(2)
                begins.append((time.time(), current))
                say(line)           # ARM-BEGIN: the run log's crash detector reads it
                pending[current] = [line]
                began_any = True
                current_t0 = time.monotonic()
                deadline = current_t0 + budget.get(current, opt["boot"])
                continue
        rc = proc.wait()
        vram_tokens.release(tokens)
        t.join(timeout=5)
        step, leak = curve_findings(curve)
        pts = " ".join("%s=%d" % (a, mb) for a, mb in curve)
        if step:
            say("STEP %s.%s +%d (process %d: %s)" % (pool, step[0], step[1], process, pts))
        if leak:
            say("LEAK %s +%d over %d arms (process %d: %s)" % (pool, leak[0], leak[1], process, pts))
        plog.close()
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
        flush(proc.pid, since, begins)
        remaining = [a for a in remaining if a[0] not in verdicts]
        if remaining and not began_any:
            say("---- %s process %d: its output before any arm (%d line(s)) ----" % (pool, process, len(boot)))
            for l in boot[-200:]: say("| " + l)
            # The process never started an arm: a boot failure. Restarting would fail the
            # same way; every remaining arm is named.
            for n, _, _ in remaining:
                settle(n, "CRASH", 0, "the pool's process never began an arm (%s)" % how)
            flush(proc.pid, since, begins)
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
    if journal_bad:
        say("POOL %s: FINDING — no Xid check (the kernel journal is unreadable; devprocess.kernel_journal)" % pool)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
