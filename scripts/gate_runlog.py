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
START (ctest's `Start N: <name>` line), runs ctest -V to read each suite's own output (`target:`
lines, the pools' `ARM <name> PASS|FAIL|CRASH` lines) and WRITES EACH ROW'S RECORDS WHEN THE ROW ENDS
(GATE-COST-1 P6: a killed run keeps every row it finished), and exits with ctest's exit code. A
`run` is a GATE: it holds THE GATE SLOT for its whole run (scripts/vram_tokens.py, one gate at a time
box-wide, FIFO), runs the `hygiene` rows first as their own CPU phase (P8), re-queues a row that got
no admission at the end of the run (P5), and stops — DISPLAY_LOST, the abort line — when its
display dies (P6). `import` turns an existing ctest output log into records marked
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

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCHEMA = 2      # 2 (VERDICT-1 + GATE-LOG-1): xid, noadmit_arms, slot_wait_s, drain_s, hold_s, box.mem, box.queue_depth
# THE TIER NAMES (TESTING-DEBTS-1 T11) — the only ones a record may carry; testing/runs/README.md
# documents the same list. gate-scope.sh writes the first three (`scoped`; `scoped-fallback` = a
# scoped gate that fell back to the whole tier; `scoped-tier` = the tier by rule, a fork pin) and
# `target` (GATE-SPEED-1: the target tests' own step, after the gating verdict — reported, never
# gating); rc-gate.sh writes JAH_GATE_TIER (merge by default: stage, stage-close, push, smoke — the
# owner's smoke rc —, fork; scoped for a BATCH candidate, BATCH-GATE-1). `joint` is gone with
# `--joint` (BATCH-GATE-1: the batch gate is the joint gate); its old records stay readable.
TIERS = ("scoped", "scoped-fallback", "scoped-tier", "target", "merge", "stage", "stage-close", "push",
         "smoke", "fork", "lane", "solo")
# GATE-COST-2 #12: `lane` — a lane tool's own runs, recorded under the lane's name (the batch and push
# judgement ignores it); `solo` — a --solo batch's retries (gate-scope's default for --solo, and the solo
# re-run of a row an abort dropped red).


def lane_list(lane):
    """THE RECORD'S `lanes` (BATCH-GATE-1): the batch's lanes as a LIST — a gate on a batch candidate
    is every lane's gate at once; the single-lane case is a one-element list. Takes a name, a
    comma-separated string, or a list of either; empty parts are dropped."""
    if lane is None:
        return []
    parts = lane if isinstance(lane, (list, tuple)) else [lane]
    out = []
    for p in parts:
        for n in str(p).split(","):
            n = n.strip()
            if n and n not in out:
                out.append(n)
    return out


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


# THE FORCED EXIT (TESTING-CLEANUP-2 H8c; src/services/forcedexit.h): every path that ends the app with _Exit because
# an orderly end cannot be trusted — the shutdown watchdog, the worker-reap refusal, the unanswered device-loss end —
# prints ONE line `shutdown watchdog: <why> — forcing process exit (code 86)` and exits 86. A row whose output carries
# it is recorded with `forced_exit: <code>` and the first `forced_exit_why` (`forced_exits: <n>` when a pool's
# processes took it more than once) — PASS or red: a green row whose app could not end in order is a finding, and
# gate-report counts them. (Before H8c the same path exited 0 and nothing anywhere said so.)
_FORCED = re.compile(r"shutdown watchdog: (.*?) — forcing process exit \(code (\d+)\)")


def forced_exit(text):
    """(code, why, count) of the forced-exit lines in a row's output, or None when the app ended in order."""
    hits = [m for m in (_FORCED.search(l) for l in (text or "").splitlines()) if m]
    if not hits:
        return None
    return int(hits[0].group(2)), hits[0].group(1)[:200], len(hits)


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


# A ROW THAT TIMED OUT STILL WAITING FOR ITS ADMISSION NEVER RAN (TESTING-CLEANUP-2B item 2): the admission's wait is
# no longer covered by a widened TIMEOUT — inside a gate ctest schedules the rows by their tokens (RESOURCE_GROUPS,
# tests/cmake/vram_rows.cmake), so only a process OUTSIDE the gate (a hand-run app holding tokens) can make a row
# wait, and if the row's declared TIMEOUT ends while it is still waiting its code never started. That is NOADMIT —
# re-queued like any (P5) — never a TIMEOUT charged to the row. Read from the admission's own lines: a `vram:
# waiting` / `gate-slot: queued` line with no `vram: admitted` / `already admitted` / `gpu-lock: waited` after it.
_WAITING = re.compile(r"^\s*(?:\|\s*)*((?:vram: waiting for \d+ tokens?|gate-slot: queued)\b.*?)\s*$")
_ADMITTED = re.compile(r"^\s*(?:\|\s*)*(?:vram: admitted with|vram: already admitted|gpu-lock: waited|gate-slot: already held)")


def unadmitted_wait(text):
    """The admission's waiting line when the row's output ends still waiting (never admitted), else None."""
    waiting = None
    for line in (text or "").splitlines():
        m = _WAITING.match(line)
        if m:
            waiting = waiting or m.group(1)[:300]
        elif waiting and _ADMITTED.match(line):
            waiting = None
    return waiting


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


# THE Xid IN THE RECORD (VERDICT-1 U3): scripts/vram_tokens.py's supervise() reads the kernel journal
# through scripts/kernel_xid.py after every admitted row and prints `XID <n> from pid <p> of the row
# <label> — …: <the kernel's line>` per fault from the row's OWN process tree, then `XID-WINDOW
# <start>..<end> <label>`; tests/support/run_pool.py turns an arm whose process faulted into `ARM
# <pool>.<arm> CRASH <ms> xid <n> (the kernel's GPU fault from pid <p> at HH:MM:SS)`. The record
# carries `xid: null | {pid, window, lines[]}` — a red with an xid is a DEFECT by law (CLAUDE.md:
# never environmental), and ci_gate_check's verdict door accepts only `real:<defect id>` on it.
_XID_ROW = re.compile(r"^\s*(?:\|\s*)*XID (\d+) from pid (\d+) of the row .*?: (.*)$")
_XID_WINDOW = re.compile(r"^\s*(?:\|\s*)*XID-WINDOW (\S+\.\.\S+)")
_XID_ARM = re.compile(r"^\s*ARM\s+(\S+)\s+CRASH\b.*?\bxid (\d+) \(the kernel's GPU fault from pid (\d+) at "
                      r"(\d\d:\d\d:\d\d)\)")


_UNREADABLE = re.compile(r"kernel journal is unreadable")


def xid_of(text, day=None):
    """(the row's xid, {arm: its xid}) from a row's output — each None / absent when no fault."""
    row, window, arms = None, None, {}
    day = day or datetime.date.today().isoformat()
    for line in (text or "").splitlines():
        m = _XID_ROW.match(line)
        if m:
            if row is None: row = {"pid": int(m.group(2)), "window": None, "lines": []}
            row["lines"].append(m.group(3).strip()[:300]); continue
        m = _XID_WINDOW.match(line)
        if m:
            window = m.group(1); continue
        m = _XID_ARM.match(line)
        if m:
            at = f"{day}T{m.group(4)}"
            arms[m.group(1)] = {"pid": int(m.group(3)), "window": f"{at}..{at}",
                                "lines": [line.strip()[:300]]}
    if row is not None:
        row["window"] = window
    for a in arms.values():
        if window: a["window"] = window
    if row is None and arms:
        first = next(iter(arms.values()))
        row = {"pid": first["pid"], "window": first["window"], "lines": [l for a in arms.values() for l in a["lines"]]}
    return row, arms


def row_verdict(status, text, arms):
    """(verdict, status, budget line|None) of a row from ctest's status and its output: NOADMIT
    (never ran), else OOM / LOST for a red that carries the budget texts, else ctest's class."""
    v, st = verdict_of(status), status.strip("* ")
    na = noadmit_line(text) if v == "FAIL" else None
    if na and not arms:
        return "NOADMIT", na, None
    # VERDICT-1 U2: A POOL WHOSE EVERY ARM GOT NO ADMISSION NEVER RAN — NOADMIT, never FAIL (13 such
    # pools were recorded FAIL in the audit week, and 4 of them were cleared by a verdict's prose); a
    # pool with a MIX stays FAIL, its NOADMIT arms named on the row (`noadmit_arms`)
    if v != "PASS" and arms and all(a[1] == "NOADMIT" for a in arms):
        return "NOADMIT", na or ("every arm NOADMIT (%d): the pool never ran" % len(arms)), None
    if v == "TIMEOUT" and not arms:
        w = unadmitted_wait(text)
        if w:
            return "NOADMIT", "NOADMIT vram: the row's TIMEOUT ended its admission wait — it never ran (%s)" % w, None
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
    if "not run" in s or "disabled" in s or "skipped" in s: return "NOTRUN"   # SKIP_RETURN_CODE: it did not run
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


# THE DEFECT REGISTRY (TESTING_V3_SPEC §1.5; lane VERDICT-1): <workspace>/testing/defects.json — what "known" means.
# {"defects": [{id, rows, kind, cause, first_seen {tip, pin, run}, state, recheck, found_by, ...}]}:
#   kind      defect | nondeterminism | selector | box | combination
#   state     "open" | {"fixed": {"tip": <sha>}} | "retired"
#   recheck   a DATE (YYYY-MM-DD) the judge enforces: an entry past it is refused until re-verdicted
#   found_by  read | gate | owner | lane
# and, on a nondeterminism entry, ONE of:
#   uses: 1, suspects: [lanes], census {...}   a NOT REPRODUCED entry (attribution): SINGLE-USE — it clears only
#                                              reds at first_seen.tip (the merge that registered it), never again
#   enrolled {by, rate, census, date}          a STANDING entry (the lead's explicit `defect enrol`, with a measured
#                                              rate and the census): the contention class
# PENDING entries: BATCH-GATE-1 writes testing/defects.pending/<id>.json (one entry each); this reader ingests them
# with the registry (one namespace, the same shape).
# Read by the verdict door (`real:<id>` must name an entry whose `rows` hold the row), by the push tier and
# the stage-close judge (KNOWN RED), and by gate-report.py. THE CONTENTION CLASS IS ITS SUBSET `kind:
# nondeterminism`, state open (there is no separate file: contention.json is gone, forward only). Written
# by `lane.sh defect add` (the lead, PROCESS-1 — against this schema) and by the tools.
DEFECT_KINDS = ("defect", "nondeterminism", "selector", "box", "combination")
FOUND_BY = ("read", "gate", "owner", "lane")
DEFECT_FIELDS = ("id", "rows", "kind", "cause", "first_seen", "state", "recheck", "found_by")
_RUN_DATE = re.compile(r"^(\d{4})(\d\d)(\d\d)T")
_DATE = re.compile(r"^\d{4}-\d\d-\d\d$")


def defects_file():
    return os.environ.get("JAH_DEFECTS_FILE") or os.path.join(workspace_root(), "testing", "defects.json")


def defects_pending_dir():
    return os.path.join(os.path.dirname(defects_file()), "defects.pending")


def recheck_past(e, today=None):
    """True when an entry's recheck DATE has passed (the judge refuses it until it is re-verdicted)."""
    today = today or datetime.date.today().isoformat()
    return str(e.get("recheck")) < today


def single_use(e):
    """A NOT REPRODUCED entry: it clears reds only at the tip that registered it."""
    return e.get("kind") == "nondeterminism" and e.get("uses") is not None


def defect_state(e):
    """'open' | 'fixed' | 'retired' of a registry entry."""
    st = e.get("state")
    return "fixed" if isinstance(st, dict) and "fixed" in st else st


def defect_date(e):
    """The date an entry was first seen (YYYY-MM-DD, from first_seen.run's stamp), or None."""
    m = _RUN_DATE.match(str((e.get("first_seen") or {}).get("run") or ""))
    return f"{m.group(1)}-{m.group(2)}-{m.group(3)}" if m else None


def _defect_problems(e):
    if not isinstance(e, dict): return ["not an object"]
    bad = [f"missing {f}" for f in DEFECT_FIELDS if e.get(f) in (None, "", [], {})]
    if bad: return bad
    if not (isinstance(e["rows"], list) and all(isinstance(x, str) and x for x in e["rows"])):
        bad.append("rows is not a list of row names")
    if e["kind"] not in DEFECT_KINDS: bad.append(f"kind '{e['kind']}' is not one of {'|'.join(DEFECT_KINDS)}")
    if e["found_by"] not in FOUND_BY: bad.append(f"found_by '{e['found_by']}' is not one of {'|'.join(FOUND_BY)}")
    fs = e["first_seen"]
    if not (isinstance(fs, dict) and all(isinstance(fs.get(k), str) and fs.get(k) for k in ("tip", "pin", "run"))):
        bad.append("first_seen is not {tip, pin, run}")
    elif not defect_date(e):
        bad.append(f"first_seen.run '{fs['run']}' carries no date (<yyyymmddThhmmss>-<tip>)")
    if not _DATE.match(str(e["recheck"])):
        bad.append(f"recheck '{e['recheck']}' is not a DATE (YYYY-MM-DD; the judge enforces it)")
    if e["kind"] == "nondeterminism":
        if e.get("uses") is not None:
            if e.get("uses") != 1 or not isinstance(e.get("suspects"), list) or not isinstance(e.get("census"), dict):
                bad.append("a NOT REPRODUCED entry carries uses: 1, suspects: [lanes] and its census")
        elif not (isinstance(e.get("enrolled"), dict) and all(e["enrolled"].get(k) for k in ("by", "rate", "census", "date"))):
            bad.append("a nondeterminism entry is either single-use (uses: 1, suspects, census) or ENROLLED by the lead "
                       "(enrolled {by, rate, census, date})")
    st = e["state"]
    if not (st in ("open", "retired") or (isinstance(st, dict) and isinstance((st.get("fixed") or {}).get("tip"), str)
                                           and st["fixed"]["tip"])):
        bad.append("state is not open | {fixed: {tip}} | retired")
    return bad


def defects_quarantine_dir():
    return os.path.join(os.path.dirname(defects_file()), "defects.quarantine")


def _quarantine(name, entry, why, src=None, log=sys.stderr):
    """A malformed entry or pending file is QUARANTINED, never silent and never fatal to the rest: a pending file is
    MOVED to testing/defects.quarantine/ (the registry file's own bad entry is COPIED there — the tracked file is the
    lead's to edit), its reason in a `<name>.why` sidecar, and `REGISTRY: <file> quarantined: <why>` printed."""
    q = defects_quarantine_dir()
    safe = re.sub(r"[^A-Za-z0-9_.-]", "_", name)[:80] or "entry"
    try:
        os.makedirs(q, exist_ok=True)
        dest = os.path.join(q, safe if safe.endswith(".json") else safe + ".json")
        if src:
            os.replace(src, dest)
        elif not os.path.exists(dest):
            with open(dest, "w") as f:
                json.dump(entry, f, indent=1, sort_keys=True)
        with open(dest[:-5] + ".why", "w") as f:
            f.write(why + "\n")
    except OSError as e:
        why += f" (and it could not be quarantined: {e})"
    if log is not None:
        log.write(f"REGISTRY: {src or name} quarantined: {why}\n")
        log.flush()


def defects_quarantined():
    """[(file, why)] of every quarantined entry — a FINDING for the lead's `status` until it is fixed or removed."""
    q = defects_quarantine_dir()
    out = []
    if os.path.isdir(q):
        for f in sorted(os.listdir(q)):
            if f.endswith(".json"):
                try: why = open(os.path.join(q, f[:-5] + ".why")).read().strip()
                except OSError: why = "?"
                out.append((os.path.join(q, f), why))
    return out


def defects_load(log=sys.stderr):
    """({id: entry}, None) — or (None, why) when the registry FILE itself cannot be read. A malformed entry (or a
    malformed / unreadable pending file, or a duplicate id) is QUARANTINED (_quarantine) and the rest loads — one bad
    file never disables the door; every quarantined file is printed as a REGISTRY finding on each load."""
    path = defects_file()
    try:
        d = json.load(open(path))
    except (OSError, ValueError) as e:
        return None, f"the defect registry {path} is missing or unreadable ({e.__class__.__name__})"
    lst = d.get("defects") if isinstance(d, dict) else None
    if not isinstance(lst, list):
        return None, f"the defect registry {path} has no `defects` list"
    items = [(e, None) for e in lst]
    pend = defects_pending_dir()
    if os.path.isdir(pend):
        for f in sorted(os.listdir(pend)):
            if not f.endswith(".json"): continue
            fp = os.path.join(pend, f)
            try:
                items.append((json.load(open(fp)), fp))
            except (OSError, ValueError) as e:
                _quarantine(f, None, f"unreadable ({e.__class__.__name__})", src=fp, log=log)
    out = {}
    for k, (e, src) in enumerate(items):
        probs = _defect_problems(e)
        name = (e.get("id") if isinstance(e, dict) else None) or f"entry-{k}"
        if not probs and name in out: probs = ["a duplicate id"]
        if probs:
            _quarantine(os.path.basename(src) if src else f"{name}", e,
                        f"{name}: {', '.join(probs)} (TESTING_V3 §1.5's shape)", src=src, log=log)
            continue
        out[name] = e
    q = defects_quarantined()
    if q and log is not None:
        log.write(f"REGISTRY: FINDING — {len(q)} quarantined entr{'y' if len(q) == 1 else 'ies'} in "
                  f"{defects_quarantine_dir()} (fix and return them, or delete them)\n")
    return out, None


def contention_of(defects):
    """THE CONTENTION CLASS: {row: its STANDING entry} — open, ENROLLED `nondeterminism` entries whose recheck
    date has not passed (a single-use NOT REPRODUCED entry is never the class)."""
    out = {}
    for e in (defects or {}).values():
        if (e["kind"] == "nondeterminism" and defect_state(e) == "open" and e.get("enrolled")
                and not recheck_past(e)):
            for r in e["rows"]:
                out.setdefault(r, e)
    return out


def contention_list():
    """{row: one line} of the contention class (the registry's open nondeterminism entries), or None when the
    registry cannot be read (defects_load says why) — gate-scope.sh --solo's reader."""
    d, _ = defects_load()
    if d is None: return None
    return {r: f"{e['id']}: {e['cause']} [{defect_date(e)}; recheck: {e['recheck']}]" for r, e in contention_of(d).items()}


def lanes_of(r):
    """The lanes a record belongs to: BATCH-GATE-1's `lanes` list (forward only — a record without it belongs to no
    lane for the judge)."""
    v = r.get("lanes")
    return [x for x in v if isinstance(x, str) and x] if isinstance(v, list) else []


def own_lane(r):
    """The lane a record is a LANE'S OWN record of (`lanes == [<lane>]`, no `batch` tag), else None — the only
    records a rebase carries reds from (TESTING_V3 §1.4: batch records never carry)."""
    if r.get("batch"): return None
    v = lanes_of(r)
    return v[0] if len(v) == 1 else None


def _git(args, cwd=None):
    cwd = cwd or ROOT
    try:
        r = subprocess.run(["git"] + args, cwd=cwd, capture_output=True, text=True)
        return r.stdout.strip() if r.returncode == 0 else ""
    except OSError:
        return ""


def tree_shas(root=None):
    """The tree that ran — this checkout's, or `root`'s (a batch's attribution runs each lane's solos
    in THAT lane's worktree: its records must carry the lane's own tip)."""
    root = root or ROOT
    studio = _git(["rev-parse", "HEAD"], cwd=root)
    irisgl = _git(["rev-parse", "HEAD"], cwd=os.path.join(root, "irisgl"))
    fork = _git(["rev-parse", "HEAD"], cwd=os.path.join(root, "irisgl", "thirdparty", "ogre-next"))
    # THE TREE THAT RAN: Studio's own files AND irisgl's (an uncommitted engine edit gates green
    # and is never committed otherwise — the second Fable read, F1). irisgl's vendored submodules'
    # CONTENT is ignored: assimp's applied patch stack is configure-time state, not dirt.
    s_dirty = bool(_git(["status", "--porcelain", "--untracked-files=no", "--ignore-submodules=dirty"], cwd=root))
    i_dirty = bool(_git(["status", "--porcelain", "--untracked-files=no", "--ignore-submodules=dirty"],
                        cwd=os.path.join(root, "irisgl")))
    return {"studio": studio, "irisgl": irisgl, "fork": fork, "studio_dirty": s_dirty or i_dirty,
            "irisgl_dirty": i_dirty}


def built_from(build):
    """What <build> was BUILT from (GATE-COST-2 #8): {studio, irisgl, dirty} read from <build>/BUILT_FROM,
    which every build writes as its last step (cmake/BuiltFrom.cmake), or None (a build dir without the
    stamp: a toy project, a tree built before the stamp existed)."""
    try:
        kv = dict(l.split("=", 1) for l in open(os.path.join(build, "BUILT_FROM")).read().splitlines() if "=" in l)
    except OSError:
        return None
    return {"studio": kv.get("studio", ""), "irisgl": kv.get("irisgl", ""), "dirty": kv.get("dirty") == "1"}


def stale_build(shas):
    """None when the record's build is its tip's; else why not — the built commits differ from the tip's, the
    build was made from a dirty tree, or it carries NO stamp (F4: forward-only — a record whose build was not
    stamped is never the tip's run; every gate's no-op build writes the stamp before its rows)."""
    b = (shas or {}).get("built")
    if not b:
        return "built from an UNKNOWN commit (no BUILT_FROM stamp)"
    if b.get("dirty"):
        return "built from a DIRTY tree"
    if b.get("studio") != shas.get("studio") or b.get("irisgl") != shas.get("irisgl"):
        return "built from studio %s / irisgl %s, not the tip" % ((b.get("studio") or "?")[:9], (b.get("irisgl") or "?")[:9])
    return None


PREBUILD_MAX_EDGES = 50


def prebuild(build, jobs=None):
    """THE GATE'S NO-OP BUILD (GATE-COST-2 F4): before any row, `cmake --build <build>` — seconds when nothing
    changed — rebuilds whatever is stale (a reverted edit, a `--target` build's leftovers) and refreshes
    BUILT_FROM, so the binaries match HEAD's sources by construction. At the box's priority and a width the
    memory law allows (JAH_GATE_BUILD_JOBS, default 3). Returns None when it built (or the dir is not a CMake
    build), else the refusal text — a failed no-op build never gates."""
    cache = os.path.join(build, "CMakeCache.txt")
    if not os.path.isfile(cache):
        return None
    cmd = "nice -n 19 ionice -c 3 cmake --build %s -j %s" % (build, jobs or os.environ.get("JAH_GATE_BUILD_JOBS", "3"))
    # NEVER A SILENT FULL BUILD (round 2, B): a tree that was never built (no stamp, no app binary) or that is
    # far from built (ninja -n: more than PREBUILD_MAX_EDGES) is refused with the command that builds it — the
    # gate's build is the no-op one, seconds, not a lane's build under the gate's name
    first = "REFUSING TO RUN: %s is not a built tree — build the tree first: %s" % (build, cmd)
    if not os.path.isfile(os.path.join(build, "BUILT_FROM")):
        return first + "  (no BUILT_FROM stamp)"
    try:
        project = re.search(r"^CMAKE_PROJECT_NAME:\w+=(.*)$", open(cache).read(), re.M)
    except OSError:
        project = None
    if project and project.group(1).strip() == "Jahshaka" and not os.path.exists(os.path.join(build, "bin", "Jahshaka")):
        return first + "  (no bin/Jahshaka)"
    if os.path.isfile(os.path.join(build, "build.ninja")):
        r = subprocess.run(["ninja", "-C", build, "-n"], capture_output=True, text=True)
        edges = [l for l in r.stdout.splitlines() if re.match(r"^\[\d+/\d+\]", l)]
        if len(edges) > PREBUILD_MAX_EDGES:
            return first + "  (ninja -n: %d edges to rebuild, more than %d)" % (len(edges), PREBUILD_MAX_EDGES)
    log = os.path.join(build, "gate-prebuild.log")
    print("=== the no-op build before any row: %s (log %s) ===" % (cmd, log))
    sys.stdout.flush()
    t0 = time.time()
    jobs = jobs or os.environ.get("JAH_GATE_BUILD_JOBS", "3")
    with open(log, "w") as out:
        rc = subprocess.run(["nice", "-n", "19", "ionice", "-c", "3", "cmake", "--build", build, "-j", str(jobs)],
                            stdout=out, stderr=subprocess.STDOUT).returncode
    tail = open(log, errors="replace").read().splitlines()[-6:]
    if rc != 0:
        return ("REFUSING TO RUN: the gate's no-op build of %s FAILED (exit %d; %s):\n  %s"
                % (build, rc, log, "\n  ".join(tail)))
    print("=== the no-op build: %.0f s (%s) — the binaries are HEAD's, BUILT_FROM refreshed ===" % (time.time() - t0, log))
    sys.stdout.flush()
    return None


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


_BUILD_COMMS = ("ninja", "cmake", "make", "gmake")


def box_mem():
    """GATE-LOG-1: the memory pressure a row started under — `psi10` (/proc/pressure/memory, `some`
    avg10, %), `swap_used_mb`, `builds` (ninja/cmake/make processes on the box). Each None where the
    box cannot say (macOS)."""
    out = {"psi10": None, "swap_used_mb": None, "builds": None}
    try:
        for line in open("/proc/pressure/memory"):
            if line.startswith("some"):
                out["psi10"] = float(dict(kv.split("=") for kv in line.split()[1:])["avg10"])
    except (OSError, ValueError, KeyError):
        pass
    try:
        mi = {}
        for line in open("/proc/meminfo"):
            k, _, v = line.partition(":")
            mi[k] = int(v.split()[0])
        out["swap_used_mb"] = round((mi["SwapTotal"] - mi["SwapFree"]) / 1024.0, 1)
    except (OSError, ValueError, KeyError, IndexError):
        pass
    try:
        n = 0
        for d in os.listdir("/proc"):
            if not d.isdigit(): continue
            try:
                with open(f"/proc/{d}/comm") as f:
                    if f.read().strip() in _BUILD_COMMS: n += 1
            except OSError:
                continue
        out["builds"] = n
    except OSError:
        pass
    return out


def queue_depth():
    """GATE-LOG-1: the gates WAITING for the box's gate slot now (its holder not counted), or None
    when the queue cannot be read."""
    try:
        return max(0, len(_vram().gate_queue()) - 1)
    except Exception:          # noqa: BLE001 — a reading, never a reason for a gate to fail
        return None


def _psi(kind):
    try:
        for line in open(f"/proc/pressure/{kind}"):
            if line.startswith("some"):
                return float(dict(kv.split("=") for kv in line.split()[1:])["avg10"])
    except (OSError, ValueError, KeyError):
        pass
    return None


def gpu_apps(own_root=None):
    """[{pid, name, mib, ours}] of the GPU processes OUTSIDE this gate's process tree (nvidia-smi
    --query-compute-apps), or None when nvidia-smi cannot say (macOS, no driver). `ours` = a process of OUR stack
    (the Jahshaka binary, a test_/bench_ binary, anything run from a jahshaka tree, an Xvfb client)."""
    try:
        r = subprocess.run(["nvidia-smi", "--query-compute-apps=pid,process_name,used_memory",
                            "--format=csv,noheader,nounits"], capture_output=True, text=True, timeout=10)
    except (OSError, subprocess.TimeoutExpired):
        return None
    if r.returncode != 0: return None
    roots = {os.getpid()} | ({own_root} if own_root else set())
    out = []
    for line in r.stdout.splitlines():
        f = [x.strip() for x in line.split(",")]
        if len(f) < 3 or not f[0].isdigit(): continue
        pid = int(f[0])
        if _descends_from(pid, roots): continue
        name = os.path.basename(f[1])[:40]
        try: exe = os.path.realpath(os.readlink(f"/proc/{pid}/exe"))
        except OSError: exe = None          # exited, or not ours to read: UNKNOWN — recorded, never counted
        rec = {"pid": pid, "name": name, "exe": exe, "mib": int(f[2]) if f[2].isdigit() else None, "ours": _ours(pid, name)}
        if exe is None: rec["unknown"] = True
        out.append(rec)
    return out


_MAIN_TREE = []


def main_tree():
    """The Studio repo's MAIN tree (the parent of the git common dir — the same for every worktree). Read once per
    process (one git call, never one per GPU app)."""
    if _MAIN_TREE: return _MAIN_TREE[0]
    _MAIN_TREE.append(_main_tree())
    return _MAIN_TREE[0]


def _main_tree():
    try:
        cd = subprocess.run(["git", "rev-parse", "--path-format=absolute", "--git-common-dir"],
                            cwd=ROOT, capture_output=True, text=True).stdout.strip()
        if cd: return os.path.realpath(os.path.dirname(cd))
    except OSError:
        pass
    return os.path.realpath(ROOT)


def _under(path, root):
    return bool(path) and (path == root or path.startswith(root.rstrip(os.sep) + os.sep))


def ours_exe(exe):
    """A binary of OUR trees: a lane / rc worktree (<main>/.claude/worktrees/) or the main tree's build
    (<main>/build-linux) — never a match on the string "/jahshaka/" (the home directory carries it)."""
    m = main_tree()
    return _under(exe, os.path.join(m, ".claude", "worktrees")) or _under(exe, os.path.join(m, "build-linux"))


def _ours(pid, name):
    if name == "Jahshaka" or name.startswith(("test_", "bench_")):
        return True
    try:
        if ours_exe(os.path.realpath(os.readlink(f"/proc/{pid}/exe"))):
            return True
    except OSError:
        pass
    try:
        env = open(f"/proc/{pid}/environ", "rb").read().split(b"\0")
        disp = [e[8:].decode("ascii", "replace") for e in env if e.startswith(b"DISPLAY=")]
        return bool(disp) and disp[0] not in ("", ":0", ":0.0")     # a client of a rig Xvfb, never the desktop's
    except OSError:
        return False


# THE COMPETITOR CENSUS (TESTING_V3 §1.4/§1.6; the merge read's F3): what ELSE competed when a row started — the
# one thing a `contention:` verdict may stand on. A GPU competitor is a process of OURS outside the gate's tree, a
# process whose exe is NOT in the box's IDLE BASELINE (testing/box-baseline.json `desktop`, written by the lead with
# nvidia-smi on the idle desktop: the browser, the terminal, the editor are not competition), or ANY process above
# the baseline's `vram_floor_mb` — without the baseline only our own processes count, and the census says so. Builds are those OUTSIDE the gate
# (not its descendants, not run from its tree). Never the gate's own queue or drain.
CENSUS_PSI = 10.0           # avg10 % of memory or IO pressure that counts as a competitor
VRAM_FLOOR_MIB = 512


def baseline_file():
    return os.environ.get("JAH_BOX_BASELINE") or os.path.join(workspace_root(), "testing", "box-baseline.json")


def box_baseline():
    """({idle desktop exe}, vram floor MiB), or None when there is no readable baseline."""
    try:
        d = json.load(open(baseline_file()))
        return {os.path.realpath(x) for x in d["desktop"]}, int(d.get("vram_floor_mb") or VRAM_FLOOR_MIB)
    except (OSError, ValueError, KeyError, TypeError, AttributeError):
        return None


def _own_tree(path):
    """Is `path` inside the gate's OWN tree — this worktree (ROOT), but not a sibling worktree nested under the main
    tree's .claude/worktrees/ (a gate in the main tree must still see every lane's build)?"""
    root = os.path.realpath(ROOT)
    if not _under(path, root): return False
    return not _under(path, os.path.join(root, ".claude", "worktrees"))


def builds_outside(own_root=None):
    """ninja/cmake/make processes that are NOT the gate's own (its descendants, or run from its own tree)."""
    roots = {os.getpid()} | ({own_root} if own_root else set())
    n = 0
    try:
        procs = [d for d in os.listdir("/proc") if d.isdigit()]
    except OSError:
        return None
    for d in procs:
        try:
            with open(f"/proc/{d}/comm") as f:
                if f.read().strip() not in _BUILD_COMMS: continue
            if _descends_from(int(d), roots): continue
            try:
                if _own_tree(os.path.realpath(os.readlink(f"/proc/{d}/cwd"))): continue
            except OSError:
                pass
            n += 1
        except OSError:
            continue
    return n


def census(own_root=None, mem=None, ctests=None):
    m = mem or box_mem()
    oc = other_ctests(own_root) if ctests is None else ctests
    apps = gpu_apps(own_root)
    base = box_baseline()
    comp = None if apps is None else [a for a in apps if a.get("ours") or (
        base is not None and not a.get("unknown") and (a.get("exe") not in base[0] or (a.get("mib") or 0) >= base[1]))]
    return {"gpu_apps": apps, "gpu_competitors": comp,
            "baseline": baseline_file() if base is not None else "MISSING: only our own GPU processes counted",
            "other_ctests": oc, "builds": builds_outside(own_root), "psi10_mem": m.get("psi10"), "psi10_io": _psi("io")}


def competitors(c):
    """The census's competitors as words ([] = none measured)."""
    c = c or {}
    out = []
    if c.get("gpu_competitors"): out.append("%d GPU process(es) competing (%s)" % (
        len(c["gpu_competitors"]), ", ".join(f"{a['name']}:{a['pid']}" for a in c["gpu_competitors"][:3])))
    if (c.get("other_ctests") or 0) > 0: out.append(f"{c['other_ctests']} sibling ctest(s)")
    if (c.get("builds") or 0) > 0: out.append(f"{c['builds']} build process(es) outside the gate")
    for k in ("psi10_mem", "psi10_io"):
        if (c.get(k) or 0) >= CENSUS_PSI: out.append(f"{k.split('_')[1]} pressure {c[k]} %")
    return out


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
    targets, arms, begun = [], [], []
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
        if m: targets.append(m.group(1)[:200])
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
    return targets or None, out


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


def append_records(records, tier, tip, _cache={}):
    """Append records to the run log's file for (tier, tip). `retries` counts the earlier records of
    the same (suite, arm) in that file; the count is cached per file and re-read whenever the file
    grew by someone else's hand (a per-row writer appends hundreds of times per run)."""
    path = _file_for(check_tier(tier), tip)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    try: size = os.path.getsize(path)
    except OSError: size = 0
    hit = _cache.get(path)
    prior = hit[1] if hit and hit[0] == size else _prior_counts(path)
    with open(path, "a") as f:
        for r in records:
            k = (r["suite"], r.get("arm"))
            r["retries"] = prior.get(k, 0)
            prior[k] = r["retries"] + 1
            f.write(json.dumps(r, sort_keys=True) + "\n")
    _cache[path] = (os.path.getsize(path), prior)
    return path


# ---- THE PER-ROW RUN (GATE-COST-1 P5/P6/P8; SPECS/audits/GATE_COST_2026-10-09.md) -----------------
# EACH ROW'S RECORD IS WRITTEN WHEN THE ROW ENDS (P6). The records used to be written after ctest
# exited, from its junit file (`p.wait()` first): the oomd kill of 2026-10-09 09:24 took 303 finished
# rows of rc-smoke16a with it, 11.7 h of gate wall in all. ctest runs with -V now and every line a row
# prints arrives prefixed with its test number (`45: …`); the runner keeps each row's own lines
# (ctest's preamble — the command, the directory, the environment, the timeout — dropped) and turns
# them into the row's records at its result line, appended at once. The stream the caller sees is
# the one --output-on-failure printed: the result lines, and a red row's output after its line.
OUTPUT_CAP = 262144          # bytes of a row's output kept (the head — ctest's own truncation was)
_VLINE = re.compile(r"^(\d+): ?(.*)$")
_VNOISE = re.compile(r"^(test \d+|UpdateCTestConfiguration .*|Constructing a list of tests|Done constructing a list "
                     r"of tests|Updating test list for fixtures|Added \d+ tests? to meet fixture requirements|"
                     r"Checking test dependency graph(\.\.\.| end))\s*$")
_PREAMBLE = re.compile(r"^(Test command: |Working Directory: |Environment variables: ?$|Environment variable "
                       r"modifications: ?$|Test timeout computed to be: )")
_RESULT_ID = re.compile(r"^\s*\d+/\d+\s+Test\s+#(\d+):")
TIMING_LABEL = "timing"      # gate-scope.py's: a row that measures (the serial phase)
CPU_LABEL = "hygiene"        # P8: the lint and selector rows — their own CPU phase, first
DISPLAY_LOST = 6             # the exit code of a run that stopped because its display died (P6)
ABORTED = None               # the abort line of the last run_ctest() that stopped, else None
BATCH = None                 # the batch tag every record of this process carries (`batch`, TESTING_V3 §1.6), else none


def _vram():
    """scripts/vram_tokens.py — the box's one admission (the tokens, the gate slot)."""
    here = os.path.dirname(os.path.abspath(__file__))
    if here not in sys.path:
        sys.path.insert(0, here)
    import vram_tokens
    return vram_tokens


# THE LAW SWITCHES (GATE-COST-2 #10, fix round F2): an environment A HUMAN set that changes how a gate admits,
# waits or judges. Every record of a run carries the ones in force as `overrides: [names]` (empty when none);
# the push judge (VERDICT-1) refuses a candidate whose records carry any. What the TOOLS set is not an
# override: the JAH_VRAM_ALL a --solo batch sets after its whole-card drain timed out is recorded as
# `fallback: drain-timeout` on every record of that run instead (and a timing row's record carrying a
# fallback is not a measurement: the judge refuses it until the row re-runs under a real whole-card hold).
# A --verdict is a lawful act through the door, never an override.
LAW_SWITCHES = ("JAH_GATE_SLOT", "JAH_VRAM_TOKENS", "JAH_VRAM_ALL", "JAH_JUDGE_READ", "JAH_VRAM_WAIT",
                "JAH_VRAM_PHASE_WAIT", "JAH_GATE_REQUEUE", "JAH_DISPLAY_POLL_S", "JAH_KERNEL_JOURNAL",
                "JAH_VRAM_PROC_LOCKS",
                # F6: a private token universe is admission off for the box; a hand-set slot holder bypasses it
                "JAH_VRAM_DIR", "JAH_GATE_SLOT_HELD")


def law_overrides(env=None, tool_set=()):
    """[NAME=value …] of the law switches set in `env` (default os.environ), minus `tool_set` — the switches
    the tools set themselves for this run (its `fallback` says why). JAH_VRAM_TOKENS counts only when it is
    not the default 11; JAH_GATE_SLOT only when it turns the slot off."""
    env = os.environ if env is None else env
    out = []
    for k in LAW_SWITCHES:
        v = env.get(k)
        if v is None or k in tool_set:
            continue
        if k == "JAH_VRAM_TOKENS" and v.strip() == "11":
            continue
        if k == "JAH_GATE_SLOT" and v.strip() != "0":
            continue
        if k == "JAH_VRAM_DIR" and os.path.normpath(v) == "/tmp/jah-vram":
            continue
        if k == "JAH_GATE_SLOT_HELD":
            # F6: a gate's rows inherit it from the gate (a live ticket holder); any other value is a hand-set
            # bypass of the slot
            if _vram().slot_held_valid(v):
                continue
            v = v + " (no live holder)"
        out.append(f"{k}={v}")
    return out


def requeue_times():
    """P5: how many times a row that got no admission is re-queued inside the same run (JAH_GATE_REQUEUE)."""
    try:
        return max(0, int(os.environ.get("JAH_GATE_REQUEUE", "2")))
    except ValueError:
        return 2


class DisplayGuard:
    """THE GATE'S DISPLAY (P6): a gate whose X server died used to keep "running" — every row after
    it failed at engine create ("Malformed resolution string"), 634 such records in the audit's
    window. Read at the start of a run: the local display's server pid (its lock file) and its
    socket; at every row's end and every 2 s the same pid must be alive, still own the lock (a
    display NUMBER is reused the moment it is free) and accept a connection. A DISPLAY that is not
    a local `:N`, or none (the Mac, a headless run), is not guarded. JAH_X11_ROOT moves /tmp (a test)."""

    def __init__(self, env=None):
        d = (env if env is not None else os.environ).get("DISPLAY") or ""
        m = re.match(r"^:(\d+)(?:\.\d+)?$", d)
        root = os.environ.get("JAH_X11_ROOT", "/tmp")
        self.display, self.active, self.pid = d, False, None
        if not m:
            return
        self.lock = os.path.join(root, ".X%s-lock" % m.group(1))
        self.sock = os.path.join(root, ".X11-unix", "X%s" % m.group(1))
        self.pid = self._lock_pid()
        self.active = True

    def _lock_pid(self):
        try:
            return int(open(self.lock).read().split()[0])
        except (OSError, ValueError, IndexError):
            return None

    def dead(self):
        """None while the display lives, else why it is gone."""
        if not self.active:
            return None
        if self.pid is None:
            return "no X server owns %s (no %s)" % (self.display, self.lock)
        try:
            os.kill(self.pid, 0)
        except ProcessLookupError:
            return "the X server of %s (pid %d) is gone" % (self.display, self.pid)
        except PermissionError:
            pass
        if self._lock_pid() != self.pid:
            return "%s now belongs to another server (lock pid %s, not %d)" % (self.display, self._lock_pid(), self.pid)
        import socket
        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        s.settimeout(2.0)
        try:
            s.connect(self.sock)
        except OSError as e:
            return "%s refuses a connection (%s)" % (self.display, e.strerror or e)
        finally:
            s.close()
        return None


def _descendants(pid):
    kids = {}
    for d in os.listdir("/proc"):
        if d.isdigit():
            pp = _ppid(int(d))
            if pp: kids.setdefault(pp, []).append(int(d))
    out, todo = [], [pid]
    while todo:
        p = todo.pop()
        for c in kids.get(p, []):
            out.append(c); todo.append(c)
    return out


def _kill_tree(pid):
    """SIGTERM the run's whole process tree — ctest is its own process group (ctest itself dies of a
    SIGTERM and ORPHANS its rows: measured), so the group and every descendant still parented to it —
    and SIGKILL what is left 15 s later. The timer is NOT a daemon: a gate that is exiting waits for it
    rather than leave the rows running (the merge read's worth-a-look 1)."""
    import signal
    procs = [pid] + _descendants(pid)

    def hit(sig):
        try: os.killpg(pid, sig)
        except OSError: pass
        for p in procs:
            try: os.kill(p, sig)
            except OSError: pass
    hit(signal.SIGTERM)
    t = threading.Timer(15.0, hit, args=(signal.SIGKILL,)); t.daemon = False; t.start()
    return t


# THE ctest TREE DIES WITH ITS GATE (GATE-COST-1 F2, the merge read): a gate process that dies — oomd, a
# kill, a timeout — used to leave its ctest running, unrecorded, and the lane's own --resume queued
# behind the orphan. ctest starts in its OWN process group with PR_SET_PDEATHSIG = SIGTERM (it dies with
# the gate), and a REAPER — a tiny detached process that outlives a SIGKILLed gate — kills the group
# (SIGTERM, then SIGKILL 15 s later) the moment the gate's pid is gone, since ctest's SIGTERM orphans its
# rows. A gate that is signalled (SIGTERM/SIGINT/SIGHUP: on_signals()) stops the tree itself and releases
# its slot and tokens on the way out. Neither the slot nor the tokens are inherited by ctest: they are
# the gate process's, so they are free the moment the gate is gone.
_REAPER = r"""
import os, sys, time, signal
gate, grp = int(sys.argv[1]), int(sys.argv[2])
def alive(pid):
    try: os.kill(pid, 0); return True
    except ProcessLookupError: return False
    except PermissionError: return True
def group():
    try: os.killpg(grp, 0); return True
    except OSError: return False
while alive(gate) and group(): time.sleep(1.0)
if group():
    try: os.killpg(grp, signal.SIGTERM)
    except OSError: pass
    for _ in range(15):
        time.sleep(1.0)
        if not group(): break
    try: os.killpg(grp, signal.SIGKILL)
    except OSError: pass
"""


def _child_setup():
    """In ctest's child, before exec: its own process group, and SIGTERM when the gate dies."""
    os.setpgrp()
    try:
        import ctypes
        ctypes.CDLL(None, use_errno=True).prctl(1, 15)      # PR_SET_PDEATHSIG, SIGTERM
    except (OSError, AttributeError):
        pass


class GateSignal(BaseException):
    """A gate was told to stop (SIGTERM/SIGINT/SIGHUP): the runs below stop their trees on the way out."""
    def __init__(self, sig):
        super().__init__(sig); self.sig = sig


def on_signals():
    """THE GATE'S OWN SIGNALS (F2): turn SIGTERM/SIGINT/SIGHUP into GateSignal, so every `finally` on the
    way out runs — the phase stops its ctest tree, the run releases its tokens, the process its slot."""
    import signal

    def raise_(sig, _frame):
        raise GateSignal(sig)
    for sig in (signal.SIGTERM, signal.SIGINT, signal.SIGHUP):
        try: signal.signal(sig, raise_)
        except (OSError, ValueError): pass        # not the main thread (a test drives run_ctest from one)


def phase_record(kind, tier, lane, rng, shas, **fields):
    """ONE RECORD ABOUT A RUN, NOT A ROW (GATE-COST-2): `kind: drain-timeout` (a whole-card hold that never
    drained: the holders and their age) or `kind: abort` (the rows a dead display dropped). Its suite is
    `@<kind>`, which no selection ever names, so the refusal reads nothing from it; the reports do.
    Returns the file it went to."""
    now = datetime.datetime.now().astimezone().isoformat(timespec="seconds")
    rec = dict(fields, schema=SCHEMA, kind=kind, suite="@" + kind, arm=None, verdict=kind.upper(), tier=tier,
               lanes=lane_list(lane), range=rng, tip=shas, ts=now, source="run", gating=False, retry=False,
               overrides=law_overrides())
    if BATCH:
        rec["batch"] = BATCH
    try:
        return append_records([rec], tier, shas["studio"])
    except (OSError, ValueError, KeyError):
        return None


def dropped_red(shas):
    """{suite: the abort record's ts} of the rows an abort at this tip dropped RED (status not Passed)."""
    out, d = {}, log_dir()
    if not os.path.isdir(d):
        return out
    for f in os.listdir(d):
        if not f.endswith(".jsonl") or shas["studio"][:9] not in f:
            continue
        for line in open(os.path.join(d, f), errors="replace"):
            try: r = json.loads(line)
            except ValueError: continue
            if r.get("kind") == "abort" and (r.get("tip") or {}).get("studio") == shas["studio"]:
                for n in r.get("droppedRed") or []:
                    out[n] = max(out.get(n, ""), r.get("ts") or "")
    return out


def recorded_rows(shas=None):
    """{suite} with a record that RAN at this tree (studio and irisgl shas, neither dirty) — what
    `gate-scope.sh --resume` does not run again. NOADMIT/NOTRUN are no run (ci_gate_check's rule). A row an
    ABORT dropped red is gate-scope's to re-run as a SOLO (owed_solos), never in the ordinary pass."""
    shas = shas or tree_shas()
    out, d = set(), log_dir()
    if shas.get("studio_dirty") or not os.path.isdir(d):
        return out
    for f in os.listdir(d):
        if not f.endswith(".jsonl") or shas["studio"][:9] not in f:
            continue
        for line in open(os.path.join(d, f), errors="replace"):
            try: r = json.loads(line)
            except ValueError: continue
            t = r.get("tip") or {}
            if (t.get("studio") == shas["studio"] and t.get("irisgl") == shas["irisgl"] and not t.get("studio_dirty")
                    and not t.get("irisgl_dirty") and not stale_build(t) and r.get("kind") != "verdict" and r.get("arm") is None
                    and r.get("verdict") not in ("NOADMIT", "NOTRUN") and not r.get("kind")):
                out.add(r.get("suite"))
    return out


SOLO_OWED = 3          # the solos a dropped red needs strictly after its abort (the flake law's 3/3)


def owed_solos(shas=None):
    """[suite] an abort at this tip dropped RED and fewer than SOLO_OWED solo retries have run STRICTLY AFTER
    it (GATE-COST-2 #9, fix round F3): such a row re-runs as a solo — 3x, tier `solo`, retry — never as a plain
    row in the next pass (a plain green after a dropped red would be a retry hiding a red). An interrupted solo
    pass (one or two runs) leaves it owed; a solo in the abort's own second does not count (records are
    stamped to the second: it cannot be shown to be after). The abort record keeps `droppedRed` for the judge,
    which demands the 3/3."""
    shas = shas or tree_shas()
    owed = dropped_red(shas)
    if not owed:
        return []
    after, d = {}, log_dir()
    for f in os.listdir(d):
        if not f.endswith(".jsonl") or shas["studio"][:9] not in f:
            continue
        for line in open(os.path.join(d, f), errors="replace"):
            try: r = json.loads(line)
            except ValueError: continue
            # a solo is a ROW's record that RAN (as recorded_rows counts): never a pool's arm records, never
            # a held NOADMIT try (round 2, A)
            if r.get("retry") and not r.get("kind") and r.get("suite") in owed and r.get("arm") is None \
                    and r.get("verdict") not in ("NOADMIT", "NOTRUN") \
                    and (r.get("tip") or {}).get("studio") == shas["studio"]:
                if (r.get("ts") or "") > owed[r["suite"]]:
                    after[r["suite"]] = after.get(r["suite"], 0) + 1
    return sorted(n for n in owed if after.get(n, 0) < SOLO_OWED)


def _listed_rows(cmd, cwd, env):
    """(the rows a ctest command line selects — the fixture setups it pulls in included — in ctest's
    order, the subset that takes part in a FIXTURE), or (None, set()) if it cannot list. Read from
    `<cmd> --show-only=json-v1`."""
    r = subprocess.run(f"{cmd} --show-only=json-v1", cwd=cwd, shell=True, capture_output=True, text=True, env=env,
                       errors="replace")
    try:
        tests = json.loads(r.stdout).get("tests", [])
    except ValueError:
        return None, set()
    rows, fx = [], set()
    for t in tests:
        props = {p_["name"]: p_["value"] for p_ in t.get("properties", [])}
        rows.append(t["name"])
        if any(props.get(k) for k in ("FIXTURES_SETUP", "FIXTURES_CLEANUP", "FIXTURES_REQUIRED")):
            fx.add(t["name"])
    return rows, fx


def _verbose(cmd):
    """(the command with -V instead of --output-on-failure, whether it asked for the red output)."""
    toks = cmd.split(" ")
    oof = "--output-on-failure" in toks
    toks = [t for t in toks if t != "--output-on-failure"]
    return " ".join(toks[:1] + ["-V"] + toks[1:]), oof


class _Run:
    """One run_ctest() call: the context its phases share (one run id, one sampler, one guard)."""

    def __init__(self, tier, lane, jobs, reasons, gating, rng, retry, labels, echo, env, root=None, build=None,
                 fallback=None):
        self.tier, self.lanes, self.reasons, self.gating, self.rng = tier, lane_list(lane), reasons or {}, gating, rng
        self.retry, self.labels, self.echo, self.env = retry, labels or {}, echo, env
        self.shas = tree_shas() if root is None else tree_shas(root)
        self.fallback = fallback
        self.overrides = law_overrides(env, tool_set=("JAH_VRAM_ALL",) if fallback else ())
        # WHAT THE BINARIES WERE BUILT FROM rides every record (GATE-COST-2 #8): the refusal never counts a
        # record of a stale build as the tip's run
        self.shas["built"] = built_from(build) if build else None
        self.box0 = {"jobs": jobs, "display": (env or os.environ).get("DISPLAY"), "gpu_clocks": gpu_clocks(),
                     "other_ctests": other_ctests(), "host": os.uname().nodename, "mem": box_mem()}
        # the gate's queue wait for the slot: a gate's own environment, or the env a whole-card hold handed this
        # run (hold_card exports it when IT took the slot: a --solo batch, a dropped-red solo)
        e_ = env or os.environ
        sw = e_.get("JAH_GATE_SLOT_WAIT_S") if e_.get("JAH_GATE_SLOT_HELD") else None
        try: self.slot_wait = float(sw) if sw is not None else None
        except ValueError: self.slot_wait = None
        self.run_id = f"{datetime.datetime.now().strftime('%Y%m%dT%H%M%S')}-{self.shas['studio'][:9]}"
        self.sampler = LoadSampler(); self.sampler.start()
        self.guard = DisplayGuard(env)
        self.recorded, self.dropped, self.path, self.dead = 0, [], None, None
        self.running = set()          # rows started and not ended (the abort record's inFlight)

    def costs(self, t_end):
        """GATE-LOG-1: {slot_wait_s, drain_s, hold_s} of a record ending at t_end — the gate's wait for
        the slot (None outside a slot), the phase's whole-card drain and how long the card had been
        held at the row's end (None when the row ran with per-row admission)."""
        e = self.env or os.environ
        drain = held = None
        try:
            if e.get("JAH_VRAM_DRAIN_S") is not None: drain = float(e["JAH_VRAM_DRAIN_S"])
            if e.get("JAH_VRAM_HELD_AT") is not None: held = round(max(0.0, t_end - float(e["JAH_VRAM_HELD_AT"])), 1)
        except ValueError:
            pass
        return {"slot_wait_s": self.slot_wait, "drain_s": drain, "hold_s": held}

    def say(self, text):
        if self.echo:
            sys.stdout.write(text + "\n"); sys.stdout.flush()

    def records(self, name, status, secs, t_end, load, text, at):
        """The row's record and its arms' — the fields run_ctest has always written."""
        targets, arms = _suite_facts(text)
        base = {"schema": SCHEMA, "run": self.run_id,
                "ts": datetime.datetime.fromtimestamp(t_end).astimezone().isoformat(timespec="seconds"),
                "suite": name, "tier": self.tier, "lanes": self.lanes, "range": self.rng, "tip": self.shas,
                "reason": self.reasons.get(name, self.tier), "gating": (self.gating(name) if self.gating else True),
                "retry": self.retry, "labels": sorted(self.labels.get(name, [])), "overrides": self.overrides,
                **({"fallback": self.fallback} if self.fallback else {}),
                "box": dict(self.box0, gpu_clocks=at.get("gpu_clocks", self.box0["gpu_clocks"]),
                            other_ctests=at.get("other_ctests", self.box0["other_ctests"]),
                            load=[round(x, 2) for x in load],
                            load_mean=self.sampler.mean(t_end - secs, t_end),
                            mem=at.get("mem", self.box0.get("mem")), queue_depth=at.get("queue_depth"),
                            census=at.get("census")),
                "source": "run"}
        # GATE-LOG-1: where the gate's wall went that was not a row's — the slot queue, the phase's
        # drain and hold of the whole card, the queue behind this gate at the row's start
        base.update(self.costs(t_end))
        if BATCH: base["batch"] = BATCH        # a batch candidate's gate or attribution: never a lane's own record
        v, st, bline = row_verdict(status, text, arms)
        wait, twait = lock_wait(text), token_wait(text)
        # a timing row's lock line and its admission line are the SAME wait: subtract it once
        queued = wait if wait is not None else twait
        row = dict(base, arm=None, verdict=v, status=st,
                   seconds=(round(max(0.0, secs - queued), 2) if queued is not None else secs), targets=targets)
        if wait is not None: row["lockWaitS"] = wait
        if twait is not None: row["tokenWaitS"] = twait
        if queued is not None: row["wallSeconds"] = secs
        if v != "PASS":
            fl = fail_line(text)
            if fl: row["failLine"] = fl
        if bline: row["budget"] = bline
        xid, arm_xid = xid_of(text, datetime.date.fromtimestamp(t_end).isoformat())
        row["xid"] = xid
        # the door's `xid-read:` is for THIS case only: no xid because the journal could not be read
        # (kernel_xid.FINDING, printed by supervise and by the pool runner) — journal_unreadable
        unreadable = bool(_UNREADABLE.search(text or ""))
        if unreadable: row["journal_unreadable"] = True
        noadmit = [a for a, av, _ in arms if av == "NOADMIT"]
        if v != "PASS" and v != "NOADMIT" and noadmit:
            row["noadmit_arms"] = noadmit
            if "failLine" not in row:
                row["failLine"] = f"NOADMIT arm(s), never ran: {' '.join(noadmit[:12])}"
        fx = forced_exit(text)
        if fx:
            row["forced_exit"], row["forced_exit_why"] = fx[0], fx[1]
            if fx[2] > 1: row["forced_exits"] = fx[2]
        mem = _mem_of(text)
        if mem is not None: row["mem"] = mem
        leak = _leaks_of(text)
        if leak is not None: row["leak"] = leak
        recs = [row]
        arm_mem, arm_find = _arm_mems(text), _arm_findings(text)
        for arm, av, s in arms:
            # an arm's reason: the selector's for that arm (`<row>::<arm>`), else its row's
            ar = self.reasons.get(f"{name}::{arm.split('.', 1)[-1]}", base["reason"])
            rec = dict(base, arm=arm, verdict=av, status=av, seconds=s, targets=None, reason=ar,
                       xid=arm_xid.get(arm))
            if unreadable: rec["journal_unreadable"] = True
            if arm in arm_mem: rec["mem"] = arm_mem[arm]
            rec.update(arm_find.get(arm, {}))
            recs.append(rec)
        return recs

    def phase(self, cmd, cwd, env, final, attempt=0):
        """Run one ctest line; each row's records are appended as it ends. A row that never got its
        admission (P5: NOADMIT, or a pool whose every arm was) is HELD BACK unless `final`, and
        returned to be re-queued. Returns (rc, reds, held)."""
        vcmd, oof = _verbose(cmd)
        linux = sys.platform.startswith("linux")
        # `exec`: the shell BECOMES ctest, so the group leader and the PDEATHSIG are ctest's own
        p = subprocess.Popen("exec " + vcmd, cwd=cwd, shell=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                             text=True, bufsize=1, errors="replace", env=env,
                             preexec_fn=_child_setup if linux else None, start_new_session=not linux)
        reaper = subprocess.Popen([sys.executable, "-c", _REAPER, str(os.getpid()), str(p.pid)],
                                  stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                                  start_new_session=True, close_fds=True)
        stop = threading.Event()
        try:
            return self._stream(p, oof, final, stop, attempt)
        finally:
            stop.set()
            if p.poll() is None:                 # an exception or a signal on the way out: never an orphan
                _kill_tree(p.pid)
                try: p.wait(timeout=20)
                except subprocess.TimeoutExpired: pass
            reaper.poll()

    def _stream(self, p, oof, final, stop, attempt=0):

        def watch():
            # every 2 s (JAH_DISPLAY_POLL_S: a test widens it to prove the row-end check alone)
            while not stop.wait(float(os.environ.get("JAH_DISPLAY_POLL_S", "2.0"))):
                why = self.guard.dead()
                if why and not self.dead:
                    self.dead = why
                    _kill_tree(p.pid)
                    return
        if self.guard.active:
            threading.Thread(target=watch, daemon=True).start()
        buf, size, pre, starts, reds, held = {}, {}, {}, {}, [], []
        for line in p.stdout:
            ln = line.rstrip("\n")
            m = _VLINE.match(ln)
            if m:
                i, txt = m.group(1), m.group(2)
                st = pre.get(i, True)          # True: the preamble; "env": inside its environment list
                if st:
                    if txt.startswith("Test timeout computed to be: "):
                        pre[i] = False; continue
                    if _PREAMBLE.match(txt):
                        pre[i] = "env" if txt.startswith("Environment variable") else True; continue
                    if st == "env" and txt.startswith(" "):
                        continue
                    pre[i] = False
                if size.get(i, 0) < OUTPUT_CAP:
                    buf.setdefault(i, []).append(txt)
                    size[i] = size.get(i, 0) + len(txt) + 1
                continue
            if _VNOISE.match(ln):
                continue
            if self.echo:
                sys.stdout.write(line); sys.stdout.flush()
            m = _START.match(ln)
            if m:
                bm, oc = box_mem(), other_ctests(p.pid)
                starts[m.group(1)] = {"other_ctests": oc, "gpu_clocks": gpu_clocks(),
                                      "mem": bm, "queue_depth": queue_depth(), "census": census(p.pid, bm, oc)}
                self.running.add(m.group(1))
                continue
            m = _RESULT.match(ln)
            if not m:
                continue
            name, status, secs = m.group(1), m.group(2), float(m.group(3))
            self.running.discard(name)
            idm = _RESULT_ID.match(ln)
            i = idm.group(1) if idm else ""
            text = "\n".join(buf.pop(i, []))
            pre.pop(i, None); size.pop(i, None)
            ok = verdict_of(status) == "PASS"
            if not ok and oof and self.echo and text:
                sys.stdout.write(text + "\n"); sys.stdout.flush()
            # NEVER A RECORD AGAINST A DEAD DISPLAY (P6): the row ended after the server did, or at it
            why = self.dead or self.guard.dead()
            if why:
                if not self.dead:
                    self.dead = why
                    _kill_tree(p.pid)
                d_ = {"suite": name, "status": status.strip("* "), "verdict": verdict_of(status)}
                if d_["verdict"] != "PASS" and fail_line(text):
                    d_["failLine"] = fail_line(text)
                self.dropped.append(d_)
                continue
            recs = self.records(name, status, secs, time.time(), os.getloadavg(), text, starts.get(name) or {})
            v = recs[0]["verdict"]
            arms = recs[1:]
            never = v == "NOADMIT" or (v != "PASS" and arms and all(a["verdict"] == "NOADMIT" for a in arms))
            if attempt or (never and not final):
                for r_ in recs: r_["requeued"] = attempt       # the try: 0 = the first run, k = the k-th re-queue
            if never and not final:
                # EVERY HELD TRY IS A RECORD (GATE-COST-2, the band-aid audit's #1): verdict NOADMIT with its
                # try number — never-ran to the refusal (ci_gate_check NEVER_RAN), counted by the reports. The
                # row is re-queued at the end of the run; only a final try can be anything but NOADMIT.
                recs[0]["verdict"] = "NOADMIT"
                self.path = append_records(recs, self.tier, self.shas["studio"])
                self.recorded += len(recs)
                held.append(name)
                continue
            if v != "PASS":
                reds.append(name)
            self.path = append_records(recs, self.tier, self.shas["studio"])
            self.recorded += len(recs)
        rc = p.wait()
        return rc, reds, held


def run_ctest(cmd, cwd, tier, lane, jobs, reasons=None, gating=None, rng=None, retry=False,
              labels=None, echo=True, env=None, exclude=None, whole_card=None, root=None, fallback=None):
    """Run a ctest command line (a string, as gate-scope prints it), stream its output, and append
    each row's records (+ its arms') to the run log AS THE ROW ENDS. Returns ctest's exit code — 0
    when every row's last run passed — or DISPLAY_LOST when the display died (ABORTED says why).

      * P8: the rows labelled `hygiene` (the lints and the selector's own rows: no display, no GPU)
        run FIRST, as their own CPU phase, at the same -j, before any GPU row starts;
      * P5: a row that got no admission within its wait is re-queued at the end of the run (normal
        admission, after the other rows), up to requeue_times(); only its last try is recorded;
      * P6: `exclude` = rows not to run (gate-scope --resume: those with a record at the tip); the
        ctest tree dies with the gate (F2: _child_setup, the reaper, on_signals);
      * P2: `whole_card` — the run takes EVERY VRAM token once and its rows run nested on them (one
        drain for the phase, not one per row); None = when every row it selects is a `timing` row
        (the serial phase, however it was started);
      * `lane`: the record's `lanes` — one name, a comma-separated list or a list (lane_list);
      * `root`: the tree whose shas the records carry (default this checkout; BATCH-GATE-1's
        attribution runs a lane's solos in that lane's worktree)."""
    global ABORTED
    ABORTED = None
    check_tier(tier)       # before the run, never after an hour of it
    if env is not None and os.environ.get("JAH_GATE_SLOT_HELD"):
        # a row of this gate that starts a gate of its own (a selector test) must never queue behind it
        env = dict(env, JAH_GATE_SLOT_HELD=os.environ["JAH_GATE_SLOT_HELD"])
    R = _Run(tier, lane, jobs, reasons, gating, rng, retry, labels, echo, env, root=root, build=cwd, fallback=fallback)
    stale = stale_build(R.shas)
    if stale:
        R.say(f"=== STALE BUILD: {cwd} was {stale} ({R.shas['studio'][:9]}) — its records will NOT count as the tip's "
              f"run (ci_gate_check); `cmake --build` first (a no-op build refreshes BUILT_FROM) ===")
    why = R.guard.dead()
    if why:
        ABORTED = f"=== GATE ABORTED before its first row: {why} — nothing ran, nothing recorded ==="
        R.say(ABORTED); R.sampler.stop()
        return DISPLAY_LOST
    scratch = tempfile.mkdtemp(prefix="gate-rows-")

    def listfile(name, rows):
        path = os.path.join(scratch, name)
        with open(path, "w") as f:
            f.write("".join(r + "\n" for r in rows))
        return path
    exclude = set(exclude or ())
    rows, fixtures = _listed_rows(cmd, cwd, env) if (exclude or labels) else (None, set())
    if whole_card is None:
        # the serial phase however it was started: a `-L "^timing$"` line (the fixtures it pulls in — a
        # fresh_home setup — carry no label), or rows that are all `timing`
        whole_card = bool(re.search(r"""-L\s+["']?\^?%s\$?["']?(\s|$)""" % TIMING_LABEL, cmd)) or (
            bool(rows) and all(TIMING_LABEL in (labels or {}).get(r, ()) for r in rows))
    card = []
    if whole_card and not (env or os.environ).get("JAH_VRAM_HELD"):
        card, held_env = _vram().hold_card(f"{'+'.join(lane_list(lane))} {tier} phase", log=sys.stdout)
        # THE CALLER'S ENVIRONMENT OVER THE HELD COPY (F5): JAH_POOL_ARMS and the rest survive the hold; the
        # hold's own keys (the card, its drain and hold start — GATE-LOG-1 —, the slot a whole-card hold takes:
        # GATE-COST-2) go over it
        env = dict(held_env, **(env or {}), **{k: held_env[k] for k in ("JAH_VRAM_HELD", "JAH_GATE_SLOT_HELD",
                                                                         "JAH_VRAM_DRAIN_S", "JAH_VRAM_HELD_AT")
                                               if k in held_env})
        R.env = env                # GATE-LOG-1: the phase's records read its drain and hold from here
        drained = _vram().LAST_DRAIN_TIMEOUT
        if drained:
            phase_record("drain-timeout", tier, lane, rng, tree_shas(), fallback="drain-timeout", **drained)
            R.fallback = "drain-timeout"           # every record of this run: not a whole-card measurement
    phases = [(cmd, None)]
    if rows is not None:
        skip = [r for r in rows if r in exclude]
        # a row in a FIXTURE stays with its partners in the GPU phase: excluded there, ctest would pull a
        # setup back in and run it twice (the merge read's worth-a-look 3; none is labelled hygiene today)
        cpu = [r for r in rows if r not in exclude and r not in fixtures and CPU_LABEL in (labels or {}).get(r, ())]
        gpu = [r for r in rows if r not in exclude and r not in cpu]
        if skip:
            R.say(f"=== --resume: {len(skip)} of {len(rows)} row(s) already have a record at this tip; "
                  f"running the other {len(rows) - len(skip)} ===")
        phases = []
        if cpu:
            phases.append((f"{cmd} --tests-from-file {listfile('cpu', cpu)}",
                           f"the CPU phase: {len(cpu)} lint/selector row(s), before any GPU row"))
        if gpu:
            out = skip + cpu
            phases.append((f"{cmd} --exclude-from-file {listfile('done', out)}" if out else cmd,
                           f"the GPU phase: {len(gpu)} row(s)" if cpu else None))
    rc, reds, held = 0, [], []
    try:
        for c, title in phases:
            if title: R.say(f"\n=== {title} ===")
            prc, preds, pheld = R.phase(c, cwd, env, final=requeue_times() == 0)
            if R.dead: break
            reds += preds; held += pheld
            if prc and not preds and not pheld: rc = rc or prc       # ctest's own error, no red row
            if prc < 0 or prc > 128:
                # ctest itself was killed (a signal; through the shell, 128 + it): what it finished is
                # recorded; nothing else starts
                R.say(f"\n=== ctest was killed (signal {-prc if prc < 0 else prc - 128}): the rows it finished are in the run log; the rest "
                      f"never ran — scripts/gate-scope.sh <range> --resume runs them ===")
                held = []
                break
        n = requeue_times()
        for k in range(1, n + 1):
            if not held or R.dead: break
            R.say(f"\n=== re-queued {len(held)} row(s) that got no admission (try {k + 1} of {n + 1}, normal "
                  f"admission, after the other rows): {' '.join(held[:12])}{' …' if len(held) > 12 else ''} ===")
            prc, preds, pheld = R.phase(f"{cmd} --tests-from-file {listfile('requeue%d' % k, held)}", cwd, env,
                                        final=k == n, attempt=k)
            if R.dead: break
            reds += preds; held = pheld
            if prc and not preds and not pheld: rc = rc or prc
    finally:
        R.sampler.stop()
        for fd in card:
            try: os.close(fd)
            except OSError: pass
        import shutil
        shutil.rmtree(scratch, ignore_errors=True)
    if R.path and echo:
        print(f"\nrun log: {R.recorded} record(s) -> {R.path}")
    if R.dead:
        # THE ABORT LEAVES A RECORD (GATE-COST-2, the band-aid audit's #5): one `kind: abort` record naming every
        # row that ended after the death with its status and FAIL line — no row's record (the refusal reads it
        # as nothing: suite "@abort"), but a red among them is never only a stdout line: --resume re-runs a
        # row dropped red even if it has an older record, and the abort record stays.
        reds_ = [d_ for d_ in R.dropped if d_["verdict"] != "PASS"]
        R.path = phase_record("abort", tier, lane, rng, R.shas, why=R.dead, dropped=R.dropped,
                              droppedRed=[d_["suite"] for d_ in reds_], inFlight=sorted(R.running),
                              run=R.run_id, **({"fallback": R.fallback} if R.fallback else {})) or R.path
        ABORTED = (f"=== GATE ABORTED: {R.dead} — the run was stopped; {R.recorded} record(s) were written before "
                   f"it, {len(R.dropped)} row(s) that ended after it were NOT recorded ({len(reds_)} of them red: "
                   f"{' '.join(d_['suite'] for d_ in reds_[:8])}) — the abort record lists them; on a live display: "
                   f"scripts/gate-scope.sh <range> --run --resume ===")
        R.say(ABORTED)
        return DISPLAY_LOST
    return rc or (8 if reds or held else 0)


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
                     "suite": m.group(1), "arm": None, "tier": tier, "lanes": lane_list(lane), "range": None,
                     "tip": {"studio": tip}, "reason": reason or f"imported from {path}",
                     "gating": True, "retry": False, "labels": [],
                     "verdict": verdict_of(m.group(2)), "status": m.group(2).strip("* "),
                     "seconds": float(m.group(3)), "targets": None,
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


# THE TREND CHECK (TEST-1 item 1; the perf audit's D2). The run log has carried a row's `target:` line since
# 2026-09-27 (every line, each its own series keyed by target_key, since V2-P0B) and nothing compared
# one merge's number with the last; gi.rt_reflect_cost went 0.39 -> 1.03 ms and sat four days unread (D1).
# `trend` reads each target line as a SERIES over tips and names the first tip of every STEP and the lane
# that brought it. It is a REPORT, never a verdict: a step gets the lead's
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


_MET = re.compile(r"\s*--\s*MET$")


def target_key(line):
    """A target line's KEY: its text with every number masked (`#`) and the ` -- MET` mark dropped,
    so one line keeps one key from tip to tip whatever it reads."""
    return re.sub(rf"(?<![\w.]){_NUM}", "#", _MET.sub("", (line or "").strip()))


def target_lines(r):
    """A record's target lines as [(key, line)], in print order. A record written before V2-P0B carries
    its one line in `target` (the 1-target case). Two lines with one key take their order (`<key> [2]`)."""
    lines = r.get("targets") or ([r["target"]] if r.get("target") else [])
    out, seen = [], {}
    for line in lines:
        k = target_key(line)
        seen[k] = seen.get(k, 0) + 1
        out.append((k if seen[k] == 1 else f"{k} [{seen[k]}]", line))
    return out


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
    """(suite, target key, condition) -> [(first ts, tip sha, lane, value, n readings)], tips in time
    order: every target line of a row is its own series."""
    acc = {}
    for r in _records(days):
        if r.get("arm") or (suites and r["suite"] not in suites):
            continue
        tip = (r.get("tip") or {}).get("studio") or "?"
        for tkey, line in target_lines(r):
            v = target_value(line)
            if v is None:
                continue
            key = (r["suite"], tkey, _condition(r))
            e = acc.setdefault(key, {}).setdefault(tip, {"ts": r.get("ts") or "", "lane": "+".join(r.get("lanes") or []) or "?", "v": []})
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


def query_trend(days=30, suites=None, tip=None, k=TREND_K, quiet_ok=False, record=None):
    """Prints every step (or, with `tip`, the steps whose first tip is `tip` plus where each of `suites`
    stands at it). Returns the number of steps printed."""
    series = trend_series(days, suites)
    anc = tip_ancestry({x[1] for rows in series.values() for x in rows}, days)
    n = 0
    lines = []
    for (suite, tkey, cond), rows in sorted(series.items()):
        for s in trend_steps(rows, anc, k=k):
            ts, t, lane, v, cnt = rows[s["i"]]
            if tip and not any(rows[j][1].startswith(tip[:9]) for j in [s["i"]] + (s.get("siblings") or [])):
                continue
            rel = (s["after"] / s["before"] - 1.0) * 100 if s["before"] else float("inf")
            good = f", last in band {s['good'][:9]}" if s.get("good") else ""
            sib = "".join(f"; sibling {rows[j][1][:9]} ({rows[j][2]})" for j in s.get("siblings") or [])
            if record:
                # A TREND STEP IS A RECORD (GATE-COST-2 #11), not only a line: (tier, lane) of the gate that saw it
                phase_record("trend-step", record[0], record[1], None, tree_shas(), row=suite, target=tkey,
                             delta={"before": s["before"], "after": s["after"],
                                    "rel": None if rel == float("inf") else round(rel, 2)},
                             step=s["kind"], condition=str(cond), firstTip=t)
            lines.append(f"  {s['kind']:11s} {suite:34s} {s['before']:.4g} -> {s['after']:.4g} ({rel:+.0f} %, "
                         f"band +-{s['band']:.3g}; read {', '.join(f'{x:.4g}' for x in s['readings'])}) "
                         f"first at {t[:9]} ({lane}, {ts[:16]}{good}{sib}) [{cond}] {tkey}")
            n += 1
    if lines or not quiet_ok:
        print(f"TREND (target rows, last {days} d; k={k} x MAD of the trailing {TREND_WINDOW} tips in one "
              f"condition{'' if anc is not None else '; NO COMMIT GRAPH: time order'}; NON-GATING: a step "
              f"gets the lead's verdict):")
        print("\n".join(lines) if lines else "  no step")
    return n


PROMOTE_AFTER = 3


def promotions(days=30, tip=None):
    """THE PROMOTION LIST (the deep audit's T6): (suite, key) of every target line of a target row
    that read green at the last PROMOTE_AFTER tips it ran at, the newest being `tip` when given. Green
    = the line says MET, or its row PASSed; a `bar none` line has nothing to promote. Information
    only: the lead decides a promotion."""
    acc = {}
    for r in _records(days):
        if r.get("arm") or r.get("gating", True):
            continue
        t = (r.get("tip") or {}).get("studio") or "?"
        for k, line in target_lines(r):
            if "bar none" in line:
                continue
            e = acc.setdefault((r["suite"], k), {}).setdefault(t, {"ts": r.get("ts") or "", "green": True})
            e["ts"] = min(e["ts"], r.get("ts") or e["ts"])
            e["green"] &= bool(_MET.search(line.strip())) or r.get("verdict") == "PASS"
    out = []
    for key, tips in sorted(acc.items()):
        last = sorted(tips.items(), key=lambda x: x[1]["ts"])[-PROMOTE_AFTER:]
        if len(last) == PROMOTE_AFTER and all(e["green"] for _, e in last) \
                and (not tip or last[-1][0].startswith(tip[:9])):
            out.append(key)
    return out


def trend_at_gate_end(tip=None, tier=None, lane=None):
    """THE GATE'S TREND LINE (TEST-1): every gate prints, after its verdict, the target lines whose
    reading at THIS tip left its history's band — the day a step lands it is one UNCONFIRMED reading —
    and a PROMOTE line for every target line green at its last PROMOTE_AFTER tips, this one the newest.
    A report: it never changes an exit code and never raises."""
    try:
        tip = tip or tree_shas()["studio"]
        query_trend(30, None, tip, record=(tier, lane) if tier else None)
        for suite, key in promotions(30, tip):
            print(f"PROMOTE {suite} {key}    (green at its last {PROMOTE_AFTER} tips; information only)")
    except Exception as e:             # a report must never break the gate it reports on
        print(f"TREND: not computed ({type(e).__name__}: {e})")


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    r = sub.add_parser("run", help="run a ctest command and log every suite")
    r.add_argument("--tier", required=True, choices=TIERS)
    r.add_argument("--lane", action="append", default=None,
                   help="the record's lanes: repeat it or give a comma-separated list (a batch's lanes)")
    r.add_argument("--jobs", type=int, default=None)
    r.add_argument("--build", default="build-linux")
    r.add_argument("ctest", nargs=argparse.REMAINDER)
    i = sub.add_parser("import", help="records from an existing ctest output log")
    i.add_argument("log"); i.add_argument("--tier", required=True, choices=TIERS); i.add_argument("--tip", required=True)
    i.add_argument("--lane", action="append", default=None); i.add_argument("--jobs", type=int, default=None)
    t = sub.add_parser("times", help="median PASS seconds per suite from the log")
    t.add_argument("--days", type=int, default=14)
    q = sub.add_parser("longest", help="the longest suites/arms by median PASS seconds")
    q.add_argument("--days", type=int, default=7); q.add_argument("-n", type=int, default=10)
    q2 = sub.add_parser("load-reds", help="red in a gate, green solo at the same tip")
    q2.add_argument("--days", type=int, default=7)
    q4 = sub.add_parser("clocks", help="the GPU's clock state now (exit 3 when it reads locked)")
    hm = sub.add_parser("hash-move", help="record a selftest hash that moved off its record (kind: hash-move)")
    hm.add_argument("--tier", required=True, choices=TIERS); hm.add_argument("--lane", default=None)
    hm.add_argument("--pose", required=True); hm.add_argument("--old", required=True); hm.add_argument("--new", required=True)
    q3 = sub.add_parser("trend", help="steps in the target rows' readings across tips (non-gating)")
    q3.add_argument("--days", type=int, default=30); q3.add_argument("--k", type=float, default=TREND_K)
    q3.add_argument("--tip", default=None, help="only the steps whose first tip is this sha")
    q3.add_argument("--suite", action="append", default=None)
    q3.add_argument("--record", nargs=2, metavar=("TIER", "LANE"), default=None,
                    help="also write every step as a `kind: trend-step` record under TIER/LANE")
    a = ap.parse_args()
    if a.cmd == "run":
        cmd = a.ctest[1:] if a.ctest and a.ctest[0] == "--" else a.ctest
        if not cmd: ap.error("give the ctest command after --")
        bad = fork_pin_problem()
        if bad:
            sys.stderr.write(bad + "\n"); sys.exit(4)
        build = a.build if os.path.isabs(a.build) else os.path.join(os.getcwd(), a.build) \
            if os.path.isdir(os.path.join(os.getcwd(), a.build)) else os.path.join(ROOT, a.build)
        lane = lane_list(a.lane) or [_git(["rev-parse", "--abbrev-ref", "HEAD"])]
        jobs = a.jobs
        if jobs is None:
            m = re.search(r"-j\s*(\d+)", " ".join(cmd)); jobs = int(m.group(1)) if m else None
        # one argument = a command string exactly as gate-scope.py --merge-tier prints it (its -LE
        # alternation carries `|`, so it must reach the shell as ONE string); several = an argv
        line = cmd[0] if len(cmd) == 1 else shlex.join(cmd)
        # A `run` IS A GATE (GATE-COST-1 P1): one at a time, box-wide — it queues for the slot first
        bad = prebuild(build)
        if bad:
            sys.stderr.write(bad + "\n"); sys.exit(5)
        on_signals()
        slot = None
        try:
            slot = _vram().gate_slot(f"{'+'.join(lane)} {a.tier} (gate_runlog run)", log=sys.stdout)
            rc = run_ctest(line, build, a.tier, lane, jobs, labels=inventory_labels(build))
        except GateSignal as e:
            print(f"\n=== GATE ABORTED: signal {e.sig} — the ctest tree was stopped, the slot and the card released ===")
            rc = 128 + e.sig
        finally:
            if slot is not None:
                os.close(slot)
        sys.exit(rc)
    if a.cmd == "import":
        import_log(a.log, a.tier, a.tip, a.lane, a.jobs); return
    if a.cmd == "longest":
        query_longest(a.days, a.n); return
    if a.cmd == "load-reds":
        query_load_reds(a.days); return
    if a.cmd == "trend":
        if a.record:
            check_tier(a.record[0])
        query_trend(a.days, a.suite, a.tip, a.k, record=tuple(a.record) if a.record else None); return
    if a.cmd == "hash-move":
        # A HASH MOVE IS A RECORD (GATE-COST-2 #11): rc-gate.sh writes one per pose that left its record
        path = phase_record("hash-move", a.tier, a.lane or _git(["rev-parse", "--abbrev-ref", "HEAD"]), None,
                            tree_shas(), pose=a.pose, old=a.old, new=a.new)
        print(f"hash-move: pose {a.pose} {a.old[:12]} -> {a.new[:12]} recorded -> {path}"); return
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
