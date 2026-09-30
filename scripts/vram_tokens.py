#!/usr/bin/env python3
"""vram_tokens.py — THE BOX-WIDE VRAM BUDGET (lane GATE-ADMIT-1; docs/TESTING_GATE.md §4b).

Every lane's gate is its own ctest process, and ctest's RESOURCE_GROUPS / RESOURCE_LOCK /
RUN_SERIAL all stop at the edge of ONE ctest process — so four lanes at -j4 put ~16 Vulkan
processes on one 16 GB card and the late ones die of VK_ERROR_OUT_OF_DEVICE_MEMORY
(SPECS/audits/GPU_LOSS_AUDIT_2026-09-27.md A3: 47 such reds on 2026-09-27). The budget has to
live OUTSIDE ctest, like the GPU-timing lock (scripts/gpu-exclusive.sh): N advisory flock
files in one directory, and every process that boots Vulkan in a test holds k of them for its
life. MEASURED: 1 token = 1,090 MiB at the peak (two MERGE tiers sharing the budget: 13,333 MiB
with 12 held over a 280 MiB desktop; ~820 MiB at p95).

THE CONTRACT
  * N tokens: /tmp/jah-vram/token.00 .. token.<N-1>; N = JAH_VRAM_TOKENS (default 11 — ~12.2 GB
    at the measured 1,090 MiB/token + the 280 MiB desktop, leaving ~1.3 GB beside the owner's
    2.8 GB instance on the 16.4 GB card; 12 left 243 MiB). Nothing untokened is inside it: the
    owner's instance, the desktop, an app started by hand without this helper.
    JAH_VRAM_TOKENS=0 turns admission off (the command runs at once).
  * ALL OR NOTHING, LOWEST FIRST, NEVER HOLD-AND-WAIT. An acquirer takes the TURNSTILE
    (/tmp/jah-vram/turnstile) first; holding it, it reads the free tokens from the kernel's lock
    table (/proc/locks) and locks the k LOWEST free ones only when k are free — it never holds
    part of a request (without /proc/locks: a non-blocking scan from index 0 that drops every
    token on a short count). Short of k it polls again, still holding the turnstile, so a
    3-token request at the head of the queue is not starved by a stream of 1-token ones behind
    it (they wait on the turnstile, holding nothing). A token holder never waits for anything,
    so there is no cycle.
  * THE TOKENS DIE WITH THE PROCESS. The flocks live in the kernel's open-file table: `admit`
    holds them and runs the command as its CHILD with the token fds inherited (TEST-SELECTOR-1
    H4: the admission stays to read the kernel's word on the row — kernel_xid.py — after it), so
    the pid ctest started holds them and every process it spawns does too; a crash, a ctest
    timeout kill or a SIGKILL frees them (the child dies with the admission: PR_SET_PDEATHSIG).
    Nothing durable is written (the file bytes are only a label for a reader).
  * THE WAIT IS BOUNDED (JAH_VRAM_WAIT seconds, default 900, the same bound as the GPU lock) and
    it happens BEFORE the command starts; the CMake helpers add the bound to the row's TIMEOUT
    once, so a wait never eats a row's own budget. An expired wait exits 75 (EX_TEMPFAIL) and
    never runs the command. A wait prints ONE line, `vram: waiting for <k> tokens, <n> free`,
    and a closing `vram: admitted …` line with the tokens it got, so the triage sees it.
  * nvidia-smi is NOT consulted (racy, slow, and blind to what a process will allocate next):
    the token count is the contract, tuned by measurement.

  * ONE ADMISSION FOR TIME TOO (TEST-SELECTOR-1 G1+G2, plan 9ab GPU-RATIO-LOCK-1): a row that
    measures time or GPU budget asks for ALL the tokens (`admit all`, what scripts/gpu-exclusive.sh
    runs). The turnstile above already keeps a small-request stream from starving it, so the GPU
    drains to the timing row and NOTHING untimed shares the card while it measures — the separate
    flock it used to hold excluded only other flock holders, and the ratio rows went red beside
    sibling GPU rows (gi.rt_reflect_cost, gi.field_scroll). A solo retry (`gate-scope.sh --solo`)
    sets JAH_VRAM_ALL=1: every admission of it takes all the tokens, so "solo" is solo on the card.

Usage:
  vram_tokens.py admit <k>|all [--label <text>] [--timing] [--run-timeout <s>] -- <command> [args...]
      (scripts/gpu-admit.sh; scripts/gpu-exclusive.sh = `admit all --timing`). --timing prints the
      wait as `gpu-lock: waited <s> s` (the run log's lockWaitS, never the row's time);
      --run-timeout starts the row's own budget AFTER the admission (timeout(1)).
  vram_tokens.py status                                              (who holds what, now)
Python: `acquire(k, label)` -> [fds] (inheritable), `release(fds)`; run_pool.py uses these.
"""
import errno
import fcntl
import os
import sys
import time

EX_TEMPFAIL = 75
POLL_S = 0.1
LAST_WAIT_S = 0.0        # the last acquire()'s wait, seconds (the --timing line reads it)


class AdmitTimeout(Exception):
    pass


def token_dir():
    return os.environ.get("JAH_VRAM_DIR", "/tmp/jah-vram")


def token_count():
    try:
        return max(0, int(os.environ.get("JAH_VRAM_TOKENS", "11")))
    except ValueError:
        return 11


def wait_bound():
    try:
        return float(os.environ.get("JAH_VRAM_WAIT", "900"))
    except ValueError:
        return 900.0


def _open(path):
    # O_CLOEXEC off for tokens (they must survive exec); the turnstile is closed before exec.
    fd = os.open(path, os.O_RDWR | os.O_CREAT, 0o666)
    return fd


def _try_lock(fd):
    try:
        fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
        return True
    except OSError as e:
        if e.errno in (errno.EWOULDBLOCK, errno.EAGAIN, errno.EACCES):
            return False
        raise


def _paths(n):
    d = token_dir()
    return [os.path.join(d, "token.%02d" % i) for i in range(n)]


def _free_indices(n):
    """The free token indices right now, read from the kernel's lock table (/proc/locks) WITHOUT
    touching a token. None where /proc/locks does not exist (macOS: the lock scan is used)."""
    try:
        with open(os.environ.get("JAH_VRAM_PROC_LOCKS", "/proc/locks")) as f:   # the override: a test's fake table
            held = set()
            for line in f:
                p = line.split()
                # "1: FLOCK  ADVISORY  WRITE 12345 00:1f:67890 0 EOF" (a blocked waiter: "1: -> FLOCK …")
                if len(p) >= 6 and p[1] == "FLOCK":
                    held.add(p[5].lower())
    except OSError:
        return None
    free = []
    for i, path in enumerate(_paths(n)):
        try:
            if lock_key(os.stat(path)) not in held:
                free.append(i)
        except OSError:
            free.append(i)
    return free


def lock_key(st):
    """A file's identity as /proc/locks prints it: MAJ:MIN:INODE, the device in hex (two digits
    at least), the inode in decimal. The DEVICE matters: an inode number alone matches a flock on
    any file of any other filesystem with the same number — a phantom HELD token and a wait of up
    to the bound for nothing."""
    return "%02x:%02x:%d" % (os.major(st.st_dev), os.minor(st.st_dev), st.st_ino)


def _free_count(n):
    f = _free_indices(n)
    return None if f is None else len(f)


def _say(log, text):
    if log is not None:
        log.write(text + "\n")
        log.flush()


def acquire(k, label="", wait=None, log=sys.stderr):
    """Take k tokens (all or nothing, lowest free indices first). Returns the token fds,
    inheritable, which the caller keeps open for exactly the life of the GPU process. Raises
    AdmitTimeout past the wait bound, holding nothing."""
    global LAST_WAIT_S
    LAST_WAIT_S = 0.0
    n = token_count()
    if n <= 0 or k <= 0:
        return []
    if os.environ.get("JAH_VRAM_ALL") and k < n and not os.environ.get("JAH_VRAM_HELD"):
        _say(log, "vram: a solo run (JAH_VRAM_ALL) — taking all %d tokens, not %d — %s" % (n, k, label))
        k = n
    if os.environ.get("JAH_VRAM_HELD"):
        # NEVER HOLD-AND-WAIT: a process already admitted (a gpu-admit.sh row whose command
        # starts another admitted command, a pool app) runs on its parent's tokens — a nested
        # wait could deadlock the box with every parent holding tokens its child waits for.
        _say(log, "vram: already admitted by the parent (%s tokens) — %s" % (os.environ["JAH_VRAM_HELD"], label))
        return []
    if k > n:
        _say(log, "vram: %d tokens asked, the box has %d — taking all %d" % (k, n, n))
        k = n
    wait = wait_bound() if wait is None else wait
    d = token_dir()
    os.makedirs(d, exist_ok=True)
    t0 = time.monotonic()
    deadline = t0 + wait
    waited = False
    turnstile = _open(os.path.join(d, "turnstile"))
    try:
        # 1. THE TURNSTILE: one acquirer scans at a time, and the head of the queue keeps it.
        while not _try_lock(turnstile):
            if not waited:
                f = _free_count(n)
                _say(log, "vram: waiting for %d tokens, %s free (queued behind another request)%s"
                     % (k, "?" if f is None else f, (" — " + label) if label else ""))
                waited = True
            if time.monotonic() > deadline:
                raise AdmitTimeout("NOADMIT vram: no admission for %d tokens within %.0f s (%s free, queued behind "
                                   "another request)%s" % (k, wait, _free_count(n), (" — " + label) if label else ""))
            time.sleep(POLL_S)
        # 2. THE SCAN: lowest free first, all or nothing. On Linux the free set is READ from the
        # kernel's lock table first and the k lowest are locked only when k are free — so a
        # waiter never holds part of its request, not even for a scan (only the turnstile holder
        # acquires, so a token seen free stays free until it is taken). Elsewhere (no
        # /proc/locks) the tokens are try-locked in order and all dropped on a short count.
        while True:
            got, idx = [], []
            view = _free_indices(n)
            candidates = view[:k] if view is not None and len(view) >= k else (
                [] if view is not None else list(range(n)))
            for i in candidates:
                if len(got) == k:
                    break
                fd = _open(_paths(n)[i])
                if _try_lock(fd):
                    got.append(fd)
                    idx.append(i)
                else:
                    os.close(fd)
                    if view is not None:
                        break       # a foreign lock raced the view: drop everything, look again
            if len(got) == k:
                break
            for fd in got:
                os.close(fd)        # closing the fd drops its flock: nothing is held while waiting
            free = len(view) if view is not None else len(got)
            if not waited:
                _say(log, "vram: waiting for %d tokens, %d free%s" % (k, free, (" — " + label) if label else ""))
                waited = True
            if time.monotonic() > deadline:
                raise AdmitTimeout("NOADMIT vram: no admission for %d tokens within %.0f s (%d of %d free at "
                                   "the last look)%s" % (k, wait, free, n, (" — " + label) if label else ""))
            time.sleep(POLL_S)
    finally:
        os.close(turnstile)
    for fd in got:
        os.set_inheritable(fd, True)
        try:
            os.ftruncate(fd, 0)
            os.pwrite(fd, ("%d %s\n" % (os.getpid(), label)).encode(), 0)
        except OSError:
            pass
    LAST_WAIT_S = time.monotonic() - t0
    if waited:
        _say(log, "vram: admitted with %d tokens %s after %.1f s%s"
             % (k, ",".join(str(i) for i in idx), LAST_WAIT_S, (" — " + label) if label else ""))
    return got


def release(fds):
    for fd in fds:
        try:
            os.close(fd)
        except OSError:
            pass


def status(out=sys.stdout):
    """Which tokens are held right now and the label the holder wrote — read from /proc/locks
    (never touching a token); elsewhere a non-blocking probe per token."""
    n = token_count()
    held = 0
    view = _free_indices(n)
    for i, path in enumerate(_paths(n)):
        if view is not None:
            if i in view:
                out.write("%s free\n" % os.path.basename(path))
            else:
                held += 1
                try:
                    with open(path) as f:
                        label = f.read(200).strip()
                except OSError:
                    label = ""
                out.write("%s HELD by %s\n" % (os.path.basename(path), label or "?"))
            continue
        if not os.path.exists(path):
            out.write("%s free (never used)\n" % os.path.basename(path))
            continue
        fd = os.open(path, os.O_RDONLY)
        try:
            if _try_lock(fd):
                fcntl.flock(fd, fcntl.LOCK_UN)
                out.write("%s free\n" % os.path.basename(path))
            else:
                held += 1
                label = os.pread(fd, 200, 0).decode("utf-8", "replace").strip()
                out.write("%s HELD by %s\n" % (os.path.basename(path), label or "?"))
        finally:
            os.close(fd)
    out.write("vram: %d of %d tokens held (%s)\n" % (held, n, token_dir()))
    return held


def main(argv):
    if not argv or argv[0] in ("-h", "--help"):
        sys.stderr.write(__doc__)
        return 64
    if argv[0] == "status":
        status()
        return 0
    if argv[0] != "admit" or len(argv) < 2:
        sys.stderr.write("usage: vram_tokens.py admit <k>|all [--label <text>] [--timing] [--run-timeout <s>] "
                         "-- <command> [args...]\n")
        return 64
    if argv[1] == "all":
        k = max(1, token_count())            # admission off (0 tokens) still runs the command at once
    else:
        try:
            k = int(argv[1])
        except ValueError:
            sys.stderr.write("vram_tokens.py: token count '%s' is not a number or 'all'\n" % argv[1])
            return 64
    rest = argv[2:]
    label, timing, run = "", False, ""
    while rest and rest[0] in ("--label", "--timing", "--run-timeout"):
        if rest[0] == "--timing":
            timing, rest = True, rest[1:]
        elif len(rest) >= 2:
            if rest[0] == "--label": label = rest[1]
            else: run = rest[1]
            rest = rest[2:]
        else:
            break
    if rest[:1] == ["--"]:
        rest = rest[1:]
    if not rest:
        sys.stderr.write("usage: vram_tokens.py admit <k>|all [--label <text>] [--timing] [--run-timeout <s>] "
                         "-- <command> [args...]\n")
        return 64
    if not label:
        label = os.path.basename(rest[0]) + (" " + " ".join(os.path.basename(a) for a in rest[1:3]) if len(rest) > 1 else "")
    try:
        held = acquire(k, label)
    except AdmitTimeout as e:
        sys.stderr.write(str(e) + " — the command did not run\n")
        return EX_TEMPFAIL
    if timing:
        # THE WAIT IS NOT THE ROW'S TIME (LOCK-WAIT-1): one line, read by the run log as lockWaitS
        sys.stderr.write("gpu-lock: waited %.1f s\n" % LAST_WAIT_S)
        sys.stderr.flush()
    if held:
        os.environ["JAH_VRAM_HELD"] = str(len(held))
    # THE ROW'S OWN BUDGET, from AFTER the admission (LOCK-WAIT-1): the queue is never charged to it.
    if run:
        rest = ["timeout", "--verbose", "-k", "15", run] + rest
    return supervise(rest, held, label)


def _pdeathsig():
    """In the child, before exec: die with the admission (a SIGKILL of the pid ctest started must
    not orphan the row). Linux only; elsewhere nothing."""
    try:
        import ctypes
        ctypes.CDLL(None, use_errno=True).prctl(1, 9)      # PR_SET_PDEATHSIG, SIGKILL
    except (OSError, AttributeError):
        pass


def supervise(argv, held, label):
    """THE ROW RUNS AS THIS ADMISSION'S CHILD (TEST-SELECTOR-1 H4 — it was exec'd in place, and
    no one was left to read how it ended): the tokens stay held here and are inherited by the row;
    a SIGTERM/SIGINT/SIGHUP to the pid ctest started is forwarded; the row's process tree is
    tracked while it lives, and after it the kernel journal is read for an Xid from ANY pid of
    that tree since the launch (scripts/kernel_xid.py, the one reader): a GPU fault turns the row
    red, printed with the kernel's own lines — an Xid from a row's pid is never environmental.
    An unreadable journal is a FINDING line (devprocess.kernel_journal is the row that reds for
    it). The row's exit code passes through, and a death by signal is re-raised as the same
    signal, so ctest still names a segfault a segfault."""
    import signal
    import subprocess
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import kernel_xid
    t0 = time.time()
    try:
        child = subprocess.Popen(argv, pass_fds=tuple(held), preexec_fn=_pdeathsig if sys.platform.startswith("linux") else None)
    except OSError as e:
        sys.stderr.write("vram_tokens.py: cannot run %s: %s\n" % (argv[0], e))
        return 127
    forward = lambda sig, _frame: child.poll() is None and os.kill(child.pid, sig)
    for sig in (signal.SIGTERM, signal.SIGINT, signal.SIGHUP, signal.SIGQUIT):
        signal.signal(sig, forward)
    tracker = kernel_xid.TreeTracker(child.pid)
    tracker.start()
    while True:
        try:
            rc = child.wait()
            break
        except InterruptedError:
            continue
    tracker.stop()
    if rc != 0 and not os.environ.get("JAH_KERNEL_JOURNAL") and sys.platform.startswith("linux"):
        time.sleep(1.0)      # journald's ingest of the ring (run_pool.py: why one second is enough)
    xids = kernel_xid.kernel_xids(t0 - 1, tracker.pids)
    if xids is None:
        sys.stderr.write("vram: %s (%s)\n" % (kernel_xid.FINDING, label))
    elif xids:
        for t, n, pid, line in xids:
            sys.stderr.write("XID %d from pid %d of the row %s — THE GPU FAULTED (never environmental): %s\n"
                             % (n, pid, label, line))
    sys.stderr.flush()
    if rc < 0:
        sys.stderr.write("row-exit: %s died of signal %d\n" % (label, -rc)); sys.stderr.flush()
        release(held)
        try:
            import resource
            resource.setrlimit(resource.RLIMIT_CORE, (0, 0))    # the row's core is the row's, not ours
        except (ImportError, ValueError, OSError):
            pass
        signal.signal(-rc, signal.SIG_DFL)
        os.kill(os.getpid(), -rc)
        return 128 - rc
    if xids and rc == 0:
        return 1
    return rc


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
