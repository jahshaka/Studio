#!/usr/bin/env python3
"""devprocess.vram_admit — THE VRAM BUDGET HOLDS (lane GATE-ADMIT-1; docs/TESTING_GATE.md §4b).

scripts/gpu-admit.sh is what bounds the card across every lane's ctest process; a budget that
leaks one token, grants half a request or lets a stream of small rows starve a big one is a
full card again. Judged on the KERNEL'S OWN LOCK TABLE (/proc/locks, sampled every 5 ms: which
pid holds which token inode) and on the order of events the rows record — never on a sleep of
fixed length, and every hold is counted in FRAMES (1/60 s) of a fake command. No GPU is
touched: a private token directory (JAH_VRAM_DIR), never the box's /tmp/jah-vram.

  1. THE BOUND. 14 fake rows (app 2, engine 1, vr 3 tokens — 26 asked of 12) started together:
     at no sampled instant are more than 12 tokens held; every row ran; some row WAITED and
     printed `vram: waiting for <k> tokens, <n> free`.
  2. ALL OR NOTHING. No waiting row holds part of its request: a pid seen holding fewer tokens
     than it asked WHILE ASLEEP (state S with an unchanged voluntary context-switch count across
     the lock-table read — it slept through it) is a hold-and-wait. (The helper reads the free
     set from /proc/locks and locks only when k are free, so it should never hold part at all.)
  3. LOWEST FIRST, AND A KILLED HOLDER FREES. On an empty pool A(2) holds {0,1}, B(1) holds {2};
     SIGKILL A's process tree (a child it spawned inherited the tokens and holds them for as long
     as it lives — the VRAM is still in use); C(1) then holds {0}.
  4. JAH_VRAM_TOKENS=4. Six 2-token rows: never more than 4 tokens held, never a token index >= 4.
  5. THE BOUNDED WAIT. With every token held, JAH_VRAM_WAIT=1 exits 75 and never runs the command.
  6. EXEC IN PLACE. The pid started IS the command (`$$` of the fake = the Popen pid), its exit
     code passes through, and a nested admission (JAH_VRAM_HELD) never waits.

usage: vram_admit.py <scripts/gpu-admit.sh> <scratch-dir>
"""
import os
import shutil
import signal
import subprocess
import sys
import threading
import time

ADMIT, D = sys.argv[1], sys.argv[2]
sys.path.insert(0, os.path.dirname(os.path.abspath(ADMIT)))
import vram_tokens  # noqa: E402
FRAME = 1.0 / 60.0
failures = 0


def check(cond, what):
    global failures
    print(("  ok: " if cond else "  FAIL: ") + what)
    if not cond:
        failures += 1


shutil.rmtree(D, ignore_errors=True)
os.makedirs(D)
FAKE = os.path.join(D, "fake.sh")
with open(FAKE, "w") as f:
    # fake.sh <name> <seconds> <events>: records its pid when admitted, holds for <seconds>
    # (the callers pass FRAMES converted at 1/60 s: frames(n))
    f.write('#!/bin/sh\necho "$1 $$ start" >> "$3"\nsleep "$2"\necho "$1 $$ end" >> "$3"\n')
os.chmod(FAKE, 0o755)


def frames(n):
    return "%.4f" % (n * FRAME)


def env_for(tokdir, n, **extra):
    e = dict(os.environ)
    e.pop("JAH_VRAM_HELD", None)
    e.update({"JAH_VRAM_DIR": tokdir, "JAH_VRAM_TOKENS": str(n), "JAH_VRAM_WAIT": "120"})
    e.update({k: str(v) for k, v in extra.items()})
    return e


def token_inodes(tokdir):
    out = {}
    for f in os.listdir(tokdir) if os.path.isdir(tokdir) else []:
        if f.startswith("token."):
            out[vram_tokens.lock_key(os.stat(os.path.join(tokdir, f)))] = int(f.split(".")[1])
    return out


def held_by_pid(tokdir):
    """{pid: set(token index)} from /proc/locks (granted FLOCKs only, never the waiters)."""
    inodes = token_inodes(tokdir)
    res = {}
    with open("/proc/locks") as f:
        for line in f:
            p = line.split()
            if len(p) >= 6 and p[1] == "FLOCK":
                ino = p[5].lower()
                if ino in inodes:
                    res.setdefault(int(p[4]), set()).add(inodes[ino])
    return res


def sleep_state(pid):
    """(state, voluntary context switches) of a pid, or None once it is gone."""
    try:
        st = {}
        with open("/proc/%d/status" % pid) as f:
            for line in f:
                k, _, v = line.partition(":")
                st[k] = v.strip()
        return st.get("State", "?")[:1], st.get("voluntary_ctxt_switches")
    except OSError:
        return None


class Sampler(threading.Thread):
    """Samples the lock table every 5 ms. THE ALL-OR-NOTHING WITNESS: a pid that holds part of
    its request (0 < held < asked) while it is ASLEEP — state S, and the same voluntary
    context-switch count read before AND after the lock table, so it slept through the read — is
    holding tokens while it waits. A pid caught mid-lock is running (R) and is not counted."""
    def __init__(self, tokdir, asked):
        super().__init__(daemon=True)
        self.tokdir, self.asked = tokdir, asked
        self.stop, self.max_held, self.max_index, self.samples = False, 0, -1, 0
        self.partial_asleep = []
        self.partial_seen = 0

    def run(self):
        while not self.stop:
            pre = {pid: sleep_state(pid) for pid in self.asked}
            h = held_by_pid(self.tokdir)
            post = {pid: sleep_state(pid) for pid in self.asked}
            self.samples += 1
            total = sum(len(s) for s in h.values())
            self.max_held = max(self.max_held, total)
            for s in h.values():
                if s:
                    self.max_index = max(self.max_index, max(s))
            for pid, s in h.items():
                if pid in self.asked and 0 < len(s) < self.asked[pid]:
                    self.partial_seen += 1
                    if pre[pid] and pre[pid] == post[pid] and pre[pid][0] == "S":
                        self.partial_asleep.append((pid, sorted(s)))
            time.sleep(0.005)


def start_rows(rows, tokdir, n, events):
    procs = []
    for name, k, nframes in rows:
        p = subprocess.Popen([ADMIT, str(k), "--label", name, "--", FAKE, name, frames(nframes), events],
                             env=env_for(tokdir, n), stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
        procs.append((name, k, p))
    return procs


# ---- 1 + 2: fourteen rows of three classes against twelve tokens --------------------------
print("1/2. fourteen rows (app 2, engine 1, vr 3) against 12 tokens")
tok = os.path.join(D, "tok12"); ev = os.path.join(D, "events12")
rows = [("app%d" % i, 2, 20 + 3 * i) for i in range(6)] + \
       [("engine%d" % i, 1, 12 + 2 * i) for i in range(5)] + \
       [("vr%d" % i, 3, 25 + 5 * i) for i in range(3)]
procs = start_rows(rows, tok, 12, ev)
sampler = Sampler(tok, {p.pid: k for _, k, p in procs})
sampler.start()
errs = {}
bad_rc = []
for name, k, p in procs:
    _, err = p.communicate()
    errs[name] = err
    if p.returncode != 0:
        bad_rc.append((name, p.returncode))
check(not bad_rc, "every row exited 0 (non-zero: %s)" % bad_rc)
sampler.stop = True
sampler.join()
lines = open(ev).read().splitlines()
check(sum(1 for l in lines if l.endswith(" end")) == 14, "all 14 rows ran to their end (%d)" % sum(1 for l in lines if l.endswith(" end")))
check(sampler.max_held <= 12 and sampler.max_held > 0,
      "at no sampled instant were more than 12 tokens held (max %d over %d samples)" % (sampler.max_held, sampler.samples))
# the event log is a second, sampler-free witness: replay start/end with each row's tokens
cost = {n: k for n, k, _ in rows}
cur = peak = 0
for l in lines:
    name, _, what = l.split()
    cur += cost[name] if what == "start" else -cost[name]
    peak = max(peak, cur)
check(peak <= 12, "replaying the rows' own start/end events: at most %d tokens were ever in use" % peak)
waits = [e for e in errs.values() if "vram: waiting for " in e]
check(len(waits) >= 1 and any(" tokens, " in e and " free" in e for e in waits),
      "26 tokens asked of 12: %d row(s) WAITED, each with the one `vram: waiting for <k> tokens, <n> free` line" % len(waits))
check(not sampler.partial_asleep,
      "all or nothing: no row held part of its request while it waited (asleep holding part: %s; "
      "partial holds seen at all: %d)" % (sampler.partial_asleep[:5], sampler.partial_seen))

# ---- 3: lowest first; a killed holder frees ------------------------------------------------
print("3. lowest free first; SIGKILL frees")
tok = os.path.join(D, "tok3"); ev = os.path.join(D, "events3")


def hold(name, k, nframes=60 * 120):
    return subprocess.Popen([ADMIT, str(k), "--label", name, "--", FAKE, name, frames(nframes), ev],
                            env=env_for(tok, 12), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                            start_new_session=True)


def wait_holds(pid, k):
    for _ in range(400):
        if len(held_by_pid(tok).get(pid, ())) == k:
            return held_by_pid(tok)[pid]
        time.sleep(0.01)
    return held_by_pid(tok).get(pid, set())


A = hold("A", 2); a = wait_holds(A.pid, 2)
B = hold("B", 1); b = wait_holds(B.pid, 1)
check(a == {0, 1} and b == {2}, "A(2) holds %s, B(1) holds %s — the lowest free indices" % (sorted(a), sorted(b)))
# The holder is the row's WHOLE process tree (fake.sh's `sleep` inherited the fds, as an app
# spawned by a suite would): SIGKILL the group, as ctest's timeout kill does.
os.killpg(A.pid, signal.SIGKILL); A.wait()
for _ in range(400):
    if A.pid not in held_by_pid(tok):
        break
    time.sleep(0.01)
check(A.pid not in held_by_pid(tok), "SIGKILL on A's process tree: the kernel released its tokens")
C = hold("C", 1); c = wait_holds(C.pid, 1)
check(c == {0}, "C(1) then holds %s — the killed holder's lowest token" % sorted(c))
for p in (B, C):
    os.killpg(p.pid, signal.SIGKILL); p.wait()

# ---- 4: JAH_VRAM_TOKENS=4 ------------------------------------------------------------------
print("4. JAH_VRAM_TOKENS=4")
tok = os.path.join(D, "tok4"); ev = os.path.join(D, "events4")
procs = start_rows([("four%d" % i, 2, 15) for i in range(6)], tok, 4, ev)
sampler = Sampler(tok, {p.pid: k for _, k, p in procs})
sampler.start()
for _, _, p in procs:
    p.communicate()
sampler.stop = True
sampler.join()
check(sampler.max_held <= 4 and sampler.max_index <= 3,
      "never more than 4 tokens held (max %d), never a token index above 3 (max %d)" % (sampler.max_held, sampler.max_index))
check(sum(1 for l in open(ev) if l.rstrip().endswith(" end")) == 6, "all six rows ran")

# ---- 5: the bounded wait -------------------------------------------------------------------
print("5. the bounded wait")
tok = os.path.join(D, "tok5"); ev = os.path.join(D, "events5")
full = subprocess.Popen([ADMIT, "4", "--", FAKE, "full", frames(60 * 120), ev], env=env_for(tok, 4),
                        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, start_new_session=True)
for _ in range(400):
    if len(held_by_pid(tok).get(full.pid, ())) == 4:
        break
    time.sleep(0.01)
r5 = r = subprocess.run([ADMIT, "1", "--", FAKE, "never", frames(1), ev], env=env_for(tok, 4, JAH_VRAM_WAIT=1),
                   stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
check(r.returncode == 75 and "never" not in open(ev).read(),
      "a wait past JAH_VRAM_WAIT exits 75 and never runs the command (rc %d)" % r.returncode)
os.killpg(full.pid, signal.SIGKILL); full.wait()

# ---- 6: the row is the admission's CHILD (TEST-SELECTOR-1 H4), exit codes, signals, nesting ---
print("6. the row runs as the admission's child")
tok = os.path.join(D, "tok6")
p = subprocess.Popen([ADMIT, "2", "--", "sh", "-c", 'echo $PPID; exit 7'], env=env_for(tok, 12),
                     stdout=subprocess.PIPE, text=True)
out, _ = p.communicate()
check(out.strip() == str(p.pid) and p.returncode == 7,
      "the command is the started pid's child (%s == %d) and its exit code passes through (%d)"
      % (out.strip(), p.pid, p.returncode))
p = subprocess.run([ADMIT, "1", "--", "sh", "-c", "kill -SEGV $$"], env=env_for(tok, 12),
                   stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
check(p.returncode == -signal.SIGSEGV, "a row killed by SIGSEGV reads to ctest as SIGSEGV (%d)" % p.returncode)
p = subprocess.run([ADMIT, "1", "--", "sh", "-c", "kill -KILL $$"], env=env_for(tok, 12),
                   stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
check(p.returncode == -signal.SIGKILL and "Traceback" not in p.stderr,
      "a row killed by SIGKILL reads to ctest as SIGKILL, no traceback (%d)" % p.returncode)
mark = os.path.join(D, "term6")
p = subprocess.Popen([ADMIT, "1", "--", "sh", "-c", "trap 'echo got > %s; exit 3' TERM; while :; do sleep 0.05; done" % mark],
                     env=env_for(tok, 12), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
for _ in range(400):
    if len(held_by_pid(tok).get(p.pid, ())) == 1: break
    time.sleep(0.01)
time.sleep(0.3)
p.send_signal(signal.SIGTERM); p.wait()
check(os.path.exists(mark) and p.returncode == 3, "a SIGTERM to the admission reaches the row (rc %d)" % p.returncode)
# THE KERNEL'S WORD (H4): an Xid line from the row's pid — or a GRANDCHILD's, as an app spawned by a
# harness — since the launch turns a passing row red, printed; an unreadable journal is a FINDING.
journal = os.path.join(D, "journal6")
open(journal, "w").close()
fault = os.path.join(D, "fault6.sh")
with open(fault, "w") as f:
    f.write("#!/bin/sh\n# a grandchild that logs an Xid for its own pid after 1.5 s, then the row passes\n"
            "sh -c 'sleep 1.5; echo \"$(date +%s).5 box kernel: NVRM: Xid (PCI:0000:01:00): 109, pid=$$, "
            "name=sh, Ch 0000\" >> \"$0\"' \"$1\"\nexit 0\n")
p = subprocess.run([ADMIT, "1", "--", "sh", fault, journal],
                   env=env_for(tok, 12, JAH_KERNEL_JOURNAL=journal), stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                   text=True)
check(p.returncode != 0 and "XID 109 from pid" in p.stderr and "THE GPU FAULTED" in p.stderr,
      "an Xid from the row's grandchild turns a passing row red, with the kernel's line (rc %d: %s)"
      % (p.returncode, p.stderr.strip()[-160:]))
p = subprocess.run([ADMIT, "1", "--", "true"], env=env_for(tok, 12, JAH_KERNEL_JOURNAL=os.path.join(D, "absent")),
                   stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
check(p.returncode == 0 and "FINDING: the kernel journal is unreadable" in p.stderr,
      "an unreadable journal is a FINDING line, never a red (rc %d)" % p.returncode)
r = subprocess.run([ADMIT, "1", "--", ADMIT, "1", "--", "sh", "-c", "echo nested-ran"],
                   env=env_for(tok, 1, JAH_VRAM_WAIT=2), stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
check(r.returncode == 0 and "nested-ran" in r.stdout and "already admitted by the parent" in r.stderr,
      "a nested admission runs on its parent's tokens and never waits (1 token, both levels)")

# ---- 7: the lock table is read by DEVICE AND INODE ---------------------------------------
print("7. /proc/locks identity = MAJ:MIN:INODE")
tok = os.path.join(D, "tok7")
os.makedirs(tok)
open(os.path.join(tok, "token.00"), "w").close()
st = os.stat(os.path.join(tok, "token.00"))
fake = os.path.join(D, "locks7")
other_dev = "%02x:%02x:%d" % (os.major(st.st_dev) ^ 0x7, os.minor(st.st_dev) ^ 0x5a, st.st_ino)
with open(fake, "w") as f:
    f.write("1: FLOCK  ADVISORY  WRITE 4242 %s 0 EOF\n" % other_dev)
os.environ.update({"JAH_VRAM_DIR": tok, "JAH_VRAM_TOKENS": "1", "JAH_VRAM_PROC_LOCKS": fake})
check(vram_tokens._free_indices(1) == [0],
      "a flock on ANOTHER device's file with the same inode number (%s) leaves the token free" % other_dev)
with open(fake, "w") as f:
    f.write("1: FLOCK  ADVISORY  WRITE 4242 %s 0 EOF\n" % vram_tokens.lock_key(st))
check(vram_tokens._free_indices(1) == [], "the token's own MAJ:MIN:INODE reads as held (%s)" % vram_tokens.lock_key(st))
for k in ("JAH_VRAM_DIR", "JAH_VRAM_TOKENS", "JAH_VRAM_PROC_LOCKS"):
    os.environ.pop(k, None)

# ---- 8: the cap's line names the request (the run log's NOADMIT class reads it) ------------
print("8. the NOADMIT line")
check(r5.returncode == 75 and "NOADMIT vram: no admission for 1 tokens within 1 s" in r5.stderr,
      "an expired wait prints `NOADMIT vram: no admission for <k> tokens …` (%s)" % r5.stderr.strip()[-120:])

shutil.rmtree(D, ignore_errors=True)
print("devprocess.vram_admit: %s" % ("%d failure(s)" % failures if failures else "all ok"))
sys.exit(1 if failures else 0)
