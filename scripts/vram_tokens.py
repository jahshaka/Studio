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
    it happens BEFORE the command starts. It is no longer in any row's TIMEOUT (TESTING-CLEANUP-2B:
    the +900 s widening is gone): inside a gate ctest schedules the rows by their tokens
    (RESOURCE_GROUPS against the build's resource spec, tests/cmake/vram_rows.cmake), so a gate's rows
    never wait here for each other; a short admission inside a gate is another process's tokens and
    exits 75 at once (row_wait_bound: NOADMIT, re-queued by the gate); outside a gate a row whose
    TIMEOUT ends while still waiting is recorded NOADMIT by the run log (gate_runlog.unadmitted_wait). An expired wait exits 75 (EX_TEMPFAIL) and
    never runs the command. A wait prints ONE line, `vram: waiting for <k> tokens, <n> free`,
    and a closing `vram: admitted …` line with the tokens it got, so the triage sees it.
  * nvidia-smi is NOT consulted (racy, slow, and blind to what a process will allocate next):
    the token count is the contract, tuned by measurement.

  * ONE ADMISSION FOR TIME TOO (TEST-SELECTOR-1 G1+G2, plan 9ab GPU-RATIO-LOCK-1): a row that
    measures time or GPU budget asks for ALL the tokens (`admit all`, what scripts/gpu-exclusive.sh
    runs). The turnstile above already keeps a small-request stream from starving it, so the GPU
    drains to the timing row and NOTHING untimed shares the card while it measures — the separate
    flock it used to hold excluded only other flock holders, and the ratio rows went red beside
    sibling GPU rows (gi.rt_reflect_cost, gi.field_scroll). A solo retry batch (`gate-scope.sh --solo`)
    holds every token once for the batch (hold_card below); when that drain times out it sets
    JAH_VRAM_ALL=1 instead: every admission of it takes all the tokens. Either way "solo" is solo on the card.

  * THE CLOCKS COME BACK (lane TEST-1, plan 9cl CLOCK-TRAP-1: PHOTON-I-1's cost run left the card
    locked at 2550 MHz when its own trap did not run). `--lock-clocks MIN,MAX` locks the GPU clocks
    AFTER the admission (the card is this row's), RECORDS the lock with its owner
    (/tmp/jah-gpu-clocks.lock: pid, label, spec) and restores them before the tokens go — a normal
    end, a non-zero exit, a forwarded SIGTERM/SIGINT/SIGHUP, a crash of the row. It restores ONLY a
    lock it recorded itself: a lock somebody else holds (a live owner's record) is neither taken
    nor undone, and a lock nobody recorded is nobody's business here (rc-gate's close reports it).
    Locking needs `sudo -n nvidia-smi`; refused, the run says `gpu-clocks: NOT locked … provisional`.

  * ONE GATE AT A TIME, BOX-WIDE (GATE-COST-1 P1; SPECS/audits/GATE_COST_2026-10-09.md §3e: the same
    ~600-row tier took 0.5-0.7 h with no sibling gate and 2.6-6.6 h beside 2.6-3.9 of them — the box
    finished FEWER tiers per hour the more ran at once). THE GATE SLOT sits beside the tokens: a gate
    run (`gate-scope.sh --run`, `--fork-tier`, `gate_runlog.py run` — the rc tiers and a batch candidate's)
    holds it for its whole run, every phase and the target step included. Waiters queue BY CLASS (below, item 13) on
    tickets (<dir>/gate-queue/<seq>.<pid>, each flocked by its waiter for exactly its life — a dead
    waiter's ticket is reaped by the next reader), the head of the queue IS the holder, and there is
    NO bound: a waiting gate prints `gate-slot: queued at position <p> behind <holder>` (again when the
    position moves) and runs when its turn comes. The ticket fd is the GATE PROCESS's (not its
    ctest's): the gate's ctest tree dies with it (gate_runlog: PDEATHSIG, a reaper, its signal
    handlers), so the slot is free the moment the gate is gone. (`gate` exec's its command with it.)
    A WHOLE-CARD HOLD ALWAYS TAKES IT (GATE-COST-2, the band-aid audit's #2): hold_card() outside a
    gate — a `--solo` batch, an attribution, a timing phase run by hand — queues for the slot (class 2, behind any waiting gate) with
    the gates before it drains the card. (Out of the slot, its drain kept the turnstile up to 3600 s
    while the gate in the slot NOADMITted every row at 900 s: the audit's 62-of-70 "turnstile-queued"
    cause.) So does `admit all` (gpu-exclusive.sh: a timing row run outside a gate), inside the admission's
    own bound. Per-row admissions of k tokens (`admit <k>`, a pool's app, a hand run) never take it, nor
    does a build. A process already inside a gate (JAH_GATE_SLOT_HELD) never queues again.
    JAH_GATE_SLOT=0 turns the slot off. The waiting line is re-printed every 10 min with the holder's
    AGE, and `status` prints every holder's age, so a hung holder is visible.
  * THE WHOLE CARD ONCE PER PHASE (GATE-COST-1 P2; the audit's §3f: 278 per-row drains in 38 h,
    17.0 h of whole-card drain or hold): the serial timing phase, a `--solo` batch and the target step
    take EVERY token once (`hold_card()`), and their rows run nested on it (JAH_VRAM_HELD — the path
    above): one drain per phase instead of one per row. A drain past JAH_VRAM_PHASE_WAIT (3600 s) prints
    `status()` — every holder, its label and its AGE — and leaves LAST_DRAIN_TIMEOUT for the caller's
    `kind: drain-timeout` run-log record (gate_runlog); the phase then runs with per-row admission. Nothing shares the card while a timing row
    measures — now true for the whole phase — and each row still prints its `gpu-lock: waited` line.

Usage:
  vram_tokens.py admit <k>|all [--label <text>] [--timing] [--run-timeout <s>] [--lock-clocks MIN,MAX]
                 -- <command> [args...]
      (scripts/gpu-admit.sh; scripts/gpu-exclusive.sh = `admit all --timing`). --timing prints the
      wait as `gpu-lock: waited <s> s` (the run log's lockWaitS, never the row's time);
      --run-timeout starts the row's own budget AFTER the admission (timeout(1));
      --lock-clocks locks the GPU clocks for the row and restores them on every exit path.
  vram_tokens.py gate [--label <text>] -- <command> [args...]
      run <command> holding THE GATE SLOT (queued by class, no bound; `gate --class N`); scripts/gpu-admit.sh gate
  vram_tokens.py status                                              (who holds what, now)
Python: `acquire(k, label)` -> [fds] (inheritable), `release(fds)`; run_pool.py uses these.
        `gate_slot(label)` -> fd|None (the slot, inheritable; close it to give it up);
        `hold_card(label)` -> (fds, env) — every token for a phase, and the env its rows run in.
"""
import errno
import fcntl
import os
import re
import sys
import time

EX_TEMPFAIL = 75
POLL_S = 0.1
LAST_WAIT_S = 0.0        # the last acquire()'s wait, seconds (the --timing line reads it)
_T_ACQ = 0.0             # when the last acquire() began (monotonic)
LAST_DRAIN_TIMEOUT = None   # hold_card()'s last drain timeout: {label, waitS, why, holders} (GATE-COST-2)
LAST_SLOT_WAIT_S = None  # the last queue wait for the gate slot, seconds (GATE-LOG-1: the run log's slot_wait_s;
                         # None when it did not queue — held already or off)


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


def row_wait_bound():
    """THE ADMISSION NEVER WAITS INSIDE A GATE (TESTING-CLEANUP-2B fix C). Inside one gate ctest already schedules the
    rows by their tokens (RESOURCE_GROUPS, tests/cmake/vram_rows.cmake), so a row that finds its tokens taken is
    blocked by a process OUTSIDE the gate — and every second it waited was charged to the row's own TIMEOUT (a row
    admitted late, then killed, read as a TIMEOUT of its code). So inside a gate (a live JAH_GATE_SLOT_HELD) the
    admission tries ONCE: short, it exits 75 (NOADMIT, never ran) and the gate re-queues the row at its end (P5); a
    final try that is still short stays NOADMIT — MISSING to the judge, which a re-run answers, never a skip.
    Outside a gate (a hand run) the bound is JAH_VRAM_WAIT's, as before.
    THE ONE EXCEPTION INSIDE THE SLOT (delta F-C2, decided): a `--solo` batch or an `--attribute` run whose whole-card
    drain timed out falls back to JAH_VRAM_ALL=1 (each admission takes the whole card itself) while holding the slot;
    its cells are a measurement's own runs, not a gate's rows re-queued at its end, so they keep the wait — without it
    every cell of that path failed at once and the attribution read INCOMPLETE."""
    if os.environ.get("JAH_VRAM_ALL"):
        return wait_bound()
    if os.environ.get("JAH_GATE_SLOT_HELD") and slot_held_valid():
        return 0.0
    return wait_bound()


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


def lock_holder(path):
    """'pid <p> (<label>)' of the process holding `path`'s flock, read from /proc/locks (None: unheld, or no table).
    The label is the holder's `--label` argument, else its command line (200 chars)."""
    try:
        key = lock_key(os.stat(path))
        with open(os.environ.get("JAH_VRAM_PROC_LOCKS", "/proc/locks")) as f:
            for line in f:
                p = line.split()
                if len(p) >= 6 and p[1] == "FLOCK" and p[5].lower() == key:
                    pid = p[4]
                    try:
                        argv = open("/proc/%s/cmdline" % pid, "rb").read().split(b"\0")
                        argv = [a.decode(errors="replace") for a in argv if a]
                    except OSError:
                        argv = []
                    lab = argv[argv.index("--label") + 1] if "--label" in argv[:-1] else " ".join(argv)[:200]
                    return "pid %s (%s)" % (pid, lab or "?")
    except OSError:
        return None
    return None


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
    global LAST_WAIT_S, _T_ACQ
    LAST_WAIT_S = 0.0
    _T_ACQ = time.monotonic()
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
    wait = row_wait_bound() if wait is None else wait
    d = token_dir()
    os.makedirs(d, exist_ok=True)
    t0 = time.monotonic()
    deadline = t0 + wait
    waited = False
    # A GATE ROW OWNS THE CARD (TESTING-CLEANUP-2C item 2): inside a gate (a live slot, no JAH_VRAM_ALL) the admission
    # tries ONCE — and a FOREIGN request waiting at the turnstile (a sibling's frozen ctest, a hand run outside the gate)
    # must not refuse it while enough tokens are free: batch C2 lost four rows to "NOADMIT … within 0 s (9 free, queued
    # behind another request)". So a gate row skips a turnstile someone else holds and scans the tokens itself (each
    # token is its own flock, all-or-nothing: no double hold); NOADMIT only when tokens are truly short, naming the
    # foreign request (pid, label) that sat ahead.
    gate_row = wait <= 0 and not os.environ.get("JAH_VRAM_ALL") and bool(os.environ.get("JAH_GATE_SLOT_HELD")) \
        and slot_held_valid()
    ahead = None
    turnstile = _open(os.path.join(d, "turnstile"))
    try:
        # 1. THE TURNSTILE: one acquirer scans at a time, and the head of the queue keeps it.
        while not _try_lock(turnstile):
            if gate_row:
                ahead = lock_holder(os.path.join(d, "turnstile")) or "a request (unnamed)"
                _say(log, "vram: a gate row owns the card — the turnstile is held by a foreign request (%s); "
                     "scanning the tokens without it%s" % (ahead, (" — " + label) if label else ""))
                break
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
                                   "the last look)%s%s" % (k, wait, free, n,
                                                           ("; ahead at the turnstile: " + ahead) if ahead else "",
                                                           (" — " + label) if label else ""))
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


# ---- THE GATE SLOT (GATE-COST-1 P1; the docstring's ONE GATE AT A TIME) ----------------------------
SLOT_POLL_S = 1.0
# A ticket: <seq>.<pid>.c<class>, and <seq>.<pid>.c<class>.held once its process HOLDS the slot (renamed under the
# queue's counter lock, its flock kept — the lock lives on the inode).
_TICKET = re.compile(r"^(\d+)\.(\d+)\.c(\d)(\.held)?$")
# A ticket of the FIFO slot before item 13 (<seq>.<pid>), asked by a tree whose scripts predate it: read as a class-1
# gate, and the lowest such ticket as HOLDING (that is what its own reader believes) — so a new asker never takes the
# slot beside an old holder. (The other way round cannot be closed: an old reader does not see these tickets — every
# tree that runs a gate takes the new scripts at once.)
_OLD_TICKET = re.compile(r"^(\d+)\.(\d+)$")

# THE SLOT IS BY PRIORITY, NOT ARRIVAL (TESTING-CLEANUP-2B item 13; the owner 2026-10-10: "the gate needs to run, not
# wait for everything else"). Three classes:
#   0  the owner's smoke rc (rc-gate JAH_GATE_TIER=smoke)
#   1  a GATE: a batch / end / push / stage-close / fork tier (rc-gate), a `gate-scope.py --run`, `gate_runlog.py run`
#   2  a lane's MEASUREMENT: gpu-exclusive.sh (`admit all`), `--solo`, `--attribute`, a whole-card hold run by hand
# A waiter of a lower class number is served before every waiting higher one (by ticket inside a class); it never
# displaces the HOLDER. A free slot is taken at once by whoever asks. A class-2 holder that holds the slot across rows
# (--solo, --attribute) checks the queue between rows and YIELDS to a waiting class 0/1 (yield_slot / yield_card):
# it gives the slot (and the card) up, re-asks at its class and resumes where it was — its recorded runs stay.
SLOT_CLASSES = {0: "smoke", 1: "gate", 2: "measurement"}


def slot_class(label="", cls=None):
    """The asker's class: the explicit one, else read from the label the caller gives (rc-gate's `rc-<tag> <tier>`:
    `smoke` -> 0, any other rc tier -> 1); anything else a caller did not name is a gate (1)."""
    if cls is not None:
        return max(0, min(2, int(cls)))
    if re.search(r"(^|\s)smoke(\s|$)", label or ""):
        return 0
    return 1


def slot_enabled():
    return os.environ.get("JAH_GATE_SLOT", "1") != "0"


def _queue_dir():
    return os.path.join(token_dir(), "gate-queue")


def _ticket_alive(path):
    """A ticket lives while its waiter — or a process that inherited its fd — holds its flock."""
    try:
        fd = os.open(path, os.O_RDONLY)
    except OSError:
        return False
    try:
        return not _try_lock(fd)        # a lock we got is a dead ticket's (it goes with the close)
    finally:
        os.close(fd)


def _label_of(path):
    try:
        with open(path) as f:
            return f.read(200).strip() or "?"
    except OSError:
        return "?"


def gate_queue():
    """[(seq, pid, path, cls, held)] of the live tickets — the HOLDER first, then the waiters in the order they will be
    served (class, then ticket). A dead waiter's or holder's ticket is reaped here (nothing durable outlives a gate)."""
    q = []
    try:
        names = os.listdir(_queue_dir())
    except OSError:
        return q
    old = []
    for n in names:
        m = _TICKET.match(n)
        mo = None if m else _OLD_TICKET.match(n)
        if not m and not mo:
            continue
        path = os.path.join(_queue_dir(), n)
        if _ticket_alive(path):
            if m:
                q.append((int(m.group(1)), int(m.group(2)), path, int(m.group(3)), bool(m.group(4))))
            else:
                old.append((int(mo.group(1)), int(mo.group(2)), path, 1, False))
        else:
            try:
                os.unlink(path)
            except OSError:
                pass
    if old:
        old.sort()
        old[0] = old[0][:4] + (not any(t[4] for t in q),)
        q += old
    return sorted(q, key=lambda t: (not t[4], t[3], t[0]))


def _take_ticket(label, cls=1):
    """The next sequence number and its ticket, BOTH under the counter's flock (F3, the merge read: with
    the rename after the unlock, a later number could be renamed in first, find no lower ticket and take
    the slot while the earlier one, renamed in after it, did the same — two holders). The ticket is
    LOCKED BEFORE it is visible (made under a private name, flocked, then renamed in): no reader ever
    sees a live ticket unlocked and reaps it."""
    d = _queue_dir()
    os.makedirs(d, exist_ok=True)
    cf = _open(os.path.join(token_dir(), "gate-queue.seq"))
    try:
        fcntl.flock(cf, fcntl.LOCK_EX)
        raw = os.pread(cf, 32, 0).decode("ascii", "replace").strip()
        seq = (int(raw) if raw.isdigit() else 0) + 1
        os.ftruncate(cf, 0)
        os.pwrite(cf, str(seq).encode(), 0)
        tmp = os.path.join(d, ".new.%d" % os.getpid())
        fd = os.open(tmp, os.O_RDWR | os.O_CREAT | os.O_TRUNC, 0o666)
        fcntl.flock(fd, fcntl.LOCK_EX)
        os.pwrite(fd, ("%d %s\n" % (os.getpid(), label)).encode(), 0)
        os.rename(tmp, os.path.join(d, "%012d.%d.c%d" % (seq, os.getpid(), cls)))
    finally:
        os.close(cf)
    return seq, fd


def _try_take(seq, cls):
    """Under the queue's counter lock: take the slot when nobody holds it and no waiter outranks this ticket (a lower
    class, or the same class and an earlier ticket). Returns (taken, the tickets ahead of it)."""
    cf = _open(os.path.join(token_dir(), "gate-queue.seq"))
    try:
        fcntl.flock(cf, fcntl.LOCK_EX)
        q = gate_queue()
        ahead = [t for t in q if t[4] or (t[3], t[0]) < (cls, seq)]
        if ahead:
            return False, ahead
        mine = [t for t in q if t[0] == seq]
        if mine:
            os.rename(mine[0][2], mine[0][2] + ".held")
        return True, []
    finally:
        os.close(cf)


SLOT_REPRINT_S = 600.0


def _age(path):
    """How long ago a ticket or token was taken (its file was written at the take), as text."""
    try:
        s = max(0.0, time.time() - os.stat(path).st_mtime)
    except OSError:
        return "?"
    return "%dh%02dm" % (s // 3600, s % 3600 // 60) if s >= 3600 else "%dm%02ds" % (s // 60, s % 60)


def gate_slot(label="", log=sys.stderr, cls=None):
    """Queue for THE GATE SLOT (by class, no bound) and return its fd once this gate is the head —
    inheritable across an exec (the `gate` command), never handed to a ctest; closing it gives it up. None when the
    slot is off (JAH_GATE_SLOT=0) or this process is already inside a gate (JAH_GATE_SLOT_HELD).
    The process is marked as inside a gate (JAH_GATE_SLOT_HELD) from here on, and the queue's wait is
    RETURNED to the run log (GATE-LOG-1: every record of this gate carries it as slot_wait_s — through the
    module and JAH_GATE_SLOT_WAIT_S, which the gate's own run_ctest() reads)."""
    fd = _queue_for_slot(label, log, cls=cls)
    if fd is not None:
        os.environ["JAH_GATE_SLOT_HELD"] = str(os.getpid())
        os.environ["JAH_GATE_SLOT_WAIT_S"] = str(LAST_SLOT_WAIT_S)
    return fd


def slot_held_valid(value=None):
    """Whether JAH_GATE_SLOT_HELD (or `value`) names a LIVE pid that holds a ticket in this token dir's queue
    (GATE-COST-2 F6): a gate's own pid, inherited by its rows. A hand-set or stale value is not a holder."""
    v = os.environ.get("JAH_GATE_SLOT_HELD") if value is None else value
    try:
        pid = int(v)
    except (TypeError, ValueError):
        return False
    return any(t[1] == pid for t in gate_queue())


def _queue_for_slot(label, log, wait=None, cls=None):
    """The queue's wait itself (by class, then ticket): the slot's fd once this process is the head, None when the slot is off or
    already this process's. Leaves the environment alone (hold_card hands JAH_GATE_SLOT_HELD to its
    phase's rows only). Sets LAST_SLOT_WAIT_S (the queue's wait; None when it did not queue)."""
    global LAST_SLOT_WAIT_S
    LAST_SLOT_WAIT_S = None
    if os.environ.get("JAH_GATE_SLOT_HELD"):
        if slot_held_valid():
            _say(log, "gate-slot: already held by this gate (pid %s) — %s" % (os.environ["JAH_GATE_SLOT_HELD"], label))
            return None
        # F6: a JAH_GATE_SLOT_HELD that names no live ticket holder is no holder — queue like anyone (the
        # records name it as an override)
        _say(log, "gate-slot: JAH_GATE_SLOT_HELD=%s names no live holder — queueing — %s"
             % (os.environ["JAH_GATE_SLOT_HELD"], label))
    if not slot_enabled():
        return None
    cls = slot_class(label, cls)
    seq, fd = _take_ticket(label, cls)
    # the ticket orders a class (by it): said once, so a reader orders gates by the queue's own tickets, never by
    # when it happened to start them (GATE-COST-2: no wall clock)
    _say(log, "gate-slot: queued ticket %d, class %d (%s) — %s" % (seq, cls, SLOT_CLASSES[cls], label))
    t0, last, said = time.monotonic(), None, time.monotonic()
    while True:
        taken, ahead = _try_take(seq, cls)
        if taken:
            break
        if wait is not None and time.monotonic() - t0 > wait:
            os.close(fd)                     # out of the queue: the ticket dies with its fd
            raise AdmitTimeout("NOADMIT vram: the gate slot was not free within %.0f s (held by %s)%s"
                               % (wait, _label_of(ahead[0][2]), (" — " + label) if label else ""))
        if any(t[1] == os.getpid() for t in ahead):
            # this very process already holds (or waits for) the slot: never queue behind yourself
            os.close(fd)
            _say(log, "gate-slot: already held by this process (pid %d) — %s" % (os.getpid(), label))
            return None
        if len(ahead) != last or time.monotonic() - said >= SLOT_REPRINT_S:
            _say(log, "gate-slot: queued at position %d (%d ahead) behind %s, holding it for %s — waiting for the slot, "
                 "no bound — %s" % (len(ahead), len(ahead), _label_of(ahead[0][2]), _age(ahead[0][2]), label))
            last, said = len(ahead), time.monotonic()
        time.sleep(SLOT_POLL_S)
    os.set_inheritable(fd, True)
    LAST_SLOT_WAIT_S = round(time.monotonic() - t0, 1)
    _say(log, "gate-slot: taken%s — %s" % ((" after %.0f s in the queue" % LAST_SLOT_WAIT_S) if last else "", label))
    return fd


def phase_wait():
    try:
        return float(os.environ.get("JAH_VRAM_PHASE_WAIT", "3600"))
    except ValueError:
        return 3600.0


def hold_card(label="", log=sys.stderr, wait=None, cls=2):
    """THE WHOLE CARD FOR A PHASE (GATE-COST-1 P2): every token, taken ONCE (one drain), and the
    environment the phase's rows run in (JAH_VRAM_HELD: each row's own admission runs on these
    tokens at once — the nested path of acquire()). Returns (fds, env); the caller keeps the fds
    open for the phase and releases them after it. Already admitted, or admission off: ([], env).
    A WHOLE-CARD HOLD TAKES THE GATE SLOT FIRST (GATE-COST-2): outside a gate it queues (class 2) with the
    gates (no bound) and the slot's fd is the LAST of the returned fds — released with them; the env
    carries JAH_GATE_SLOT_HELD for the phase's rows. A drain past JAH_VRAM_PHASE_WAIT (3600 s) prints
    status() (the holders and their AGE), records it in LAST_DRAIN_TIMEOUT (the caller's run-log
    record) and the phase runs as before, each row asking for its own tokens — still inside the slot."""
    global LAST_DRAIN_TIMEOUT
    LAST_DRAIN_TIMEOUT = None
    env = dict(os.environ)
    n = token_count()
    if os.environ.get("JAH_VRAM_HELD") or n <= 0:
        return [], env
    slot = _queue_for_slot(label + " (whole card)", log, cls=cls)
    if slot is not None:
        env["JAH_GATE_SLOT_HELD"] = str(os.getpid())
        env["JAH_GATE_SLOT_WAIT_S"] = str(LAST_SLOT_WAIT_S)     # GATE-LOG-1: the hold's queue wait rides its records
    try:
        fds = acquire(n, label, wait=phase_wait() if wait is None else wait, log=log)
    except AdmitTimeout as e:
        import io
        st = io.StringIO()
        status(st)
        holders = [l for l in st.getvalue().splitlines() if " HELD by " in l]
        LAST_DRAIN_TIMEOUT = {"label": label, "waitS": round(time.monotonic() - _T_ACQ, 1), "why": str(e),
                              "holders": holders}
        _say(log, "vram: %s — the phase runs with per-row admission instead. The card's holders now:\n%s"
             % (e, st.getvalue().rstrip()))
        return ([slot] if slot is not None else []), env
    _say(log, "vram: the whole card (%d tokens) held for the phase, drained in %.1f s — %s" % (n, LAST_WAIT_S, label))
    env["JAH_VRAM_HELD"] = str(n)
    # GATE-LOG-1: the phase's drain and the moment its hold began — each record of the phase carries
    # drain_s and hold_s (the hold so far at the row's end; the phase's last row reads the whole hold)
    env["JAH_VRAM_DRAIN_S"] = "%.1f" % LAST_WAIT_S
    env["JAH_VRAM_HELD_AT"] = "%.3f" % time.time()
    return fds + ([slot] if slot is not None else []), env


def outranked(cls=2):
    """True when THIS process holds the slot and a waiter of a lower class number (a gate, the owner's smoke) asks."""
    q = gate_queue()
    if not any(t[1] == os.getpid() and t[4] for t in q):
        return False
    return any(not t[4] and t[3] < cls for t in q)


def yield_card(card, env, label="", log=sys.stderr, cls=2):
    """BETWEEN ROWS of a class-2 hold (--solo, --attribute): when a gate or the smoke waits, give the card and the slot
    up, re-ask at this class (behind it) and return the new (card, env) — the caller resumes at its next row; what it
    already ran is recorded. Unchanged (card, env) when nothing outranks it (or it never held the slot itself)."""
    if not outranked(cls):
        return card, env
    waiting = [t for t in gate_queue() if not t[4] and t[3] < cls]
    _say(log, "gate-slot: YIELDING to a class %d asker (%s) — the hold gives the card and the slot up and re-asks — %s"
         % (waiting[0][3], _label_of(waiting[0][2]), label))
    release(card)
    held = os.environ.pop("JAH_GATE_SLOT_HELD", None)
    try:
        card, env = hold_card(label, log=log, cls=cls)
    finally:
        if held is not None:
            os.environ["JAH_GATE_SLOT_HELD"] = held
    _say(log, "gate-slot: resumed after the yield — %s" % label)
    return card, env


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
                out.write("%s HELD by %s for %s\n" % (os.path.basename(path), label or "?", _age(path)))
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
                out.write("%s HELD by %s for %s\n" % (os.path.basename(path), label or "?", _age(path)))
        finally:
            os.close(fd)
    out.write("vram: %d of %d tokens held (%s)\n" % (held, n, token_dir()))
    q = gate_queue()
    if not q:
        out.write("gate-slot: nobody holds it\n")      # (never the word " free": a token count greps for it)
    for i, (seq, pid, path, cls, held) in enumerate(q):
        out.write("gate-slot: %s %s (class %d %s, ticket %d, for %s)\n"
                  % ("HELD by" if held else "queued %d:" % (i if q[0][4] else i + 1), _label_of(path), cls,
                     SLOT_CLASSES[cls], seq, _age(path)))
    return held


def main(argv):
    if not argv or argv[0] in ("-h", "--help"):
        sys.stderr.write(__doc__)
        return 64
    if argv[0] == "status":
        status()
        return 0
    if argv[0] == "gate":
        # a whole gate under the slot (scripts/gpu-admit.sh gate): the command is EXEC'D with the
        # slot's fd inherited, so the slot is free when the command and everything it spawned are gone
        rest, label, cls = argv[1:], "", None
        while rest[:1] in (["--label"], ["--class"]) and len(rest) >= 2:
            if rest[0] == "--label":
                label = rest[1]
            else:
                if rest[1] not in ("0", "1", "2"):
                    sys.stderr.write("vram_tokens.py gate: --class is 0 (smoke), 1 (gate) or 2 (measurement)\n")
                    return 64
                cls = int(rest[1])
            rest = rest[2:]
        if rest[:1] == ["--"]:
            rest = rest[1:]
        if not rest:
            sys.stderr.write("usage: vram_tokens.py gate [--label <text>] -- <command> [args...]\n")
            return 64
        gate_slot(label or " ".join(os.path.basename(a) for a in rest[:3]), cls=cls)
        sys.stderr.flush()
        os.execvp(rest[0], rest)
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
    label, timing, run, lock = "", False, "", ""
    while rest and rest[0] in ("--label", "--timing", "--run-timeout", "--lock-clocks"):
        if rest[0] == "--timing":
            timing, rest = True, rest[1:]
        elif len(rest) >= 2:
            if rest[0] == "--label": label = rest[1]
            elif rest[0] == "--lock-clocks": lock = rest[1]
            else: run = rest[1]
            rest = rest[2:]
        else:
            break
    if lock and not __import__("re").fullmatch(r"\d{3,4},\d{3,4}", lock):
        sys.stderr.write("vram_tokens.py: --lock-clocks wants MIN,MAX in MHz (e.g. 2100,2550), not '%s'\n" % lock)
        return 64
    if rest[:1] == ["--"]:
        rest = rest[1:]
    if not rest:
        sys.stderr.write("usage: vram_tokens.py admit <k>|all [--label <text>] [--timing] [--run-timeout <s>] "
                         "-- <command> [args...]\n")
        return 64
    if not label:
        label = os.path.basename(rest[0]) + (" " + " ".join(os.path.basename(a) for a in rest[1:3]) if len(rest) > 1 else "")
    # `admit all` IS A WHOLE-CARD HOLD (gpu-exclusive.sh: a timing row run outside a gate): it takes the gate
    # slot like every other (GATE-COST-2) — inside the admission's own bound, so a hand-run timing row queues
    # behind a gate (NOADMIT past the bound) instead of draining the card under it. Inside a gate the slot is
    # the gate's (JAH_GATE_SLOT_HELD); nested or with admission off there is nothing to drain.
    slot, t_slot = None, time.monotonic()
    try:
        if argv[1] == "all" and token_count() > 0 and not os.environ.get("JAH_VRAM_HELD"):
            slot = _queue_for_slot(label + " (admit all)", sys.stderr, wait=wait_bound(), cls=2)
        slot_wait = time.monotonic() - t_slot
        held = acquire(k, label, wait=max(0.0, row_wait_bound() - slot_wait))
    except AdmitTimeout as e:
        if slot is not None:
            os.close(slot)
        sys.stderr.write(str(e) + " — the command did not run\n")
        return EX_TEMPFAIL
    global LAST_WAIT_S
    LAST_WAIT_S += slot_wait                 # the queue, slot and drain, is never the row's time
    if slot is not None:
        os.environ["JAH_GATE_SLOT_HELD"] = str(os.getpid())   # the row's own processes are inside it
    if timing:
        # THE WAIT IS NOT THE ROW'S TIME (LOCK-WAIT-1): one line, read by the run log as lockWaitS
        sys.stderr.write("gpu-lock: waited %.1f s\n" % LAST_WAIT_S)
        sys.stderr.flush()
    if held:
        os.environ["JAH_VRAM_HELD"] = str(len(held))
    # THE ROW'S OWN BUDGET, from AFTER the admission (LOCK-WAIT-1): the queue is never charged to it.
    if run:
        rest = ["timeout", "--verbose", "-k", "15", run] + rest
    locked = lock_clocks(lock, label) if lock else False
    done = []

    def clocks_back():
        # CLOCK-TRAP-1: back to the driver's own clocks before the tokens go, whatever ended the row.
        # Called by supervise the moment the row ends — BEFORE it re-raises a signal the row died of
        # (a re-raised signal never reaches a `finally`) — and by the `finally` below for anything
        # else (an exception out of supervise itself). Once.
        if done: return
        done.append(1)
        if locked:
            restore_clocks("the row's lock")
    try:
        return supervise(rest, held, label, on_end=clocks_back)
    finally:
        clocks_back()


def _smi(args):
    import subprocess
    try:
        r = subprocess.run(["sudo", "-n", "nvidia-smi"] + args, capture_output=True, text=True, timeout=20)
        return r.returncode, (r.stdout + r.stderr).strip()
    except (OSError, subprocess.TimeoutExpired) as e:
        return 1, str(e)


CLOCK_RECORD = "/tmp/jah-gpu-clocks.lock"


def _record():
    """The live clock lock's record {pid, label, spec}, or None (none, or its owner is dead)."""
    try:
        rec = __import__("json").load(open(CLOCK_RECORD))
        os.kill(int(rec["pid"]), 0)
        return rec
    except (OSError, ValueError, KeyError, TypeError):
        return None


def lock_clocks(spec, label):
    """Locks the GPU clocks to MIN,MAX (sudo -n nvidia-smi -lgc) and RECORDS the lock with its owner
    (this admission's pid, the row's label) in CLOCK_RECORD. A lock another live owner recorded is
    left alone — this row runs at that owner's clocks and restores nothing. True = this run locked."""
    held = _record()
    if held:
        sys.stderr.write("gpu-clocks: held by %s (pid %s, %s) — this run neither locks nor restores\n"
                         % (held.get("label"), held.get("pid"), held.get("spec")))
        return False
    rc, out = _smi(["-lgc", spec])
    if rc != 0:
        sys.stderr.write("gpu-clocks: NOT locked (%s) — every GPU ms of this run is provisional\n"
                         % (out.splitlines()[-1] if out else "sudo refused"))
        sys.stderr.flush()
        return False
    with open(CLOCK_RECORD, "w") as f:
        __import__("json").dump({"pid": os.getpid(), "label": label, "spec": spec}, f)
    sys.stderr.write("gpu-clocks: locked %s (recorded: pid %d)\n" % (spec, os.getpid()))
    sys.stderr.flush()
    return True


def restore_clocks(why):
    """Restores the clocks THIS process locked — and only those: its own record, nobody else's."""
    held = _record()
    if held and int(held.get("pid", -1)) != os.getpid():
        sys.stderr.write("gpu-clocks: the record is %s's, not ours — nothing restored\n" % held.get("label"))
        return
    rc, out = _smi(["-rgc"])
    if rc == 0:
        try:
            os.unlink(CLOCK_RECORD)
        except OSError:
            pass
        sys.stderr.write("gpu-clocks: restored (%s)\n" % why)
    else:
        sys.stderr.write("gpu-clocks: COULD NOT RESTORE (%s: %s) — run `sudo nvidia-smi -rgc`\n"
                         % (why, out.splitlines()[-1] if out else "sudo refused"))
    sys.stderr.flush()


def _pdeathsig():
    """In the child, before exec: die with the admission (a SIGKILL of the pid ctest started must
    not orphan the row). Linux only; elsewhere nothing."""
    try:
        import ctypes
        ctypes.CDLL(None, use_errno=True).prctl(1, 9)      # PR_SET_PDEATHSIG, SIGKILL
    except (OSError, AttributeError):
        pass


def supervise(argv, held, label, on_end=None):
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
    tracker.scan()           # the tree AT the row's end too: a descendant still alive there is the row's (GATE-COST-2)
    tracker.stop()
    if on_end:
        on_end()
    if not os.environ.get("JAH_KERNEL_JOURNAL") and sys.platform.startswith("linux"):
        # journald's ingest of the ring (run_pool.py: why one second is enough) — on EVERY row, green
        # too (GATE-COST-2, the band-aid audit's #13: a fault on a passing row must not be read too early)
        time.sleep(1.0)
    xids = kernel_xid.kernel_xids(t0 - 1, tracker.pids)
    if xids is None:
        sys.stderr.write("vram: %s (%s)\n" % (kernel_xid.FINDING, label))
    elif xids:
        for t, n, pid, line in xids:
            sys.stderr.write("XID %d from pid %d of the row %s — THE GPU FAULTED (never environmental): %s\n"
                             % (n, pid, label, line))
        # VERDICT-1 U3: the journal window this read covered — the run log's `xid.window`
        sys.stderr.write("XID-WINDOW %s %s\n" % (kernel_xid.window(t0 - 1, time.time()), label))
    sys.stderr.flush()
    if rc < 0:
        sys.stderr.write("row-exit: %s died of signal %d\n" % (label, -rc)); sys.stderr.flush()
        release(held)
        try:
            import resource
            resource.setrlimit(resource.RLIMIT_CORE, (0, 0))    # the row's core is the row's, not ours
        except (ImportError, ValueError, OSError):
            pass
        # the same signal, re-raised: SIGKILL/SIGSTOP cannot (and need not) be reset — signal.signal on
        # them raises (the merge read's D5: a traceback and exit 1 read as FAIL, not CRASH)
        if -rc not in (signal.SIGKILL, signal.SIGSTOP):
            try:
                signal.signal(-rc, signal.SIG_DFL)
            except (OSError, ValueError):
                return 128 - rc
        os.kill(os.getpid(), -rc)
        return 128 - rc         # a signal that did not end us (an ignored one): the shell's convention
    if xids and rc == 0:
        return 1
    return rc


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
