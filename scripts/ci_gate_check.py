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



def _when(r):
    """A record's time, comparable across the log's two spellings (with and without an offset)."""
    ts = r.get("ts") or ""
    try:
        t = datetime.datetime.fromisoformat(ts)
    except ValueError:
        return datetime.datetime.min.replace(tzinfo=datetime.timezone.utc)
    return t if t.tzinfo else t.astimezone()


NEVER_RAN = ("NOADMIT", "NOTRUN")


def judge(key, recs, contention):
    """(state, why) for one row/arm: state 'green' | 'missing' | 'red'.

    A run that never happened (NOADMIT: the admission's bound; NOTRUN) is no run: a row with only
    those is MISSING, which no verdict clears. A VERDICT is per row and timestamped: it clears
    only the reds logged BEFORE it — a red after it needs its own answer (the merge read's D4)."""
    runs = sorted((r for r in recs if r.get("kind") != "verdict" and r.get("verdict") not in NEVER_RAN), key=_when)
    verdicts = sorted((r for r in recs if r.get("kind") == "verdict"), key=_when)
    if not runs:
        never = [r.get("verdict") for r in recs if r.get("verdict") in NEVER_RAN]
        return "missing", "never run at the tip" + (f" ({never[-1]}: it never ran)" if never else "")
    reds = [r for r in runs if r.get("verdict") != "PASS"]
    if not reds:
        return "green", ""
    last_verdict = _when(verdicts[-1]) if verdicts else None
    open_reds = [r for r in reds if last_verdict is None or _when(r) > last_verdict]
    if not open_reds:
        return "green", "recorded verdict: " + (verdicts[-1].get("text") or "")[:120]
    reds = open_reds
    name, arm = key
    listed = name in contention or (arm and arm in contention)
    gate_reds = [r for r in reds if not r.get("retry")]
    last_gate_red = max((_when(r) for r in gate_reds), default=None)
    solos = [r for r in runs if r.get("retry") and (last_gate_red is None or _when(r) > last_gate_red)
             and (last_verdict is None or _when(r) > last_verdict)]
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


def _git(args, cwd):
    r = subprocess.run(["git"] + args, cwd=cwd, capture_output=True, text=True)
    return r.stdout.strip() if r.returncode == 0 else ""


def lane_commits(gs, base, tip_sha):
    """The lane's OWN earlier commits, newest first: base..tip without the tip, minus the integration
    line (a d-build commit a forward merge carried in is not a commit of this lane)."""
    line = gs.integration_line()
    revs = _git(["rev-list", "--topo-order", f"{base}..{tip_sha}"], gs.ROOT).split()
    return [c for c in revs if c != tip_sha and c not in line]


def fork_pin(gs, irisgl_sha):
    """The ogre-next commit an irisgl commit pins ('' when it cannot be read: no re-use across it)."""
    if not irisgl_sha: return ""
    return _git(["rev-parse", f"{irisgl_sha}:thirdparty/ogre-next"], os.path.join(gs.ROOT, "irisgl"))


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
    contention = gate_runlog.contention_list()
    if contention is None:
        return None, [f"the contention list {gate_runlog.contention_file()} is missing or unreadable — the refusal "
                      f"cannot apply the flake law without it (JAH_CONTENTION_FILE overrides)"]
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
    reach = None

    def judge_all():
        """{key: (state, why, source sha)} — the tip's records first, then (for a row the tip has no
        run of) the newest earlier commit's, under the rule in this file's header."""
        nonlocal reach
        got = records_by_tip(pins)
        out = {}
        tip_fork = fork_pin(gs, pin)
        have_earlier = [c for c in earlier if got[c]]
        for k in need:
            st, why = judge(k, got[tip_sha].get(k, []), contention)
            src = tip_sha
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
                    cst, cwhy = judge(k, recs, contention)
                    if cst == "missing": continue
                    st, src = cst, c
                    why = (f"re-used from {c[:9]} ({c[:9]}..{tip_sha[:9]} does not reach it)"
                           + (f"; {cwhy}" if cwhy else "")) if cst == "green" else \
                          (f"red at {c[:9]}, the newest run of it on the lane ({c[:9]}..{tip_sha[:9]} does not reach "
                           f"it; answer it there): {cwhy}")
                    break
            out[k] = (st, why, src)
        return out

    judged = judge_all()
    if verdicts:
        pairs, unknown = {}, []
        for name, text in verdicts.items():
            keys = [k for k in need if k[0] == name or (k[1] and k[1] == name)]
            red_keys = [k for k in keys if judged[k][0] == "red"]
            for k in red_keys:
                pairs.setdefault(judged[k][2], []).append((k, text))     # recorded where the red happened
            if not red_keys:
                unknown.append(f"{name} ({'not selected' if not keys else judged[keys[0]][0]})")
        for u in unknown:
            print(f"ci-gate-check: no verdict recorded for {u} — a verdict answers a red row only")
        for at, ps in pairs.items():
            path = record_verdicts(ps, at, pins.get(at) or "")
            print(f"ci-gate-check: recorded {len(ps)} verdict(s) at {at[:9]} -> {path}")
        if pairs:
            judged = judge_all()
    # THE PROVENANCE, per row (GATE-SPEED-1): which commit's record answered it
    for k in need:
        st, why, src = judged[k]
        print(f"ci-gate-check: row {label(k)} <- {src[:9]}{' (the tip)' if src == tip_sha else ''}: {st}"
              + (f" — {why}" if why else ""))
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
    if not (missing or red):
        at_tip = len(need) - sum(reused.values())
        reasons.append(f"{what}: {len(need)} row(s) green — {at_tip} at {tip_sha[:9]}"
                       + "".join(f", {n} re-used from {c[:9]}" for c, n in reused.items())
                       + (f"; cleared by the law: {cleared[:6]}" if cleared else ""))
    return not (missing or red), reasons


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("range")
    ap.add_argument("--build", default="build-linux")
    ap.add_argument("--verdict", nargs="+", default=None, metavar="ROW=TEXT",
                    help="record a verdict per red row (`<row or pool.arm>=<the reader's text>`) in the run log, "
                         "then re-check; it clears only the reds logged before it")
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
