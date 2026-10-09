#!/usr/bin/env python3
"""ci_gate_check — THE MERGE REFUSAL behind scripts/ci-gate-check.sh (MODULAR-GATE-1 T6; the law
since TEST-SELECTOR-1 L2/L3, docs/TESTING_GATE.md §4).

Given a range, it re-derives the scoped selection (gate-scope.py, the same code the lane ran — the
lane's OWN diff across forward merges) and reads THE RUN LOG at the range's tip (the studio sha,
irisgl at the sha the tip pins, neither tree dirty). Every selected row — and, for a pool selected
in part, every selected arm — must have been run there, and THE FLAKE LAW decides its reds:

  * no red record                      -> green;
  * a CONTENTION-CLASS suite (the one list, <workspace>/testing/contention.json — data, read here
    and by `gate-scope.sh --solo`) that went red -> needs 3/3 SOLO retries after its last gate red,
    every one PASS (a solo red means it is not the contention class: it needs a verdict);
  * any other red                      -> needs a RECORDED VERDICT (below);
  and a row never run at the tip is refused, whatever else holds (a verdict does not stand in for
  a run). One solo PASS no longer erases a red (the audit's L2: gi.field_scroll PASS/FAIL/PASS
  beside a sibling was accepted).

THE FIX ROUND RE-USES WHAT IT CANNOT REACH (GATE-SPEED-1 item 1; TESTING_GATE §3b made mechanical —
the owner, 2026-09-11: "no double checking"). A row with no record at the tip takes the NEWEST record
of that row at an earlier commit A OF THE LANE (its own commits in base..tip: not the integration
line a forward merge carried in), provided
  (a) the scoped selection of A..tip — the same selector, the same reach — does NOT include the row
      (a pool's arm: does not include that arm), and
  (b) the fork pin is the same at A and at the tip, and A..tip neither falls back nor is the tier by
      rule (a pin change invalidates every earlier record: §7b rule 4 stays);
and that newest record decides: green re-uses it; red (with no verdict, or a contention red without
its 3/3) refuses — a later red blocks the older green, and is answered where it happened
(`--verdict` records it at that commit). A row the fix reaches needs its record at the tip, as before.
Every row prints the commit its record came from. Before this lane the key was the EXACT tip, so
every fix round re-ran the lane's whole selection: 45.9 % of all suite-time since 09-28 (the
gate-speed audit's R1). A miss is a selector defect, which is already the law's answer.

A range that MOVES THE FORK PIN needs the whole MERGE tier at the tip (§7b rule 4: the one full
tier per bump, `gate-scope.sh <range> --run --fork-tier`; the lane's own gates select by the fork
diff's reach). So does a selection that fell back or is the tier by rule.

THE VERDICT IS PER ROW AND TIMESTAMPED: `--verdict "<row>=<text>" ...` records one `VERDICT` record
per named red row into the run log (`<date>-verdict-<tip>.jsonl`, the same directory) and
re-checks; it clears only the reds logged before it. A row that never ran (NOADMIT, NOTRUN) is
MISSING, which no verdict clears. scripts/lead/merge-dbuild-lane.sh calls this
and refuses the merge on a failure; its own `--verdict` passes through here.

THE VERDICT DOOR (VERDICT-1 U1/U3; TESTING_GATE §4): a verdict's text is parsed and its token CHECKED —
`real:<DEFECT-ID> fixed` (the row's PASS at the tip) or `pre-existing` (a recorded solo red at the base);
`contention:` (FAIL/TIMEOUT of a LISTED row whose red shows a measured competitor, with 3/3 solo PASS after it);
`xid-read:<journal window>` (LOST/OOM/CRASH whose journal was unreadable, covering the red); a red whose record
carries an `xid` takes `real:` only; solos below 3/3 are never cleared by text. A refused verdict
prints `VERDICT REFUSED <row>: <why>`. A REBASE CARRIES ITS OPEN REDS (U4): every schema-2 record of the lane's name
(`lane` or `lanes`) at any other tip is read, and an open red there refuses the tip (`OPEN RED carried from <tip>`)
until the row runs green at the tip or a verdict at that tip passes the door. THE LIST'S SHAPE (U5): an entry
without {reason, date, recheck} makes the list unusable (exit 2).

Exit 0 accepted, 1 refused, 2 unusable (no contention list or one without its shape, an unresolvable range).
"""
import argparse
import datetime
import importlib.util
import json
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import gate_runlog  # noqa: E402

SOLO_NEEDED = 3


def load_gs():
    spec = importlib.util.spec_from_file_location("gate_scope", os.path.join(HERE, "gate-scope.py"))
    gs = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(gs)
    return gs


def records_by_tip(pins):
    """{tip: {(suite, arm): [records, oldest first]}} for every tip in `pins` ({studio sha: the irisgl
    sha it pins}) — clean tree, irisgl at THAT tip's pin — read in ONE pass over the log, the verdict
    records included (kind == "verdict")."""
    out = {t: {} for t in pins}
    d = gate_runlog.log_dir()
    if not os.path.isdir(d): return out
    for f in sorted(os.listdir(d)):
        if not f.endswith(".jsonl"): continue
        for line in open(os.path.join(d, f)):
            try: r = json.loads(line)
            except ValueError: continue
            t = r.get("tip") or {}
            sha = t.get("studio")
            if sha not in out or t.get("studio_dirty") or t.get("irisgl_dirty"): continue
            # the engine that ran must be the one the tip PINS (F1): a record from a tree whose
            # irisgl was checked out elsewhere tested other code
            if pins[sha] and t.get("irisgl") != pins[sha]: continue
            out[sha].setdefault((r.get("suite"), r.get("arm")), []).append(r)
    for recs in out.values():
        for v in recs.values():
            v.sort(key=lambda r: r.get("ts") or "")
    return out



def lane_records(lanes, skip):
    """VERDICT-1 U4 — A REBASE CARRIES ITS OPEN REDS: {tip: {(suite, arm): [records]}} of every gating
    record of a lane named in `lanes` (`lane` or BATCH-GATE-1's `lanes` list — both shapes) at ANY tip
    not in `skip`, ancestor or not, with every verdict record at those tips. Forward only: run records
    of schema >= 2 (written since this rule landed) — the historic cases are gate-report.py --carried."""
    out, verdicts = {}, {}
    d = gate_runlog.log_dir()
    if not lanes or not os.path.isdir(d): return out
    for f in sorted(os.listdir(d)):
        if not f.endswith(".jsonl"): continue
        for line in open(os.path.join(d, f), errors="replace"):
            try: r = json.loads(line)
            except ValueError: continue
            t = r.get("tip") or {}
            sha = t.get("studio")
            if not sha or sha in skip or t.get("studio_dirty") or t.get("irisgl_dirty"): continue
            if r.get("kind") == "verdict":
                verdicts.setdefault(sha, []).append(r); continue
            if (r.get("schema") or 1) < 2 or r.get("gating") is False or r.get("tier") == "target": continue
            if not (gate_runlog.record_lanes(r) & lanes): continue
            out.setdefault(sha, {}).setdefault((r.get("suite"), r.get("arm")), []).append(r)
    for sha, recs in out.items():
        for v in verdicts.get(sha, []):
            k = (v.get("suite"), v.get("arm"))
            if k in recs: recs[k].append(v)
    return out


def _when(r):
    """A record's time, comparable across the log's two spellings (with and without an offset)."""
    ts = r.get("ts") or ""
    try:
        t = datetime.datetime.fromisoformat(ts)
    except ValueError:
        return datetime.datetime.min.replace(tzinfo=datetime.timezone.utc)
    return t if t.tzinfo else t.astimezone()


NEVER_RAN = ("NOADMIT", "NOTRUN")
HARD = ("LOST", "OOM", "CRASH")          # the classes that are never cleared without a defect id or a journal read

# THE VERDICT DOOR (VERDICT-1 U1; ONE_PICTURE_SPEC H1; the lead's band-aid addendum): a verdict's TEXT is parsed for a
# class token, and every token is CHECKED against the run log, never trusted:
#   real:<DEFECT-ID> fixed       the lane fixed it: a PASS record of the row AT THE TIP after the red is required
#   real:<DEFECT-ID> pre-existing  the defect predates the lane: the same red REPRODUCED ON THE BASE (a recorded solo
#                                at the range's base, `gate-scope.sh --solo` there) is required
#   contention:<evidence>        only for a row with a DATED entry in contention.json, whose red record(s) show a
#                                MEASURED competitor (box.other_ctests > 0, box.queue_depth > 0, or a whole-card drain
#                                drain_s > 0), and with 3/3 solo PASS after the red; an unlisted row: never
#   xid-read:<window>            a LOST/OOM/CRASH whose record carries NO xid BECAUSE THE JOURNAL WAS UNREADABLE
#                                (journal_unreadable) — the reader read it by hand over that window (covering the red)
DEFECT_ID = re.compile(r"\breal:\s*([A-Z][A-Z0-9]*(?:-[A-Z0-9]+)*-\d+[a-z]?)\b")
REAL_FIXED = re.compile(r"\bfixed\b", re.I)
REAL_BASE = re.compile(r"\bpre-?existing\b|\bon (the )?base\b", re.I)
CONTENTION_TOKEN = re.compile(r"\bcontention:\s*\S")
XID_READ = re.compile(r"\bxid-read:\s*(\d{4}-\d\d-\d\dT\d\d:\d\d(?::\d\d)?(?:[+-]\d\d:?\d\d)?)\.\."
                      r"((?:\d{4}-\d\d-\d\dT)?\d\d:\d\d(?::\d\d)?(?:[+-]\d\d:?\d\d)?)")
ENVIRONMENTAL = re.compile(r"\benvironment(al)?\b", re.I)


def _xid_window(text):
    """(start, end) datetimes of a verdict's `xid-read:<start>..<end>` (an end without a date takes
    the start's), or None when the text carries no readable window."""
    m = XID_READ.search(text or "")
    if not m: return None
    try:
        a = datetime.datetime.fromisoformat(m.group(1))
        b_txt = m.group(2) if "T" in m.group(2) else f"{m.group(1).split('T')[0]}T{m.group(2)}"
        b = datetime.datetime.fromisoformat(b_txt)
    except ValueError:
        return None
    a = a if a.tzinfo else a.astimezone()
    b = b if b.tzinfo else b.astimezone()
    return (a, b) if b >= a else None


def competitor(r):
    """The measured competitor a red record shows (GATE-LOG-1's fields), or None."""
    box = r.get("box") or {}
    if (box.get("other_ctests") or 0) > 0: return f"{box['other_ctests']} sibling ctest(s)"
    if (box.get("queue_depth") or 0) > 0: return f"{box['queue_depth']} gate(s) queued for the slot"
    if (r.get("drain_s") or 0) > 0: return f"a whole-card drain of {r['drain_s']} s"
    return None


def _real(m, text, reds, tip_recs, base_recs):
    """`real:<id>` checked: (ok, why)."""
    last = max(_when(r) for r in reds)
    if REAL_FIXED.search(text):
        if tip_recs is None:
            return False, f"real:{m.group(1)} fixed — the judge cannot read the tip's records here: unverified"
        if any(r.get("kind") != "verdict" and r.get("verdict") == "PASS" and _when(r) > last for r in tip_recs):
            return True, f"real:{m.group(1)} fixed (a PASS of the row at the tip after the red)"
        return False, (f"real:{m.group(1)} fixed — but no PASS record of the row at the tip after the red: a claimed "
                       f"fix is proved by its row passing at the tip")
    if REAL_BASE.search(text):
        if base_recs is None:
            return False, f"real:{m.group(1)} pre-existing — the judge has no base records here: unverified"
        if any(r.get("kind") != "verdict" and r.get("retry") and r.get("verdict") not in ("PASS",) + NEVER_RAN
               for r in base_recs):
            return True, f"real:{m.group(1)} pre-existing (the red reproduced by a recorded solo at the base)"
        return False, (f"real:{m.group(1)} pre-existing — but no recorded solo red of the row at the BASE: reproduce it "
                       f"there (gate-scope.sh --solo <row> on the base tree) first")
    return False, (f"real:{m.group(1)} names a ticket only — say `fixed` (a PASS at the tip proves it) or "
                   f"`pre-existing` (a solo red at the base proves it); naming a ticket alone clears nothing")


def door(text, reds, solos, listed=False, tip_recs=None, base_recs=None):
    """(ok, why) — may this verdict text clear these reds (the open reds logged before it)? `solos`:
    the solo retries after the reds' last gate red (any time — a verdict may be written before its
    solos run); `listed`: the row has a (dated) contention entry; `tip_recs` / `base_recs`: the row's
    records at the range's tip and base (None: unknown — a real: claim is then unverified).
    The rules, in order (each refusal names its rule):
      0. a red whose record carries an Xid from the row's own process tree is a DEFECT by law: only a
         CHECKED `real:<id>` clears it;
      1. solos below 3/3 are NEVER cleared by text, listed or not;
      2. LOST / OOM / CRASH: a checked `real:<id>`, or `xid-read:<window>` ONLY when the record's xid is
         absent because the journal was unreadable; `environmental` on a LOST is refused;
      3. FAIL / TIMEOUT: a checked `real:<id>`, or `contention:` on a LISTED row whose red shows a measured
         competitor, with 3/3 solo PASS after the red."""
    text = text or ""
    kinds = {r.get("verdict") for r in reds}
    real = DEFECT_ID.search(text)
    xid = [r for r in reds if r.get("xid")]
    if xid:
        if real: return _real(real, text, reds, tip_recs, base_recs)
        x = xid[-1]["xid"]
        return False, (f"the red carries an Xid from the row's own process (pid {x.get('pid')}, "
                       f"{x.get('window')}) — a DEFECT by law, never environmental: only real:<defect id> clears it")
    spass = sum(1 for r in solos if r.get("verdict") == "PASS")
    if solos and (len(solos) < SOLO_NEEDED or spass < len(solos)):
        return False, (f"solos below 3/3 ({spass}/{len(solos)} solo PASS after the red) are never cleared by text, "
                       f"listed or not — run --solo to 3/3, or fix it")
    hard = kinds & set(HARD)
    if hard:
        cls = "/".join(sorted(hard))
        if "LOST" in hard and ENVIRONMENTAL.search(text):
            return False, (f"a LOST is never environmental (ONE_PICTURE H1): the verdict on a {cls} carries "
                           f"real:<defect id> (checked) or, when the journal was unreadable, xid-read:<window>")
        if real: return _real(real, text, reds, tip_recs, base_recs)
        w = _xid_window(text)
        if w:
            if not all(r.get("journal_unreadable") for r in reds if r.get("verdict") in HARD):
                return False, (f"xid-read: is for a {cls} whose record has no xid BECAUSE the journal was unreadable — "
                               f"this record's journal was read (xid: null = no Xid of the row): only real:<defect id>")
            last, first = max(_when(r) for r in reds), min(_when(r) for r in reds)
            if w[0] <= last and first - datetime.timedelta(hours=1) <= w[1]:
                return True, f"xid-read:{w[0].isoformat(timespec='minutes')}..{w[1].isoformat(timespec='minutes')}"
            return False, (f"the xid-read window {w[0].isoformat(timespec='minutes')}..{w[1].isoformat(timespec='minutes')} "
                           f"does not cover the {cls} at {last.isoformat(timespec='minutes')}")
        return False, (f"a verdict on a {cls} carries a checked real:<defect id> (fixed / pre-existing), or "
                       f"xid-read:<journal window> when the journal was unreadable — no other text clears it")
    if real: return _real(real, text, reds, tip_recs, base_recs)
    if CONTENTION_TOKEN.search(text):
        if not listed:
            return False, ("contention: clears only a row with a dated entry in contention.json — this row is not "
                           "listed: real:<defect id>, or the list gains it by its own verdict first")
        bare = [r for r in reds if not r.get("retry") and not competitor(r)]
        if bare:
            return False, (f"contention: needs a MEASURED competitor in the red's record (box.other_ctests > 0, "
                           f"box.queue_depth > 0 or drain_s > 0) — the red at {_when(bare[-1]).isoformat(timespec='minutes')} "
                           f"shows none")
        if spass >= SOLO_NEEDED:
            return True, f"contention: ({competitor([r for r in reds if not r.get('retry')][-1])}) with {spass}/{len(solos)} solo PASS"
        return False, (f"contention: needs the row's 3/3 solo PASS after the red ({spass}/{len(solos)}) — "
                       f"gate-scope.sh --solo <row>")
    return False, ("a verdict on a FAIL/TIMEOUT carries a class token: real:<defect id> fixed|pre-existing, or "
                   "contention:<evidence> (a listed row, a measured competitor, 3/3 solo) — any other text clears nothing")


def judge(key, recs, contention, tip_recs=None, base_recs=None):
    """(state, why) for one row/arm: state 'green' | 'missing' | 'red'.

    A run that never happened (NOADMIT: the admission's bound; NOTRUN) is no run: a row with only
    those is MISSING, which no verdict clears. A VERDICT is per row and timestamped: it clears
    only the reds logged BEFORE it — a red after it needs its own answer (the merge read's D4) —
    and only through THE DOOR (door() above): a refused verdict leaves its reds open, and the why
    begins `VERDICT REFUSED: `."""
    runs = sorted((r for r in recs if r.get("kind") != "verdict" and r.get("verdict") not in NEVER_RAN), key=_when)
    verdicts = sorted((r for r in recs if r.get("kind") == "verdict"), key=_when)
    if not runs:
        never = [r.get("verdict") for r in recs if r.get("verdict") in NEVER_RAN]
        return "missing", "never run at the tip" + (f" ({never[-1]}: it never ran)" if never else "")
    reds = [r for r in runs if r.get("verdict") != "PASS"]
    if not reds:
        return "green", ""
    # the verdicts in time order: each one that passes the door clears the reds logged before it
    cleared, accepted, refused = None, None, None
    for v in verdicts:
        tv = _when(v)
        pending = [r for r in reds if _when(r) < tv and (cleared is None or _when(r) > cleared)]
        if not pending: continue
        last_gate = max((_when(r) for r in pending if not r.get("retry")), default=None)
        solos = [r for r in runs if r.get("retry") and last_gate is not None and _when(r) > last_gate]
        name, arm = key
        ok, why = door(v.get("text"), pending, solos, listed=name in contention or bool(arm and arm in contention),
                       tip_recs=recs if tip_recs is None else tip_recs, base_recs=base_recs)
        if ok:
            cleared, accepted, refused = tv, (v, why), None
        else:
            refused = (v, why)
    open_reds = [r for r in reds if cleared is None or _when(r) > cleared]
    if not open_reds:
        v, why = accepted
        return "green", f"recorded verdict ({why}): " + (v.get("text") or "")[:120]
    if refused is not None:
        return "red", f"VERDICT REFUSED: {refused[1]} — the verdict read: " + (refused[0].get("text") or "")[:100]
    reds = open_reds
    name, arm = key
    listed = name in contention or (arm and arm in contention)
    gate_reds = [r for r in reds if not r.get("retry")]
    last_gate_red = max((_when(r) for r in gate_reds), default=None)
    solos = [r for r in runs if r.get("retry") and (last_gate_red is None or _when(r) > last_gate_red)
             and (cleared is None or _when(r) > cleared)]
    solo_reds = [r for r in solos if r.get("verdict") != "PASS"]
    hard = sorted({r.get("verdict") for r in reds} & set(HARD))
    xid = [r for r in reds if r.get("xid")]
    if xid:
        return "red", (f"{reds[-1].get('verdict')} with an Xid from the row's own process (pid "
                       f"{xid[-1]['xid'].get('pid')}) — a DEFECT by law: needs a verdict real:<defect id>")
    if not listed:
        return "red", (f"{reds[-1].get('verdict')} and not in the contention class — needs a recorded verdict"
                       + (f" ({len(solos) - len(solo_reds)}/{len(solos)} solo PASS do not clear it)" if solos else ""))
    if hard:
        return "red", (f"{'/'.join(hard)} on a contention-class row: a {'/'.join(hard)} is never cleared by solos — "
                       f"needs a verdict real:<defect id> or xid-read:<journal window>")
    if solo_reds:
        return "red", (f"contention-class, but a SOLO retry went red ({len(solos) - len(solo_reds)}/{len(solos)}): "
                       f"not contention — needs a recorded verdict")
    if not gate_reds:
        return "red", f"contention-class, red only in its solo retries — needs a recorded verdict"
    if len(solos) < SOLO_NEEDED:
        return "red", f"contention-class: {len(solos)}/{SOLO_NEEDED} solo PASS after its gate red (the law is 3/3)"
    return "green", f"contention-class, {len(solos)}/{len(solos)} solo PASS after the red"


def needed_rows(gs, S):
    """[(row, arm|None)] the refusal asks a record for, and the name of the selection."""
    inv = S.inv
    gating = lambda n: not (inv[n]["labels"] & (gs.SCOPE_EXCLUDED_LABELS | gs.TARGET_LABELS))
    if S.fallback or S.full_tier or S.fork_bump:
        rows = [n for n, t in inv.items() if not (t["labels"] & (gs.NIGHTLY_LABELS | gs.TARGET_LABELS))]
        what = ("the MERGE tier (the fork pin moved: §7b rule 4's one full tier, `--fork-tier`)" if S.fork_bump
                and not (S.fallback or S.full_tier) else
                "the MERGE tier (%s)" % ("fallback" if S.fallback else "by rule"))
        return [(n, None) for n in rows], what
    subsets = S.arm_subsets()
    need = []
    for n in sorted(S.selected):
        if not gating(n): continue
        need.append((n, None))
        for arm in subsets.get(n, []):
            need.append((n, f"{inv[n]['pool']}.{arm}"))
    return need, "the scoped selection"


def _git(args, cwd):
    r = subprocess.run(["git"] + args, cwd=cwd, capture_output=True, text=True)
    return r.stdout.strip() if r.returncode == 0 else ""


def lane_commits(gs, base, tip_sha):
    """The lane's OWN earlier commits, newest first: base..tip without the tip, minus the integration
    line (a d-build commit a forward merge carried in is not a commit of this lane)."""
    line = gs.integration_line()
    revs = _git(["rev-list", "--topo-order", f"{base}..{tip_sha}"], gs.ROOT).split()
    return [c for c in revs if c != tip_sha and c not in line]


_FORK_PINS = {}


def fork_pin(gs, irisgl_sha):
    """The ogre-next commit an irisgl commit pins ('' when it cannot be read: no re-use across it).
    Cached: the refusal asks it once per row per earlier commit."""
    if not irisgl_sha: return ""
    if irisgl_sha not in _FORK_PINS:
        _FORK_PINS[irisgl_sha] = _git(["rev-parse", f"{irisgl_sha}:thirdparty/ogre-next"],
                                      os.path.join(gs.ROOT, "irisgl"))
    return _FORK_PINS[irisgl_sha]


class Reach:
    """What the scoped selection of <A>..<tip> reaches, per earlier commit A (computed once each, on
    one build graph and one inventory). ALL = every row: a fork pin change, a fallback, the tier by
    rule, or a range the selector cannot read (no re-use across it — the safe side)."""
    ALL = None

    def __init__(self, gs, build, tip_sha, tip_fork):
        import copy
        import gate_graph
        self.gs, self.build, self.tip, self.tip_fork, self.copy = gs, build, tip_sha, tip_fork, copy
        self.graph = gate_graph.NinjaGraph.load(build)
        self.inv0 = gs.load_inventory(build)
        self.cache = {}

    def of(self, a, a_fork):
        if a in self.cache: return self.cache[a]
        reach = Reach.ALL
        if a_fork and a_fork == self.tip_fork:
            rng = f"{a}..{self.tip}"
            try:
                S = self.gs.select(self.gs.touched_paths(rng), rng, self.build, self.gs.GATE_JOBS, graph=self.graph,
                                   inv=self.copy.deepcopy(self.inv0), quiet_graph=True)
                if not (S.fallback or S.full_tier or S.fork_bump):
                    subsets = S.arm_subsets()
                    reach = set()
                    for n in S.selected:
                        reach.add((n, None))
                        pool = S.inv[n].get("pool")
                        if n in subsets:
                            reach |= {(n, f"{pool}.{arm}") for arm in subsets[n]}
                        elif pool:
                            reach.add((n, "*"))      # the whole pool: every arm
            except SystemExit:
                reach = Reach.ALL                    # an unreadable or empty range: nothing is re-used
        self.cache[a] = reach
        return reach

    @staticmethod
    def reaches(reach, key):
        if reach is Reach.ALL: return True
        return key in reach or (key[1] is not None and (key[0], "*") in reach)


def record_verdicts(pairs, tip_sha, pin):
    """[(key, text)] -> one VERDICT record per row, stamped now."""
    now = datetime.datetime.now().astimezone()
    path = os.path.join(gate_runlog.log_dir(), f"{now.date().isoformat()}-verdict-{tip_sha[:9]}.jsonl")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "a") as f:
        for (n, a), text in pairs:
            f.write(json.dumps({"schema": gate_runlog.SCHEMA, "kind": "verdict", "suite": n, "arm": a,
                                "verdict": "VERDICT", "text": text, "ts": now.isoformat(timespec="seconds"),
                                "tip": {"studio": tip_sha, "irisgl": pin, "studio_dirty": False,
                                        "irisgl_dirty": False},
                                "reader": os.environ.get("USER", "")}, sort_keys=True) + "\n")
    return path


def check(rng, build, gs=None, verdicts=None):
    """(ok, reasons) for a range. `verdicts`: {row or pool.arm: text} recorded for those rows first
    (per row, timestamped; a row that is not red now is refused as a verdict, said out loud)."""
    gs = gs or load_gs()
    contention, problem = gate_runlog.contention_load()
    if contention is None:
        return None, [f"{problem} — the refusal cannot apply the flake law without it (JAH_CONTENTION_FILE overrides)"]
    base, tip = rng.split("..", 1)
    tip_sha = subprocess.run(["git", "rev-parse", tip], cwd=gs.ROOT, capture_output=True, text=True).stdout.strip()
    if not tip_sha:
        return None, [f"cannot resolve {tip}"]
    base_sha = _git(["rev-parse", base], gs.ROOT) or base
    S = gs.select(gs.touched_paths(rng), rng, build, gs.GATE_JOBS, quiet_graph=True)
    need, what = needed_rows(gs, S)
    pin = _git(["rev-parse", f"{tip_sha}:irisgl"], gs.ROOT)
    label = lambda k: f"{k[0]}{' :: ' + k[1] if k[1] else ''}"
    # THE LANE'S EARLIER COMMITS and the pins they ran on (records at each are read in one pass)
    earlier = lane_commits(gs, base_sha, tip_sha)
    pins = {tip_sha: pin or None}
    for c in earlier:
        pins[c] = _git(["rev-parse", f"{c}:irisgl"], gs.ROOT) or None
    # THE BASE (the door's `real:<id> pre-existing` reads the red reproduced there by a recorded solo)
    if base_sha not in pins:
        pins[base_sha] = _git(["rev-parse", f"{base_sha}:irisgl"], gs.ROOT) or None
    reach = None

    def judge_all():
        """{key: (state, why, source sha)} — the tip's records first, then (for a row the tip has no
        run of) the newest earlier commit's, under the rule in this file's header. AND THE FLAKE LAW
        ACROSS COMMITS (the lead's merge read, F1): a row green at the tip is still refused while an
        earlier commit that the fix does NOT reach holds an open red of it (no verdict; a contention
        red without its 3/3) — a green re-run at a later commit does not answer a red the fix never
        touched; a verdict does, or --solo 3/3 for the contention class."""
        nonlocal reach
        got = records_by_tip(pins)
        out = {}
        tip_fork = fork_pin(gs, pin)
        have_earlier = [c for c in earlier if got[c]]
        for k in need:
            ctx = dict(tip_recs=got[tip_sha].get(k, []), base_recs=got.get(base_sha, {}).get(k, []))
            st, why = judge(k, got[tip_sha].get(k, []), contention, **ctx)
            src = tip_sha
            if st == "green" and have_earlier:
                if reach is None:
                    reach = Reach(gs, build, tip_sha, tip_fork)
                for c in have_earlier:
                    if Reach.reaches(reach.of(c, fork_pin(gs, pins[c])), k):
                        break                   # the fix reached it: the tip's run answers what came before
                    recs = got[c].get(k, [])
                    if not recs: continue
                    cst, cwhy = judge(k, recs, contention, **ctx)
                    if cst == "red":
                        st, src = "red", c
                        why = (f"an OPEN red at {c[:9]} that {c[:9]}..{tip_sha[:9]} does not reach — the green at the "
                               f"tip does not answer it (a verdict does; --solo 3/3 for the contention class): {cwhy}")
                        break
            if st == "missing" and have_earlier:
                if reach is None:
                    reach = Reach(gs, build, tip_sha, tip_fork)
                for c in have_earlier:
                    r = reach.of(c, fork_pin(gs, pins[c]))
                    if Reach.reaches(r, k):
                        why += (f"; {c[:9]}..{tip_sha[:9]} reaches it" if r is not Reach.ALL else
                                f"; no re-use across {c[:9]} (a fork pin change, a fallback or the tier by rule "
                                f"in {c[:9]}..{tip_sha[:9]})")
                        break
                    recs = got[c].get(k, [])
                    if not recs: continue
                    cst, cwhy = judge(k, recs, contention, **ctx)
                    if cst == "missing": continue
                    st, src = cst, c
                    why = (f"re-used from {c[:9]} ({c[:9]}..{tip_sha[:9]} does not reach it)"
                           + (f"; {cwhy}" if cwhy else "")) if cst == "green" else \
                          (f"red at {c[:9]}, the newest run of it on the lane ({c[:9]}..{tip_sha[:9]} does not reach "
                           f"it; answer it there): {cwhy}")
                    break
            out[k] = (st, why, src)
        # U4: THE LANE'S OTHER TIPS (a rebase, a superseded fix round): an open red there that the
        # current tip never re-ran (a later record of the same row+arm at the tip that passes) and no
        # accepted verdict answered is carried to this tip
        carried.clear()
        lanes = set()
        for c in [tip_sha] + earlier:
            for rs in got.get(c, {}).values():
                for r in rs:
                    if r.get("kind") != "verdict": lanes |= gate_runlog.record_lanes(r)
        for t, recs in lane_records(lanes, {tip_sha, *earlier}).items():
            for k, rs in recs.items():
                cst, cwhy = judge(k, rs, contention, tip_recs=got[tip_sha].get(k, []),
                                  base_recs=got.get(base_sha, {}).get(k, []))
                if cst != "red": continue
                last = max(_when(r) for r in rs if r.get("kind") != "verdict" and r.get("verdict") != "PASS")
                at_tip = got[tip_sha].get(k, [])
                later = [r for r in at_tip if r.get("kind") != "verdict" and r.get("verdict") not in NEVER_RAN
                         and _when(r) > last]
                if later and judge(k, at_tip, contention)[0] == "green": continue
                carried[(t, k)] = cwhy
        return out

    carried = {}
    judged = judge_all()
    if verdicts:
        pairs, unknown = {}, []
        for name, text in verdicts.items():
            keys = [k for k in need if k[0] == name or (k[1] and k[1] == name)]
            red_keys = [k for k in keys if judged[k][0] == "red"]
            for k in red_keys:
                pairs.setdefault(judged[k][2], []).append((k, text))     # recorded where the red happened
            for (t, k) in carried:                                       # U4: answered at the old tip
                if k[0] == name or (k[1] and k[1] == name):
                    pairs.setdefault(t, []).append((k, text)); red_keys.append(k)
            if not red_keys:
                unknown.append(f"{name} ({'not selected' if not keys else judged[keys[0]][0]})")
        for u in unknown:
            print(f"ci-gate-check: no verdict recorded for {u} — a verdict answers a red row only")
        for at, ps in pairs.items():
            path = record_verdicts(ps, at, pins.get(at) or _git(["rev-parse", f"{at}:irisgl"], gs.ROOT))
            print(f"ci-gate-check: recorded {len(ps)} verdict(s) at {at[:9]} -> {path}")
        if pairs:
            judged = judge_all()
    # THE PROVENANCE, per row (GATE-SPEED-1): which commit's record answered it
    for k in need:
        st, why, src = judged[k]
        print(f"ci-gate-check: row {label(k)} <- {src[:9]}{' (the tip)' if src == tip_sha else ''}: {st}"
              + (f" — {why}" if why else ""))
        if st == "red" and why.startswith("VERDICT REFUSED: "):
            print(f"VERDICT REFUSED {label(k)}: {why[len('VERDICT REFUSED: '):]}")
    for (t, k), why in sorted(carried.items()):
        print(f"OPEN RED carried from {t[:9]}: {label(k)} — {why}")
        if why.startswith("VERDICT REFUSED: "):
            print(f"VERDICT REFUSED {label(k)}: {why[len('VERDICT REFUSED: '):]}")
    missing = [label(k) for k, (st, _, _) in judged.items() if st == "missing"]
    red = [f"{label(k)}: {why}" for k, (st, why, _) in judged.items() if st == "red"]
    cleared = [f"{label(k)}: {why}" for k, (st, why, src) in judged.items()
               if st == "green" and why and src == tip_sha]
    reused = {}
    for k, (st, _, src) in judged.items():
        if st == "green" and src != tip_sha: reused[src] = reused.get(src, 0) + 1
    reasons = []
    if missing: reasons.append(f"{len(missing)} of {len(need)} row(s) of {what} have no record at {tip_sha[:9]} "
                               f"(nor a re-usable one earlier on the lane): {missing[:8]}")
    for r in red[:20]: reasons.append("RED " + r)
    for (t, k), why in sorted(carried.items())[:20]:
        reasons.append(f"OPEN RED carried from {t[:9]}: {label(k)} — {why}")
    if not (missing or red or carried):
        at_tip = len(need) - sum(reused.values())
        reasons.append(f"{what}: {len(need)} row(s) green — {at_tip} at {tip_sha[:9]}"
                       + "".join(f", {n} re-used from {c[:9]}" for c, n in reused.items())
                       + (f"; cleared by the law: {cleared[:6]}" if cleared else ""))
    return not (missing or red or carried), reasons


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("range")
    ap.add_argument("--build", default="build-linux")
    # action="extend": `--verdict a=x b=y` and `--verdict a=x --verdict b=y` (the form merge-dbuild-lane.sh
    # builds) both record EVERY verdict — with nargs="+" alone a repeated flag kept only the last one
    ap.add_argument("--verdict", nargs="+", action="extend", default=None, metavar="ROW=TEXT",
                    help="record a verdict per red row (`<row or pool.arm>=<the reader's text>`) in the run log, "
                         "then re-check; it clears only the reds logged before it (repeatable)")
    a = ap.parse_args()
    verdicts = None
    if a.verdict:
        verdicts = {}
        for pair in a.verdict:
            if "=" not in pair or not pair.split("=", 1)[1].strip():
                ap.error(f"--verdict takes <row>=<text> pairs, got '{pair}'")
            k, v = pair.split("=", 1)
            verdicts[k.strip()] = v.strip()
    gs = load_gs()
    ok, reasons = check(a.range, gs.resolve_build(a.build), gs, verdicts=verdicts)
    if ok is None:
        for r in reasons: print("ci-gate-check: UNUSABLE — " + r)
        sys.exit(2)
    for r in reasons: print(("ci-gate-check: " if ok else "ci-gate-check: REFUSED — ") + r)
    if not ok:
        print(f"ci-gate-check: run the FIX ROUND `scripts/gate-scope.sh <pre-fix tip>..{a.range.split('..', 1)[1]} --run` "
              f"(§3b; or `{a.range} --run` for a lane never gated); a contention-class red takes `--solo <suite>` "
              f"(3/3); any other red a verdict: `--verdict \"<row>=<text>\" ...`")
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
