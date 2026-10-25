#!/usr/bin/env python3
"""ci_gate_check — THE SERVING: which rows of a build already have their answer (DOCS/PROCESS.md; TESTING-SIMPLE).

    scripts/ci_gate_check.py serve --build <dir> [--tip <sha>] (--rows <file>|- | --all) [--json]

A row is SERVED at the tip C when the newest record of the row at C — else at the nearest ancestor A of C that holds
one — PASSED, and:
  * its build was its tip's (BUILT_FROM clean and equal to the record's tip), irisgl at that tip's own pin;
  * its DECLARATION and DATA hashes equal the build's at C (a hash unknown on either side is never equal);
  * the fork pin at A equals C's, or the row does not reach the fork (the build graph);
  * the whole range A..C does not reach the row (gate-scope's selection: the graph, the symbols, the rules);
  * it is not a measuring row (label `measure`: a measurement is never served).
A row is SERVED-RED (URGENT #54: re-running a red at the same input is a quiet retry that could flip it green by chance)
when its newest record is a red RESULT (FAIL, TIMEOUT, CRASH; NOTRUN and NOADMIT are not results) at THIS very tree (never
an ancestor's: the diff may have fixed it) with the build configuration, declaration and data hashes equal: it is in
neither "served" nor "run" but in "red" ({row: {from, run, verdict, failLine, seconds}}), reported RED with its record;
a deliberate re-run is spec.sh test's --rerun-red <row> --why <text>.
Every other row RUNS, with the reason. The serving decides nothing else: a red is the run's, read by the lead.
Exit 0; 2 when the build or the tip cannot be read.
"""
import argparse
import importlib.util
import json
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import gate_runlog  # noqa: E402

CANDIDATES_MAX = 40          # the newest ancestors holding records that one serving reads (a selection each)


def load_gs():
    spec = importlib.util.spec_from_file_location("gate_scope", os.path.join(HERE, "gate-scope.py"))
    gs = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(gs)
    return gs


def _git(args, cwd):
    try:
        r = subprocess.run(["git"] + args, cwd=cwd, capture_output=True, text=True)
    except OSError:                  # a tree without irisgl (a toy)
        return ""
    return r.stdout.strip() if r.returncode == 0 else ""


def candidates(root, tip):
    """[(A, A's irisgl pin, A's fork pin)]: the tip and its ancestors that hold a record, newest first."""
    have, d = set(), gate_runlog.log_dir()
    rx = re.compile(r'"studio": "([0-9a-f]{40})"')
    if os.path.isdir(d):
        for f in os.listdir(d):
            if f.endswith(".jsonl"):
                with open(os.path.join(d, f), errors="replace") as fh:
                    have |= set(rx.findall(fh.read()))
    out = []
    for c in _git(["rev-list", "--topo-order", tip], root).split():
        if c in have:
            ig = _git(["rev-parse", f"{c}:irisgl"], root)
            fk = _git(["rev-parse", f"{ig}:thirdparty/ogre-next"], os.path.join(root, "irisgl")) if ig else ""
            out.append((c, ig, fk))
            if len(out) >= CANDIDATES_MAX:
                break
    return out


def records_by_tip(pins):
    """{sha: {row: [run records, oldest first]}} for every sha of `pins` ({sha: its irisgl pin}): a clean tree (the fork's
    unknown = dirty), a build of that tip, irisgl at its pin; row records only (a pool's arms ride on their row)."""
    out = {t: {} for t in pins}
    d = gate_runlog.log_dir()
    if not os.path.isdir(d):
        return out
    for f in sorted(os.listdir(d)):
        if not f.endswith(".jsonl"):
            continue
        for line in open(os.path.join(d, f), errors="replace"):
            try:
                r = json.loads(line)
            except ValueError:
                continue
            t = r.get("tip") or {}
            sha = t.get("studio")
            if sha not in out or r.get("kind") or r.get("arm") or t.get("studio_dirty") or t.get("irisgl_dirty") \
                    or t.get("fork_dirty") is not False or (t.get("built") or {}).get("fork_dirty") is not False:
                continue
            if gate_runlog.stale_build(t) or (pins[sha] and t.get("irisgl") != pins[sha]):
                continue
            if r.get("verdict") in ("NOTRUN", "NOADMIT", None):
                continue
            out[sha].setdefault(r.get("suite"), []).append(r)
    for recs in out.values():
        for v in recs.values():
            v.sort(key=lambda r: str(r.get("ts")))
    return out


def fork_rows(build, graph, inv):
    """{row} that reach the fork by the graph (its executables, or the app for an app row, depend on ogre-next)."""
    if graph is None:
        return set(inv)
    fe = set()
    for n in list(graph.rev):
        if "/irisgl/thirdparty/ogre-next" in n:
            fe |= graph.reach(n)
    app = os.path.normpath(os.path.join(build, "bin", "Jahshaka"))
    return {n for n, t in inv.items() if (t.get("exes") or set()) & fe or (t.get("kind") == "app" and app in fe)}


class Reach:
    """The rows a range A..C reaches (gate-scope's selection on one graph and one inventory); None = every row."""

    def __init__(self, gs, build, tip):
        import copy
        import gate_graph
        self.gs, self.build, self.tip, self.copy = gs, build, tip, copy
        self.graph = gate_graph.NinjaGraph.load(build)
        self.inv0 = gs.load_inventory(build)
        gs.classify_rows(self.inv0, self.graph, build)
        self.fork = fork_rows(build, self.graph, self.inv0)
        self.cache = {}

    def same_tree(self, a):
        """Is commit `a`'s tree this tip's (the same Studio tree, so the same irisgl pin and fork)?"""
        return a == self.tip or subprocess.run(["git", "diff", "--quiet", a, self.tip], cwd=self.gs.ROOT).returncode == 0

    def of(self, a):
        if a in self.cache:
            return self.cache[a]
        reach = None
        if a == self.tip or subprocess.run(["git", "diff", "--quiet", a, self.tip], cwd=self.gs.ROOT).returncode == 0:
            reach = set()                          # the same tree: nothing reaches anything
        else:
            try:
                rng = f"{a}..{self.tip}"
                S = self.gs.select(self.gs.touched_paths(rng), rng, self.build, graph=self.graph,
                                   inv=self.copy.deepcopy(self.inv0), quiet_graph=True)
                if not S.full_tier:
                    reach = set(S.selected) | set(S.tools_dropped)
                    if S.fork_bump:
                        reach |= self.fork
            except SystemExit:
                reach = None
        self.cache[a] = reach
        return reach


def why_not(rec, fact, row, reach, a_fork, tip_fork, cfg=None):
    """Why record `rec` at A cannot stand for the row at C (None when it can); `cfg`: the build's configuration."""
    if rec.get("verdict") != "PASS":
        return f"its newest record is {rec.get('verdict')} ({(rec.get('failLine') or '')[:120]})"
    if fact is None:
        return "the row is not in the build's ctest inventory"
    if (row.startswith("source.") or "lint" in (fact.get("labels") or ()) or "hygiene" in (fact.get("labels") or ())) \
            and not reach.same_tree(rec["tip"]["studio"]):
        # a lint reads the whole tree, so only a record of THIS tree answers it (2026-10-14: seven unpriced rows passed R2 by a
        # served record of an older tree); at the same tree it is the same answer (URGENT FIX LIST #20)
        return "a lint reads the whole tree: served only by a record of this very tree"
    rc = (rec.get("config") or {}).get("hash")
    if not rc or not cfg or rc != cfg.get("hash"):
        return "its build configuration differs (or is unknown): %s vs this build's %s" % (
            (rec.get("config") or {}).get("build_type"), (cfg or {}).get("build_type"))
    d = rec.get("declaration")
    d = d.get("hash") if isinstance(d, dict) else None
    if not d or d != (fact.get("declaration") or {}).get("hash"):
        return "its declaration changed (or is unknown)"
    if not rec.get("data_hash") or rec.get("data_hash") != fact.get("data_hash"):
        return "its data changed (or is unknown)"
    if row in reach.fork and a_fork != tip_fork:
        return "the fork pin moved and the row reaches the fork"
    r = reach.of(rec["tip"]["studio"])
    if r is None or row in r:
        return f"{rec['tip']['studio'][:9]}..{reach.tip[:9]} reaches it"
    return None


# THE RESULTS THAT ARE RED (URGENT #54): a verdict other than PASS that is a result. NOTRUN and NOADMIT are not results
# (records_by_tip drops them): a row that never ran has no answer to serve, red or green.
RED_RESULTS = ("FAIL", "TIMEOUT", "CRASH")


def red_stands(rec, fact, reach, cfg):
    """Why the red record `rec` does NOT stand at this tip (None when it does: SERVED-RED). URGENT #54: re-running a red at
    the same input is a quiet retry that could flip it green by chance, so a red recorded at THIS tree (never an ancestor's:
    the diff may have fixed it), with the same build configuration, declaration and data, is reported with its record and
    not run again (a deliberate re-run is spec.sh test's --rerun-red <row> --why <text>)."""
    if rec.get("verdict") not in RED_RESULTS:
        return f"its verdict {rec.get('verdict')} is not a red result"
    if fact is None:
        return "the row is not in the build's ctest inventory"
    if not reach.same_tree(rec["tip"]["studio"]):
        return "a red of an ancestor's tree (the diff may have fixed it)"
    rc = (rec.get("config") or {}).get("hash")
    if not rc or not cfg or rc != cfg.get("hash"):
        return "its build configuration differs (or is unknown)"
    d = rec.get("declaration")
    d = d.get("hash") if isinstance(d, dict) else None
    if not d or d != (fact.get("declaration") or {}).get("hash"):
        return "its declaration changed (or is unknown)"
    if not rec.get("data_hash") or rec.get("data_hash") != fact.get("data_hash"):
        return "its data changed (or is unknown)"
    return None


def classify(rec, fact, row, reach, a_fork, tip_fork, cfg):
    """("served", None) | ("red", None) | ("run", why) for the row's newest record `rec` at the tip."""
    if rec.get("verdict") in RED_RESULTS:
        why = red_stands(rec, fact, reach, cfg)
        return ("red", None) if why is None else ("run", why if "ancestor" in why else
                                                  f"its newest record is {rec.get('verdict')} ({(rec.get('failLine') or '')[:120]}); {why}")
    why = why_not(rec, fact, row, reach, a_fork, tip_fork, cfg)
    return ("run", why) if why else ("served", None)


def serve(build, tip, rows):
    """({row: {from, record}}, {row: why}, {row: the red record}) for `rows` at `tip` over `build`: served (a PASS that
    answers the row), to run (with why), SERVED-RED (a red result at this very tree and inputs: neither served nor run)."""
    gs = load_gs()
    build = gs.resolve_build(build)
    reach = Reach(gs, build, tip)
    facts, cfg, _ = gate_runlog.row_facts(build)
    cands = candidates(gs.ROOT, tip)
    got = records_by_tip({c: ig for c, ig, _ in cands})
    tip_fork = next((fk for c, _, fk in cands if c == tip), None) or \
        _git(["rev-parse", f"{_git(['rev-parse', f'{tip}:irisgl'], gs.ROOT)}:thirdparty/ogre-next"],
             os.path.join(gs.ROOT, "irisgl"))
    served, run, red = {}, {}, {}
    for row in rows:
        labels = set((reach.inv0.get(row) or {}).get("labels") or ())
        if labels & gate_runlog.MEASURING:
            run[row] = "a measurement is never served"
            continue
        hit = next(((c, fk) for c, _, fk in cands if got[c].get(row)), None)
        if not hit:
            run[row] = "no record of it at the tip or an ancestor"
            continue
        rec = got[hit[0]][row][-1]
        kind, why = classify(rec, facts.get(row), row, reach, hit[1], tip_fork, cfg)
        if kind == "run":
            run[row] = f"{hit[0][:9]}: {why}"
        elif kind == "red":
            red[row] = {"from": hit[0], "run": rec.get("run"), "verdict": rec.get("verdict"),
                        "failLine": rec.get("failLine"), "seconds": rec.get("seconds")}
        else:
            served[row] = {"from": hit[0], "run": rec.get("run"), "tier": rec.get("tier"), "seconds": rec.get("seconds")}
    return served, run, red


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("serve", help="which rows a record answers at the tip, and why the others run")
    s.add_argument("--build", required=True)
    s.add_argument("--tip", default=None)
    s.add_argument("--rows", default=None, help="a file of row names, one per line ('-' = stdin)")
    s.add_argument("--all", action="store_true", help="every row of the build")
    s.add_argument("--json", action="store_true")
    a = ap.parse_args()
    gs = load_gs()
    build = gs.resolve_build(a.build)
    tip = _git(["rev-parse", a.tip or "HEAD"], gs.ROOT)
    if not tip:
        print(f"ci_gate_check: cannot resolve the tip {a.tip or 'HEAD'} in {gs.ROOT}", file=sys.stderr)
        return 2
    if a.all:
        rows = sorted(gs.load_inventory(build))
    elif a.rows:
        rows = [l.strip() for l in (sys.stdin if a.rows == "-" else open(a.rows)) if l.strip()]
    else:
        ap.error("give --rows <file> or --all")
    served, run, red = serve(build, tip, rows)
    if a.json:
        print(json.dumps({"tip": tip, "served": served, "run": run, "red": red}, sort_keys=True))
    else:
        for r, v in sorted(served.items()):
            print(f"SERVED {r}  from {v['from'][:9]} ({v.get('tier')}, {v.get('run')})")
        for r, v in sorted(red.items()):
            print(f"RED    {r}  from {v['from'][:9]} ({v.get('verdict')}, {v.get('run')}): {(v.get('failLine') or '')[:120]}")
        for r, why in sorted(run.items()):
            print(f"RUN    {r}  {why}")
        print(f"{len(served)} served, {len(red)} served-red, {len(run)} to run, at {tip[:9]}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
