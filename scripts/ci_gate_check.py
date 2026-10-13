#!/usr/bin/env python3
"""ci_gate_check — THE MERGE REFUSAL behind scripts/ci-gate-check.sh (MODULAR-GATE-1 T6; the law
since TEST-SELECTOR-1 L2/L3, docs/TESTING_GATE.md §4).

Given a range, it re-derives the scoped selection (gate-scope.py, the same code the lane ran — the
lane's OWN diff across forward merges) and reads THE RUN LOG at the range's tip (the studio sha,
irisgl at the sha the tip pins, neither tree dirty). Every selected row — and, for a pool selected
in part, every selected arm — must have been run there, and THE FLAKE LAW decides its reds:

  * no red record                      -> green;
  * a CONTENTION-CLASS suite (the defect registry's open `nondeterminism` entries, <workspace>/testing/
    defects.json — data, read here and by `gate-scope.sh --solo`; its red must show a competitor census) that went red -> needs 3/3 SOLO retries after its last gate red,
    every one PASS (a solo red means it is not the contention class: it needs a verdict);
  * any other red                      -> needs a RECORDED VERDICT (below);
  and a row never run at the tip is refused, whatever else holds (a verdict does not stand in for
  a run). One solo PASS no longer erases a red (the audit's L2: gi.field_scroll PASS/FAIL/PASS
  beside a sibling was accepted).

A GREEN HOLDS UNTIL A CHANGE REACHES THE ROW (RUNNER-A2; TESTING_RUNNER_SPEC v2.1 rule 2 — GATE-SPEED-1's fix-round
re-use made general: across merges, across tiers, with no integration-line exclusion). A row with no run at the tip
takes the NEWEST record of that row at ANY ANCESTOR A of the tip holding one (serve_key, THE ONE PREDICATE — `runner.sh
select` serves with it too), provided the WHOLE range A..tip (forward merges included) does not reach the row (a pin
move reaches every row that reaches the fork by the graph; a fallback, the tier by rule or an unreadable range reaches
everything), the fork pin is equal or the row does not reach the fork, the record's declaration and data hashes equal
the build's at the tip (a null is unknown: never equal — a record before schema 3 serves nothing), its build was clean,
its tier admissible (a lane-tool record only when it carries seconds, the census, labels and the Xid read), and the row
is neither MEASURING nor a REGISTERED NONDETERMINISM. That newest record decides: green serves; red refuses until
answered there. THE UNIVERSE IS THE REACH (TESTING-SPEED-A item 1): the range's scoped rows + `--named` (the lane brief's
rows) — a row outside it is not asked; `--full` (the SHIP only) asks EVERY merge-tier row this way (tier_need); a measuring
row the range does not reach is not asked. Every row prints the commit its record came from.

A VERDICT COVERS ITS RANGE (TESTING-CLEANUP-2D item 1): a verdict is per ROW per RANGE — the verdict records at the
TIP are in scope for the same row's reds at every earlier commit of the range the judge reads (the lane tip the
attribution ran solos at), through the same door, the same proof and the same timestamp rule (a
verdict clears only reds logged before it); a verdict recorded at that earlier commit itself still counts. `--verdict`
still records a verdict where the red is open (else at the carried tip).

THE BASE'S PROOF TRAVELS WHILE NOTHING REACHES THE ROW (TESTING-CLEANUP-2D item 2): KNOWN RED is the same red on the
BASE's recorded solos; when the base holds NO RUN of the row at all (a gate PASS there is the base's own evidence: a red
at the candidate is then a regression, never known), the solos at the NEAREST ancestor A of the base that has
them (by ancestry; one hop, records under the same filters) prove it at the base — only when A's solos are ALL red and
A..base does NOT reach the row, where an unreadable, fallback, tree-equal or fork-pin-changing range counts as
reaching everything (Reach.ALL: the proof does not travel) — a stage-close row the selector set aside for the batch
included. The why names A; the proof is read only when the door reaches it.

A range that MOVES THE FORK PIN asks the fork diff's COMPONENT reach (TESTING-SPEED-A item 4: FORK_COMPONENTS — never
the whole tier; the old `--fork-tier` rule is gone; the lane's own gates select by the fork
diff's reach). A fallback is its directory's subsystem; only the tier BY RULE (a top-level CMake command, a vendored path no
one owns, an unreadable fork diff) asks the whole MERGE tier.

THE VERDICT IS PER ROW AND TIMESTAMPED: `--verdict "<row>=<text>" ...` records one `VERDICT` record
per named red row into the run log (`<date>-verdict-<tip>.jsonl`, the same directory) and
re-checks; it clears only the reds logged before it. A row that never ran (NOADMIT, NOTRUN) is
MISSING, which no verdict clears. `runner.sh judge` (scripts/lead/lib/runner/judge.py) calls this and the
runner's merge refuses on a failure; the attribution's `real:<id>` verdicts are recorded through `--verdict`.

THE VERDICT DOOR (TESTING_V3_SPEC §1.4; VERDICT-1; door() below): a verdict clears a red only by a REGISTERED FACT
— `real:<ID>` (in testing/defects.json for this row; proved by a PASS at a later sha that reaches the row, or by the SAME red reproduced
by a recorded solo at the base = KNOWN RED: `--mode merge` passes it, `--mode push|stage-close` refuses it),
`contention:` (an open nondeterminism row whose red's competitor census shows competition, 3/3 solo), `xid-read:`
(a LOST/OOM/CRASH whose journal was unreadable). A refused verdict prints `VERDICT REFUSED <row>: <why>`. A REBASE
CARRIES ITS OPEN REDS from the lane's OWN records (`lanes == [<lane>]`) at any other tip (`OPEN RED carried from
<tip>`). A registry entry without its shape makes the judge unusable (exit 2).

THE STAGE-CLOSE JUDGE (STAGE-CLOSE-1; docs/TESTING_GATE.md §1d): `<base>..<tip> --stage-close [--mode push]`
is a SELECTION only — EVERY `stage-close` row of the build (STAGE_CLOSE_LABELS: the rows the MERGE and PUSH tiers
leave out) instead of the range's scoped rows — judged by the same judge in mode stage-close (or push): the door
(a proved `real:<id>` clears through the Prover against the base), the aborts, a nondeterminism clearance re-asked
3/3 at the tip, the overrides. A push and a stage close are REFUSED while one is red or unrecorded.

Exit 0 accepted, 1 refused, 2 unusable (no defect registry or an entry without its shape, an unresolvable range).
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
from gate_runlog import is_solo  # noqa: E402  (F1: the one solo predicate, tier `solo` or gate-scope's retry)

SOLO_NEEDED = 3


def load_gs():
    spec = importlib.util.spec_from_file_location("gate_scope", os.path.join(HERE, "gate-scope.py"))
    gs = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(gs)
    return gs


ABORTS = {}        # {tip: [kind: abort records]} — read with the records (their droppedRed rows need 3/3 solo)
# A RUN IS A RECORD OF KIND None (a gate's row or arm) OR `self-audit` (RUNNER-FIX-4 item 10): `verdict` and `abort` records are
# read for what they are; every other kind the tools write — the runner's `attribution`, `scan-verdict`, `recheck`,
# `served`, `hash`, `push` — is NO RUN: its verdict is None, and VOXEL-READOUT-1's attribution records were judged as
# reds ("None and not in the contention class") and carried to its next merge
RUN_KINDS = (None, "self-audit")


def is_run(r):
    return r.get("kind") in RUN_KINDS


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
            if r.get("kind") in (None, "self-audit") and gate_runlog.stale_build(t): continue   # a RUN of another build (GATE-COST-2 #8, F4)
            # the engine that ran must be the one the tip PINS (F1): a record from a tree whose
            # irisgl was checked out elsewhere tested other code
            if pins[sha] and t.get("irisgl") != pins[sha]: continue
            # A LANE-TOOL RECORD ANSWERS WHEN IT IS COMPLETE (RUNNER-A2; N5's blanket exclusion deleted): a builder's
            # record that carries seconds, the census, the labels and the Xid read (A1's complete self-audit record;
            # A5's queue writes lane-test's rows the same) is a run like a gate's; one without them stays out (it
            # still carries its reds: lane_records)
            if r.get("tier") == "lane" and not complete_lane(r): continue
            if r.get("kind") == "abort":
                ABORTS.setdefault(sha, []).append(r); continue
            # a SERVING record (`kind: served`, runner.sh select) is the select's written evidence, never a run: the judge
            # serves through the same predicate itself (serve_key); nor is any other kind of the tools (is_run)
            if r.get("kind") != "verdict" and not is_run(r): continue
            out[sha].setdefault((r.get("suite"), r.get("arm")), []).append(r)
    for recs in out.values():
        for v in recs.values():
            v.sort(key=lambda r: gate_runlog.instant(r.get("ts")))     # instants, never the ISO text (F20)
    return out



def complete_lane(r):
    """A lane-tier record that may answer a row: it carries seconds, the census, its labels and the Xid read."""
    b = r.get("box") if isinstance(r.get("box"), dict) else {}
    return (r.get("seconds") is not None and isinstance(b.get("census"), dict) and isinstance(r.get("labels"), list)
            and "xid" in r)


def lane_records(lanes, skip):
    """VERDICT-1 U4 — A REBASE CARRIES ITS OPEN REDS: {tip: {(suite, arm): [records]}} of every gating
    record that is a LANE'S OWN record of a lane in `lanes` (`lanes == [<lane>]`) at ANY tip not in `skip`, ancestor or not, with every
    verdict record at those tips. Forward only: run records
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
            if not is_run(r): continue          # the runner's attribution / served / hash ... records are no runs (item 10)
            if (r.get("schema") or 1) < 2 or r.get("gating") is False or r.get("tier") == "target": continue
            if gate_runlog.own_lane(r) not in lanes: continue
            out.setdefault(sha, {}).setdefault((r.get("suite"), r.get("arm")), []).append(r)
    for sha, recs in out.items():
        for v in verdicts.get(sha, []):
            k = (v.get("suite"), v.get("arm"))
            if k in recs: recs[k].append(v)
    return out


def _when(r):
    """A record's time as an instant (gate_runlog.instant, the ONE helper: offsets and DST compare right)."""
    return gate_runlog.instant(r.get("ts"))


NEVER_RAN = ("NOADMIT", "NOTRUN")
HARD = ("LOST", "OOM", "CRASH")          # never cleared by solos, never by `contention:`

# THE VERDICT DOOR (TESTING_V3_SPEC §1.4; lane VERDICT-1 + the lead's band-aid addendum): a verdict clears an open red
# only by a REGISTERED FACT, every token CHECKED against the run log and the defect registry (testing/defects.json):
#   real:<ID>          <ID> EXISTS in the registry and its `rows` hold this row, and EITHER a PASS of the row at a
#                      LATER sha THAT REACHES THE ROW (a descendant of the red's sha whose change selects it) proves
#                      the fix, OR the same red is REPRODUCED ON THE BASE (a recorded solo
#                      red at the range's base, a d-build commit) — then the row is KNOWN RED: a merge may pass, a push
#                      or a stage close may not. An `nondeterminism` entry (NOT REPRODUCED) clears by 3/3 solo PASS.
#   contention:<...>   FAIL/TIMEOUT only; the row has an OPEN `nondeterminism` entry (the contention class), the red's
#                      COMPETITOR CENSUS (box.census) shows real competition — GPU processes outside the gate, sibling
#                      ctests, builds, memory/IO pressure; never the gate's own queue or drain — and 3/3 solo PASS.
#   xid-read:<window>  LOST/OOM/CRASH whose record says journal_unreadable (else the Xid would be in the record).
#                      WHAT A RECORD'S `xid` IS (RUNNER-AUDIT-FIX-2, F11 closed as no defect): ONLY the Xids of the ROW'S
#                      OWN process tree — vram_tokens.py supervise() reads kernel_xid.kernel_xids(t0, tracker.pids) and
#                      tests/support/run_pool.py the arm's {pid}; no record ever carries the box's Xid from another pid.
#                      So an Xid in a record is the row's, a defect by law, cleared by real:<id> only (above). This read
#                      is checked against the RECORD, never a name alone: it is accepted only when every hard red's
#                      record says journal_unreadable (no Xid of the row COULD be read) and the window covers the red.
# A red whose record carries an `xid` takes `real:` only. Solos below 3/3 are red. `environmental`, ENOSPC and a dead
# display are never verdicts. Nothing accepts prose.
# `real:<id>` names ANY token (TESTING-CLEANUP-2B item 14): the REGISTRY is the grammar — _real looks the token up and
# refuses an id it does not hold. A pattern of its own (it was [A-Z…]-<n>) could not name the ids the attribution
# itself registered (lowercase, dots, no number), so every KNOWN RED verdict on them fell through to prose and was refused.
DEFECT_ID = re.compile(r"\breal:\s*([^\s,;()\[\]]+?)[.:]?(?=[\s,;()\[\]]|$)")
CONTENTION_TOKEN = re.compile(r"\bcontention:\s*\S")
XID_READ = re.compile(r"\bxid-read:\s*(\d{4}-\d\d-\d\dT\d\d:\d\d(?::\d\d)?(?:[+-]\d\d:?\d\d)?)\.\."
                      r"((?:\d{4}-\d\d-\d\dT)?\d\d:\d\d(?::\d\d)?(?:[+-]\d\d:?\d\d)?)")
NEVER_A_VERDICT = re.compile(r"\benvironment(al)?\b|\bENOSPC\b|\bno space left\b|\b(dead|died|death of the) display\b|"
                             r"\bdisplay (died|death|dead|lost)\b", re.I)


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


def census_of(r):
    """A record's competitor census as words ([] = no competitor measured; a record before the census = [])."""
    return gate_runlog.competitors((r.get("box") or {}).get("census"))


def _solos_ok(solos):
    return len(solos) >= SOLO_NEEDED and all(r.get("verdict") == "PASS" for r in solos)


# THE PATHS A FAILURE NAMES ARE THE TREE'S, NOT THE FAILURE'S (TESTING-CLEANUP-2C item 1): a failLine that embeds its tree
# root ("…must be inside /…/rc-merge-<lane>/build-linux/tests/…") must read the same at the base's tree (rc-base-<sha>) — batch
# C2 refused three KNOWN RED verdicts whose masked lines were identical up to the root. Each prefix below is ONE token;
# what follows it (the path INSIDE the tree) stays, so a different file is still a different failure.
_D = r"""[^/\s'"`(),;=:]"""                     # a path component's character (a delimiter ends the path)
_START = r"""(?<![^\s'"`(),;=:])"""             # a path STARTS at the line's start or after a delimiter (fix round 3
                                                #  F2: an unanchored prefix ate glued text — a=<p> read as b=<p>)
_PATH_MASKS = [
    # any worktree (lane, rc, rc-base): the path up to and including its name
    (re.compile(_START + r"(?:/" + _D + r"+)+?/\.claude/worktrees/" + _D + r"+"), "<tree>"),
    # the main tree under either spelling (never jahshaka-old: the next character is not a name's)
    (re.compile(_START + r"(?:/home/" + _D + r"+|/mnt/work)/Developer(?:\.usb)?/jahshaka(?![\w.-])"), "<tree>"),
    # /tmp/jah-* (lead + rig scratch)
    (re.compile(_START + r"/tmp/jah-" + _D + r"*"), "<jah-tmp>"),
    # a data root OUTSIDE a tree — a user's home or a /tmp scratch home; a home INSIDE a tree was masked to <tree>/…
    # above and keeps its own name (e2e-home-clip_ref and e2e-home-tex_ref stay two reds — fix round 3 F2)
    # (fix round 4, item 4: a user's home and a /tmp scratch home are DIFFERENT tokens — a regression that writes the
    # owner's real data root never reads as a scratch home's red)
    (re.compile(_START + r"/home/" + _D + r"+/\.local/share/Jahshaka(?![\w.-])"), "<home-data-root>"),
    (re.compile(_START + r"/tmp/" + _D + r"+/\.local/share/Jahshaka(?![\w.-])"), "<tmp-data-root>"),
]


def masked(line):
    """A failLine with its tree paths and numbers masked — the same failure reads the same at base and tip: every path
    under a tree root (a `.claude/worktrees/<name>`, the main tree), /tmp/jah-* and a data root OUTSIDE a tree (a user's
    or a /tmp home; an in-tree home keeps its name) is one token, each matched only where a path STARTS
    (_PATH_MASKS), then every number is `#`. A different failure (another message, another file in the tree) stays
    different."""
    t = (line or "").strip()
    for rx, tok in _PATH_MASKS:
        t = rx.sub(tok, t)
    return re.sub(r"\d+(\.\d+)?", "#", t)


def _sha(r):
    return (r.get("tip") or {}).get("studio")


# THE LANE(S) THE CHECK JUDGES FOR (round 6): a by-design entry clears its REGISTERING LANE's candidate whatever the tip
# after the tool's rebases — check() names the lane (--lane, else the lanes the tip's records name)
CURRENT_LANES = set()


def by_design_entry(defects, row, lanes):
    """An open RED BY DESIGN entry naming `row` registered at the delivery of one of `lanes` (not retired, not past its
    recheck), or None."""
    for e in (defects or {}).values():
        if row in (e.get("rows") or []) and by_design_lane(e) in (lanes or ()) \
                and gate_runlog.defect_state(e) == "open" and not gate_runlog.recheck_past(e):
            return e
    return None


class KnownRegistry:
    """THE REGISTRY AS THE MERGE'S PROOF (round 7): a row with an open registry entry that is a DOOR PROOF for this
    candidate is a KNOWN RED at a merge — SERVED as such by the select (never run, never re-run) and read so by the judge
    (no per-record hand verdict): (a) a RED BY DESIGN entry registered at the delivery of the lane the merge is for
    (CURRENT_LANES); (b) an open `kind: defect` entry whose red THE BASE'S PROOF reproduces — `base_recs(key)`, the door's
    own base_of (the base's own runs, else the one hop to the nearest ancestor holding solos, under the proof-travel rules):
    every base solo red, and every red the candidate holds of the row the same red (the door's signature). A push and a
    stage close refuse every known red (unchanged)."""

    def __init__(self, defects, base_recs):
        self.defects, self.base_recs = defects or {}, base_recs

    def known(self, key, tip_recs=()):
        """(True, why) when the row is a registered known red for this candidate, else (False, why)."""
        row = key[0]
        e = by_design_entry(self.defects, row, CURRENT_LANES)
        if e:
            return True, (f"KNOWN RED: real:{e.get('id')} RED BY DESIGN — registered at {by_design_lane(e)}'s delivery; "
                          f"until {e.get('recheck')}")
        ents = [x for x in self.defects.values() if row in (x.get("rows") or []) and x.get("kind") == "defect"
                and gate_runlog.defect_state(x) == "open" and not gate_runlog.recheck_past(x) and not by_design_lane(x)]
        if not ents:
            return False, "no registry entry is a proof here"
        base = self.base_recs(key)
        if callable(base):
            base = base()
        solos = [r for r in (base or []) if r.get("kind") != "verdict" and is_solo(r) and r.get("verdict") not in NEVER_RAN]
        reds = [r for r in solos if r.get("verdict") != "PASS"]
        if not reds or len(reds) != len(solos):
            return False, "the base's proof holds no red solo of the row (or green beside red)"
        sig = lambda r: (r.get("verdict"), masked(r.get("failLine")), masked(r.get("status")))
        if len({r.get("verdict") for r in reds}) > 1:
            return False, "the base's solos flip class"
        mine = [r for r in (tip_recs or []) if r.get("kind") != "verdict" and r.get("verdict") not in ("PASS",) + NEVER_RAN]
        if [r for r in mine if sig(r) not in {sig(b) for b in reds}]:
            return False, "the candidate's red is not the base's red"
        frm = reds[0].get("_proofFrom")
        return True, (f"KNOWN RED: real:{ents[0].get('id')} — the same red reproduced by {len(reds)} solo(s) at the base"
                      + (f" (the proof from {frm[:9]})" if frm else ""))


BY_DESIGN_NOTE = re.compile(r"registered at (\S+?)'s delivery: an acceptance row RED BY DESIGN")


def by_design_lane(e):
    """The lane whose delivery registered entry `e` as an acceptance row RED BY DESIGN (registry.register_by_design's
    note), or None."""
    for n in (e.get("notes") or []):
        m = BY_DESIGN_NOTE.search(str(n))
        if m:
            return m.group(1)
    return None


def _real(m, row, reds, solos, defects, tip_recs, base_recs, tip_sha=None, mode="merge", proves=None, key=None):
    """`real:<ID>` checked: (ok, why, kind) — kind None | "known" (KNOWN RED) | "nondet" (cleared as nondeterminism:
    a push / stage close re-asks it 3/3 at the candidate)."""
    if callable(base_recs):          # the base's proof, computed only here (TESTING-CLEANUP-2D read 1 F2)
        base_recs = base_recs()
    did = m.group(1)
    e = (defects or {}).get(did)
    if e is None:
        return False, f"real:{did} is not in the defect registry (testing/defects.json) — register it first", False
    if row not in e["rows"]:
        return False, f"real:{did} is registered for {e['rows'][:4]}, not for {row}", False
    if gate_runlog.defect_state(e) == "retired":
        return False, f"real:{did} is RETIRED in the registry", False
    if gate_runlog.recheck_past(e):
        return False, f"real:{did} is past its recheck date {e['recheck']} — re-verdict the entry first", False
    hard = sorted({r.get("verdict") for r in reds} & set(HARD))
    if e["kind"] == "nondeterminism" and (hard or any(r.get("xid") for r in reds)):
        return False, (f"real:{did} is a nondeterminism entry — a {'/'.join(hard) or 'red with an Xid'} takes only a "
                       f"`kind: defect` entry (a crash, a loss, an Xid is never nondeterminism)"), False
    # AT A MERGE THE LEAD'S real:<id> NAMING AN OPEN ENTRY OF THE ROW CLEARS IT (round 13: the lead decides a red from one
    # rerun and the log; a merge passes a known red) — "known" for a defect, "nondet" for a nondeterminism entry, whatever
    # the solo count; a SINGLE-USE entry still answers only the merge that registered it; the 3/3 re-ask and the proofs
    # stay the push's and the stage close's
    if mode == "merge" and not (e["kind"] == "nondeterminism" and gate_runlog.single_use(e)) and not by_design_lane(e):
        return True, (f"real:{did} — an OPEN registry entry ({e['kind']}) naming {row}: the lead's verdict, a merge passes "
                      f"it; the push and the stage close re-ask"), ("nondet" if e["kind"] == "nondeterminism" else "known")
    if e["kind"] == "nondeterminism":
        if gate_runlog.single_use(e) and mode != "merge":
            return False, (f"real:{did} is a SINGLE-USE NOT REPRODUCED entry: it clears the merge that registered it, "
                           f"never a {mode}"), False
        if gate_runlog.single_use(e):
            at = (e.get("first_seen") or {}).get("tip") or ""
            if not (tip_sha and at and tip_sha.startswith(at)):
                return False, (f"real:{did} is a SINGLE-USE NOT REPRODUCED entry of tip {at[:9]} — it answers only the merge "
                               f"that registered it; a later red needs the lead's `defect enrol` (a measured rate and "
                               f"the census) or its own verdict"), False
        if _solos_ok(solos):
            return True, f"real:{did} (nondeterminism, {len(solos)}/{len(solos)} solo PASS)", "nondet"
        return False, f"real:{did} is a nondeterminism entry: it clears only by 3/3 solo PASS after the red", False
    # A RED REGISTERED BY DESIGN AT ITS LANE'S DELIVERY IS A DOOR PROOF (round 5, 1): an entry whose notes say it was
    # "registered at <lane>'s delivery: an acceptance row RED BY DESIGN" clears the row at that lane's candidate — the
    # registering delivery's tip (first_seen.tip) or a descendant of it — until its recheck date (checked above); a merge
    # passes it as a KNOWN RED, a push/stage close refuses it like every known red (never push on a red)
    bd = by_design_lane(e)
    if bd:
        at = (e.get("first_seen") or {}).get("tip") or ""
        if bd in CURRENT_LANES:
            # THE REGISTERING LANE'S CANDIDATE (round 6): matched by the lane, whatever its tip after the tool's rebases
            return True, (f"real:{did} RED BY DESIGN — registered at {bd}'s delivery (an acceptance row the brief declares "
                          f"red by design); the candidate is {bd}'s (its tip {(tip_sha or '?')[:9]}, the delivery's "
                          f"{at[:9]}: rebases keep the proof); until the entry's recheck {e.get('recheck')}"), "known"
        ok_at = bool(at and tip_sha) and (tip_sha.startswith(at) or (proves is not None and proves.ancestor(at, tip_sha)))
        if ok_at:
            return True, (f"real:{did} RED BY DESIGN — registered at {bd}'s delivery (an acceptance row the brief declares "
                          f"red by design), the candidate {tip_sha[:9]} is that delivery's tip or a descendant of it "
                          f"{at[:9]}; until the entry's recheck {e.get('recheck')}"), "known"
        return False, (f"real:{did} is RED BY DESIGN for {bd}'s delivery at {at[:9]} — {(tip_sha or '?')[:9]} is not that "
                       f"tip nor a descendant of it: it proves nothing here"), False
    last = max(_when(r) for r in reds)
    red_shas = {_sha(r) for r in reds}
    later = [r for r in (tip_recs or []) if r.get("kind") != "verdict" and r.get("verdict") == "PASS" and _when(r) > last]
    # F1: THE FIX IS PROVED AT ANOTHER SHA — a PASS at the red's own sha is the same code passing once (nondeterminism:
    # 3/3 solo, never "fixed")
    # ROUND 2 F1: the proving PASS is at a sha DESCENDED from every red's sha whose range from it REACHES the row
    # (`proves`, the caller's Prover); without that context nothing is proved here
    good = [r for r in later if proves is not None and all(proves(rs, _sha(r), key or (row, None)) for rs in red_shas)]
    if good:
        return True, f"real:{did} fixed (a PASS at {(_sha(good[-1]) or '')[:9]}, a later sha that reaches the row)", False
    # KNOWN RED: the SAME red on the base — the same verdict class and the same masked failLine (a lane that turns a
    # base FAIL into a CRASH, or another assertion, is not "known"). EVERY PENDING RED (TESTING-CLEANUP-2B fix round 3,
    # F1): the gate's red AND each red solo after it must match a base solo red — the newest alone let a CRASH or another
    # assertion among the solos pass — and the base's solos must ALL be red: a base with green solos beside its red is
    # nondeterminism on the base, not a known red.
    def _sig(r):
        return (r.get("verdict"), masked(r.get("failLine")), masked(r.get("status")))
    lane_red = sorted(reds, key=_when)[-1]
    pending = list(reds) + [r for r in (solos or []) if r.get("verdict") not in ("PASS",) + NEVER_RAN
                            and not any(r is x for x in reds)]      # `reds` may already hold the solos: count each once
    base_solos = [r for r in (base_recs or []) if r.get("kind") != "verdict" and is_solo(r)
                  and r.get("verdict") not in NEVER_RAN]
    base_reds = [r for r in base_solos if r.get("verdict") != "PASS"]
    base_sigs = {_sig(r) for r in base_reds}
    odd = [r for r in pending if _sig(r) not in base_sigs]
    # ONE RED ON THE BASE (fix round 4, item 6): a base whose own solos flip class (FAIL / CRASH) is not one known red —
    # every pending red matching SOME base red would let an all-CRASH candidate through on one CRASH base solo
    base_classes = sorted({r.get("verdict") for r in base_reds})
    if base_reds and len(base_classes) > 1:
        return False, (f"real:{did}: the base's solos flip class ({'/'.join(base_classes)}) — not one KNOWN RED"), False
    if base_reds and len(base_reds) == len(base_solos) and not odd:
        frm = base_reds[0].get("_proofFrom")
        return True, (f"real:{did} KNOWN RED (the same {lane_red.get('verdict')}"
                      f"{' — ' + masked(lane_red.get('failLine'))[:60] if lane_red.get('failLine') else ''} — every one of "
                      f"the {len(pending)} pending red(s) reproduced by the base's {len(base_reds)} red solo(s)"
                      + (f"; the base's proof is {frm[:9]}'s solos; {frm[:9]}..the base does not reach the row" if frm else "")
                      + ")"), "known"
    if base_reds and len(base_reds) < len(base_solos) and not odd:
        return False, (f"real:{did}: the base's solos are {len(base_reds)} red / {len(base_solos) - len(base_reds)} "
                       f"green — nondeterminism on the base, not a KNOWN RED"), False
    if base_reds and odd and any(_sig(r) in base_sigs for r in pending):
        o = odd[-1]
        return False, (f"real:{did}: not every pending red is the base's red — {o.get('verdict')} "
                       f"'{masked(o.get('failLine') or o.get('status'))[:50]}' among them is not — not known"), False
    other = [r for r in (base_recs or []) if r.get("kind") != "verdict" and is_solo(r)
             and r.get("verdict") not in ("PASS",) + NEVER_RAN]
    if later and not other:
        return False, (f"real:{did}: no PASS after the red is at a later sha that REACHES the row (a descendant of the "
                       f"red's sha whose change selects it) — the same code passing again proves no fix (3/3 solo is "
                       f"nondeterminism)"), False
    if other:
        return False, (f"real:{did}: the base's solo red is not the SAME red ({other[-1].get('verdict')} "
                       f"'{masked(other[-1].get('failLine') or other[-1].get('status'))[:50]}' vs the lane's "
                       f"{lane_red.get('verdict')} '{masked(lane_red.get('failLine') or lane_red.get('status'))[:50]}') "
                       f"— not known"), False
    return False, (f"real:{did} is registered, but neither a PASS at a later sha that reaches the row (the fix) nor the "
                   f"same red reproduced by a recorded solo at the BASE proves it — naming a ticket clears nothing"), False


def door(text, reds, solos, row, defects=None, tip_recs=None, base_recs=None, tip_sha=None, mode="merge", proves=None,
         key=None):
    """(ok, why, known) — may this verdict text clear these reds (the open reds logged before it)?
    `solos`: the solo retries after the reds' last gate red (any time — a verdict may be written before its
    solos run); `row`: the row (or pool.arm) name; `defects`: the loaded registry; `tip_recs` / `base_recs`:
    the row's records at the range's tip and base. `known`: accepted as KNOWN RED (a merge passes it; a push
    or a stage close refuses it). Each refusal names its rule."""
    text = text or ""
    if NEVER_A_VERDICT.search(text):
        return False, ("'environmental', ENOSPC and a dead display are never verdicts — the cause is a registered "
                       "defect of the box (real:<id>) or the run's abort record"), False
    kinds = {r.get("verdict") for r in reds}
    real = DEFECT_ID.search(text)
    contention = gate_runlog.contention_of(defects)
    xid = [r for r in reds if r.get("xid")]
    if xid:
        if real: return _real(real, row, reds, solos, defects, tip_recs, base_recs, tip_sha, mode, proves, key)
        x = xid[-1]["xid"]
        return False, (f"the red carries an Xid from the row's own process (pid {x.get('pid')}, "
                       f"{x.get('window')}) — a DEFECT by law: only real:<registered id> clears it"), False
    if real and mode == "merge":
        # round 13: the registry entry the lead names decides at a merge (_real's merge rule), before the solo rule
        ok_, why_, kind_ = _real(real, row, reds, solos, defects, tip_recs, base_recs, tip_sha, mode, proves, key)
        if ok_:
            return ok_, why_, kind_
    spass = sum(1 for r in solos if r.get("verdict") == "PASS")
    # THE SOLO RULE IS FOR A CLAIM OF NONDETERMINISM (TESTING-CLEANUP-2C, the lead's item 4): solos below 3/3 refuse
    # prose, a contention: claim and a nondeterminism entry (which must SHOW 3/3 green) — but a real:<id> of a DEFECT
    # whose solos are ALL red is the defect reproducing, and its proof is the SAME red on the base's solos (_real's
    # KNOWN RED) or a PASS at a later sha that reaches the row: the red solos are its evidence, not a refusal. Batch C2's
    # attribution recorded 3/3 red at the candidate and at the base for five rows and the door refused every
    # `real:C2-defect-<row>` here, before the KNOWN RED door could read the base. MIXED solos (some green, some red) are
    # nondeterminism by definition and stay refused for any verdict.
    all_red = bool(solos) and spass == 0
    if solos and not _solos_ok(solos) and not (real and all_red):
        return False, (f"solos below 3/3 ({spass}/{len(solos)} solo PASS after the red) are red — never cleared by "
                       f"text, listed or not"), False
    hard = kinds & set(HARD)
    if hard:
        cls = "/".join(sorted(hard))
        if real: return _real(real, row, reds, solos, defects, tip_recs, base_recs, tip_sha, mode, proves, key)
        # xid-read: the record's own fact decides (journal_unreadable), the text only names the window that covers the
        # red — a record WITH an xid never reaches here (the row's own Xid: real:<id> only, above); F11 (FIX-2)
        w = _xid_window(text)
        if w:
            if not all(r.get("journal_unreadable") for r in reds if r.get("verdict") in HARD):
                return False, (f"xid-read: is for a {cls} whose record has no xid BECAUSE the journal was unreadable — "
                               f"this record's journal was read (xid: null = no Xid of the row): only real:<id>"), False
            last, first = max(_when(r) for r in reds), min(_when(r) for r in reds)
            if w[0] <= last and first - datetime.timedelta(hours=1) <= w[1]:
                return True, f"xid-read:{w[0].isoformat(timespec='minutes')}..{w[1].isoformat(timespec='minutes')}", False
            return False, (f"the xid-read window {w[0].isoformat(timespec='minutes')}..{w[1].isoformat(timespec='minutes')} "
                           f"does not cover the {cls} at {last.isoformat(timespec='minutes')}"), False
        return False, (f"a verdict on a {cls} is real:<registered id> (or xid-read:<window> when the journal was "
                       f"unreadable) — a {cls} is never contention"), False
    if real: return _real(real, row, reds, solos, defects, tip_recs, base_recs, tip_sha, mode, proves, key)
    if CONTENTION_TOKEN.search(text):
        if row not in contention:
            return False, ("contention: clears only a row with an OPEN dated `nondeterminism` entry in the defect "
                           "registry — this row has none"), False
        bare = [r for r in reds if not is_solo(r) and not census_of(r)]
        if bare:
            return False, (f"contention: needs a COMPETITOR CENSUS showing real competition in the red's record (GPU "
                           f"processes outside the gate, sibling ctests, builds, memory/IO pressure — never the gate's "
                           f"own queue or drain) — the red at {_when(bare[-1]).isoformat(timespec='minutes')} shows none"), False
        if _solos_ok(solos):
            return True, (f"contention: ({contention[row]['id']}; {census_of([r for r in reds if not is_solo(r)][-1])[0]}) "
                          f"with {spass}/{len(solos)} solo PASS"), "nondet"
        return False, f"contention: needs 3/3 solo PASS after the red ({spass}/{len(solos)}) — gate-scope.sh --solo <row>", False
    return False, ("a verdict carries a registered fact: real:<id> (in testing/defects.json, proved by a PASS at a "
                   "later sha that reaches the row, or the same red reproduced on the base) or contention: (a nondeterminism row, a competitor census, 3/3 "
                   "solo) — nothing accepts prose"), False


def resolve_not_run(out, tip_recs, judge_parent, tip_sha):
    """A ROW NOT RUN BECAUSE ITS PARENT FAILED IS ANSWERED BY THE PARENT (TESTING-CLEANUP-2B fix round 3 F3). An
    inspector whose run fixture's parent went red is ctest's "Not Run" — NOTRUN, no run, so MISSING, which no verdict
    clears, and a --solo of it re-runs the red parent first: a KNOWN RED parent would block every merge selecting its
    inspector. The runner names the parent on the record (`notRunBecause`); a MISSING row whose newest such record names
    parents takes the WORST parent state at the tip when that state is RED or KNOWN (the parent's verdict covers both);
    a green / nondet / missing parent leaves it MISSING (its own assertion never ran; --resume / --solo run both). `out`: {key: (state, why, src)}, updated in place; `tip_recs`: {key: [records]};
    `judge_parent(key)` -> (state, why) for a parent not in `out`."""
    order = ["red", "missing", "known", "nondet", "green"]
    for k in list(out):
        if out[k][0] != "missing" or k[1]:
            continue
        # THE NEWEST RECORD OVERALL (fix round 4, item 1): an older parent-Not-Run beside a newer bare Not Run resolves
        # nothing
        runs_ = sorted((r for r in tip_recs.get(k, []) if r.get("kind") != "verdict"), key=_when)
        if not runs_ or runs_[-1].get("verdict") != "NOTRUN" or not runs_[-1].get("notRunBecause"):
            continue
        pst = []
        for pn in runs_[-1]["notRunBecause"]:
            pk = (pn, None)
            pst.append((pn,) + (tuple(out[pk][:2]) if pk in out else tuple(judge_parent(pk))))
        worst = min(pst, key=lambda t: order.index(t[1]) if t[1] in order else 0)
        # ONLY A RED OR A KNOWN RED PARENT ANSWERS IT (fix round 4, item 1): a green / nondet parent means the inspector's
        # own assertion never ran at the tip — it stays MISSING (a --solo of it runs the parent first)
        if worst[1] not in ("red", "known"):
            continue
        out[k] = (worst[1], f"not run: its parent {worst[0]} is {worst[1]} — answered by the parent's record and verdict "
                            f"({(worst[2] or '')[:100]})", tip_sha)


def judge(key, recs, defects, tip_recs=None, base_recs=None, tip_sha=None, mode="merge", proves=None):
    """(state, why) for one row/arm: state 'green' | 'known' | 'nondet' | 'missing' | 'red'.

    `defects`: the loaded registry (gate_runlog.defects_load()); its open `nondeterminism` entries are the
    contention class. `tip_recs` / `base_recs`: the row's records at the range's tip and base (the door's
    proofs; `tip_recs` defaults to `recs`). 'known' = KNOWN RED (a registered defect reproduced on the base):
    a merge passes it, a push or a stage close refuses it. 'nondet' = cleared as nondeterminism (an enrolled /
    single-use entry, `contention:`, the class's 3/3): a merge passes it, a push or stage close re-asks 3/3 solo at
    the candidate. `mode` (merge | push | stage-close): a single-use entry never clears outside a merge.

    A run that never happened (NOADMIT; NOTRUN) is no run: a row with only those is MISSING, which no verdict
    clears. A VERDICT is per row and timestamped: it clears only the reds logged BEFORE it, and only through
    THE DOOR; a refused verdict leaves its reds open, and the why begins `VERDICT REFUSED: `."""
    name, arm = key
    row = arm or name
    # GATE-COST-2: a MEASURING row's record that ran without its whole-card hold (`fallback`) is no evidence
    unheld = lambda r: r.get("fallback") and set(r.get("labels") or ()) & gate_runlog.MEASURING
    runs = sorted((r for r in recs if r.get("kind") != "verdict" and r.get("verdict") not in NEVER_RAN
                   and not unheld(r)), key=_when)
    verdicts = sorted((r for r in recs if r.get("kind") == "verdict"), key=_when)
    if not runs:
        never = [r.get("verdict") for r in recs if r.get("verdict") in NEVER_RAN]
        if any(unheld(r) for r in recs):
            return "missing", "measured under a fallback (no whole-card hold) — not evidence: re-run it"
        return "missing", "never run at the tip" + (f" ({never[-1]}: it never ran)" if never else "")
    reds = [r for r in runs if r.get("verdict") != "PASS"]
    if not reds:
        return "green", ""
    cleared, accepted, refused, known = None, None, None, False
    for v in verdicts:
        tv = _when(v)
        pending = [r for r in reds if _when(r) < tv and (cleared is None or _when(r) > cleared)]
        if not pending: continue
        last_gate = max((_when(r) for r in pending if not is_solo(r)), default=None)
        solos = [r for r in runs if is_solo(r) and last_gate is not None and _when(r) > last_gate]
        ok, why, kn = door(v.get("text"), pending, solos, row, defects,
                           tip_recs=recs if tip_recs is None else tip_recs, base_recs=base_recs, tip_sha=tip_sha,
                           mode=mode, proves=proves, key=key)
        if ok:
            cleared, accepted, refused = tv, (v, why), None
            known = "known" if "known" in (known, kn) else (kn or known)
        else:
            refused = (v, why)
    open_reds = [r for r in reds if cleared is None or _when(r) > cleared]
    if not open_reds:
        v, why = accepted
        return (known or "green"), f"recorded verdict ({why}): " + (v.get("text") or "")[:120]
    if refused is not None:
        return "red", f"VERDICT REFUSED: {refused[1]} — the verdict read: " + (refused[0].get("text") or "")[:100]
    reds = open_reds
    listed = row in gate_runlog.contention_of(defects) or name in gate_runlog.contention_of(defects)
    gate_reds = [r for r in reds if not is_solo(r)]
    last_gate_red = max((_when(r) for r in gate_reds), default=None)
    solos = [r for r in runs if is_solo(r) and (last_gate_red is None or _when(r) > last_gate_red)
             and (cleared is None or _when(r) > cleared)]
    solo_reds = [r for r in solos if r.get("verdict") != "PASS"]
    hard = sorted({r.get("verdict") for r in reds} & set(HARD))
    xid = [r for r in reds if r.get("xid")]
    if xid:
        return "red", (f"{reds[-1].get('verdict')} with an Xid from the row's own process (pid "
                       f"{xid[-1]['xid'].get('pid')}) — a DEFECT by law: needs real:<registered id>")
    if not listed:
        return "red", (f"{reds[-1].get('verdict')} and not in the contention class — needs a verdict through the door"
                       + (f" ({len(solos) - len(solo_reds)}/{len(solos)} solo PASS do not clear it)" if solos else ""))
    if hard:
        return "red", (f"{'/'.join(hard)} on a contention-class row: a {'/'.join(hard)} is never cleared by solos — "
                       f"needs real:<registered id>")
    if solo_reds:
        return "red", (f"contention-class, but a SOLO retry went red ({len(solos) - len(solo_reds)}/{len(solos)}): "
                       f"not contention — needs a verdict through the door")
    if not gate_reds:
        return "red", f"contention-class, red only in its solo retries — needs a verdict through the door"
    bare = [r for r in gate_reds if not census_of(r)]
    if bare:
        return "red", ("contention-class, but the red's competitor census shows no competition (no GPU process outside "
                       "the gate, no sibling ctest, no build, no pressure): the solos do not clear it")
    if len(solos) < SOLO_NEEDED:
        return "red", f"contention-class: {len(solos)}/{SOLO_NEEDED} solo PASS after its gate red (the law is 3/3)"
    return "nondet", (f"contention-class ({gate_runlog.contention_of(defects).get(row, gate_runlog.contention_of(defects).get(name))['id']}; "
                     f"{census_of(gate_reds[-1])[0]}), {len(solos)}/{len(solos)} solo PASS after the red")


def needed_rows(gs, S):
    """[(row, arm|None)] the refusal asks a record for, and the name of the selection. THE WHOLE TIER ONLY BY RULE
    (TESTING-SPEED-A item 1): `full_tier` — a top-level CMake command, a vendored path no one owns, a patch with no
    compiled copy, an unreadable fork diff. A fallback is its directory's subsystem; a pin move its component reach."""
    inv = S.inv
    gating = lambda n: not (inv[n]["labels"] & (gs.SCOPE_EXCLUDED_LABELS | gs.TARGET_LABELS))
    if S.full_tier:
        rows = [n for n, t in inv.items() if not (t["labels"] & (gs.STAGE_CLOSE_LABELS | gs.TARGET_LABELS))
                and S.own_lane_row(n)]
        return [(n, None) for n in rows], "the MERGE tier (by rule: %s)" % S.full_tier[0][:120]
    subsets = S.arm_subsets()
    need = []
    for n in sorted(S.selected):
        if not gating(n): continue
        need.append((n, None))
        for arm in subsets.get(n, []):
            need.append((n, f"{inv[n]['pool']}.{arm}"))
    what = "the reach" + (" (the pin move: the fork diff's components)" if S.fork_bump else "")
    return need, what


def _git(args, cwd):
    r = subprocess.run(["git"] + args, cwd=cwd, capture_output=True, text=True)
    return r.stdout.strip() if r.returncode == 0 else ""


def range_commits(gs, base, tip_sha):
    """The range's earlier commits, newest first: base..tip without the tip — EVERY one (RUNNER-A2: the integration-line
    exclusion is deleted; a d-build commit a forward merge carried in is a commit like any other, and its records are
    evidence like any other)."""
    revs = _git(["rev-list", "--topo-order", f"{base}..{tip_sha}"], gs.ROOT).split()
    return [c for c in revs if c != tip_sha]


_FORK_PINS = {}


def fork_pin(gs, irisgl_sha):
    """The ogre-next commit an irisgl commit pins ('' when it cannot be read: no re-use across it).
    Cached: the refusal asks it once per row per earlier commit."""
    if not irisgl_sha: return ""
    if irisgl_sha not in _FORK_PINS:
        _FORK_PINS[irisgl_sha] = _git(["rev-parse", f"{irisgl_sha}:thirdparty/ogre-next"],
                                      os.path.join(gs.ROOT, "irisgl"))
    return _FORK_PINS[irisgl_sha]


def solo_index(base_sha, root):
    """{key: {sha: [solo records]}} — ONE pass over the run log (TESTING-CLEANUP-2D read 1 F2: every caller shares it;
    it used to re-read the log per row on the carried path) indexing every clean sha's SOLO records (is_solo: tier solo or a retry run, no
    `kind`) of every row, under records_by_tip's filters (a clean tree, not a stale build, irisgl at THAT sha's pin —
    resolved in `root`, the selector's tree (I5) —, never a lane-tool record); the base itself excluded."""
    out = {}
    d = gate_runlog.log_dir()
    if not os.path.isdir(d): return out
    pins = {}
    for f in sorted(os.listdir(d)):
        if not f.endswith(".jsonl"): continue
        for line in open(os.path.join(d, f), errors="replace"):
            try: r = json.loads(line)
            except ValueError: continue
            if r.get("kind") or not is_solo(r) or r.get("tier") == "lane": continue
            t = r.get("tip") or {}
            sha = t.get("studio")
            if not sha or sha == base_sha or t.get("studio_dirty") or t.get("irisgl_dirty"): continue
            if gate_runlog.stale_build(t): continue
            if sha not in pins:
                pins[sha] = _git(["rev-parse", f"{sha}:irisgl"], root)
            if pins[sha] and t.get("irisgl") != pins[sha]: continue
            out.setdefault((r.get("suite"), r.get("arm")), {}).setdefault(sha, []).append(r)
    return out


def ancestor_base_proof(key, base_sha, solos_at, is_ancestor, distance, reaches):
    """THE BASE'S PROOF TRAVELS WHILE NOTHING REACHES THE ROW (TESTING-CLEANUP-2D item 2): (records, A, note). `solos_at`:
    {sha: [solo records of the row]}; the NEAREST sha (by ancestry from the base: `distance`, ties by the newest record)
    that is an ANCESTOR of the base is the one candidate — one hop, never an older one behind it. Its solos prove the row
    at the base only when every one is red (green beside red is nondeterminism) and `reaches(A, base, key)` is False (the
    caller's range answers ALL for anything unreadable: the proof does not travel). The records come back tagged
    `_proofFrom: A` (the door's why names it); refusals return ([], None, why)."""
    # A CANDIDATE HOLDS A RUN (read 2 I2): a sha whose solos are all NOADMIT / NOTRUN never ran the row — no run, the same
    # rule as at the base — so it is not the one hop and never hides an older ancestor's red
    ran = {a: [r for r in rs if r.get("verdict") not in NEVER_RAN] for a, rs in solos_at.items()}
    cands = [a for a, rs in ran.items() if rs and a != base_sha and is_ancestor(a, base_sha)]
    if not cands:
        return [], None, "no ancestor of the base holds a run of the row"
    a = min(cands, key=lambda s: (distance(s, base_sha), -max(_when(r).timestamp() for r in ran[s])))
    sol = ran[a]
    green = sum(1 for r in sol if r.get("verdict") == "PASS")
    if green == len(sol):
        return [], None, f"{a[:9]}'s solos are green: no red to prove"
    if green:
        return [], None, (f"{a[:9]} (the nearest ancestor with solos) has {green} green of {len(sol)} — nondeterminism, "
                          f"never a base proof")
    if reaches(a, base_sha, key):
        return [], None, f"{a[:9]}..{base_sha[:9]} reaches the row (or cannot be read) — {a[:9]}'s solos prove nothing here"
    return ([dict(r, _proofFrom=a) for r in sol], a,
            f"the base's proof is {a[:9]}'s solos; {a[:9]}..{base_sha[:9]} does not reach the row")


def tree_paths(gs, a, b):
    """The paths whose content differs between commit a's tree and b's (irisgl's own diff between their pins, prefixed)."""
    files = [x for x in _git(["diff", "--name-only", a, b], gs.ROOT).splitlines() if x]
    out = [f for f in files if f != "irisgl"]
    if "irisgl" in files:
        old, new = _git(["rev-parse", f"{a}:irisgl"], gs.ROOT), _git(["rev-parse", f"{b}:irisgl"], gs.ROOT)
        ig = os.path.join(gs.ROOT, "irisgl")
        out += ["irisgl/" + x for x in _git(["diff", "--name-only", old, new], ig).splitlines() if x]
    return sorted(set(out))


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

    def between(self, a, b, unreadable=ALL, fork_rows=None):
        """What the scoped selection of <a>..<b> reaches: a set of keys, or ALL (a fork pin change, a fallback, the
        tier by rule). `unreadable`: the answer for a range the selector cannot read or that selects nothing —
        ALL for re-use (the safe side there), an empty set for PROVING a fix (the safe side there)."""
        ck = (a, b, unreadable is Reach.ALL, fork_rows is not None, self.gs.WHOLE_RANGES)   # per caller's safe side
        if ck in self.cache: return self.cache[ck]
        rng = f"{a}..{b}"
        reach = unreadable
        try:
            # a NON-ANCESTOR a (the lane's pre-rebase twin, item 2): what changed between the two TREES, not a range's
            # commits (own_base would read the whole lane from the merge-base)
            if subprocess.run(["git", "merge-base", "--is-ancestor", a, b], cwd=self.gs.ROOT).returncode != 0:
                paths = tree_paths(self.gs, a, b)
                if not paths:
                    self.cache[ck] = set()
                    return self.cache[ck]
            else:
                paths = self.gs.touched_paths(rng)
            S = self.gs.select(paths, rng, self.build, self.gs.GATE_JOBS, graph=self.graph,
                               inv=self.copy.deepcopy(self.inv0), quiet_graph=True)
            if S.full_tier or (S.fork_bump and fork_rows is None):
                reach = Reach.ALL
            else:
                # (TESTING-SPEED-A item 1: a FALLBACK is its directory's subsystem — what it selected, never ALL)
                subsets = S.arm_subsets()
                reach = set()
                for n in S.selected:
                    reach.add((n, None))
                    pool = S.inv[n].get("pool")
                    if n in subsets:
                        reach |= {(n, f"{pool}.{arm}") for arm in subsets[n]}
                    elif pool:
                        reach.add((n, "*"))      # the whole pool: every arm
                # A STAGE-CLOSE ROW THE SELECTOR SET ASIDE WAS STILL REACHED (TESTING-CLEANUP-2D read 1 F1, read 2 F1): gate-
                # scope pops a stage-close row reached by a rule or the graph — anything but its own subject — into
                # stage_close_left for COST only (the batch runs it); unlike a fallback it WAS reached, so it is a reach on
                # EVERY side: for re-use, for a travelling base proof, and for PROVING a fix (every arm with it)
                for n in getattr(S, "stage_close_left", None) or {}:
                    reach |= {(n, None), (n, "*")}
                # THE TOOLS' ROWS A PRODUCT PATH REACHED (TESTING-SPEED-A item 5) are not RUN by the merge, but they were
                # reached: never served across the range
                for n in getattr(S, "tools_dropped", None) or ():
                    reach |= {(n, None), (n, "*")}
                # A PIN MOVE, FOR SERVING (RUNNER-A2, rule 3): every row that reaches the fork by the graph, every arm
                if S.fork_bump and fork_rows is not None:
                    reach |= {(n, None) for n in fork_rows} | {(n, "*") for n in fork_rows}
        except SystemExit:
            reach = unreadable
        self.cache[ck] = reach
        return reach

    @staticmethod
    def reaches(reach, key):
        if reach is Reach.ALL: return True
        return key in reach or (key[1] is not None and (key[0], "*") in reach)


class Prover:
    """ROUND 2 F1: may a PASS at <b> prove the fix of a red at <a>? Only when <a> is an ANCESTOR of <b> and the scoped
    selection of <a>..<b> REACHES the row — a PASS at a commit that does not touch the row (a docs-only or empty
    commit, the red's own sha) is the same code passing again: nondeterminism, 3/3 solo."""

    def __init__(self, gs, build, reach_fn):
        self.gs, self.build, self.reach_fn, self.anc = gs, build, reach_fn, {}

    def ancestor(self, a, b):
        if (a, b) not in self.anc:
            self.anc[(a, b)] = subprocess.run(["git", "merge-base", "--is-ancestor", a, b], cwd=self.gs.ROOT,
                                              capture_output=True).returncode == 0
        return self.anc[(a, b)]

    def __call__(self, red_sha, pass_sha, key):
        if not red_sha or not pass_sha or red_sha == pass_sha: return False
        if not self.ancestor(red_sha, pass_sha): return False
        return Reach.reaches(self.reach_fn().between(red_sha, pass_sha, unreadable=set()), key)


# ---- THE KEY: A GREEN HOLDS UNTIL A CHANGE REACHES THE ROW (RUNNER-A2; TESTING_RUNNER_SPEC v2.1 rule 2, §3.2) ---------
# ONE PREDICATE, used by `runner.sh select` (the merge run's row set) and by the judge (a row with no run at the tip):
# the newest record of the row at an ANCESTOR A of the tip C answers the row at C when
#   * the WHOLE range A..C (forward merges included — gate-scope.WHOLE_RANGES) does not reach the row: the scoped
#     selector's reach (the graph, the symbols, the area rules); a pin move reaches every row that reaches the fork by the
#     graph (FORK_ROWS) on top; a fallback, the tier by rule or an unreadable range reaches everything;
#   * the record is admissible (a run of a gating tier; a lane-tool record only when complete), its BUILT_FROM clean
#     (records_by_tip drops a stale or dirty build), from irisgl at A's own pin;
#   * the fork pin at A equals C's, or the row does not reach the fork;
#   * the row's DECLARATION and DATA hashes in the record equal the build's at C (gate_graph: root-stripped) — a null
#     on either side is UNKNOWN and never equal (a record before schema 3 serves nothing);
#   * the row is not MEASURING (gate_runlog.MEASURING: timing / quiet-box / perf: a measurement is never served) and not a REGISTERED
#     NONDETERMINISM (an open `nondeterminism` entry: its green is not a fact);
# then that record's state is the row's at C — green serves; a red stays red until a change reaches it. Candidates are
# the tip's ancestors that hold records, newest first; the newest one holding a run of the row decides (an older A
# lies behind a range that holds the newer one's).
SERVE_SELECTIONS_MAX = 40      # selections one serving pass may compute (a range each; ~1 s each on a cached graph)


def fork_rows(gs, build, graph, inv):
    """{row} that REACH THE FORK by the graph: a row whose executables (or the app, for an app row) depend on any file
    of irisgl/thirdparty/ogre-next (the checkout's media, the install's headers and libraries) — what a pin move reaches
    (rule 3)."""
    if graph is None:
        return set(inv)
    fe = set()
    for n in list(graph.rev):
        if "/irisgl/thirdparty/ogre-next" in n:
            fe |= graph.reach(n)
    app = os.path.normpath(os.path.join(build, "bin", "Jahshaka"))
    return {n for n, t in inv.items() if (t.get("exes") or set()) & fe or (t.get("kind") == "app" and app in fe)}


class Server:
    """THE KEY, as one object per tip: the facts at C (the build's rows: labels, declaration, data hash), the fork rows,
    the registered nondeterminism, and the selections A..C (cached)."""

    def __init__(self, gs, build, tip_sha, tip_fork, defects, reach):
        self.gs, self.build, self.tip, self.tip_fork, self.reach = gs, build, tip_sha, tip_fork, reach
        self.facts, self.config, _ = gate_runlog.row_facts(build)
        self.inv = reach.inv0
        gs.classify_rows(self.inv, reach.graph, build)
        self.fork = fork_rows(gs, build, reach.graph, self.inv)
        self.nondet = {}
        for e in (defects or {}).values():
            if e.get("kind") == "nondeterminism" and gate_runlog.defect_state(e) == "open":
                for r in e.get("rows") or []:
                    self.nondet.setdefault(r, e["id"])
        self.sel = {}           # A -> the reach of A..C (a set of keys, or Reach.ALL)
        self.computed = 0

    def row_never(self, key):
        """Why the row is never served whatever its records (None when it may be)."""
        n = key[0]
        labels = set((self.inv.get(n) or {}).get("labels") or ())
        if labels & gate_runlog.MEASURING:
            return f"MEASURING ({'/'.join(sorted(labels & gate_runlog.MEASURING))}): a measurement is never served"
        if n in self.nondet or (key[1] and key[1] in self.nondet):
            return f"a REGISTERED NONDETERMINISM ({self.nondet.get(n) or self.nondet.get(key[1])}): its green is not a fact"
        return None

    def record_why_not(self, key, r, a_fork):
        """Why record r at A cannot stand for the row at C, the selection aside (None when it can)."""
        if r.get("tier") == "target" or r.get("gating") is False:
            return f"a non-gating record (tier {r.get('tier')})"
        if r.get("fallback") and set(r.get("labels") or ()) & gate_runlog.MEASURING:
            return "a measurement under a fallback"
        if key[0] in self.fork and (not a_fork or a_fork != self.tip_fork):
            return f"the fork pin moved ({(a_fork or '?')[:9]} -> {(self.tip_fork or '?')[:9]}) and the row reaches the fork"
        f = self.facts.get(key[0])
        if f is None:
            return "the row is not in the build's ctest inventory"
        d = (r.get("declaration") or {}).get("hash") if isinstance(r.get("declaration"), dict) else None
        if not d:
            return "its record carries no declaration (unknown is never equal: a record before schema 3)"
        if d != (f.get("declaration") or {}).get("hash"):
            return "its DECLARATION changed since the record"
        if not r.get("data_hash") or not f.get("data_hash"):
            return "a data hash is unknown (never equal)"
        if r["data_hash"] != f["data_hash"]:
            return "its DATA changed since the record"
        return None

    def reach_of(self, a):
        """The reach of the WHOLE range A..C, for serving (cached; the budget spent: ALL)."""
        if a in self.sel:
            return self.sel[a]
        if self.computed >= SERVE_SELECTIONS_MAX:
            return Reach.ALL
        if subprocess.run(["git", "diff", "--quiet", a, self.tip], cwd=self.gs.ROOT).returncode == 0:
            self.sel[a] = set()          # the same tree (a merge that changed nothing): nothing reaches anything
            return self.sel[a]
        self.computed += 1
        old = self.gs.WHOLE_RANGES
        self.gs.WHOLE_RANGES = True
        try:
            r = self.reach.between(a, self.tip, unreadable=Reach.ALL, fork_rows=self.fork)
        finally:
            self.gs.WHOLE_RANGES = old
        self.sel[a] = r
        return r


def serve_key(key, server, cands, got, judge_at):
    """THE PREDICATE applied: ("served", A, state, why) | ("red", A, why) | ("unserved", why) for one row at the tip.
    `cands`: [(A, A's fork pin)] newest first; `got`: records_by_tip's {A: {key: [records]}}; `judge_at(key, recs)` ->
    (state, why) of the records at A (the flake law, the door)."""
    never = server.row_never(key)
    if never:
        return ("unserved", never)
    first_why = None
    for a, a_fork in cands:
        recs = [r for r in got.get(a, {}).get(key, []) if r.get("kind") != "verdict"]
        runs = [r for r in recs if r.get("verdict") not in NEVER_RAN]
        if not runs:
            continue
        newest = max(runs, key=_when)
        why = server.record_why_not(key, newest, a_fork)
        if why:
            return ("unserved", f"{a[:9]}'s record: {why}")
        reach = server.reach_of(a)
        if Reach.reaches(reach, key) and a in _TWINS:
            # A TWIN IS NOT AN ANCESTOR (TESTING-SPEED-A): the lane's non-ancestor tips are unordered among themselves — one
            # whose tree diff reaches the row says nothing of the next; the walk goes on (ancestors stay terminal: an older
            # one's range reaches at least what a newer one's does)
            first_why = first_why or f"{a[:9]}..{server.tip[:9]} reaches it (a twin)"
            continue
        if Reach.reaches(reach, key):
            return ("unserved", (f"{a[:9]}..{server.tip[:9]} reaches it" if reach is not Reach.ALL else
                                 f"{a[:9]}..{server.tip[:9]} reaches everything (a fallback, the tier by rule, an "
                                 f"unreadable range, or the selection budget spent)"))
        st, swhy = judge_at(key, got[a].get(key, []))
        if st in PASSING:
            where = (f"its record AT the candidate {a[:9]} (rule 1: tested once at its tip)" if a == server.tip else
                     f"{a[:9]}..{server.tip[:9]} does not reach it")
            return ("served", a, st, f"served from {a[:9]} ({newest.get('tier')}, {newest.get('run') or '-'}; {where})"
                    + (f"; {swhy}" if swhy else ""))
        if st == "red":
            return ("red", a, f"red at {a[:9]}, and {a[:9]}..{server.tip[:9]} does not reach it (answer it there): {swhy}")
        first_why = first_why or f"{a[:9]}: {st} ({swhy})"
    return ("unserved", first_why or "no record of it at any ancestor of the tip")


def lane_tips(gs, lane, tip_sha, limit=8):
    """THE LANE'S OWN EARLIER TIPS THAT ARE NOT ANCESTORS of the tip (RUNNER-FIX-4 item 2: a rebase rewrites the lane's
    commits — VOXEL-READOUT-1's builder records sat at c8d21b245, the pre-rebase twin of the candidate 498931e06): every
    clean tip holding a RUN record of the lane's own (`lanes == [<lane>]`), newest record first. Their reach to the tip is
    the TREE DIFF (Reach.between): a row its inputs did not change in is served from there (rule 2: the inputs equal).
    candidates() asks them right after the tip itself, before the ancestors."""
    if not lane:
        return []
    d = gate_runlog.log_dir()
    newest = {}
    if not os.path.isdir(d):
        return []
    for f in os.listdir(d):
        if not f.endswith(".jsonl"):
            continue
        for line in open(os.path.join(d, f), errors="replace"):
            if lane not in line:
                continue
            try:
                r = json.loads(line)
            except ValueError:
                continue
            t = r.get("tip") or {}
            sha = t.get("studio")
            if not sha or sha == tip_sha or t.get("studio_dirty") or t.get("irisgl_dirty") or not is_run(r):
                continue
            if gate_runlog.own_lane(r) != lane:
                continue
            w = _when(r)
            if sha not in newest or w > newest[sha]:
                newest[sha] = w
    out = []
    for sha in sorted(newest, key=lambda x: newest[x], reverse=True):
        if subprocess.run(["git", "cat-file", "-e", f"{sha}^{{commit}}"], cwd=gs.ROOT).returncode != 0:
            continue
        if subprocess.run(["git", "merge-base", "--is-ancestor", sha, tip_sha], cwd=gs.ROOT).returncode == 0:
            continue                       # an ancestor is a candidate already
        out.append(sha)
        if len(out) >= limit:
            break
    return out


_TWINS = set()       # the lane's non-ancestor tips of the last candidates() call (serve_key walks past a reaching twin)


def candidates(gs, tip_sha, pins_of=None, lane=None):
    """[(A, A's irisgl pin, A's fork pin)] — THE TIP ITSELF and every ancestor of it holding a record in the run log,
    newest first (topological: the tip first), and the irisgl pin its records must carry. THE TIP IS A CANDIDATE
    (RUNNER-AUDIT-FIX-1 F4, rule 1): a clean, stamped, same-pin, non-measuring green record AT the candidate — the
    builder's self-audit rows at its tip, which a fast-forward makes the candidate — is the row tested once at its tip;
    C..C reaches nothing, and the same predicates (records_by_tip, row_never, record_why_not) decide."""
    have = set()
    d = gate_runlog.log_dir()
    if os.path.isdir(d):
        rx = re.compile(r'"studio": "([0-9a-f]{40})"')
        for f in os.listdir(d):
            if f.endswith(".jsonl"):
                with open(os.path.join(d, f), errors="replace") as fh:
                    have |= set(rx.findall(fh.read()))
    out = []
    for c in _git(["rev-list", "--topo-order", tip_sha], gs.ROOT).split():
        if c in have:
            ig = _git(["rev-parse", f"{c}:irisgl"], gs.ROOT)
            out.append((c, ig or None, fork_pin(gs, ig)))
    # THE LANE'S OWN NON-ANCESTOR TIPS (a rebase's pre-rebase twin; item 2), when the lane is named — right AFTER the tip
    # itself and BEFORE the ancestors: serve_key stops at the first candidate holding a record (newest first: an older
    # ancestor's range reaches at least what a newer one's does), and the twin's tree diff to the tip is the smallest
    # (VOXEL-READOUT-1: c8d21b245..498931e06 is one tooling file; an ancestor 60cd0e674's range is the whole lane)
    twins = []
    _TWINS.clear()
    for c in lane_tips(gs, lane, tip_sha):
        _TWINS.add(c)
        ig = _git(["rev-parse", f"{c}:irisgl"], gs.ROOT)
        twins.append((c, ig or None, fork_pin(gs, ig)))
    at = 1 if out and out[0][0] == tip_sha else 0
    return out[:at] + twins + out[at:]


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


MODES = ("merge", "push", "stage-close")
PASSING = ("green", "known", "nondet")    # judge() states a merge accepts (known / nondet are re-asked at a push)


def stage_close_rows(gs, build):
    """THE STAGE-CLOSE SELECTION (STAGE-CLOSE-1): every row of the build labelled with STAGE_CLOSE_LABELS — the rows the
    MERGE and PUSH tiers leave out, which the stage-close batch runs. The judge over them is check()'s, unchanged."""
    inv = gs.load_inventory(build)
    return [(n, None) for n in sorted(inv) if inv[n]["labels"] & gs.STAGE_CLOSE_LABELS]


def tier_need(gs, S, need):
    """THE TIER AS THE UNIVERSE — `--full` ONLY (the SHIP; TESTING-SPEED-A item 1: a merge's universe is its reach): the
    scoped keys plus EVERY other row of the merge tier (not stage-close, not a target, not quiet-box, not another lane's
    `lane:` row) — each answered at the tip or SERVED (rule 2). A MEASURING row the change does not reach is not asked
    (a measurement is never served; it runs when reached, at a stage close, at a ship)."""
    inv, have = S.inv, set(need)
    sel = {k[0] for k in need}
    out = list(need)
    for n in sorted(inv):
        if inv[n]["labels"] & (gs.STAGE_CLOSE_LABELS | gs.TARGET_LABELS | gs.SCOPE_EXCLUDED_LABELS):
            continue
        if not S.own_lane_row(n):
            continue
        if (n, None) in have or ((inv[n]["labels"] & gate_runlog.MEASURING) and n not in sel):
            continue
        out.append((n, None))
    return out


def check(rng, build, gs=None, verdicts=None, mode="merge", lane=None, stage_close=False, full=False, named=()):
    """(ok, reasons) for a range. `verdicts`: {row or pool.arm: text} recorded for those rows first
    (per row, timestamped; a row that is not red now is refused as a verdict, said out loud). THE UNIVERSE
    (TESTING-SPEED-A item 1): the range's REACH (its scoped rows) + `named` (the lane's brief rows) — a row outside is not
    the change's and is not asked; `full` (the SHIP only) every merge-tier row, at the tip or SERVED (tier_need). `mode`:
    merge (a KNOWN RED passes) | push | stage-close (a KNOWN RED refuses: never push on a red). `lane`:
    the lane whose own records carry their open reds to this tip (default: the lane named by the tip's
    own records, else the tip's branch name)."""
    gs = gs or load_gs()
    if mode not in MODES:
        return None, [f"mode '{mode}' is not one of {', '.join(MODES)}"]
    CURRENT_LANES.clear()
    if lane:
        CURRENT_LANES.add(lane)
    defects, problem = gate_runlog.defects_load()
    if defects is None:
        return None, [f"{problem} — the door cannot check a verdict without it (JAH_DEFECTS_FILE overrides)"]
    base, tip = rng.split("..", 1)
    tip_sha = subprocess.run(["git", "rev-parse", tip], cwd=gs.ROOT, capture_output=True, text=True).stdout.strip()
    if not tip_sha:
        return None, [f"cannot resolve {tip}"]
    base_sha = _git(["rev-parse", base], gs.ROOT) or base
    if stage_close:
        # THE STAGE-CLOSE JUDGE: the same judge (the door, the Prover against the base, the aborts, a nondeterminism
        # clearance re-asked 3/3 at the candidate) over a different SELECTION — every stage-close row, at a push or
        # a stage close only (a merge never judges the batch)
        if mode == "merge":
            return None, ["--stage-close judges a push or a stage close: --mode push|stage-close (never merge)"]
        need, what = stage_close_rows(gs, build), "the stage-close rows"
        if not need:
            return None, [f"{build} registers no stage-close row — not the build of a tree with the batch"]
    else:
        S = gs.select(gs.touched_paths(rng), rng, build, gs.GATE_JOBS, quiet_graph=True, lane=lane)
        need, what = needed_rows(gs, S)
        have = set(need)
        for n in sorted(set(named or ()) - {k[0] for k in need}):
            if n in S.inv and (n, None) not in have:
                need.append((n, None))
        if full and not S.full_tier:
            need, what = tier_need(gs, S, need), f"the merge tier ({what} at the tip, every other row served or run: --full)"
    pin = _git(["rev-parse", f"{tip_sha}:irisgl"], gs.ROOT)
    label = lambda k: f"{k[0]}{' :: ' + k[1] if k[1] else ''}"
    # THE RANGE'S EARLIER COMMITS, and EVERY ANCESTOR of the tip holding records (the serving candidates, RUNNER-A2), with
    # the pins they ran on (records at each are read in one pass)
    earlier = range_commits(gs, base_sha, tip_sha)
    cands = candidates(gs, tip_sha, lane=lane)        # the lane named: its non-ancestor tips serve too (item 2)
    pins = {tip_sha: pin or None}
    for c, ig, _ in cands:
        pins[c] = ig
    for c in earlier:
        if c not in pins:
            pins[c] = _git(["rev-parse", f"{c}:irisgl"], gs.ROOT) or None
    # THE BASE (the door's `real:<id> pre-existing` reads the red reproduced there by a recorded solo)
    if base_sha not in pins:
        pins[base_sha] = _git(["rev-parse", f"{base_sha}:irisgl"], gs.ROOT) or None
    reach = None
    server_ = []

    def get_server():
        if not server_:
            server_.append(Server(gs, build, tip_sha, fork_pin(gs, pin), defects, get_reach()))
        return server_[0]

    def get_reach():
        nonlocal reach
        if reach is None:
            reach = Reach(gs, build, tip_sha, fork_pin(gs, pin))
        return reach
    prover = Prover(gs, build, get_reach)

    def dropped(k, st, why, at):
        """GATE-COST-2's abort record: a row it dropped RED needs 3/3 solo PASS after the abort, whatever else
        answered it (the re-run is a solo, never a quiet answer)."""
        for ab in ABORTS.get(at, []):
            names = ab.get("droppedRed") or []
            names = [x if isinstance(x, str) else (x or {}).get("row") for x in names]
            if k[0] in names or (k[1] and k[1] in names):
                after = [r for r in got_now.get(at, {}).get(k, []) if is_solo(r) and _when(r) > _when(ab)]
                if not _solos_ok(after):
                    return "red", (f"DROPPED RED by the abort at {(ab.get('ts') or '')[:16]} — needs 3/3 solo PASS after "
                                   f"it ({sum(1 for r in after if r.get('verdict') == 'PASS')}/{len(after)})")
        return st, why

    got_now = {}
    anc = {}                # TESTING-CLEANUP-2D item 2: {key: (records, A, note)} — the ancestor proof, read once per check
    anc_solos = []          # the second pass over the log, lazily, once

    dist = {}               # rev-list distance A..base, per A (read 1 F2)

    def _distance(a, b):
        if a not in dist:
            dist[a] = int(_git(["rev-list", "--count", f"{a}..{b}"], gs.ROOT) or 10 ** 9)
        return dist[a]

    def base_of(k, got):
        """THE BASE'S PROOF for one row (TESTING-CLEANUP-2D item 2) — the ONE helper every base_recs reads. The base's own
        records when it holds ANY run of the row (the lead's rule, read 1 I2: a gate PASS at the base is the base's own
        evidence — a red at the candidate is then a regression, never known — and its solos are the proof); only a base
        with NO run of the row looks to the nearest ancestor (ancestor_base_proof). LAZY (read 1 F2): a callable the door
        calls only when it reaches the KNOWN RED proof (_real) — a green row never reads the archive, never prints."""
        own = got.get(base_sha, {}).get(k, [])
        if any(r.get("kind") != "verdict" and r.get("verdict") not in NEVER_RAN for r in own):
            return own

        def proof():
            if k not in anc:
                if not anc_solos:
                    anc_solos.append(solo_index(base_sha, gs.ROOT))
                anc[k] = ancestor_base_proof(
                    k, base_sha, anc_solos[0].get(k, {}), prover.ancestor, _distance,
                    lambda a, b, key: Reach.reaches(get_reach().between(a, b, unreadable=Reach.ALL), key))
                if anc[k][1] or anc[k][2] != "no ancestor of the base holds a run of the row":
                    print(f"ci-gate-check: base proof for {label(k)}: {anc[k][2]}")
            return own + anc[k][0]
        return proof

    def with_range_verdicts(recs, k, got):
        """A VERDICT COVERS ITS RANGE (TESTING-CLEANUP-2D item 1): an earlier commit's records of the row plus
        the verdict records at the TIP — the door and its timestamp rule decide what they clear."""
        return list(recs) + [v for v in got[tip_sha].get(k, []) if v.get("kind") == "verdict" and v not in recs]

    def judge_all():
        """{key: (state, why, source sha)} — the tip's records first, then (for a row the tip has no
        run of) the newest earlier commit's, under the rule in this file's header. AND THE FLAKE LAW
        ACROSS COMMITS (the lead's merge read, F1): a row green at the tip is still refused while an
        earlier commit that the fix does NOT reach holds an open red of it (no verdict; a contention
        red without its 3/3) — a green re-run at a later commit does not answer a red the fix never
        touched; a verdict does, or --solo 3/3 for the contention class."""
        nonlocal reach
        ABORTS.clear()
        got = records_by_tip(pins)
        got_now.clear(); got_now.update(got)
        out = {}
        tip_fork = fork_pin(gs, pin)
        have_earlier = [c for c in earlier if got[c]]
        for k in need:
            ctx = dict(tip_recs=got[tip_sha].get(k, []), base_recs=base_of(k, got), tip_sha=tip_sha,
                       mode=mode, proves=prover)
            st, why = judge(k, got[tip_sha].get(k, []), defects, **ctx)
            src = tip_sha
            if st in PASSING and have_earlier:
                if reach is None:
                    reach = Reach(gs, build, tip_sha, tip_fork)
                for c in have_earlier:
                    # ROUND 3 F-A: the tip's run answers an earlier commit's red only when c..tip PROVES it (c an ancestor,
                    # the range REACHING the row; an empty / unreadable range reaches nothing — never ALL here)
                    if prover(c, tip_sha, k):
                        break
                    recs = got[c].get(k, [])
                    if not recs: continue
                    cst, cwhy = judge(k, with_range_verdicts(recs, k, got), defects, **ctx)
                    if cst == "red":
                        st, src = "red", c
                        why = (f"an OPEN red at {c[:9]} that {c[:9]}..{tip_sha[:9]} does not reach — the green at the "
                               f"tip does not answer it (a verdict does; --solo 3/3 for the contention class): {cwhy}")
                        break
            if st == "missing" and cands:
                # THE KEY (RUNNER-A2): the newest record of the row at an ancestor A whose WHOLE range A..tip does not
                # reach it answers it — served when green, red until answered (serve_key; the one predicate select uses)
                sv = serve_key(k, get_server(), [(c, f) for c, _, f in cands], got,
                               lambda kk, recs: judge(kk, with_range_verdicts(recs, kk, got), defects, **ctx))
                if sv[0] == "served":
                    st, src, why = sv[2], sv[1], sv[3]
                elif sv[0] == "red":
                    st, src, why = "red", sv[1], sv[2]
                else:
                    why += f"; not served: {sv[1]}"
            st, why = dropped(k, st, why, src)
            # (the tip's OWN red or no run at all — a red carried from an earlier commit keeps its own proof path)
            if mode == "merge" and st in ("red", "missing") and src == tip_sha:
                kn, kwhy = KnownRegistry(defects, lambda kk: base_of(kk, got)).known(k, got[tip_sha].get(k, []))
                if kn:
                    st, why, src = "known", kwhy + " (the registry: no hand verdict needed)", tip_sha
            out[k] = (st, why, src)
        resolve_not_run(out, got[tip_sha], lambda pk: judge(pk, got[tip_sha].get(pk, []), defects,
                                                            tip_recs=got[tip_sha].get(pk, []),
                                                            base_recs=base_of(pk, got),
                                                            tip_sha=tip_sha, mode=mode, proves=prover), tip_sha)
        # U4: THE LANE'S OTHER TIPS (a rebase, a superseded fix round): an open red there that the
        # current tip never re-ran (a later record of the same row+arm at the tip that passes) and no
        # accepted verdict answered is carried to this tip
        carried.clear()
        # THE LANE: named, else the lane the tip's (and the lane's earlier commits') OWN records name, else the
        # tip's branch — only `lanes == [<lane>]` records carry
        lanes = {lane} if lane else set()
        if not lanes:
            for c in [tip_sha] + earlier:
                for rs in got.get(c, {}).values():
                    for r in rs:
                        if r.get("kind") != "verdict" and gate_runlog.own_lane(r): lanes.add(gate_runlog.own_lane(r))
        if not lanes and not re.fullmatch(r"[0-9a-f]{7,40}", tip):
            lanes.add(tip)
        CURRENT_LANES.update(lanes)
        for t, recs in lane_records(lanes, {tip_sha, base_sha, *earlier}).items():
            for k, rs in recs.items():
                # A ROW OUTSIDE THE MERGE TIER IS NEVER A MERGE VERDICT AND NEVER CARRIED (RUNNER-FIX-4 item 14, the lead's
                # rule): its LABELS in this build put it outside (the stage-close rows, the *-target rows — red by design),
                # whatever tier its record names (VOXEL-READOUT-1's self-audits recorded five photon-target rows red under
                # tier `lane`); a stage close judges its own rows, so the rule is the merge's and the push's
                if not stage_close and k[0] in outside_tier:
                    continue
                # A RED OF A ROW THE REGISTRY ANSWERS FOR THIS CANDIDATE (round 8; round 6 did it for by-design entries):
                # at ANY earlier tip of the lane (an ancestor, a twin, a pre-rebase tip) it is answered by the same entry —
                # the registry answers the ROW, not one record of it — never carried
                if mode == "merge" and KnownRegistry(defects, lambda kk: base_of(kk, got)).known(
                        k, got[tip_sha].get(k, []))[0]:
                    continue
                cst, cwhy = judge(k, rs, defects, tip_recs=got[tip_sha].get(k, []),
                                  base_recs=base_of(k, got), tip_sha=tip_sha, mode=mode,
                                  proves=prover)
                if cst != "red": continue
                last = max(_when(r) for r in rs if r.get("kind") != "verdict" and r.get("verdict") != "PASS")
                at_tip = got[tip_sha].get(k, [])
                later = [r for r in at_tip if r.get("kind") != "verdict" and r.get("verdict") not in NEVER_RAN
                         and _when(r) > last]
                # ROUND 2: a lane tip that DESCENDS from the checked tip holds a red newer than the tip's code — a
                # tip PASS is pre-bug and answers nothing (the same ancestry rule as the fix's proof)
                # ROUND 3 (U4): a tip PASS answers a carried red only when the tip DESCENDS from the red's tip and the
                # range REACHES the row (the Prover) — a rebased-away tip or one that descends from the checked tip carries
                if later and prover(t, tip_sha, k) \
                        and judge(k, at_tip, defects, tip_sha=tip_sha, mode=mode, proves=prover)[0] in PASSING: continue
                # A RED AT A NON-ANCESTOR LANE TIP (round 5, 2 — rule 2's twin logic, FIX-4 item 2): a rebase leaves the
                # pre-rebase tip off the candidate's history, so "a PASS at a descendant that reaches the row" can never
                # come; the TREE diff from that tip to the candidate decides — it does not reach the row: the red is the
                # same code's and is carried; it REACHES the row: the candidate's own record decides (green there: not
                # carried; no run there: the row's own judgement at the tip says missing, never a carried red)
                if not prover.ancestor(t, tip_sha):
                    if Reach.reaches(get_reach().between(t, tip_sha, unreadable=Reach.ALL), k):
                        st_tip = judge(k, at_tip, defects, tip_sha=tip_sha, mode=mode, proves=prover)[0] if at_tip \
                            else "missing"
                        if st_tip != "red":
                            continue
                carried[(t, k)] = cwhy
        return out

    carried = {}
    # the rows the merge tier never takes, by this build's labels (gate-scope's own sets: the MERGE tier = every row but
    # the stage-close and the target rows — its serial phase runs the timing rows; needed_rows' "MERGE tier" too)
    try:
        _inv = gs.load_inventory(build)
    except Exception:          # no inventory: nothing is set aside (the safe side: carried as before)
        _inv = {}
    outside_tier = {n for n, t in _inv.items() if set(t.get("labels") or ()) & (gs.STAGE_CLOSE_LABELS | gs.TARGET_LABELS)}
    judged = judge_all()

    def carried_order(item):
        """THE ORDER OF THE CARRIED REDS (RUNNER-FIX-4 item 10): (tip, row, arm) with a row's own record (arm None) before
        its arms — a key's arm is None or a str, and a pool row carried with one of its arms (VOXEL-READOUT-1's fourth
        fire) made sorted() compare None with a str: the judge crashed and every merge stopped."""
        (t, k), _why = item
        return (t, k[0] or "", k[1] is not None, k[1] or "")
    if verdicts:
        pairs, unknown = {}, []
        for name, text in verdicts.items():
            keys = [k for k in need if k[0] == name or (k[1] and k[1] == name)]
            # (a row the REGISTRY answers as a known red (round 7) still takes the lead's verdict: recorded, harmless)
            red_keys = [k for k in keys if judged[k][0] == "red" or (judged[k][0] == "known" and "(the registry" in judged[k][1])]
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
    known = [k for k in need if judged[k][0] == "known"]
    for k in known:
        print(f"KNOWN RED {label(k)}: {judged[k][1]}" + ("" if mode == "merge" else f" — a {mode} refuses it (never push on a red)"))
    for (t, k), why in sorted(carried.items(), key=carried_order):
        print(f"OPEN RED carried from {t[:9]}: {label(k)} — {why}")
        if why.startswith("VERDICT REFUSED: "):
            print(f"VERDICT REFUSED {label(k)}: {why[len('VERDICT REFUSED: '):]}")
    missing = [label(k) for k, (st, _, _) in judged.items() if st == "missing"]
    red = [f"{label(k)}: {why}" for k, (st, why, _) in judged.items() if st == "red"]
    if mode != "merge":
        red += [f"{label(k)}: KNOWN RED — {judged[k][1]} (a {mode} never passes a red)" for k in known]
        # F4: a row cleared as NONDETERMINISM must be 3/3 green AT THE CANDIDATE for a push / stage close
        for k in need:
            if judged[k][0] != "nondet": continue
            solos = sorted((r for r in got_now.get(tip_sha, {}).get(k, []) if is_solo(r)), key=_when)[-SOLO_NEEDED:]
            if not _solos_ok(solos):
                red.append(f"{label(k)}: cleared as nondeterminism ({judged[k][1][:80]}) but not 3/3 solo green at the "
                           f"candidate {tip_sha[:9]} ({sum(1 for r in solos if r.get('verdict') == 'PASS')}/{len(solos)}) "
                           f"— a {mode} re-asks it there")
        # every law switch is written into the records as `overrides`: a push / stage close refuses a candidate
        # whose records carry any (TESTING_V3 §1.2)
        srcs = {tip_sha} | {src for (_, _, src) in judged.values()}
        ov = sorted({str(o) for c in srcs for rs in got_now.get(c, {}).values() for r in rs for o in (r.get("overrides") or [])})
        if ov:
            red.append(f"OVERRIDES in the candidate's records ({', '.join(ov[:6])}) — a {mode} refuses them")
    cleared = [f"{label(k)}: {why}" for k, (st, why, src) in judged.items()
               if st in ("green", "nondet") and why and src == tip_sha]
    reused = {}
    for k, (st, _, src) in judged.items():
        if st in PASSING and src != tip_sha: reused[src] = reused.get(src, 0) + 1
    reasons = []
    if missing: reasons.append(f"{len(missing)} of {len(need)} row(s) of {what} have no record at {tip_sha[:9]} "
                               f"(nor one SERVED from an ancestor: rule 2): {missing[:8]}")
    for r in red[:20]: reasons.append("RED " + r)
    for (t, k), why in sorted(carried.items(), key=carried_order)[:20]:
        reasons.append(f"OPEN RED carried from {t[:9]}: {label(k)} — {why}")
    if not (missing or red or carried):
        at_tip = len(need) - sum(reused.values())
        if known: reasons.append(f"KNOWN RED (a merge passes them; a push or a stage close does not): "
                                 f"{[label(k) for k in known][:8]}")
        reasons.append(f"{what}: {len(need)} row(s) green — {at_tip} at {tip_sha[:9]}"
                       + "".join(f", {n} served from {c[:9]}" for c, n in reused.items())
                       + (f"; cleared by the law: {cleared[:6]}" if cleared else ""))
    return not (missing or red or carried), reasons


# THE ESTIMATE IS WALL TIME AT THE PASS WIDTHS (TESTING-SPEED-A item 4): a merge prints its selection and its estimate
# BEFORE anything runs, and the runner refuses one over this many seconds without the lead's `--go`
ESTIMATE_LIMIT_S = 900
PASS_ORDER = ("named", "picture", "measuring", "rest")


def known_red(rng, row, gs=None):
    """THE EARLY STOP'S KNOWN-RED PREDICATE (TESTING-SPEED-A fix round, item 1): (known, why) for a row red at the range's
    tip. The merge run stops early on THE LANE'S red only — never on a red the registry already names: KNOWN when an
    open registry entry (testing/defects.json: not retired, not past its recheck) names the row AND either the nearest
    ancestor of the base (the base itself first) that holds a run of the row holds only reds, each tip red's signature
    (verdict class, masked failLine) among them (the door's own _sig), or NO ancestor holds a run of it at all (a NEW row
    declared red by design: the door and the lead decide, never the early stop). Never "the record at exactly the base":
    a base without its own run of the row looks to the nearest ancestor that has one. The door (judge) stays the
    verdict: this only says whether the early stop may refuse."""
    gs = gs or load_gs()
    defects, problem = gate_runlog.defects_load()
    if defects is None:
        return False, f"{problem}: no registry, nothing is known"
    ents = [e for e in defects.values() if row in (e.get("rows") or []) and gate_runlog.defect_state(e) != "retired"
            and not gate_runlog.recheck_past(e)]
    if not ents:
        return False, f"no open registry entry names {row}"
    ids = ", ".join(sorted(str(e.get("id")) for e in ents))
    base, tip = rng.split("..", 1)
    tip_sha, base_sha = _git(["rev-parse", tip], gs.ROOT), _git(["rev-parse", base], gs.ROOT)
    if not tip_sha or not base_sha:
        return False, f"cannot resolve {rng}"
    cands = [(c, ig) for c, ig, _ in candidates(gs, base_sha)]          # the base first, then its ancestors, newest first
    pins = {tip_sha: _git(["rev-parse", f"{tip_sha}:irisgl"], gs.ROOT) or None}
    for c, ig in cands:
        pins.setdefault(c, ig)
    got = records_by_tip(pins)
    key = (row, None)
    runs = lambda recs: [r for r in recs if r.get("kind") != "verdict" and r.get("verdict") not in NEVER_RAN]
    reds = [r for r in runs(got[tip_sha].get(key, [])) if r.get("verdict") != "PASS"]
    if not reds:
        return False, f"no red record of {row} at {tip_sha[:9]}"
    at = next((c for c, _ in cands if runs(got[c].get(key, []))), None)
    if at is None:
        return True, (f"registered ({ids}) and no ancestor of the base holds a run of {row} (a new row): the door "
                      f"decides, not the early stop")
    sig = lambda r: (r.get("verdict"), masked(r.get("failLine")), masked(r.get("status")))
    there = runs(got[at][key])
    base_reds = [r for r in there if r.get("verdict") != "PASS"]
    if not base_reds:
        return False, f"registered ({ids}) but {at[:9]} (the nearest ancestor of the base with a run) is green: the lane's red"
    if len(base_reds) < len(there):
        return False, (f"registered ({ids}) but {at[:9]} holds {len(there) - len(base_reds)} green beside "
                       f"{len(base_reds)} red: nondeterminism there, not one known red")
    odd = [r for r in reds if sig(r) not in {sig(b) for b in base_reds}]
    if odd:
        o = odd[-1]
        return False, (f"registered ({ids}) but the tip's {o.get('verdict')} "
                       f"'{masked(o.get('failLine') or o.get('status'))[:60]}' is not {at[:9]}'s red")
    return True, f"KNOWN RED ({ids}): the same red at {at[:9]}, the nearest ancestor of the base holding a run of {row}"


def passes(gs, inv, run_rows, named, arm_subsets=None, costs=None):
    """THE PASSES (TESTING-SPEED-A item 3; SPECS/TESTING_SPEED_SPEC.md §2 A5): the run rows in the order a red is found
    soonest — [the lane's named rows] -> [the PICTURE set] -> [the measuring rows, -j1 on the whole card] -> [the rest];
    a non-measuring pass runs as up to two batches, its fixtures at GATE_JOBS["fixture"] and its app rows / pools at
    GATE_JOBS["app"] (the VRAM tokens stay the bound under both). Each batch: {rows, jobs, whole, serial_s, wall_s};
    a pass's wall is its batches' sum; the plan's estimate the passes' sum (a row's price: load_costs, 10 s unknown)."""
    costs = gs.load_costs() if costs is None else costs
    arm_subsets = arm_subsets or {}
    J = gs.GATE_JOBS

    def cost(n):
        if n in arm_subsets:
            return 10.0 + sum(costs.get(f"{n}::{a}", 10.0) for a in arm_subsets[n])
        return float(costs.get(n, 10.0))
    named = set(named or ())
    sets = {p: [] for p in PASS_ORDER}
    for n in sorted(set(run_rows)):
        t = inv.get(n) or {"labels": set(), "kind": "other"}
        if t["labels"] & gate_runlog.MEASURING:
            sets["measuring"].append(n)
        elif n in named:
            sets["named"].append(n)
        elif t.get("picture") or set(arm_subsets.get(n, t.get("arms") or {})) & set(t.get("picture_arms") or ()):
            sets["picture"].append(n)
        else:
            sets["rest"].append(n)
    out = []
    for name in PASS_ORDER:
        rows = sets[name]
        if not rows:
            continue
        if name == "measuring":
            groups = [("measuring", rows, J["measuring"], True)]
        else:
            fx = [n for n in rows if (inv.get(n) or {}).get("kind") != "app"]
            ap = [n for n in rows if (inv.get(n) or {}).get("kind") == "app"]
            groups = [(k, rs, J[k], False) for k, rs in (("fixture", fx), ("app", ap)) if rs]
        batches = []
        for klass, rs, jobs, whole in groups:
            ser = sum(cost(n) for n in rs)
            wall = max(ser / max(1, jobs), max(cost(n) for n in rs))
            # THE POOLS' ARMS (fix round item 4): a pool the change reaches by some arms runs THOSE arms — the batch carries
            # JAH_POOL_ARMS (`<pool>.<arm>,...`; a pool absent from it runs every arm), as its estimate prices it
            pa = ",".join(f"{(inv.get(n) or {}).get('pool') or n.split('.', 1)[-1]}.{a}" for n in rs
                          for a in arm_subsets.get(n, ()))
            batches.append({"class": klass, "rows": rs, "jobs": jobs, "whole": whole, "serial_s": round(ser, 1),
                            "wall_s": round(wall, 1), "pool_arms": pa})
        out.append({"name": name, "batches": batches, "rows": rows,
                    "serial_s": round(sum(b["serial_s"] for b in batches), 1),
                    "wall_s": round(sum(b["wall_s"] for b in batches), 1)})
    return out


def plan(rng, build, gs=None, named=(), lane=None, full=False):
    """THE MERGE RUN'S ROW SET (`runner.sh select`; TESTING-SPEED-A item 1 — rule 3 of the runner spec restored: a merge
    runs what the change reaches and nothing else): {run: {key: reason}, served: {key: {sha, run, tier, ts, state, why}},
    passes, estimate_s, serial_s, tip, base, what, selected, universe: n}. THE UNIVERSE IS THE REACH: gate-scope's
    selection over B..C (the lane's own `lane:` rows included, nobody else's; a pin move: the fork diff's components)
    plus the rows the lane's brief names (`named`). A row outside it is NOT run, NOT served, NOT listed: it is not the
    change's. `full` (the SHIP only) takes the whole merge tier (tier_need). Every row of the universe is first asked of
    serve_key, the judge's own predicate, over C ITSELF and its ancestors (rule 1: the builder's green record at the
    candidate is the row tested once at its tip; a reached row is served only from a record whose range to C does not
    reach it, which for a reached row is C's own); the rest RUN, each with its reason, in PASSES (passes())."""
    gs = gs or load_gs()
    defects, problem = gate_runlog.defects_load()
    if defects is None:
        raise SystemExit(f"select: {problem}")
    base, tip = rng.split("..", 1)
    tip_sha = _git(["rev-parse", tip], gs.ROOT)
    base_sha = _git(["rev-parse", base], gs.ROOT)
    if not tip_sha or not base_sha:
        raise SystemExit(f"select: cannot resolve {rng}")
    S = gs.select(gs.touched_paths(rng), rng, build, gs.GATE_JOBS, quiet_graph=True, lane=lane)
    pin = _git(["rev-parse", f"{tip_sha}:irisgl"], gs.ROOT)
    reach = Reach(gs, build, tip_sha, fork_pin(gs, pin))
    server = Server(gs, build, tip_sha, fork_pin(gs, pin), defects, reach)
    scoped, what = needed_rows(gs, S)
    named = set(named or ())
    universe = list(scoped)
    have = set(universe)
    for n in sorted(named):
        if n in S.inv and (n, None) not in have:
            universe.append((n, None))
    if full and not S.full_tier:
        universe, what = tier_need(gs, S, universe), f"the merge tier (--full: {what} + every other row, served or run)"
    cands = candidates(gs, tip_sha, lane=lane)         # the lane named: its non-ancestor tips serve too (item 2)
    pins = {c: ig for c, ig, _ in cands}
    got = records_by_tip(pins)
    run, served = {}, {}
    sel_keys = set(scoped)
    reasons = {n: r for n, r in (S.selected or {}).items()}
    CURRENT_LANES.clear()
    if lane:
        CURRENT_LANES.add(lane)
    plan_got = {}
    prov = Prover(gs, build, lambda: reach)
    anc_idx = []

    def plan_base(kk):
        """The door's base proof for one row at the select (base_of's rule): the base's own runs, else the one hop."""
        if not plan_got:
            plan_got.update(records_by_tip({base_sha: _git(["rev-parse", f"{base_sha}:irisgl"], gs.ROOT) or None}))
        own = plan_got.get(base_sha, {}).get(kk, [])
        if any(r.get("kind") != "verdict" and r.get("verdict") not in NEVER_RAN for r in own):
            return own
        if not anc_idx:
            anc_idx.append(solo_index(base_sha, gs.ROOT))
        dist = lambda a, b: int(_git(["rev-list", "--count", f"{a}..{b}"], gs.ROOT) or 10 ** 9)
        recs, _a, _why = ancestor_base_proof(kk, base_sha, anc_idx[0].get(kk, {}), prov.ancestor, dist,
                                             lambda a, b, key: Reach.reaches(reach.between(a, b, unreadable=Reach.ALL), key))
        return own + recs
    known_reg = KnownRegistry(defects, plan_base)
    for k in universe:
        # A REGISTERED KNOWN RED IS SERVED AS SUCH (round 7): never run, never re-run — the judge reads the same registry
        kn, kwhy = known_reg.known(k, got.get(tip_sha, {}).get(k, []))
        if kn:
            served[k] = {"sha": base_sha, "run": "registry", "tier": "known-red", "state": "known",
                         "ts": datetime.datetime.now().astimezone().isoformat(timespec="seconds"), "why": kwhy}
            continue
        sv = serve_key(k, server, [(c, f) for c, _, f in cands], got,
                       lambda kk, recs: judge(kk, recs, defects, tip_sha=tip_sha))
        if sv[0] != "served" and k in sel_keys:
            run[k] = ("reached by " + rng + (f": {str(reasons.get(k[0]))[:120]}" if reasons.get(k[0]) else ""))
        elif sv[0] != "served" and k[0] in named:
            run[k] = "named by the lane's brief"
        elif sv[0] == "served":
            a = sv[1]
            newest = max((r for r in got[a].get(k, []) if r.get("kind") != "verdict"
                          and r.get("verdict") not in NEVER_RAN), key=_when)
            served[k] = {"sha": a, "run": newest.get("run"), "tier": newest.get("tier"), "ts": newest.get("ts"),
                         "state": sv[2], "why": sv[3]}
        else:
            run[k] = sv[-1]
    in_u = {k[0] for k in universe}
    measuring = sorted(n for n in in_u if S.inv.get(n, {}).get("labels", set()) & gate_runlog.MEASURING)
    run_rows = sorted({k[0] for k in run})
    subsets = {}
    for (n, arm) in run:
        if arm and S.inv.get(n, {}).get("pool") and n not in S.whole:
            subsets.setdefault(n, []).append(arm.split(".", 1)[1] if "." in arm else arm)
    ps = passes(gs, S.inv, run_rows, named, subsets)
    return {"tip": tip_sha, "base": base_sha, "irisgl": pin, "fork": fork_pin(gs, pin), "what": what,
            "measuring_run": measuring, "fork_rows": sorted(server.fork), "full": bool(full or S.full_tier),
            "fallback": list(S.fallback), "selected": sorted(sel_keys, key=str), "universe": len(universe), "run": run,
            "served": served, "selections": server.computed, "passes": ps,
            "estimate_s": round(sum(p["wall_s"] for p in ps), 1), "serial_s": round(sum(p["serial_s"] for p in ps), 1),
            "estimate_limit_s": ESTIMATE_LIMIT_S}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("range")
    ap.add_argument("--stage-close", action="store_true",
                    help="THE STAGE-CLOSE JUDGE: judge EVERY stage-close row at the range's tip (not the range's "
                         "selection), in --mode stage-close (the default with this flag) or push; refuse while one is "
                         "red, a KNOWN RED, an unproved nondeterminism clearance or unrecorded")
    ap.add_argument("--build", default="build-linux")
    ap.add_argument("--mode", default=None, choices=MODES,
                    help="merge: a KNOWN RED passes; push / stage-close: it refuses (never push on a red)")
    ap.add_argument("--lane", default=None, help="the lane whose own records carry open reds to the tip")
    ap.add_argument("--full", action="store_true",
                    help="THE SHIP's universe: every merge-tier row, at the tip or SERVED by rule 2 (tier_need) — the "
                         "default universe is the range's REACH + the --named rows (TESTING-SPEED-A item 1)")
    ap.add_argument("--plan", action="store_true",
                    help="print the merge run's row set as JSON (`runner.sh select`): run (with reasons) and served")
    ap.add_argument("--known-red", default=None, metavar="ROW",
                    help="print {row, known, why} as JSON: is the row's red at the tip a KNOWN red (known_red: the "
                         "registry + the nearest ancestor of the base holding a run) — the merge run's early stop asks")
    ap.add_argument("--named", action="append", default=[], help="a row the lane's brief names: in the universe (run "
                    "by --plan unless served at the tip; asked by the judge)")
    # action="extend": `--verdict a=x b=y` and `--verdict a=x --verdict b=y` (the form the runner
    # builds) both record EVERY verdict — with nargs="+" alone a repeated flag kept only the last one
    ap.add_argument("--verdict", nargs="+", action="extend", default=None, metavar="ROW=TEXT",
                    help="record a verdict per red row (`<row or pool.arm>=<the reader's text>`) in the run log, "
                         "then re-check; it clears only the reds logged before it (repeatable)")
    a = ap.parse_args()
    if a.known_red:
        k, why = known_red(a.range, a.known_red, load_gs())
        print(json.dumps({"row": a.known_red, "known": k, "why": why}))
        sys.exit(0)
    verdicts = None
    if a.verdict:
        verdicts = {}
        for pair in a.verdict:
            if "=" not in pair or not pair.split("=", 1)[1].strip():
                ap.error(f"--verdict takes <row>=<text> pairs, got '{pair}'")
            k, v = pair.split("=", 1)
            verdicts[k.strip()] = v.strip()
    gs = load_gs()
    if a.plan:
        out = plan(a.range, gs.resolve_build(a.build), gs, named=set(a.named), lane=a.lane, full=a.full)
        lab = lambda k: k[0] + (f" :: {k[1]}" if k[1] else "")
        print(json.dumps(dict(out, run={lab(k): v for k, v in out["run"].items()},
                              served={lab(k): v for k, v in out["served"].items()},
                              selected=[lab(k) for k in out["selected"]]), sort_keys=True))
        sys.exit(0)
    mode = a.mode or ("stage-close" if a.stage_close else "merge")
    if a.stage_close and mode == "merge":
        ap.error("--stage-close judges a push or a stage close: --mode push|stage-close, never merge")
    ok, reasons = check(a.range, gs.resolve_build(a.build), gs, verdicts=verdicts, mode=mode, lane=a.lane,
                        stage_close=a.stage_close, full=a.full, named=set(a.named))
    if ok is None:
        for r in reasons: print("ci-gate-check: UNUSABLE — " + r)
        sys.exit(2)
    for r in reasons: print(("ci-gate-check: " if ok else "ci-gate-check: REFUSED — ") + r)
    if not ok and a.stage_close:
        print("ci-gate-check: run the stage close at the tip (`runner.sh stage-close <tag>`, or by hand "
              "`scripts/gate-scope.sh --stage-close-tier --run`); a red answers to a fix or a verdict through the door")
        sys.exit(1)
    if not ok:
        print(f"ci-gate-check: run the FIX ROUND `scripts/gate-scope.sh <pre-fix tip>..{a.range.split('..', 1)[1]} --run` "
              f"(§3b; or `{a.range} --run` for a lane never gated); a contention-class red takes `--solo <suite>` "
              f"(3/3); any other red a verdict: `--verdict \"<row>=<text>\" ...`")
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
