#!/usr/bin/env python3
"""gate_runlog — THE RUN LOG (TESTING_V2 T8, lane MODULAR-GATE-1).

Every gate run appends ONE machine-readable record per suite (and per arm, once the pools
print arm verdicts) to `<workspace>/testing/runs/<date>-<tier>-<tip>.jsonl` — written by the
gate scripts themselves, never by hand, so the testing rules can be re-read against facts
(the owner, 2026-09-27: "log the tests as they run so we can streamline again based on fact").
The field list is `testing/runs/README.md`; the one writer is `run_ctest()` below.

As a library: gate-scope.py `--run` / `--solo` call run_ctest(). As a command (the tier
scripts — rc-gate.sh — and anyone running a whole tier):

    scripts/gate_runlog.py run --tier merge --lane rc-d --jobs 4 [--build build-linux] -- ctest -j4 ...
    scripts/gate_runlog.py import <ctest-output.log> --tier merge --tip <sha> --lane <name>
    scripts/gate_runlog.py times [--days 14]          # suite -> median PASS seconds (gate-scope's estimate)
    scripts/gate_runlog.py longest [--days 7] [-n 10] # the ten longest suites/arms this week
    scripts/gate_runlog.py load-reds [--days 7]       # red in a gate, green solo at the same tip
    scripts/gate_runlog.py trend [--days 30] [--tip <sha>] [--suite S]  # steps in the target rows (non-gating)
    scripts/gate_runlog.py clocks                     # the GPU clock state now; exit 3 = left locked

`run` streams ctest's output through (the caller still sees and may redirect every line),
samples the load average every 2 s and the box (sibling ctests, the GPU's clocks) at EACH SUITE'S
START (ctest's `Start N: <name>` line), adds `--output-junit` to read each suite's own output
(`target:` lines, the pools' `ARM <name> PASS|FAIL|CRASH` lines) and exits with ctest's exit code. `import` turns an existing ctest output log into records marked
`"source": "import"` (the before-run logs of a lane that predates the log).
"""
import argparse
import datetime
import json
import os
import re
import shlex
import statistics
import subprocess
import sys
import tempfile
import threading
import time
import xml.etree.ElementTree as ET

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCHEMA = 1
# THE TIER NAMES (TESTING-DEBTS-1 T11) — the only ones a record may carry; testing/runs/README.md
# documents the same list. gate-scope.sh writes the first four (`scoped`; `scoped-fallback` = a
# scoped gate that fell back to the whole tier; `scoped-tier` = the tier by rule, a fork pin;
# `joint` = --joint) and `target` (GATE-SPEED-1: the target tests' own step, after the gating
# verdict — reported, never gating); rc-gate.sh writes JAH_GATE_TIER (merge by default: stage,
# nightly, push, smoke — the owner's smoke rc —, fork).
TIERS = ("scoped", "scoped-fallback", "scoped-tier", "joint", "target", "merge", "stage", "nightly", "push",
         "smoke", "fork")


def check_tier(tier):
    if tier not in TIERS:
        raise ValueError(f"gate_runlog: tier '{tier}' is not one of {', '.join(TIERS)} (testing/runs/README.md)")
    return tier

# `      Start  45: gi.foo` — ctest prints it as a suite starts (the box is sampled there, L1)
_START = re.compile(r"^\s*Start\s+\d+:\s+(\S+)\s*$")
# `  12/653 Test  #45: gi.foo ..........   Passed   12.34 sec`
_RESULT = re.compile(r"^\s*\d+/\d+\s+Test\s+#\d+:\s+(\S+)\s+\.*\s*(.*?)\s+([\d.]+)\s+sec\s*$")
# The pools' arm lines (SUITE-POOL-1's runner, tests/support/run_pool.py): `ARM-BEGIN <pool>.<arm>`
# when an arm starts, `ARM <pool>.<arm> PASS|FAIL <ms> [why]` when it ends; an arm that began
# and never ended is a CRASH (the runner restarts the app and goes on).
_ARM = re.compile(r"^\s*ARM\s+(\S+)\s+(PASS|FAIL|CRASH|TIMEOUT|NOADMIT|SKIP)\b(?:\s+(\d+(?:\.\d+)?)\s*(ms|s)?)?")
_ARM_BEGIN = re.compile(r"^\s*ARM-BEGIN\s+(\S+)\s*$")
_ARM_DUMP = re.compile(r"^\s*---- (\S+\.\S+): its output \(")
# A pool's boot footprint (TEST-TIER-1, run_pool.py): `MEM <pool> gpuPoolUsed=<MB> textures=<MB>
# processMiB=<MiB|?> tier=<t>`, once per process the pool started; the row records the largest.
_MEM = re.compile(r"^\s*MEM\s+\S+\s+gpuPoolUsed=(\d+)\s+textures=(\d+)\s+processMiB=(\d+|\?)\s+tier=(\S+)")
# ...and the leak probe (run_pool.py): `MEM <pool>.<arm> gpuPoolUsed=<MB> textures=<MB>` after every
# arm's baseline (recorded on the ARM's record as `mem`), and the FINDING line `LEAK <pool> +<MB> over
# <n> arms` (recorded on the pool's row as `leak`).
_ARM_MEM = re.compile(r"^\s*MEM\s+(\S+\.\S+)\s+gpuPoolUsed=(\d+)\s+textures=(\d+)\s*$")
_LEAK = re.compile(r"^\s*LEAK\s+\S+\s+\+(\d+)\s+over\s+(\d+)\s+arms")
# ...and its two per-arm findings (TESTING-DEBTS-1 T2/T3): `STEP <pool>.<arm> +<MB>` (the one-time
# step, recorded on the arm as `stepMB`) and `OVER <pool>.<arm> gpuPoolUsed=<MB> tier=<t> boot=<MB>`
# (an arm past 3x its process's boot, recorded on the arm as `over`).
_STEP = re.compile(r"^\s*STEP\s+(\S+\.\S+)\s+\+(\d+)")
_OVER = re.compile(r"^\s*OVER\s+(\S+\.\S+)\s+gpuPoolUsed=(\d+)\s+tier=(\S+)\s+boot=(\d+)")
_TARGET = re.compile(r"^\s*target:\s*(.+?)\s*$")


# THE VRAM BUDGET's cap (GATE-ADMIT-1; scripts/vram_tokens.py): a row that waited past the bound
# for its tokens exits 75 and never ran — ctest calls it "Failed", the run log calls it NOADMIT
# (the box was over-subscribed; nothing about the row's code), with the helper's own line as why.
_NOADMIT = re.compile(r"^\s*(?:\|\s*)*(NOADMIT vram: .*?)\s*$")


# THE TIMING ADMISSION'S WAIT (LOCK-WAIT-1; scripts/gpu-exclusive.sh = every VRAM token, TEST-SELECTOR-1
# G1+G2): `gpu-lock: waited <s> s` once the card is held — recorded per row as `lockWaitS` and
# subtracted from its seconds (a queue is never the row's time). A wait past its bound is the
# admission's NOADMIT (above); timeout(1)'s own line — the row's budget, counted from after the
# admission — makes a red a TIMEOUT.
_LOCKWAIT = re.compile(r"^\s*(?:\|\s*)*gpu-lock: waited ([0-9.]+) s\s*$")
_RUNTIMEOUT = re.compile(r"^\s*(?:\|\s*)*timeout: sending signal \S+ to command")


# THE ADMISSION'S TOKEN WAIT (GATE-SPEED-1, the gate-speed audit's L2): every GPU row's admission
# (scripts/vram_tokens.py) prints `vram: admitted with <k> tokens <idx> after <s> s` when it had to
# wait — a pool prints one per app process it starts. Recorded per row as `tokenWaitS` (their sum)
# and, for a row that is not a timing row (whose `gpu-lock: waited` line already carries the same
# wait), subtracted from its seconds: a queue is never the row's time, and without it nobody can
# tell a token-bound gate from a CPU-bound one when the width is raised.
_TOKENWAIT = re.compile(r"^\s*(?:\|\s*)*vram: admitted with \d+ tokens? \S* ?after ([0-9.]+) s")


def token_wait(text):
    """The summed admission waits of a row's output (seconds), or None when it never waited."""
    total, seen = 0.0, False
    for line in (text or "").splitlines():
        m = _TOKENWAIT.match(line)
        if m:
            try: total += float(m.group(1)); seen = True
            except ValueError: pass
    return round(total, 2) if seen else None


# THE FIRST FAILING ASSERTION OF A RED (GATE-SPEED-1, the audit's L3): open.responsive went red in
# 38 of 85 gates and the log could not say why. Our suites print `FAIL: <what>` (and
# `FAIL(transport): …`), gtest `[  FAILED  ] …`, QTest `FAIL!  : …`; the first such line of a red
# row's output is recorded as `failLine`.
_FAILLINE = re.compile(r"^\s*(?:\|\s*)*(FAIL(?:[:(!]).*|\[\s+FAILED\s+\].*)$")
# ...and a --script suite's: the runner's `<script>.js:<line>: Error: assert failed: <what>` (found on
# the first rc after the field landed: perf.epic_steady_state's red carried no FAIL line at all)
_SCRIPTFAIL = re.compile(r"([^/\s]+\.js:\d+: Error: .*)$")


def fail_line(text):
    script = None
    for line in (text or "").splitlines():
        m = _FAILLINE.match(line)
        if m:
            return m.group(1).strip()[:300]
        if script is None:
            m = _SCRIPTFAIL.search(line)
            if m and "[" not in line[:2]:
                script = m.group(1).strip()[:300]
    return script


def lock_wait(text):
    for line in (text or "").splitlines():
        m = _LOCKWAIT.match(line)
        if m:
            try: return float(m.group(1))
            except ValueError: return None
    return None


def noadmit_line(text):
    for line in (text or "").splitlines():
        m = _NOADMIT.match(line)
        if m: return m.group(1)[:300]
    return None


# THE BUDGET CLASSES (TESTING-DEBTS-1 T1; FORK-OOM-1's two texts, irisgl EnginePrivate.h and
# src/viewport/devicelossend.h): a red whose output carries the engine's in-frame OOM line is
# verdict OOM — a VRAM-budget finding (TESTING_GATE §4b: an unadmitted process or a row over its
# class), never the row's code; a red that carries the device-loss line is LOST (exit 3, the
# session ended — a loss is NEVER environmental: look for the Xid). LOST wins over OOM: an OOM
# line says "the device is NOT lost", so a loss after it is the later and graver fact.
_LOST = re.compile(r"(?i)\b(?:GPU|graphics) device was lost\b")
_OOM = re.compile(r"GPU out of memory")


def budget_verdict(text):
    """('LOST'|'OOM', the line) for a red's output, or (None, None)."""
    oom = None
    for line in (text or "").splitlines():
        if _LOST.search(line): return "LOST", line.strip()[:300]
        if oom is None and _OOM.search(line): oom = line.strip()[:300]
    return ("OOM", oom) if oom else (None, None)


def row_verdict(status, text, arms):
    """(verdict, status, budget line|None) of a row from ctest's status and its output: NOADMIT
    (never ran), else OOM / LOST for a red that carries the budget texts, else ctest's class."""
    v, st = verdict_of(status), status.strip("* ")
    na = noadmit_line(text) if v == "FAIL" else None
    if na and not arms:
        return "NOADMIT", na, None
    if v == "FAIL":
        if any(_RUNTIMEOUT.match(l) for l in (text or "").splitlines()):
            v = "TIMEOUT"
    if v in ("FAIL", "CRASH", "TIMEOUT"):
        bv, bline = budget_verdict(text)
        if bv: return bv, st, bline
    return v, st, None


def verdict_of(status):
    s = status.lower()
    if "passed" in s: return "PASS"
    if "timeout" in s: return "TIMEOUT"
    if "not run" in s or "disabled" in s: return "NOTRUN"
    if "exception" in s or "segfault" in s or "abort" in s or "signal" in s: return "CRASH"
    return "FAIL"


def workspace_root():
    """The workspace that holds the docs repo: the parent of the Studio repo's main tree
    (`git rev-parse --git-common-dir` is <workspace>/jahshaka/.git for the main tree and every
    worktree, on the Linux box and on the Mac)."""
    try:
        cd = subprocess.run(["git", "rev-parse", "--path-format=absolute", "--git-common-dir"],
                            cwd=ROOT, capture_output=True, text=True).stdout.strip()
        if cd:
            return os.path.dirname(os.path.dirname(cd))
    except OSError:
        pass
    return os.path.dirname(ROOT)


def log_dir():
    return os.environ.get("JAH_RUN_LOG_DIR") or os.path.join(workspace_root(), "testing", "runs")


# THE CONTENTION CLASS IS DATA (TEST-SELECTOR-1 L2; audit §8: it was prose in three docs the tools
# could not read): <workspace>/testing/contention.json, {"suites": {<suite or pool.arm>: <verdict>}}.
# ci_gate_check's flake law (3/3 solo after a red) and `gate-scope.sh --solo` read it; a suite joins
# it by a recorded verdict, never by convenience.
def contention_file():
    return os.environ.get("JAH_CONTENTION_FILE") or os.path.join(workspace_root(), "testing", "contention.json")


def contention_list():
    """{suite or arm: why}, or None when the file cannot be read."""
    try:
        d = json.load(open(contention_file()))
    except (OSError, ValueError):
        return None
    s = d.get("suites") if isinstance(d, dict) else None
    return dict(s) if isinstance(s, dict) else None


def _git(args, cwd=None):
    cwd = cwd or ROOT
    try:
        r = subprocess.run(["git"] + args, cwd=cwd, capture_output=True, text=True)
        return r.stdout.strip() if r.returncode == 0 else ""
    except OSError:
        return ""


def tree_shas():
    studio = _git(["rev-parse", "HEAD"])
    irisgl = _git(["rev-parse", "HEAD"], cwd=os.path.join(ROOT, "irisgl"))
    fork = _git(["rev-parse", "HEAD"], cwd=os.path.join(ROOT, "irisgl", "thirdparty", "ogre-next"))
    # THE TREE THAT RAN: Studio's own files AND irisgl's (an uncommitted engine edit gates green
    # and is never committed otherwise — the second Fable read, F1). irisgl's vendored submodules'
    # CONTENT is ignored: assimp's applied patch stack is configure-time state, not dirt.
    s_dirty = bool(_git(["status", "--porcelain", "--untracked-files=no", "--ignore-submodules=dirty"]))
    i_dirty = bool(_git(["status", "--porcelain", "--untracked-files=no", "--ignore-submodules=dirty"],
                        cwd=os.path.join(ROOT, "irisgl")))
    return {"studio": studio, "irisgl": irisgl, "fork": fork, "studio_dirty": s_dirty or i_dirty,
            "irisgl_dirty": i_dirty}


def fork_pin_problem(root=None):
    """THE BUILT FORK MUST BE THE PIN (TESTING-DEBTS-1 T12). None when the ogre-next checkout AND
    the install (`<install>/BUILT_FROM`, written by irisgl/scripts/build-ogre.sh; the install is
    OGRE_PREFIX when set, as build-ogre.sh reads it) are both at the commit irisgl pins; else the
    refusal text with the exact lines that fix it. A tree with no irisgl submodule (a bare
    checkout) is not judged. REFLECT-MOVERS-1, 2026-09-28: a worktree whose install was built
    from an older fork commit than the pin ran a 124-minute gate — its PBS media failed to
    compile ("atmoNprSkyRadiance: no matching overloaded function"): 76 reds, 3 Xids, void."""
    root = root or ROOT
    ig = os.path.join(root, "irisgl")
    pin = _git(["rev-parse", "HEAD:thirdparty/ogre-next"], cwd=ig) if os.path.isdir(ig) else ""
    if not pin:
        return None
    src = os.path.join(ig, "thirdparty", "ogre-next")
    install = os.environ.get("OGRE_PREFIX") or os.path.join(ig, "thirdparty", "ogre-next-install")
    checkout = _git(["rev-parse", "HEAD"], cwd=src) if os.path.isdir(src) else ""
    try:
        rec = open(os.path.join(install, "BUILT_FROM")).read().split()
    except OSError:
        rec = []
    built, dirty = (rec[0] if rec else ""), ("dirty" in rec[1:])
    why = []
    if checkout != pin:
        why.append(f"the ogre-next checkout is at {checkout[:9] or '(none)'}")
    if not built:
        why.append(f"the install ({install}) has no BUILT_FROM record (built before build-ogre.sh wrote one)")
    elif built != pin:
        why.append(f"the install was built from {built[:9]}")
    elif dirty:
        why.append("the install was built from a DIRTY checkout of the pin")
    if not why:
        return None
    have = os.path.isdir(src) and subprocess.run(["git", "cat-file", "-e", pin + "^{commit}"], cwd=src,
                                                 capture_output=True).returncode == 0
    fix = [f"cd {root}"]
    if not have:
        fix.append("git -C irisgl/thirdparty/ogre-next fetch origin")
    fix += ["git -C irisgl submodule update --init thirdparty/ogre-next", "./irisgl/scripts/build-ogre.sh"]
    return ("REFUSING TO RUN: the built fork is not the pin — irisgl pins ogre-next " + pin[:9] + ", but "
            + "; ".join(why) + ".\nA gate on it tests an engine the tree does not describe (stale media, "
            "void reds). Fix, then re-run:\n  " + "\n  ".join(fix))


def gpu_clocks():
    """The GPU's clock state at the run: nvidia-smi exposes no 'locked' flag for
    --lock-gpu-clocks, so it is INFERRED from its signature — an idle GPU that does not clock
    down (measured on this box: 645 MHz idle free, 2550 MHz idle under `-lgc 2100,2550`).
    'locked?' = idle at >= 1500 MHz, 'free' = idle below it, 'busy' = not idle (cannot tell),
    'unknown' without nvidia-smi (the Mac, lavapipe CI). The raw numbers ride along."""
    try:
        r = subprocess.run(["nvidia-smi", "--query-gpu=clocks.gr,clocks_event_reasons.gpu_idle,pstate",
                            "--format=csv,noheader,nounits"], capture_output=True, text=True, timeout=10)
        if r.returncode != 0: return {"state": "unknown"}
        gr, idle, pstate = [p.strip() for p in r.stdout.strip().splitlines()[0].split(",")]
        mhz = int(float(gr))
        idle = idle.lower().startswith("active")
        state = ("locked?" if mhz >= 1500 else "free") if idle else "busy"
        return {"state": state, "graphics_mhz": mhz, "idle": idle, "pstate": pstate}
    except (OSError, subprocess.TimeoutExpired, ValueError, IndexError):
        return {"state": "unknown"}


def _ppid(pid):
    """The parent pid from /proc/<pid>/stat (the field after the parenthesised name), or None."""
    try:
        with open(f"/proc/{pid}/stat") as f:
            st = f.read()
        return int(st[st.rindex(")") + 2:].split()[1])
    except (OSError, ValueError, IndexError):
        return None


def _descends_from(pid, roots):
    seen = 0
    while pid and pid > 1 and seen < 64:
        if pid in roots: return True
        pid, seen = _ppid(pid), seen + 1
    return False


def other_ctests(own_root=None):
    """ctest processes on the box that are not ours (the contention context of a run).

    OURS = this process and EVERY DESCENDANT of it, plus `own_root`'s tree (the shell run_ctest
    spawned, and the ctest under it). Until GATE-SPEED-1 only os.getpid() — the python runner —
    was excluded, so the gate's own ctest was counted: every row of rc-smoke15a read 1 with no
    sibling gate live, the quiet medians (`other_ctests == 0`) stopped receiving records, and the
    §7b sibling counts were inflated (the gate-speed audit's L1)."""
    try:
        r = subprocess.run(["pgrep", "-x", "ctest"], capture_output=True, text=True)
        roots = {os.getpid()} | ({own_root} if own_root else set())
        return len([p for p in r.stdout.split() if not _descends_from(int(p), roots)])
    except OSError:
        return None


class LoadSampler(threading.Thread):
    def __init__(self, period=2.0):
        super().__init__(daemon=True)
        self.period, self.samples, self._stop = period, [], threading.Event()

    def run(self):
        while not self._stop.is_set():
            self.samples.append((time.time(), os.getloadavg()[0]))
            self._stop.wait(self.period)

    def stop(self):
        self._stop.set()

    def mean(self, t0, t1):
        v = [l for t, l in self.samples if t0 - self.period <= t <= t1 + self.period]
        return round(sum(v) / len(v), 2) if v else None


def _junit_outputs(path):
    out = {}
    try:
        root = ET.parse(path).getroot()
    except (OSError, ET.ParseError):
        return out
    for tc in root.iter("testcase"):
        so = tc.find("system-out")
        out[tc.get("name")] = so.text if so is not None and so.text else ""
    return out


def _mem_of(text):
    """A pool row's `mem` field: the largest boot footprint over its processes, or None."""
    mem = None
    for line in (text or "").splitlines():
        m = _MEM.match(line)
        if not m: continue
        pool_mb, tex_mb = int(m.group(1)), int(m.group(2))
        proc = None if m.group(3) == "?" else int(m.group(3))
        if mem is None:
            mem = {"gpuPoolUsedMB": pool_mb, "texturesMB": tex_mb, "processMiB": proc,
                   "tier": m.group(4), "boots": 1}
            continue
        mem["boots"] += 1
        mem["gpuPoolUsedMB"] = max(mem["gpuPoolUsedMB"], pool_mb)
        mem["texturesMB"] = max(mem["texturesMB"], tex_mb)
        if proc is not None:
            mem["processMiB"] = max(mem["processMiB"] or 0, proc)
    return mem


def _arm_mems(text):
    """arm -> {gpuPoolUsedMB, texturesMB}: the leak probe's point after that arm (the last, if a
    restart printed it twice)."""
    out = {}
    for line in (text or "").splitlines():
        m = _ARM_MEM.match(line)
        if m:
            out[m.group(1)] = {"gpuPoolUsedMB": int(m.group(2)), "texturesMB": int(m.group(3))}
    return out


def _arm_findings(text):
    """arm -> {stepMB?, over?}: the leak probe's per-arm findings (the last of each, per arm)."""
    out = {}
    for line in (text or "").splitlines():
        m = _STEP.match(line)
        if m: out.setdefault(m.group(1), {})["stepMB"] = int(m.group(2)); continue
        m = _OVER.match(line)
        if m:
            out.setdefault(m.group(1), {})["over"] = {"gpuPoolUsedMB": int(m.group(2)), "tier": m.group(3),
                                                      "bootMB": int(m.group(4))}
    return out


def _leaks_of(text):
    """A pool row's `leak` field: every LEAK finding line as {riseMB, arms}, or None."""
    found = [{"riseMB": int(m.group(1)), "arms": int(m.group(2))}
             for m in (_LEAK.match(l) for l in (text or "").splitlines()) if m]
    return found or None


def _suite_facts(text):
    target, arms, begun = None, [], []
    # arm -> its own output lines: from its ARM-BEGIN, and from the runner's dump of a red arm's
    # output (`---- <pool>.<arm>: its output …`, printed when the process ended), to its ARM line
    seg, cur = {}, None
    for line in (text or "").splitlines():
        m = _ARM_BEGIN.match(line)
        if m: begun.append(m.group(1)); cur = m.group(1); seg.setdefault(cur, []); continue
        m = _ARM_DUMP.match(line)
        if m: cur = m.group(1); seg.setdefault(cur, []); continue
        if cur is not None: seg[cur].append(line)
        m = _TARGET.match(line)
        if m and target is None: target = m.group(1)[:200]
        m = _ARM.match(line)
        if m:
            secs = None
            if m.group(3):
                secs = float(m.group(3)) if m.group(4) == "s" else float(m.group(3)) / 1000.0
            arms.append((m.group(1), m.group(2), secs))
            if m.group(1) == cur: cur = None
    ended = {a for a, _, _ in arms}
    arms += [(a, "CRASH", None) for a in dict.fromkeys(begun) if a not in ended]
    # a red arm whose own lines carry the budget texts takes the budget class (T1)
    out = []
    for a, v, secs in arms:
        if v in ("FAIL", "CRASH", "TIMEOUT"):
            bv, _ = budget_verdict("\n".join(seg.get(a, [])))
            v = bv or v
        out.append((a, v, secs))
    return target, out


def _file_for(tier, tip, date=None):
    d = date or datetime.date.today().isoformat()
    return os.path.join(log_dir(), f"{d}-{tier}-{(tip or 'notip')[:9]}.jsonl")


def _prior_counts(path):
    counts = {}
    try:
        with open(path) as f:
            for line in f:
                try: r = json.loads(line)
                except ValueError: continue
                k = (r.get("suite"), r.get("arm"))
                counts[k] = counts.get(k, 0) + 1
    except OSError:
        pass
    return counts


def append_records(records, tier, tip):
    path = _file_for(check_tier(tier), tip)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    prior = _prior_counts(path)
    with open(path, "a") as f:
        for r in records:
            k = (r["suite"], r.get("arm"))
            r["retries"] = prior.get(k, 0)
            prior[k] = r["retries"] + 1
            f.write(json.dumps(r, sort_keys=True) + "\n")
    return path


def run_ctest(cmd, cwd, tier, lane, jobs, reasons=None, gating=None, rng=None, retry=False,
              labels=None, echo=True, env=None):
    """Run a ctest command line (a string, as gate-scope prints it), stream its output, and
    append one record per suite (+ per arm) to the run log. Returns ctest's exit code."""
    check_tier(tier)       # before the run, never after an hour of it
    reasons = reasons or {}
    junit = tempfile.NamedTemporaryFile(prefix="gate-junit-", suffix=".xml", delete=False).name
    full = (f"{cmd} --output-junit {junit} "
            f"--test-output-size-passed 262144 --test-output-size-failed 262144")
    shas = tree_shas()
    box0 = {"jobs": jobs, "display": os.environ.get("DISPLAY"), "gpu_clocks": gpu_clocks(),
            "other_ctests": other_ctests(), "host": os.uname().nodename}
    sampler = LoadSampler(); sampler.start()
    run_id = f"{datetime.datetime.now().strftime('%Y%m%dT%H%M%S')}-{shas['studio'][:9]}"
    seen, starts = [], {}
    p = subprocess.Popen(full, cwd=cwd, shell=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                         text=True, bufsize=1, errors="replace", env=env)
    for line in p.stdout:
        if echo:
            sys.stdout.write(line); sys.stdout.flush()
        m = _START.match(line.rstrip("\n"))
        if m:
            starts[m.group(1)] = {"other_ctests": other_ctests(p.pid), "gpu_clocks": gpu_clocks()}
            continue
        m = _RESULT.match(line.rstrip("\n"))
        if m:
            seen.append((m.group(1), m.group(2), float(m.group(3)), time.time(), os.getloadavg()))
    rc = p.wait()
    sampler.stop()
    outputs = _junit_outputs(junit)
    try: os.unlink(junit)
    except OSError: pass
    recs = []
    for name, status, secs, t_end, load in seen:
        target, arms = _suite_facts(outputs.get(name, ""))
        # THE BOX AT THIS SUITE'S START (TEST-SELECTOR-1 L1): the gate's first second said nothing
        # about a suite that started an hour later beside two other gates
        at = starts.get(name) or {}
        base = {"schema": SCHEMA, "run": run_id, "ts": datetime.datetime.fromtimestamp(t_end).astimezone().isoformat(timespec="seconds"),
                "suite": name, "tier": tier, "lane": lane, "range": rng, "tip": shas,
                "reason": reasons.get(name, tier), "gating": (gating(name) if gating else True),
                "retry": retry, "labels": sorted((labels or {}).get(name, [])),
                "box": dict(box0, gpu_clocks=at.get("gpu_clocks", box0["gpu_clocks"]),
                            other_ctests=at.get("other_ctests", box0["other_ctests"]),
                            load=[round(x, 2) for x in load],
                            load_mean=sampler.mean(t_end - secs, t_end)),
                "source": "run"}
        v, st, bline = row_verdict(status, outputs.get(name, ""), arms)
        wait = lock_wait(outputs.get(name, ""))
        twait = token_wait(outputs.get(name, ""))
        # a timing row's lock line and its admission line are the SAME wait: subtract it once
        queued = wait if wait is not None else twait
        row = dict(base, arm=None, verdict=v, status=st,
                   seconds=(round(max(0.0, secs - queued), 2) if queued is not None else secs),
                   target=target)
        if wait is not None:
            row["lockWaitS"] = wait
        if twait is not None:
            row["tokenWaitS"] = twait
        if queued is not None:
            row["wallSeconds"] = secs
        if v != "PASS":
            fl = fail_line(outputs.get(name, ""))
            if fl:
                row["failLine"] = fl
        if bline:
            row["budget"] = bline
        mem = _mem_of(outputs.get(name, ""))
        if mem is not None:
            row["mem"] = mem
        leak = _leaks_of(outputs.get(name, ""))
        if leak is not None:
            row["leak"] = leak
        recs.append(row)
        arm_mem = _arm_mems(outputs.get(name, ""))
        arm_find = _arm_findings(outputs.get(name, ""))
        for arm, v, s in arms:
            # an arm's reason: the selector's for that arm (`<row>::<arm>`), else its row's
            ar = reasons.get(f"{name}::{arm.split('.', 1)[-1]}", base["reason"])
            rec = dict(base, arm=arm, verdict=v, status=v, seconds=s, target=None,
                       reason=ar)
            if arm in arm_mem:
                rec["mem"] = arm_mem[arm]
            rec.update(arm_find.get(arm, {}))
            recs.append(rec)
    if recs:
        path = append_records(recs, tier, shas["studio"])
        if echo:
            print(f"\nrun log: {len(recs)} record(s) -> {path}")
    return rc


def inventory_labels(build):
    """suite -> labels (listed from a copy of the CTestTestfile tree: gate_graph.ctest_inventory)."""
    try:
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        import gate_graph
        raw, _ = gate_graph.ctest_inventory(build)
        out = {}
        for t in json.loads(raw or "{}").get("tests", []):
            props = {p["name"]: p["value"] for p in t.get("properties", [])}
            out[t["name"]] = props.get("LABELS", []) or []
        return out
    except (OSError, ValueError):
        return {}


def import_log(path, tier, tip, lane, jobs=None, reason=None):
    recs = []
    for line in open(path, errors="replace"):
        m = _RESULT.match(line.rstrip("\n"))
        if not m: continue
        recs.append({"schema": SCHEMA, "run": f"import-{os.path.basename(path)}", "ts": None,
                     "suite": m.group(1), "arm": None, "tier": tier, "lane": lane, "range": None,
                     "tip": {"studio": tip}, "reason": reason or f"imported from {path}",
                     "gating": True, "retry": False, "labels": [],
                     "verdict": verdict_of(m.group(2)), "status": m.group(2).strip("* "),
                     "seconds": float(m.group(3)), "target": None,
                     "box": {"jobs": jobs}, "source": "import"})
    if recs:
        p = append_records(recs, tier, tip)
        print(f"imported {len(recs)} record(s) -> {p}")
    else:
        print("no ctest result lines in", path)


QUIET_MIN = 3


def median_times(days=14, verdicts=("PASS",), sources=None):
    """suite -> median wall seconds over the run log's last `days` days (PASS rows by default:
    a red's time is its failure's, not the suite's). THE QUIET MEDIAN FIRST (TESTING-DEBTS-1 T8:
    medians taken under sibling gates ran ~26 % high): a suite with >= QUIET_MIN records whose
    `box.other_ctests == 0` is costed from those alone, else from every record. `sources`, a
    dict, receives suite -> "quiet" | "all"."""
    d = log_dir()
    cutoff = (datetime.date.today() - datetime.timedelta(days=days)).isoformat()
    acc = {}
    if not os.path.isdir(d): return {}
    for f in sorted(os.listdir(d)):
        if not f.endswith(".jsonl") or f[:10] < cutoff: continue
        for line in open(os.path.join(d, f)):
            try: r = json.loads(line)
            except ValueError: continue
            if r.get("verdict") not in verdicts or r.get("seconds") is None: continue
            # an arm is costed as `<row>::<arm>` (gate-scope's key for a partial pool)
            k = r["suite"] if not r.get("arm") else f"{r['suite']}::{r['arm'].split('.', 1)[-1]}"
            quiet = (r.get("box") or {}).get("other_ctests") == 0
            acc.setdefault(k, []).append((r["seconds"], quiet))
    out = {}
    for k, v in acc.items():
        q = [x for x, quiet in v if quiet]
        use = q if len(q) >= QUIET_MIN else [x for x, _ in v]
        out[k] = statistics.median(use)
        if sources is not None: sources[k] = "quiet" if use is q else "all"
    return out


def _records(days):
    d = log_dir()
    cutoff = (datetime.date.today() - datetime.timedelta(days=days)).isoformat()
    if not os.path.isdir(d): return
    for f in sorted(os.listdir(d)):
        if not f.endswith(".jsonl") or f[:10] < cutoff: continue
        for line in open(os.path.join(d, f)):
            try: r = json.loads(line)
            except ValueError: continue
            r["_file"] = f
            yield r


def query_longest(days=7, n=10):
    """The n longest suites/arms by median wall seconds (PASS runs) over the last `days` days."""
    acc = {}
    for r in _records(days):
        if r.get("verdict") != "PASS" or r.get("seconds") is None: continue
        k = r["suite"] if not r.get("arm") else f"{r['suite']} :: {r['arm']}"
        acc.setdefault(k, []).append(r["seconds"])
    rows = sorted(((statistics.median(v), len(v), k) for k, v in acc.items()), reverse=True)[:n]
    for med, cnt, k in rows: print(f"{med:8.1f} s  x{cnt:<3d} {k}")


def query_load_reds(days=7):
    """Suites that went red in a gate and green on a solo retry at the same tip: the contention
    class, with the load and the sibling gates each red ran beside."""
    by_file = {}
    for r in _records(days):
        by_file.setdefault(r["_file"], []).append(r)
    for f, recs in sorted(by_file.items()):
        for r in recs:
            if r.get("arm") or r.get("retry") or r.get("verdict") == "PASS": continue
            solo = [x for x in recs if x["suite"] == r["suite"] and x.get("retry") and not x.get("arm")]
            if solo and all(x["verdict"] == "PASS" for x in solo):
                b = r.get("box", {})
                print(f"{f[:10]} {r['suite']:45s} {r['verdict']:7s} load {b.get('load_mean')} "
                      f"(siblings {b.get('other_ctests')}, -j{b.get('jobs')})  solo {len(solo)}/{len(solo)} PASS")


# THE TREND CHECK (TEST-1 item 1; the perf audit's D2). The run log has carried every `target:` line since
# 2026-09-27 and nothing compared one merge's number with the last; gi.rt_reflect_cost went 0.39 -> 1.03 ms
# and sat four days unread (D1). `trend` reads each target row as a SERIES over tips and names the first tip
# of every STEP and the lane that brought it. It is a REPORT, never a verdict: a step gets the lead's
# verdict, as a red does (a known trade like BAKE-WIDTH-2's bake cost is verdicted once).
#
# The rule, chosen so the comparison is honest:
#   - ONE VALUE PER TIP: the median of the row's readings at that tip under one CONDITION;
#   - a CONDITION is (the clock, the GPU's sharing). The clock: `locked` (gpu_clocks.state "locked?")
#     or `unlocked` — "free" and "busy" are both an unlocked card ("busy" is the sampler's "cannot tell
#     idle", not a third clock), and splitting on it cut D1's series in pieces too short to judge. The
#     sharing: `solo` (the row ran -j1 with no sibling ctest on the box — the timing phase, the target
#     step) or `shared` (anything else). A shared reading carries other suites' GPU and CPU time in it
#     (rt_reflect_cost read 4.9 ms beside three siblings and 1.03 alone at the same tip), so the two are
#     never compared with each other;
#   - a reading is OUT OF BAND when it sits outside k x MAD (scaled to a standard deviation, 1.4826) of
#     the trailing TREND_WINDOW tips' values in the same condition; the MAD has a floor of 1 % of the
#     median, so a row whose window is flat (a count, an exact 0) still flags any change;
#   - a STEP needs confirmation: the out-of-band reading AND the next TREND_CONFIRM - 1 readings that
#     descend from its tip must ALL sit outside the band on the same side. A shared reading's noise is
#     one-sided (contention only adds time: rt_reflect_cost read 0.6 and 2.5 at one tip) and about one
#     shared reading in three is such a spike, so a median of three still "confirmed" two spikes as a
#     step; all three out of band does not. A reading too recent to be confirmed prints as UNCONFIRMED (the day a step lands it is one
#     reading: the gate that brought it prints it, and the next gates confirm or clear it);
#   - after a confirmed step the baseline restarts at the step's first tip (the old level is history).
TREND_K = 3.5
TREND_WINDOW = 10
TREND_MIN = 3
TREND_CONFIRM = 3
_NUM = r"[-+]?\d+(?:\.\d+)?(?:[eE][-+]?\d+)?"


def target_value(text):
    """The number a `target:` line reports. The standard shape is `<value> (bar <bar>) <what>`, so a
    line that STARTS with a number (or an `a/b` ratio) is that number; otherwise the last number before
    the word `bar` (`[march: glossy floor 0.2, …] grain 9.52x the still case's (bar 1.5)` -> 9.52).
    None when there is neither (a line with no number is not a series)."""
    t = (text or "").strip()
    m = re.match(rf"({_NUM})\s*/\s*({_NUM})\b", t)
    if m and float(m.group(2)) != 0:
        return float(m.group(1)) / float(m.group(2))
    m = re.match(rf"({_NUM})(?![\w.])", t)
    if m:
        return float(m.group(1))
    i = t.find("bar")
    if i > 0:
        nums = re.findall(rf"(?<![\w.]){_NUM}", t[:i])
        if nums:
            return float(nums[-1])
    return None


def _condition(r):
    b = r.get("box") or {}
    clock = "locked" if (b.get("gpu_clocks") or {}).get("state") == "locked?" else "unlocked"
    share = "solo" if (b.get("jobs") == 1 and b.get("other_ctests") == 0) else "shared"
    return f"{clock}/{share}"


def _band(vals):
    med = statistics.median(vals)
    mad = statistics.median([abs(v - med) for v in vals]) * 1.4826
    return med, max(mad, 0.01 * abs(med), 1e-9)


def trend_series(days=30, suites=None):
    """(suite, condition) -> [(first ts, tip sha, lane, value, n readings)], tips in time order."""
    acc = {}
    for r in _records(days):
        if r.get("arm") or not r.get("target"):
            continue
        if suites and r["suite"] not in suites:
            continue
        v = target_value(r["target"])
        if v is None:
            continue
        tip = (r.get("tip") or {}).get("studio") or "?"
        key = (r["suite"], _condition(r))
        e = acc.setdefault(key, {}).setdefault(tip, {"ts": r.get("ts") or "", "lane": r.get("lane") or "?", "v": []})
        e["ts"] = min(e["ts"], r.get("ts") or e["ts"])
        e["v"].append(v)
    out = {}
    for key, tips in acc.items():
        rows = sorted(((e["ts"], t, e["lane"], statistics.median(e["v"]), len(e["v"])) for t, e in tips.items()))
        out[key] = rows
    return out


def tip_ancestry(tips, days):
    """tip -> the set of OTHER tips it descends from (the commit graph, `git rev-list --parents`).
    None when git cannot answer (a tip the object store does not hold): the caller then orders by
    time alone and says so."""
    tips = sorted(t for t in tips if re.fullmatch(r"[0-9a-f]{40}", t or ""))
    if not tips:
        return None
    r = subprocess.run(["git", "cat-file", "--batch-check"], input="\n".join(tips) + "\n",
                       cwd=ROOT, capture_output=True, text=True)
    if r.returncode != 0 or "missing" in r.stdout:
        return None
    r = subprocess.run(["git", "rev-list", "--parents", f"--since={days + 30}.days"] + tips,
                       cwd=ROOT, capture_output=True, text=True)
    if r.returncode != 0:
        return None
    parents = {}
    for line in r.stdout.splitlines():
        c, *ps = line.split()
        parents[c] = ps
    tipset, memo = set(tips), {}

    def reach(c):                      # the tips reachable from c, c excluded
        if c in memo: return memo[c]
        out, stack, seen = set(), list(parents.get(c, [])), set()
        while stack:
            x = stack.pop()
            if x in seen: continue
            seen.add(x)
            if x in tipset:
                out.add(x)
                if x in memo:          # a tip already walked: take its answer, stop here
                    out |= memo[x]; continue
            stack.extend(parents.get(x, []))
        memo[c] = out
        return out
    for t in sorted(tips, key=lambda t: len(parents.get(t, []))):
        reach(t)
    return {t: reach(t) for t in tips}


def trend_steps(rows, anc=None, k=TREND_K, window=TREND_WINDOW, min_n=TREND_MIN, confirm=TREND_CONFIRM):
    """The steps in one series: dicts {i, kind: STEP | UNCONFIRMED, before, after, band, n}; `i` indexes
    `rows` (time order), the step's first tip.

    THE BASELINE IS THE TIP'S OWN HISTORY: with the commit graph (`anc`), a tip is compared with the
    trailing tips it DESCENDS from, and confirmed by the tips that descend from IT — lanes run in
    parallel, so time order alone compares a lane's tip with a sibling lane's and reports their
    difference as a step and back again. After a confirmed step at s, a descendant of s takes its
    baseline only from s and s's descendants (the old level is history). Without the graph every earlier
    tip is an ancestor (time order)."""
    tips = [x[1] for x in rows]
    if anc is None:
        anc = {t: set(tips[:j]) for j, t in enumerate(tips)}
    steps, confirmed = [], []          # confirmed: the tips that start a confirmed step
    for i, (ts, t, lane, v, cnt) in enumerate(rows):
        a = anc.get(t, set())
        floor = [s for s in confirmed if s in a]
        hist = [x for x in rows[:i] if x[1] in a]
        if floor:
            # the latest confirmed step in this tip's history restarts the baseline
            last = max(floor, key=lambda s: tips.index(s))
            hist = [x for x in hist if x[1] == last or last in anc.get(x[1], set())]
        base = [x[3] for x in hist[-window:]]
        if len(base) < min_n:
            continue
        med, sd = _band(base)
        # the last tip of this one's history that read inside the band: the step lies between it and t
        good = next((x[1] for x in reversed(hist) if abs(x[3] - med) <= k * sd), None)
        if abs(v - med) <= k * sd:
            continue
        side = 1 if v > med else -1
        later = [x[3] for x in rows[i + 1:] if t in anc.get(x[1], set())]
        ahead = [v] + later[:confirm - 1]
        after = statistics.median(ahead)
        if len(ahead) < confirm:
            steps.append({"i": i, "kind": "UNCONFIRMED", "before": med, "after": after,
                          "band": k * sd, "n": len(ahead), "good": good, "readings": ahead})
        elif all((x - med) * side > k * sd for x in ahead):
            steps.append({"i": i, "kind": "STEP", "before": med, "after": after, "band": k * sd,
                          "n": len(ahead), "good": good, "readings": ahead})
            confirmed.append(t)
    # an UNCONFIRMED reading inside a confirmed step's run is that step's, not a second report; and
    # sibling tips that left the SAME last in-band tip on the same side are ONE step (two lanes branched
    # from the d-build commit that brought it): the earliest is named, the rest counted
    out, by_good = [], {}
    for s in steps:
        t = tips[s["i"]]
        if s["kind"] == "UNCONFIRMED" and any(c in anc.get(t, set()) for c in confirmed):
            continue
        key = (s["good"], s["after"] > s["before"])
        if s["good"] is not None and key in by_good:
            by_good[key]["siblings"].append(s["i"])
            if s["kind"] == "STEP": by_good[key]["kind"] = "STEP"
            continue
        s["siblings"] = []
        by_good[key] = s
        out.append(s)
    return out


def query_trend(days=30, suites=None, tip=None, k=TREND_K, quiet_ok=False):
    """Prints every step (or, with `tip`, the steps whose first tip is `tip` plus where each of `suites`
    stands at it). Returns the number of steps printed."""
    series = trend_series(days, suites)
    anc = tip_ancestry({x[1] for rows in series.values() for x in rows}, days)
    n = 0
    lines = []
    for (suite, cond), rows in sorted(series.items()):
        for s in trend_steps(rows, anc, k=k):
            ts, t, lane, v, cnt = rows[s["i"]]
            if tip and not any(rows[j][1].startswith(tip[:9]) for j in [s["i"]] + (s.get("siblings") or [])):
                continue
            rel = (s["after"] / s["before"] - 1.0) * 100 if s["before"] else float("inf")
            good = f", last in band {s['good'][:9]}" if s.get("good") else ""
            sib = "".join(f"; sibling {rows[j][1][:9]} ({rows[j][2]})" for j in s.get("siblings") or [])
            lines.append(f"  {s['kind']:11s} {suite:34s} {s['before']:.4g} -> {s['after']:.4g} ({rel:+.0f} %, "
                         f"band +-{s['band']:.3g}; read {', '.join(f'{x:.4g}' for x in s['readings'])}) "
                         f"first at {t[:9]} ({lane}, {ts[:16]}{good}{sib}) [{cond}]")
            n += 1
    if lines or not quiet_ok:
        print(f"TREND (target rows, last {days} d; k={k} x MAD of the trailing {TREND_WINDOW} tips in one "
              f"condition{'' if anc is not None else '; NO COMMIT GRAPH: time order'}; NON-GATING: a step "
              f"gets the lead's verdict):")
        print("\n".join(lines) if lines else "  no step")
    return n


def trend_at_gate_end(tip=None):
    """THE GATE'S TREND LINE (TEST-1): every gate prints, after its verdict, the target rows whose
    reading at THIS tip left its history's band — the day a step lands it is one UNCONFIRMED reading.
    A report: it never changes an exit code and never raises."""
    try:
        query_trend(30, None, tip or tree_shas()["studio"])
    except Exception as e:             # a report must never break the gate it reports on
        print(f"TREND: not computed ({type(e).__name__}: {e})")


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    r = sub.add_parser("run", help="run a ctest command and log every suite")
    r.add_argument("--tier", required=True, choices=TIERS)
    r.add_argument("--lane", default=None)
    r.add_argument("--jobs", type=int, default=None)
    r.add_argument("--build", default="build-linux")
    r.add_argument("ctest", nargs=argparse.REMAINDER)
    i = sub.add_parser("import", help="records from an existing ctest output log")
    i.add_argument("log"); i.add_argument("--tier", required=True, choices=TIERS); i.add_argument("--tip", required=True)
    i.add_argument("--lane", default=None); i.add_argument("--jobs", type=int, default=None)
    t = sub.add_parser("times", help="median PASS seconds per suite from the log")
    t.add_argument("--days", type=int, default=14)
    q = sub.add_parser("longest", help="the longest suites/arms by median PASS seconds")
    q.add_argument("--days", type=int, default=7); q.add_argument("-n", type=int, default=10)
    q2 = sub.add_parser("load-reds", help="red in a gate, green solo at the same tip")
    q2.add_argument("--days", type=int, default=7)
    q4 = sub.add_parser("clocks", help="the GPU's clock state now (exit 3 when it reads locked)")
    q3 = sub.add_parser("trend", help="steps in the target rows' readings across tips (non-gating)")
    q3.add_argument("--days", type=int, default=30); q3.add_argument("--k", type=float, default=TREND_K)
    q3.add_argument("--tip", default=None, help="only the steps whose first tip is this sha")
    q3.add_argument("--suite", action="append", default=None)
    a = ap.parse_args()
    if a.cmd == "run":
        cmd = a.ctest[1:] if a.ctest and a.ctest[0] == "--" else a.ctest
        if not cmd: ap.error("give the ctest command after --")
        bad = fork_pin_problem()
        if bad:
            sys.stderr.write(bad + "\n"); sys.exit(4)
        build = a.build if os.path.isabs(a.build) else os.path.join(os.getcwd(), a.build) \
            if os.path.isdir(os.path.join(os.getcwd(), a.build)) else os.path.join(ROOT, a.build)
        lane = a.lane or _git(["rev-parse", "--abbrev-ref", "HEAD"])
        jobs = a.jobs
        if jobs is None:
            m = re.search(r"-j\s*(\d+)", " ".join(cmd)); jobs = int(m.group(1)) if m else None
        # one argument = a command string exactly as gate-scope.py --merge-tier prints it (its -LE
        # alternation carries `|`, so it must reach the shell as ONE string); several = an argv
        line = cmd[0] if len(cmd) == 1 else shlex.join(cmd)
        sys.exit(run_ctest(line, build, a.tier, lane, jobs, labels=inventory_labels(build)))
    if a.cmd == "import":
        import_log(a.log, a.tier, a.tip, a.lane, a.jobs); return
    if a.cmd == "longest":
        query_longest(a.days, a.n); return
    if a.cmd == "load-reds":
        query_load_reds(a.days); return
    if a.cmd == "trend":
        query_trend(a.days, a.suite, a.tip, a.k); return
    if a.cmd == "clocks":
        # THE STAGE CLOSE'S CLOCK CHECK (plan 9cl CLOCK-TRAP-1): a card left locked after a run
        # reads `locked?` (idle and not clocking down); rc-gate.sh prints this and reds on exit 3.
        st = gpu_clocks()
        print(json.dumps(st, sort_keys=True))
        sys.exit(3 if st.get("state") == "locked?" else 0)
    if a.cmd == "times":
        for k, v in sorted(median_times(a.days).items()): print(f"{k} {v:.2f}")


if __name__ == "__main__":
    main()
