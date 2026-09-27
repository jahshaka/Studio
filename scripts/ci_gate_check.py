#!/usr/bin/env python3
"""ci_gate_check — the merge refusal behind scripts/ci-gate-check.sh (MODULAR-GATE-1, T6).

Given a range, it re-derives the scoped selection (gate-scope.py, the same code the lane ran)
and reads THE RUN LOG: every selected row — and, for a pool selected in part, every selected
arm — must have a record at the range's tip (the studio sha, irisgl at the sha the tip pins,
neither tree dirty) whose LATEST verdict
is PASS. A solo retry that passed after a gate red counts (the flake protocol, TESTING_GATE §4:
the red and the retries sit side by side in the log for the reviewer); a row never run, or
whose last run is red, refuses. A selection that is the whole tier (a fork pin, a fallback)
needs a green record for every tier row. Target tests never gate and are not asked for.
"""
import argparse
import importlib.util
import json
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import gate_runlog  # noqa: E402


def load_gs():
    spec = importlib.util.spec_from_file_location("gate_scope", os.path.join(HERE, "gate-scope.py"))
    gs = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(gs)
    return gs


def latest_verdicts(tip, irisgl=None):
    """(suite, arm) -> the latest record at this tip (clean tree, and irisgl at the tip's pin),
    across every tier's file."""
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
            k = (r.get("suite"), r.get("arm"))
            if k not in out or (r.get("ts") or "") >= (out[k].get("ts") or ""):
                out[k] = r
    return out


def check(rng, build, gs=None):
    """(ok, reasons) for a range."""
    gs = gs or load_gs()
    base, tip = rng.split("..", 1)
    tip_sha = subprocess.run(["git", "rev-parse", tip], cwd=gs.ROOT, capture_output=True, text=True).stdout.strip()
    if not tip_sha:
        return False, [f"cannot resolve {tip}"]
    S = gs.select(gs.touched_paths(rng), rng, build, 4, quiet_graph=True)
    inv = S.inv
    gating = lambda n: not (inv[n]["labels"] & (gs.SCOPE_EXCLUDED_LABELS | gs.TARGET_LABELS))
    if S.fallback or S.full_tier:
        rows = [n for n, t in inv.items() if not (t["labels"] & (gs.NIGHTLY_LABELS | gs.TARGET_LABELS))]
        need = [(n, None) for n in rows]
        what = "the MERGE tier (%s)" % ("fallback" if S.fallback else "by rule")
    else:
        subsets = S.arm_subsets()
        need = []
        for n in sorted(S.selected):
            if not gating(n): continue
            need.append((n, None))
            for arm in subsets.get(n, []):
                need.append((n, f"{inv[n]['pool']}.{arm}"))
        what = "the scoped selection"
    pin = subprocess.run(["git", "rev-parse", f"{tip_sha}:irisgl"], cwd=gs.ROOT, capture_output=True, text=True).stdout.strip()
    got = latest_verdicts(tip_sha, pin or None)
    missing = [f"{n}{' :: ' + a if a else ''}" for n, a in need if (n, a) not in got]
    red = [f"{n}{' :: ' + a if a else ''} ({got[(n, a)]['verdict']})" for n, a in need
           if (n, a) in got and got[(n, a)]["verdict"] != "PASS"]
    reasons = []
    if missing: reasons.append(f"{len(missing)} of {len(need)} selected row(s) have no record at {tip_sha[:9]}: {missing[:8]}")
    if red: reasons.append(f"{len(red)} row(s) are red at their last run: {red[:8]}")
    if not reasons:
        reasons.append(f"{what}: {len(need)} row(s) green at {tip_sha[:9]}")
    return not (missing or red), reasons


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("range")
    ap.add_argument("--build", default="build-linux")
    a = ap.parse_args()
    gs = load_gs()
    ok, reasons = check(a.range, gs.resolve_build(a.build), gs)
    for r in reasons: print(("ci-gate-check: " if ok else "ci-gate-check: REFUSED — ") + r)
    if not ok:
        print(f"ci-gate-check: run `scripts/gate-scope.sh {a.range} --run` (and `--solo <suite>` for a "
              f"contention red) at the tip, then check again")
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
