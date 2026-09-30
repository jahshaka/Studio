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

A range that MOVES THE FORK PIN needs the whole MERGE tier at the tip (§7b rule 4: the one full
tier per bump, `gate-scope.sh <range> --run --fork-tier`; the lane's own gates select by the fork
diff's reach). So does a selection that fell back or is the tier by rule.

THE VERDICT: `--verdict "<text>"` records one `VERDICT` record per currently red row (or only the
`--verdict-suite` ones) into the run log (`<date>-verdict-<tip>.jsonl`, the same directory; the
reader's text and the reds it answered) and re-checks. scripts/lead/merge-dbuild-lane.sh calls this
and refuses the merge on a failure; its own `--verdict` passes through here.

Exit 0 accepted, 1 refused, 2 unusable (no contention list, an unresolvable range).
"""
import argparse
import datetime
import importlib.util
import json
import os
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


def records_at(tip, irisgl=None):
    """(suite, arm) -> [records at this tip, oldest first] (clean tree, irisgl at the tip's pin),
    across every file of the log — the verdict records included (kind == "verdict")."""
    out = {}
    d = gate_runlog.log_dir()
    if not os.path.isdir(d): return out
    for f in sorted(os.listdir(d)):
        if not f.endswith(".jsonl"): continue
        for line in open(os.path.join(d, f)):
            try: r = json.loads(line)
            except ValueError: continue
            t = r.get("tip") or {}
            if t.get("studio") != tip or t.get("studio_dirty") or t.get("irisgl_dirty"): continue
            # the engine that ran must be the one the tip PINS (F1): a record from a tree whose
            # irisgl was checked out elsewhere tested other code
            if irisgl and t.get("irisgl") != irisgl: continue
            out.setdefault((r.get("suite"), r.get("arm")), []).append(r)
    for v in out.values():
        v.sort(key=lambda r: r.get("ts") or "")
    return out


def judge(key, recs, contention):
    """(state, why) for one row/arm: state 'green' | 'missing' | 'red'."""
    runs = [r for r in recs if r.get("kind") != "verdict"]
    verdicts = [r for r in recs if r.get("kind") == "verdict"]
    if not runs:
        return "missing", "never run at the tip"
    reds = [r for r in runs if r.get("verdict") != "PASS"]
    if not reds:
        return "green", ""
    if verdicts:
        return "green", "recorded verdict: " + (verdicts[-1].get("text") or "")[:120]
    name, arm = key
    listed = name in contention or (arm and arm in contention)
    gate_reds = [r for r in reds if not r.get("retry")]
    last_gate_red = max((r.get("ts") or "" for r in gate_reds), default="")
    solos = [r for r in runs if r.get("retry") and (r.get("ts") or "") > last_gate_red]
    solo_reds = [r for r in solos if r.get("verdict") != "PASS"]
    if not listed:
        return "red", (f"{reds[-1].get('verdict')} and not in the contention class — needs a recorded verdict"
                       + (f" ({len(solos) - len(solo_reds)}/{len(solos)} solo PASS do not clear it)" if solos else ""))
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


def record_verdicts(keys, text, tip_sha, pin):
    now = datetime.datetime.now().astimezone()
    path = os.path.join(gate_runlog.log_dir(), f"{now.date().isoformat()}-verdict-{tip_sha[:9]}.jsonl")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "a") as f:
        for n, a in keys:
            f.write(json.dumps({"schema": gate_runlog.SCHEMA, "kind": "verdict", "suite": n, "arm": a,
                                "verdict": "VERDICT", "text": text, "ts": now.isoformat(timespec="seconds"),
                                "tip": {"studio": tip_sha, "irisgl": pin, "studio_dirty": False,
                                        "irisgl_dirty": False},
                                "reader": os.environ.get("USER", "")}, sort_keys=True) + "\n")
    return path


def check(rng, build, gs=None, verdict=None, verdict_suites=None):
    """(ok, reasons) for a range. `verdict`: record it for the red rows first."""
    gs = gs or load_gs()
    contention = gate_runlog.contention_list()
    if contention is None:
        return None, [f"the contention list {gate_runlog.contention_file()} is missing or unreadable — the refusal "
                      f"cannot apply the flake law without it (JAH_CONTENTION_FILE overrides)"]
    base, tip = rng.split("..", 1)
    tip_sha = subprocess.run(["git", "rev-parse", tip], cwd=gs.ROOT, capture_output=True, text=True).stdout.strip()
    if not tip_sha:
        return None, [f"cannot resolve {tip}"]
    S = gs.select(gs.touched_paths(rng), rng, build, 4, quiet_graph=True)
    need, what = needed_rows(gs, S)
    pin = subprocess.run(["git", "rev-parse", f"{tip_sha}:irisgl"], cwd=gs.ROOT, capture_output=True, text=True).stdout.strip()
    got = records_at(tip_sha, pin or None)
    label = lambda k: f"{k[0]}{' :: ' + k[1] if k[1] else ''}"
    judged = {k: judge(k, got.get(k, []), contention) for k in need}
    if verdict:
        reds = [k for k, (st, _) in judged.items() if st == "red"
                and (not verdict_suites or k[0] in verdict_suites or (k[1] and k[1] in verdict_suites))]
        if reds:
            path = record_verdicts(reds, verdict, tip_sha, pin)
            print(f"ci-gate-check: recorded the verdict for {len(reds)} red row(s) -> {path}")
            got = records_at(tip_sha, pin or None)
            judged = {k: judge(k, got.get(k, []), contention) for k in need}
    missing = [label(k) for k, (st, _) in judged.items() if st == "missing"]
    red = [f"{label(k)}: {why}" for k, (st, why) in judged.items() if st == "red"]
    cleared = [f"{label(k)}: {why}" for k, (st, why) in judged.items() if st == "green" and why]
    reasons = []
    if missing: reasons.append(f"{len(missing)} of {len(need)} row(s) of {what} have no record at {tip_sha[:9]}: "
                               f"{missing[:8]}")
    for r in red[:20]: reasons.append("RED " + r)
    if not (missing or red):
        reasons.append(f"{what}: {len(need)} row(s) green at {tip_sha[:9]}"
                       + (f"; cleared by the law: {cleared[:6]}" if cleared else ""))
    return not (missing or red), reasons


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("range")
    ap.add_argument("--build", default="build-linux")
    ap.add_argument("--verdict", default=None,
                    help="record this verdict for every red row (the reader's text) in the run log, then re-check")
    ap.add_argument("--verdict-suite", nargs="*", default=None, help="record the verdict for these rows/arms only")
    a = ap.parse_args()
    gs = load_gs()
    ok, reasons = check(a.range, gs.resolve_build(a.build), gs, verdict=a.verdict, verdict_suites=a.verdict_suite)
    if ok is None:
        for r in reasons: print("ci-gate-check: UNUSABLE — " + r)
        sys.exit(2)
    for r in reasons: print(("ci-gate-check: " if ok else "ci-gate-check: REFUSED — ") + r)
    if not ok:
        print(f"ci-gate-check: run `scripts/gate-scope.sh {a.range} --run` at the tip; a contention-class red "
              f"takes `--solo <suite>` (3/3); any other red a verdict: `--verdict \"<text>\"`")
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
