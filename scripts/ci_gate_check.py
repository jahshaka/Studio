#!/usr/bin/env python3
"""ci_gate_check — THE ONE JUDGE (TESTING-SPEED-A round 27; the owner, 2026-10-13: the door arbitrates NOTHING, the lead
decides a red) behind `runner.sh judge` — a merge, a push and a stage close alike.

The SELECTION: a range's REACH (gate-scope.py, the same code the lane ran) + `--named` (the lane brief's rows); `--full`
(the SHIP) every merge-tier row; `--stage-close` every stage-close row. A merge's selection sets the stage-close rows
apart (the ship's).

The VERDICT, per key (a row, or a pool arm) AT THE TIP:
  * a run at the tip whose newest record PASSED -> green; with no run at the tip, the newest record at an ANCESTOR whose
    whole range to the tip does not reach the key (serve_key, THE ONE PREDICATE `runner.sh select` serves with too:
    equal declaration and data hashes, the fork pin equal or not reached, a clean build) answers it;
  * a red -> KNOWN when an OPEN registry entry OF THE RUN'S BUILD KIND names the row (known_of_kind; testing/defects.json)
    or the lead's verdict names it (`--verdict "<row>=<text>"`, free text: `runner.sh merge|push --door-verdict`), the
    entry's id / the text printed on its line — it passes; any other red REFUSES, the row named;
  * no run at the tip and none served -> NEVER-RAN [OWED]: refused (at a ship, a gap in the schedule).
An Xid on a red row is printed on its line (the law: never environmental) and changes nothing else. A merge's judge reads
THIS run's records (`--since`). Nothing is carried from another sha; no solo count, no census, no class.

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


def load_gs():
    spec = importlib.util.spec_from_file_location("gate_scope", os.path.join(HERE, "gate-scope.py"))
    gs = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(gs)
    return gs
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
            # A LANE-TOOL RECORD ANSWERS WHEN IT IS COMPLETE (RUNNER-A2): a builder's record that carries its seconds,
            # labels and the Xid read is a run like a gate's; one without them stays out
            if r.get("tier") == "lane" and not complete_lane(r): continue
            # a SERVING record (`kind: served`, runner.sh select) is the select's written evidence, never a run: the judge
            # serves through the same predicate itself (serve_key); nor is any other kind of the tools (is_run)
            if r.get("kind") != "verdict" and not is_run(r): continue
            out[sha].setdefault((r.get("suite"), r.get("arm")), []).append(r)
    for recs in out.values():
        for v in recs.values():
            v.sort(key=lambda r: gate_runlog.instant(r.get("ts")))     # instants, never the ISO text (F20)
    return out



def complete_lane(r):
    """A lane-tier record that may answer a row: it carries its seconds, its labels and the Xid read."""
    return r.get("seconds") is not None and isinstance(r.get("labels"), list) and "xid" in r


def _when(r):
    """A record's time as an instant (gate_runlog.instant, the ONE helper: offsets and DST compare right)."""
    return gate_runlog.instant(r.get("ts"))


NEVER_RAN = ("NOADMIT", "NOTRUN", "NEVER-RAN")   # NEVER-RAN: a lane row whose queue job never started it (round 20)


# THE BUILD KIND (round 18): an entry is a fact of the build it was measured under — `build: debug | asan | tsan` (absent =
# debug). A run reads ONLY its own build kind's entries (a Debug merge never serves a row KNOWN on an LSan/TSan entry; the
# ship's sanitizer tiers read theirs); OTHER_KIND keeps the rest for the door's message.
BUILD_KINDS = ("debug", "asan", "tsan")
OTHER_KIND = {}


def build_kind(build):
    """debug | asan | tsan of a run: JAH_BUILD_KIND / --build-kind when given (a sanitizer tier run IN the Debug build —
    the LSan preload of scripts/sanitize.sh — is an asan run), else the build dir's CMakeCache (JAHSHAKA_TSAN ON: tsan;
    else debug)."""
    k = os.environ.get("JAH_BUILD_KIND")
    if k:
        if k not in BUILD_KINDS:
            raise SystemExit(f"JAH_BUILD_KIND / --build-kind '{k}' is not one of {'|'.join(BUILD_KINDS)}")
        return k
    import gate_graph
    c = gate_graph._cache_entries(build) or {}
    on = lambda k: str((c.get(k) or ("", ""))[1]).upper() in ("ON", "1", "TRUE", "YES")
    # (JAHSHAKA_ASAN is ON in the daily Debug build too: it only adds the engine tests' ASan twins — never the kind; an
    # LSan/ASan tier says --build-kind asan)
    return "tsan" if on("JAHSHAKA_TSAN") else "debug"


def defects_of_kind(defects, kind):
    """The registry entries of ONE build kind (round 18); the others go to OTHER_KIND (id -> its kind)."""
    OTHER_KIND.clear()
    out = {}
    for i, e in (defects or {}).items():
        k = e.get("build") or "debug"
        if k == kind:
            out[i] = e
        else:
            OTHER_KIND[i] = k
    return out


def known_of_kind(rows, kind):
    """{row: the id of an OPEN registry entry OF THIS BUILD KIND naming it, or None} — the ship's sanitizer phases ask it
    (round 25: an LSan run in the Debug build is `asan`, the TSan build's `tsan`); None for every row without a registry."""
    defects, _ = gate_runlog.defects_load()
    reg = KnownRegistry(defects_of_kind(defects or {}, kind))
    out = {}
    for r in rows:
        e = next((x for x in reg.defects.values() if r in (x.get("rows") or [])
                  and gate_runlog.defect_state(x) == "open"), None)
        out[r] = e.get("id") if e else None
    return out


class KnownRegistry:
    """A MERGE'S KNOWN ROW (round 14: at a merge the door arbitrates nothing): any row with an OPEN registry entry naming it
    — served as KNOWN at the select (never run), passed by the merge's judge. The push and the stage close keep every
    proof rule (the door: judge / _real)."""

    def __init__(self, defects):
        self.defects = defects or {}

    def known(self, key):
        e = next((x for x in self.defects.values()
                  if (key[0] in (x.get("rows") or []) or (key[1] and key[1] in (x.get("rows") or [])))
                  and gate_runlog.defect_state(x) == "open"), None)
        return (True, f"KNOWN RED: real:{e.get('id')} — an OPEN registry entry ({e.get('kind')}) naming the row") if e \
            else (False, "no open registry entry names the row")


def resolve_not_run(out, tip_recs, judge_parent, tip_sha):
    """A ROW NOT RUN BECAUSE ITS PARENT FAILED IS ANSWERED BY THE PARENT (TESTING-CLEANUP-2B fix round 3 F3). An
    inspector whose run fixture's parent went red is ctest's "Not Run" — NOTRUN, no run, so MISSING, which no verdict
    clears, and a --solo of it re-runs the red parent first: a KNOWN RED parent would block every merge selecting its
    inspector. The runner names the parent on the record (`notRunBecause`); a MISSING row whose newest such record names
    parents takes the WORST parent state at the tip when that state is RED or KNOWN (the parent's verdict covers both);
    a green / missing parent leaves it MISSING (its own assertion never ran; --resume / --solo run both). `out`: {key: (state, why, src)}, updated in place; `tip_recs`: {key: [records]};
    `judge_parent(key)` -> (state, why) for a parent not in `out`."""
    order = ["red", "missing", "known", "green"]
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
        # ONLY A RED OR A KNOWN RED PARENT ANSWERS IT (fix round 4, item 1): a green parent means the inspector's
        # own assertion never ran at the tip — it stays MISSING (a --solo of it runs the parent first)
        if worst[1] not in ("red", "known"):
            continue
        out[k] = (worst[1], f"not run: its parent {worst[0]} is {worst[1]} — answered by the parent's record and verdict "
                            f"({(worst[2] or '')[:100]})", tip_sha)


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


_FORK_PINS = {}


def fork_pin(gs, irisgl_sha):
    """The ogre-next commit an irisgl commit pins ('' when it cannot be read: no re-use across it).
    Cached: the refusal asks it once per row per earlier commit."""
    if not irisgl_sha: return ""
    if irisgl_sha not in _FORK_PINS:
        _FORK_PINS[irisgl_sha] = _git(["rev-parse", f"{irisgl_sha}:thirdparty/ogre-next"],
                                      os.path.join(gs.ROOT, "irisgl"))
    return _FORK_PINS[irisgl_sha]


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
#   * the row is not MEASURING (gate_runlog.MEASURING: timing / quiet-box / perf: a measurement is never served);
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


# THE RECORDS THAT CANNOT BE COMPARED (round 15): {row: why} — a select says them ONCE, counted (plan()'s `uncomparable`)
UNCOMPARABLE = {}


class Server:
    """THE KEY, as one object per tip: the facts at C (the build's rows: labels, declaration, data hash), the fork rows,
    and the selections A..C (cached)."""

    def __init__(self, gs, build, tip_sha, tip_fork, defects, reach):
        self.gs, self.build, self.tip, self.tip_fork, self.reach = gs, build, tip_sha, tip_fork, reach
        self.facts, self.config, _ = gate_runlog.row_facts(build)
        self.inv = reach.inv0
        gs.classify_rows(self.inv, reach.graph, build)
        self.fork = fork_rows(gs, build, reach.graph, self.inv)
        self.sel = {}           # A -> the reach of A..C (a set of keys, or Reach.ALL)
        self.computed = 0

    def row_never(self, key):
        """Why the row is never served whatever its records (None when it may be)."""
        n = key[0]
        labels = set((self.inv.get(n) or {}).get("labels") or ())
        if labels & gate_runlog.MEASURING:
            return f"MEASURING ({'/'.join(sorted(labels & gate_runlog.MEASURING))}): a measurement is never served"
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
        rd = r.get("data_hash")
        if not rd and str((r.get("nulls") or {}).get("data_hash") or "").startswith("UNDECLARED"):
            # a record written before round 15: its row named no data and declared none — THE EMPTY SET's hash
            import gate_graph
            rd = gate_graph.EMPTY_DATA_HASH
        if not rd or not f.get("data_hash"):
            UNCOMPARABLE[key[0]] = (f.get("data_why") if not f.get("data_hash") else None) or "the record carries none"
            return "a data hash is unknown (never equal)"
        if rd != f["data_hash"]:
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


# THE JUDGE'S STATES: green (a PASS at the tip, or served), known (a red an open entry of the run's build kind names, or the
# lead's verdict names) — both pass; red, missing (NEVER-RAN: owed) — both refuse
PASSING = ("green", "known")


def state_of(key, recs, known_reg, since=None):
    """(state, why, xid) of ONE KEY from its records at one sha (oldest first): the NEWEST run decides — PASS -> green;
    a red -> KNOWN when an open entry of the run's build kind names the row (or the arm), or a verdict record of the lead
    stamped at or after it names it; else red. No run -> missing (NEVER-RAN). `since`: only the runs written since then
    (a merge's judge: THIS run's records). An Xid on the red is returned for its line; it changes nothing else."""
    # a MEASURING row's record that ran without its whole-card hold (`fallback`, GATE-COST-2) is no evidence: re-run it
    unheld = lambda r: r.get("fallback") and set(r.get("labels") or ()) & gate_runlog.MEASURING
    runs = [r for r in recs if r.get("kind") != "verdict" and r.get("verdict") not in NEVER_RAN and not unheld(r)
            and (since is None or _when(r) >= since)]
    if not runs:
        never = [r.get("verdict") for r in recs if r.get("verdict") in NEVER_RAN]
        if any(unheld(r) for r in recs):
            return "missing", "NEVER-RAN [OWED]: measured under a fallback (no whole-card hold) — re-run it", None
        return "missing", "NEVER-RAN [OWED]" + (f" (its record: {never[-1]})" if never else ""), None
    newest = max(runs, key=_when)
    if newest.get("verdict") == "PASS":
        return "green", "", None
    xid = newest.get("xid") if isinstance(newest.get("xid"), dict) and newest.get("xid") else None
    kn, kwhy = known_reg.known(key)
    if kn:
        return "known", kwhy, xid
    v = [r for r in recs if r.get("kind") == "verdict" and str(r.get("text") or "").strip() and _when(r) >= _when(newest)]
    if v:
        return "known", f"the lead's verdict: {str(v[-1].get('text'))[:160]}", xid
    fl = str(newest.get("failLine") or newest.get("status") or "")[:160]
    return "red", (f"{newest.get('verdict')}" + (f" — {fl}" if fl else "") + (f" (log {newest.get('log')})" if newest.get("log")
                   else "") + " — the lead decides: --door-verdict \"" + (key[1] or key[0]) + "=<text>\""), xid


SANITIZER_ARMS = {"lsan": "asan", "tsan": "tsan"}     # the ship's sanitizer phases' records: (row, arm) -> its build kind


def check(rng, build, gs=None, verdicts=None, mode=None, lane=None, stage_close=False, full=False, named=(), since=None,
          ship=False):
    """(ok, reasons) — THE ONE JUDGE (round 27) over a SELECTION: the range's reach + `named` (a merge: the stage-close rows
    set apart), `full` every merge-tier row (the ship), `stage_close` every stage-close row, `ship` THE PUSH DOOR: every
    merge-tier row + every stage-close row + the ship's sanitizer records at the tip (row, lsan|tsan), each judged by the
    registry entries of ITS build kind (asan for the LSan run, tsan for build-tsan). Per key at the tip (state_of):
    green or known pass; red refuses (the row named); no run and none served -> NEVER-RAN [OWED], refused. `verdicts`
    {row or arm: text}: the lead's verdicts, recorded at the tip for those rows (a row that is not red is said), then
    judged again. `mode` is accepted and ignored (one judge)."""
    gs = gs or load_gs()
    defects, problem = gate_runlog.defects_load()
    if defects is None:
        return None, [f"{problem} — the judge cannot read the registry (JAH_DEFECTS_FILE overrides)"]
    kind = build_kind(build)
    all_defects = defects
    defects = defects_of_kind(defects, kind)        # THE BUILD KIND (round 18): this run's entries only
    base, tip = rng.split("..", 1)
    tip_sha = _git(["rev-parse", tip], gs.ROOT)
    if not tip_sha:
        return None, [f"cannot resolve {tip}"]
    since = gate_runlog.instant(since) if since else None
    if stage_close:
        need, what = stage_close_rows(gs, build), "the stage-close rows"
        if not need:
            return None, [f"{build} registers no stage-close row — not the build of a tree with the batch"]
    elif ship:
        S = gs.select(gs.touched_paths(rng), rng, build, gs.GATE_JOBS, quiet_graph=True, lane=lane)
        need, _w = needed_rows(gs, S)
        need = tier_need(gs, S, need) if not S.full_tier else need
        have = set(need)
        need += [k for k in stage_close_rows(gs, build) if k not in have]
        what = "the ship's rows (the merge tier + the stage-close rows + the sanitizer runs)"
    else:
        S = gs.select(gs.touched_paths(rng), rng, build, gs.GATE_JOBS, quiet_graph=True, lane=lane)
        need, what = needed_rows(gs, S)
        have = set(need)
        for n in sorted(set(named or ()) - {k[0] for k in need}):
            if n in S.inv and (n, None) not in have:
                need.append((n, None))
        if full and not S.full_tier:
            need, what = tier_need(gs, S, need), f"the merge tier ({what} at the tip, every other row served or run: --full)"
        elif not full:
            # A STAGE-CLOSE ROW NEVER RUNS IN A MERGE (round 14 addendum): the ship runs it
            need = [k for k in need if not (S.inv.get(k[0], {}).get("labels", set()) & gs.STAGE_CLOSE_LABELS)]
            print(f"ci-gate-check: UNIVERSE {len(need)} key(s) of the reach")     # the select's universe (round 19)
    pin = _git(["rev-parse", f"{tip_sha}:irisgl"], gs.ROOT)
    label = lambda k: f"{k[0]}{' :: ' + k[1] if k[1] else ''}"
    cands = candidates(gs, tip_sha, lane=lane)
    pins = {tip_sha: pin or None}
    for c, ig, _ in cands:
        pins[c] = ig
    known_reg = KnownRegistry(defects)
    # each sanitizer arm judged by the entries of ITS build kind (round 25/27): (row, lsan) by asan, (row, tsan) by tsan
    regs = {arm: KnownRegistry(defects_of_kind(all_defects, k)) for arm, k in SANITIZER_ARMS.items()}
    defects_of_kind(all_defects, kind)              # (OTHER_KIND back to this run's kind for the messages)
    reg_of = lambda k: regs.get(k[1], known_reg)
    server_ = []

    def get_server():
        if not server_:
            server_.append(Server(gs, build, tip_sha, fork_pin(gs, pin), defects, Reach(gs, build, tip_sha,
                                                                                          fork_pin(gs, pin))))
        return server_[0]

    def judge_all():
        got = records_by_tip(pins)
        if ship:
            # THE SANITIZER RUNS (round 27: the push door reads them by kind — never the (row, no arm) blind spot)
            have_ = set(need)
            need.extend(sorted(k for k in got[tip_sha] if k[1] in SANITIZER_ARMS and k not in have_))
        out = {}
        for k in need:
            st, why, xid = state_of(k, got[tip_sha].get(k, []), reg_of(k), since)
            src = tip_sha
            if st == "missing" and cands:
                # THE KEY (RUNNER-A2): no run at the tip (or in THIS run) — an ancestor's record serves it
                sv = serve_key(k, get_server(), [(c, f) for c, _, f in cands], got,
                               lambda kk, rs: state_of(kk, rs, known_reg)[:2])
                if sv[0] == "served":
                    st, src, why = sv[2], sv[1], sv[3]
                elif sv[0] == "red":
                    st, src, why = "red", sv[1], sv[2]
                else:
                    why += f"; not served: {sv[1]}"
            if xid:
                why += f" [Xid {xid.get('codes') or xid.get('xid') or xid} from the row's own process: never environmental]"
            out[k] = (st, why, src)
        resolve_not_run(out, got[tip_sha], lambda pk: state_of(pk, got[tip_sha].get(pk, []), known_reg)[:2], tip_sha)
        return out
    judged = judge_all()
    if verdicts:
        pairs, unknown = [], []
        for name, text in verdicts.items():
            keys = [k for k in need if k[0] == name or (k[1] and k[1] == name)]
            red_keys = [k for k in keys if judged[k][0] in ("red", "known")]
            pairs += [(k, text) for k in red_keys]
            if not red_keys:
                unknown.append(f"{name} ({'not selected' if not keys else judged[keys[0]][0]})")
        for u in unknown:
            print(f"ci-gate-check: no verdict recorded for {u} — a verdict answers a red row only")
        if pairs:
            path = record_verdicts(pairs, tip_sha, pin)
            print(f"ci-gate-check: recorded {len(pairs)} verdict(s) at {tip_sha[:9]} -> {path}")
            judged = judge_all()
    for k in need:
        st, why, src = judged[k]
        print(f"ci-gate-check: row {label(k)} <- {src[:9]}{' (the tip)' if src == tip_sha else ''}: {st}"
              + (f" — {why}" if why else ""))
    known = [k for k in need if judged[k][0] == "known"]
    for k in known:
        print(f"KNOWN {label(k)}: {judged[k][1]}")
    missing = [label(k) for k in need if judged[k][0] == "missing"]
    red = [f"{label(k)}: {judged[k][1]}" for k in need if judged[k][0] == "red"]
    reused = {}
    for k, (st, _, src) in judged.items():
        if st in PASSING and src != tip_sha:
            reused[src] = reused.get(src, 0) + 1
    reasons = []
    if missing:
        reasons.append(f"{len(missing)} of {len(need)} row(s) of {what} NEVER RAN at {tip_sha[:9]} [OWED] (nor served from "
                       f"an ancestor){' — a schedule gap' if full or stage_close else ''}: {missing[:8]}")
    for r in red[:20]:
        reasons.append("RED " + r)
    if not (missing or red):
        if known:
            reasons.append(f"KNOWN ({len(known)}): {[label(k) for k in known][:8]}")
        reasons.append(f"{what}: {len(need)} row(s) — {len(need) - sum(reused.values()) - len(known)} green at "
                       f"{tip_sha[:9]}, {len(known)} known" + "".join(f", {n} served from {c[:9]}" for c, n in reused.items()))
    return not (missing or red), reasons


# THE ESTIMATE IS WALL TIME AT THE PASS WIDTHS (TESTING-SPEED-A item 4): a merge prints its selection and its estimate
# BEFORE anything runs, and the runner refuses one over this many seconds without the lead's `--go`
ESTIMATE_LIMIT_S = 900
PASS_ORDER = ("named", "picture", "measuring", "rest")


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
    defects = defects_of_kind(defects, build_kind(build))        # THE BUILD KIND (round 18): this build's entries only
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
    # A STAGE-CLOSE ROW NEVER RUNS IN A MERGE (round 14 addendum): the brief's named rows and the reached ones alike —
    # listed apart (the ship's), never run, never asked
    sc_rows = [] if full else sorted(k[0] for k in universe
                                     if S.inv.get(k[0], {}).get("labels", set()) & gs.STAGE_CLOSE_LABELS)
    universe = [k for k in universe if k[0] not in sc_rows]
    scoped = [k for k in scoped if k[0] not in sc_rows]
    cands = candidates(gs, tip_sha, lane=lane)         # the lane named: its non-ancestor tips serve too (item 2)
    pins = {c: ig for c, ig, _ in cands}
    got = records_by_tip(pins)
    run, served = {}, {}
    sel_keys = set(scoped)
    reasons = {n: r for n, r in (S.selected or {}).items()}
    known_reg = KnownRegistry(defects)
    for k in universe:
        # A KNOWN ROW (an open registry entry names it; round 14) IS SERVED AS SUCH: never run — the judge reads the same
        kn, kwhy = known_reg.known(k)
        if kn:
            served[k] = {"sha": base_sha, "run": "registry", "tier": "known-red", "state": "known",
                         "ts": datetime.datetime.now().astimezone().isoformat(timespec="seconds"), "why": kwhy}
            continue
        sv = serve_key(k, server, [(c, f) for c, _, f in cands], got,
                       lambda kk, recs: state_of(kk, recs, known_reg)[:2])
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
            "stage_close": sc_rows,
            "uncomparable": dict(sorted(UNCOMPARABLE.items())),
            "measuring_run": measuring, "fork_rows": sorted(server.fork), "full": bool(full or S.full_tier),
            "fallback": list(S.fallback), "selected": sorted(sel_keys, key=str), "universe": len(universe), "run": run,
            "served": served, "selections": server.computed, "passes": ps,
            "estimate_s": round(sum(p["wall_s"] for p in ps), 1), "serial_s": round(sum(p["serial_s"] for p in ps), 1),
            "estimate_limit_s": ESTIMATE_LIMIT_S}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("range")
    ap.add_argument("--stage-close", action="store_true",
                    help="THE STAGE-CLOSE SELECTION: every stage-close row at the range's tip (the one judge)")
    ap.add_argument("--build", default="build-linux")
    ap.add_argument("--mode", default=None, help="accepted and ignored: ONE judge at a merge, a push and a stage close "
                                                     "(round 27)")
    ap.add_argument("--lane", default=None, help="the lane whose own records carry open reds to the tip")
    ap.add_argument("--ship", action="store_true",
                    help="THE PUSH DOOR (round 27): every merge-tier row + every stage-close row + the ship's sanitizer "
                         "runs at the tip (lsan by the asan entries, tsan by the tsan ones) — one judge, one call")
    ap.add_argument("--full", action="store_true",
                    help="THE SHIP's universe: every merge-tier row, at the tip or SERVED by rule 2 (tier_need) — the "
                         "default universe is the range's REACH + the --named rows (TESTING-SPEED-A item 1)")
    ap.add_argument("--plan", action="store_true",
                    help="print the merge run's row set as JSON (`runner.sh select`): run (with reasons) and served")
    ap.add_argument("--build-kind", default=None, choices=BUILD_KINDS,
                    help="the run's BUILD KIND (round 18; default: the build's CMakeCache — JAHSHAKA_TSAN ON: tsan, else debug): the "
                         "registry's entries of that kind only answer it")
    ap.add_argument("--since", default=None, metavar="ISO",
                    help="a MERGE's judge reads the records written since this instant (the merge run's start): THIS "
                         "run's records only (round 14)")
    ap.add_argument("--named", action="append", default=[], help="a row the lane's brief names: in the universe (run "
                    "by --plan unless served at the tip; asked by the judge)")
    # action="extend": `--verdict a=x b=y` and `--verdict a=x --verdict b=y` (the form the runner
    # builds) both record EVERY verdict — with nargs="+" alone a repeated flag kept only the last one
    ap.add_argument("--verdict", nargs="+", action="extend", default=None, metavar="ROW=TEXT",
                    help="THE LEAD'S VERDICT per red row (`<row or pool.arm>=<text>`, free text) recorded at the tip, "
                         "then judged again: the row passes as KNOWN (repeatable)")
    a = ap.parse_args()
    if a.build_kind:
        os.environ["JAH_BUILD_KIND"] = a.build_kind
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
    t0 = datetime.datetime.now()
    ok, reasons = check(a.range, gs.resolve_build(a.build), gs, verdicts=verdicts, lane=a.lane,
                        stage_close=a.stage_close, full=a.full, named=set(a.named), since=a.since, ship=a.ship)
    if ok is None:
        for r in reasons: print("ci-gate-check: UNUSABLE — " + r)
        sys.exit(2)
    for r in reasons: print(("ci-gate-check: " if ok else "ci-gate-check: REFUSED — ") + r)
    print(f"ci-gate-check: {'ACCEPTED' if ok else 'REFUSED'} in {(datetime.datetime.now() - t0).total_seconds():.1f} s"
          + ("" if ok else " — a red is the lead's: the builder's fix, or `--door-verdict \"<row>=<text>\"`"))
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()