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

THE VERDICT DOOR (TESTING_V3_SPEC §1.4; VERDICT-1; door() below): a verdict clears a red only by a REGISTERED FACT
— `real:<ID>` (in testing/defects.json for this row; proved by a PASS at a later sha that reaches the row, or by the SAME red reproduced
by a recorded solo at the base = KNOWN RED: `--mode merge` passes it, `--mode push|stage-close` refuses it),
`contention:` (an open nondeterminism row whose red's competitor census shows competition, 3/3 solo), `xid-read:`
(a LOST/OOM/CRASH whose journal was unreadable). A refused verdict prints `VERDICT REFUSED <row>: <why>`. A REBASE
CARRIES ITS OPEN REDS from the lane's OWN records (`lanes == [<lane>]`) at any other tip (`OPEN RED carried from
<tip>`); batch records never carry. A registry entry without its shape makes the judge unusable (exit 2).

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

SOLO_NEEDED = 3


def load_gs():
    spec = importlib.util.spec_from_file_location("gate_scope", os.path.join(HERE, "gate-scope.py"))
    gs = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(gs)
    return gs


ABORTS = {}        # {tip: [kind: abort records]} — read with the records (their droppedRed rows need 3/3 solo)


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
            # N5: a lane-tool record (tier `lane`) never answers a row for the judge — the batch / push gate must run
            # it (lane_records reads them for the carried reds; a red is a red wherever it ran)
            if r.get("tier") == "lane": continue
            if r.get("kind") == "abort":
                ABORTS.setdefault(sha, []).append(r); continue
            out[sha].setdefault((r.get("suite"), r.get("arm")), []).append(r)
    for recs in out.values():
        for v in recs.values():
            v.sort(key=lambda r: r.get("ts") or "")
    return out



def lane_records(lanes, skip):
    """VERDICT-1 U4 — A REBASE CARRIES ITS OPEN REDS: {tip: {(suite, arm): [records]}} of every gating
    record that is a LANE'S OWN record of a lane in `lanes` (`lanes == [<lane>]`, no `batch` tag —
    TESTING_V3 §1.4: batch records never carry) at ANY tip not in `skip`, ancestor or not, with every
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
            if (r.get("schema") or 1) < 2 or r.get("gating") is False or r.get("tier") == "target": continue
            if gate_runlog.own_lane(r) not in lanes: continue
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
# A red whose record carries an `xid` takes `real:` only. Solos below 3/3 are red. `environmental`, ENOSPC and a dead
# display are never verdicts. Nothing accepts prose.
DEFECT_ID = re.compile(r"\breal:\s*([A-Z][A-Z0-9]*(?:-[A-Z0-9]+)*-\d+[a-z]?)\b")
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


def masked(line):
    """A failLine with its numbers masked — the same failure reads the same at base and tip."""
    return re.sub(r"\d+(\.\d+)?", "#", (line or "").strip())


def _sha(r):
    return (r.get("tip") or {}).get("studio")


def _real(m, row, reds, solos, defects, tip_recs, base_recs, tip_sha=None, mode="merge", proves=None, key=None):
    """`real:<ID>` checked: (ok, why, kind) — kind None | "known" (KNOWN RED) | "nondet" (cleared as nondeterminism:
    a push / stage close re-asks it 3/3 at the candidate)."""
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
    # base FAIL into a CRASH, or another assertion, is not "known")
    lane_red = sorted(reds, key=_when)[-1]
    same = [r for r in (base_recs or []) if r.get("kind") != "verdict" and r.get("retry")
            and r.get("verdict") == lane_red.get("verdict") and masked(r.get("failLine")) == masked(lane_red.get("failLine"))
            and masked(r.get("status")) == masked(lane_red.get("status"))]
    if same:
        return True, (f"real:{did} KNOWN RED (the same {lane_red.get('verdict')}"
                      f"{' — ' + masked(lane_red.get('failLine'))[:60] if lane_red.get('failLine') else ''} reproduced by a "
                      f"recorded solo at the base)"), "known"
    other = [r for r in (base_recs or []) if r.get("kind") != "verdict" and r.get("retry")
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
    spass = sum(1 for r in solos if r.get("verdict") == "PASS")
    if solos and not _solos_ok(solos):
        return False, (f"solos below 3/3 ({spass}/{len(solos)} solo PASS after the red) are red — never cleared by "
                       f"text, listed or not"), False
    hard = kinds & set(HARD)
    if hard:
        cls = "/".join(sorted(hard))
        if real: return _real(real, row, reds, solos, defects, tip_recs, base_recs, tip_sha, mode, proves, key)
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
        bare = [r for r in reds if not r.get("retry") and not census_of(r)]
        if bare:
            return False, (f"contention: needs a COMPETITOR CENSUS showing real competition in the red's record (GPU "
                           f"processes outside the gate, sibling ctests, builds, memory/IO pressure — never the gate's "
                           f"own queue or drain) — the red at {_when(bare[-1]).isoformat(timespec='minutes')} shows none"), False
        if _solos_ok(solos):
            return True, (f"contention: ({contention[row]['id']}; {census_of([r for r in reds if not r.get('retry')][-1])[0]}) "
                          f"with {spass}/{len(solos)} solo PASS"), "nondet"
        return False, f"contention: needs 3/3 solo PASS after the red ({spass}/{len(solos)}) — gate-scope.sh --solo <row>", False
    return False, ("a verdict carries a registered fact: real:<id> (in testing/defects.json, proved by a PASS at a "
                   "later sha that reaches the row, or the same red reproduced on the base) or contention: (a nondeterminism row, a competitor census, 3/3 "
                   "solo) — nothing accepts prose"), False


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
    runs = sorted((r for r in recs if r.get("kind") != "verdict" and r.get("verdict") not in NEVER_RAN), key=_when)
    verdicts = sorted((r for r in recs if r.get("kind") == "verdict"), key=_when)
    if not runs:
        never = [r.get("verdict") for r in recs if r.get("verdict") in NEVER_RAN]
        return "missing", "never run at the tip" + (f" ({never[-1]}: it never ran)" if never else "")
    reds = [r for r in runs if r.get("verdict") != "PASS"]
    if not reds:
        return "green", ""
    cleared, accepted, refused, known = None, None, None, False
    for v in verdicts:
        tv = _when(v)
        pending = [r for r in reds if _when(r) < tv and (cleared is None or _when(r) > cleared)]
        if not pending: continue
        last_gate = max((_when(r) for r in pending if not r.get("retry")), default=None)
        solos = [r for r in runs if r.get("retry") and last_gate is not None and _when(r) > last_gate]
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
    gate_reds = [r for r in reds if not r.get("retry")]
    last_gate_red = max((_when(r) for r in gate_reds), default=None)
    solos = [r for r in runs if r.get("retry") and (last_gate_red is None or _when(r) > last_gate_red)
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
        """What <a>..<the tip> reaches — for RE-USE: anything unreadable reaches everything (no re-use across it)."""
        if a_fork and a_fork == self.tip_fork:
            return self.between(a, self.tip, unreadable=Reach.ALL)
        return Reach.ALL

    def between(self, a, b, unreadable=ALL):
        """What the scoped selection of <a>..<b> reaches: a set of keys, or ALL (a fork pin change, a fallback, the
        tier by rule). `unreadable`: the answer for a range the selector cannot read or that selects nothing —
        ALL for re-use (the safe side there), an empty set for PROVING a fix (the safe side there)."""
        ck = (a, b, unreadable is Reach.ALL)          # the same range answers differently per caller's safe side
        if ck in self.cache: return self.cache[ck]
        rng = f"{a}..{b}"
        reach = unreadable
        try:
            S = self.gs.select(self.gs.touched_paths(rng), rng, self.build, self.gs.GATE_JOBS, graph=self.graph,
                               inv=self.copy.deepcopy(self.inv0), quiet_graph=True)
            if S.fallback or S.full_tier or S.fork_bump:
                reach = Reach.ALL
            else:
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


def check(rng, build, gs=None, verdicts=None, mode="merge", lane=None):
    """(ok, reasons) for a range. `verdicts`: {row or pool.arm: text} recorded for those rows first
    (per row, timestamped; a row that is not red now is refused as a verdict, said out loud). `mode`:
    merge (a KNOWN RED passes) | push | stage-close (a KNOWN RED refuses: never push on a red). `lane`:
    the lane whose own records carry their open reds to this tip (default: the lane named by the tip's
    own records, else the tip's branch name)."""
    gs = gs or load_gs()
    if mode not in MODES:
        return None, [f"mode '{mode}' is not one of {', '.join(MODES)}"]
    defects, problem = gate_runlog.defects_load()
    if defects is None:
        return None, [f"{problem} — the door cannot check a verdict without it (JAH_DEFECTS_FILE overrides)"]
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
                after = [r for r in got_now.get(at, {}).get(k, []) if r.get("retry") and _when(r) > _when(ab)]
                if not _solos_ok(after):
                    return "red", (f"DROPPED RED by the abort at {(ab.get('ts') or '')[:16]} — needs 3/3 solo PASS after "
                                   f"it ({sum(1 for r in after if r.get('verdict') == 'PASS')}/{len(after)})")
        return st, why

    got_now = {}

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
            ctx = dict(tip_recs=got[tip_sha].get(k, []), base_recs=got.get(base_sha, {}).get(k, []), tip_sha=tip_sha,
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
                    cst, cwhy = judge(k, recs, defects, **ctx)
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
                    cst, cwhy = judge(k, recs, defects, **ctx)
                    if cst == "missing": continue
                    st, src = cst, c
                    why = (f"re-used from {c[:9]} ({c[:9]}..{tip_sha[:9]} does not reach it)"
                           + (f"; {cwhy}" if cwhy else "")) if cst in PASSING else \
                          (f"red at {c[:9]}, the newest run of it on the lane ({c[:9]}..{tip_sha[:9]} does not reach "
                           f"it; answer it there): {cwhy}")
                    break
            st, why = dropped(k, st, why, src)
            out[k] = (st, why, src)
        # U4: THE LANE'S OTHER TIPS (a rebase, a superseded fix round): an open red there that the
        # current tip never re-ran (a later record of the same row+arm at the tip that passes) and no
        # accepted verdict answered is carried to this tip
        carried.clear()
        # THE LANE: named, else the lane the tip's (and the lane's earlier commits') OWN records name, else the
        # tip's branch — only `lanes == [<lane>]` records carry (batch records never do)
        lanes = {lane} if lane else set()
        if not lanes:
            for c in [tip_sha] + earlier:
                for rs in got.get(c, {}).values():
                    for r in rs:
                        if r.get("kind") != "verdict" and gate_runlog.own_lane(r): lanes.add(gate_runlog.own_lane(r))
        if not lanes and not re.fullmatch(r"[0-9a-f]{7,40}", tip):
            lanes.add(tip)
        for t, recs in lane_records(lanes, {tip_sha, base_sha, *earlier}).items():
            for k, rs in recs.items():
                cst, cwhy = judge(k, rs, defects, tip_recs=got[tip_sha].get(k, []),
                                  base_recs=got.get(base_sha, {}).get(k, []), tip_sha=tip_sha, mode=mode,
                                  proves=prover)
                if cst != "red": continue
                last = max(_when(r) for r in rs if r.get("kind") != "verdict" and r.get("verdict") != "PASS")
                at_tip = got[tip_sha].get(k, [])
                later = [r for r in at_tip if r.get("kind") != "verdict" and r.get("verdict") not in NEVER_RAN
                         and _when(r) > last]
                # ROUND 2: a lane tip that DESCENDS from the checked tip holds a red newer than the tip's code — a
                # tip PASS is pre-bug and answers nothing (the same ancestry rule as the fix's proof)
                if later and not prover.ancestor(tip_sha, t) \
                        and judge(k, at_tip, defects, tip_sha=tip_sha, mode=mode, proves=prover)[0] in PASSING: continue
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
    known = [k for k in need if judged[k][0] == "known"]
    for k in known:
        print(f"KNOWN RED {label(k)}: {judged[k][1]}" + ("" if mode == "merge" else f" — a {mode} refuses it (never push on a red)"))
    for (t, k), why in sorted(carried.items()):
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
            solos = sorted((r for r in got_now.get(tip_sha, {}).get(k, []) if r.get("retry")), key=_when)[-SOLO_NEEDED:]
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
                               f"(nor a re-usable one earlier on the lane): {missing[:8]}")
    for r in red[:20]: reasons.append("RED " + r)
    for (t, k), why in sorted(carried.items())[:20]:
        reasons.append(f"OPEN RED carried from {t[:9]}: {label(k)} — {why}")
    if not (missing or red or carried):
        at_tip = len(need) - sum(reused.values())
        if known: reasons.append(f"KNOWN RED (a merge passes them; a push or a stage close does not): "
                                 f"{[label(k) for k in known][:8]}")
        reasons.append(f"{what}: {len(need)} row(s) green — {at_tip} at {tip_sha[:9]}"
                       + "".join(f", {n} re-used from {c[:9]}" for c, n in reused.items())
                       + (f"; cleared by the law: {cleared[:6]}" if cleared else ""))
    return not (missing or red or carried), reasons


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("range")
    ap.add_argument("--build", default="build-linux")
    ap.add_argument("--mode", default="merge", choices=MODES,
                    help="merge: a KNOWN RED passes; push / stage-close: it refuses (never push on a red)")
    ap.add_argument("--lane", default=None, help="the lane whose own records carry open reds to the tip")
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
    ok, reasons = check(a.range, gs.resolve_build(a.build), gs, verdicts=verdicts, mode=a.mode, lane=a.lane)
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
