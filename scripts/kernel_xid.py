#!/usr/bin/env python3
"""kernel_xid.py — THE ONE READER OF THE KERNEL'S GPU FAULTS (TEST-SELECTOR-1 H4; GPU_LOSS_AUDIT
2026-09-27 B: a device loss is not always a red suite, and an Xid from a suite's own pid is never
environmental — DOCS/traps/GATE_AND_RIG.md).

The NVIDIA driver logs every GPU fault as `NVRM: Xid (PCI:…): <n>, pid=<pid>, name=<comm>, …` in
the kernel ring. /dev/kmsg is not readable here (dmesg_restrict=1) and a test never sudos, so the
journal is read (`journalctl -k`, readable by the adm / systemd-journal group). Its users:
  * scripts/vram_tokens.py `admit` — EVERY Vulkan row (gpu-admit.sh / gpu-exclusive.sh): it runs
    the row as its child, tracks the row's process tree, and after the row reads the Xids from any
    pid of that tree since the launch; a fault turns the row red with the kernel's lines;
  * tests/support/run_pool.py — per app process, the Xid's second mapped to the arm that ran;
  * scripts/lead/rc-gate.sh — the Xids since the gate began (never since boot).
AN UNREADABLE JOURNAL IS A FINDING in every user (a printed line), never a red of every row: the
one row that reds for it is devprocess.kernel_journal. macOS has no NVRM log (an empty answer).
JAH_KERNEL_JOURNAL=<file> reads `journalctl -o short-unix` lines from a file instead (the tests).

    kernel_xid.py since <epoch> [--pid <p> ...]     # one line per Xid; exit 1 when any, 3 unreadable
"""
import os
import re
import subprocess
import sys
import threading

XID = re.compile(r"^(\d+(?:\.\d+)?)\s.*NVRM: Xid \([^)]*\): (\d+), pid=(\d+),")
FINDING = ("FINDING: the kernel journal is unreadable (journalctl -k; the user is not in the "
           "adm/systemd-journal group): Xids cannot be read — devprocess.kernel_journal names the fix")


def journal_lines(since):
    """The kernel journal's lines since `since` (epoch seconds), `short-unix` form; None when it
    cannot be read; [] off Linux (no NVRM log)."""
    fake = os.environ.get("JAH_KERNEL_JOURNAL")
    if fake:
        try:
            return open(fake).read().splitlines()
        except OSError:
            return None
    if not sys.platform.startswith("linux"):
        return []
    try:
        r = subprocess.run(["journalctl", "-k", "--no-pager", "-q", "-o", "short-unix",
                            "--since", "@%d" % int(since)],
                           stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, timeout=30)
    except (OSError, subprocess.TimeoutExpired):
        return None
    if r.returncode != 0:
        return None
    return r.stdout.decode("utf-8", "replace").splitlines()


def kernel_xids(since, pids=None):
    """[(epoch, xid, pid, line)] for every `NVRM: Xid` line logged since `since` — from one of
    `pids` when given, else every one. None when the journal cannot be read."""
    lines = journal_lines(since)
    if lines is None:
        return None
    out = []
    for line in lines:
        m = XID.match(line)
        if m and float(m.group(1)) >= int(since) and (pids is None or int(m.group(3)) in pids):
            out.append((float(m.group(1)), int(m.group(2)), int(m.group(3)), line.strip()))
    return out


def _proc_table():
    """{pid: (ppid, pgid)} of every process now (Linux /proc); {} elsewhere."""
    table = {}
    try:
        names = os.listdir("/proc")
    except OSError:
        return table
    for n in names:
        if not n.isdigit(): continue
        try:
            with open("/proc/%s/stat" % n) as f:
                st = f.read()
        except OSError:
            continue
        rest = st[st.rfind(")") + 2:].split()
        try:
            table[int(n)] = (int(rest[1]), int(rest[2]))
        except (IndexError, ValueError):
            pass
    return table


class TreeTracker(threading.Thread):
    """Every pid of a process tree while it lives (the row, the app its harness spawned, the app a
    pool restarts): /proc polled every `period` seconds, a pid kept once seen. A GPU process lives
    seconds at least, so a one-second period misses none that can own a GPU context."""

    def __init__(self, root, period=1.0):
        super().__init__(daemon=True)
        self.root, self.period = root, period
        self.pids = {root}
        self._stop = threading.Event()

    def scan(self):
        table = _proc_table()
        grew = True
        while grew:
            grew = False
            for pid, (ppid, pgid) in table.items():
                if pid not in self.pids and (ppid in self.pids or pgid == self.root):
                    self.pids.add(pid); grew = True

    def run(self):
        while not self._stop.is_set():
            self.scan()
            self._stop.wait(self.period)

    def stop(self):
        self._stop.set()


def main(argv):
    if len(argv) < 2 or argv[0] != "since":
        sys.stderr.write(__doc__); return 64
    since = float(argv[1])
    pids = {int(p) for p in argv[argv.index("--pid") + 1:]} if "--pid" in argv else None
    xids = kernel_xids(since, pids)
    if xids is None:
        print("kernel_xid: " + FINDING); return 3
    for t, n, p, line in xids:
        print("XID %d pid %d: %s" % (n, p, line))
    return 1 if xids else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
