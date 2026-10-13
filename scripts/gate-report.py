#!/usr/bin/env python3
"""gate-report.py — THE WEEKLY TABLE OF THE RUN LOG (GATE-LOG-1, lane VERDICT-1; the end-of-sprint read).

The audit's and the preflight's queries (SPECS/audits/GATE_COST_2026-10-09.md; spikes/preflight-testing-1/
q_runlog.py + q_refine.py) as ONE command over the run log's directories (testing/runs, testing/runs-archive —
read recursively; a record later than the week's end is not read):

    scripts/gate-report.py <runs dir> [<runs dir> ...] [--week <date | date>T<hh:mm>] [--defects <file>]

  --week   the seven days ENDING at that moment (a bare date: the end of that day; default: now)

THE TABLE: gates by kind (tier) and the full-size ones (>= 400 rows) with their hours, alone vs shared (a full
gate with < 0.2 sibling ctests on average is alone); solos; NOADMIT (records, and the pools whose every arm was
NOADMIT); TIMEOUT by family; the defect registry; the stage-close rows' runs; the slot / drain / hold hours; the forced
exits (code 86). (The verdict door's and the carried reds' sections went with their rules, TESTING-SPEED-A round 27.)
Section 1 and the per-week rows read the week; the NOADMIT pools read every record up to the week's end."""
import argparse
import collections
import datetime
import json
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import ci_gate_check  # noqa: E402  (its gate_runlog: the one record reader)
# THE ONE SOLO PREDICATE (RUNNER-AUDIT-FIX-1 F1; FIX-2's hand-off): the attribution's tier-`solo` runs and gate-scope's
# retries alike
IS_SOLO = ci_gate_check.gate_runlog.is_solo

GATE_TIERS = ("scoped", "scoped-fallback", "scoped-tier", "fork")
# THE ARCHIVE'S OWN SPELLINGS: a schema-1 record (before 2026-10-09) names its lane in `lane` and a joint run's tier
# `joint` (both gone with BATCH-GATE-1). The REPORT reads them to re-count the past; the judge never does.
ARCHIVE_GATE_TIERS = ("joint",)
FULL_ROWS = 400
ALONE_SIB = 0.2


def when(r):
    try:
        t = datetime.datetime.fromisoformat(r.get("ts") or "")
    except ValueError:
        return None
    return t if t.tzinfo else t.astimezone()


def read(dirs, end):
    R = []
    for d in dirs:
        for root, _, files in os.walk(d):
            for f in sorted(files):
                if not f.endswith(".jsonl"): continue
                for line in open(os.path.join(root, f), errors="replace"):
                    try: r = json.loads(line)
                    except ValueError: continue
                    if not isinstance(r, dict): continue
                    r["_t"] = when(r)
                    if r["_t"] is not None and r["_t"] > end: continue
                    R.append(r)
    return R


def parse_end(s):
    if not s:
        return datetime.datetime.now().astimezone()
    t = datetime.datetime.fromisoformat(s)
    if len(s) == 10:
        t = t + datetime.timedelta(days=1) - datetime.timedelta(seconds=1)
    return t if t.tzinfo else t.astimezone()


def h(sec):
    return "%.1f h" % (sec / 3600.0)


def gates(R):
    rows = [r for r in R if r.get("kind") != "verdict" and r.get("arm") is None and r["_t"]]
    runs = collections.defaultdict(list)
    for r in rows:
        if not IS_SOLO(r): runs[r.get("run")].append(r)
    G = []
    for k, rs in runs.items():
        s = min(x["_t"] - datetime.timedelta(seconds=x.get("wallSeconds") or x.get("seconds") or 0) for x in rs)
        e = max(x["_t"] for x in rs)
        G.append(dict(run=k, tier=rs[0].get("tier"), lane="+".join(sorted(_lanes(rs[0]))), n=len(rs), start=s, end=e,
                      h=(e - s).total_seconds() / 3600, sib=sum((x.get("box") or {}).get("other_ctests") or 0
                                                                for x in rs) / len(rs)))
    return rows, G


def noadmit_pools(R):
    byrun = collections.defaultdict(list)
    for r in R:
        if r.get("kind") != "verdict" and (r.get("suite") or "").startswith("pool."):
            byrun[(r.get("run"), r["suite"])].append(r)
    hole, fixed = [], []
    for (run, s), rs in byrun.items():
        row = [r for r in rs if r.get("arm") is None]
        arms = [r for r in rs if r.get("arm")]
        if not (row and arms and all(a.get("verdict") == "NOADMIT" for a in arms)): continue
        if row[0].get("verdict") == "NOADMIT": fixed.append((run, s, len(arms)))
        elif row[0].get("verdict") != "PASS": hole.append((run, s, row[0].get("verdict"), len(arms), row[0].get("tier")))
    return hole, fixed


def _lanes(r):
    """`lanes` through the one reader; a schema-1 ARCHIVE record's `lane` string (the past only)."""
    v = set(ci_gate_check.gate_runlog.lanes_of(r))
    if not v and (r.get("schema") or 1) < 2 and isinstance(r.get("lane"), str) and r["lane"]:
        v = {r["lane"]}
    return v


DEFECTS = {}


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("dirs", nargs="+")
    ap.add_argument("--week", default=None)
    ap.add_argument("--defects", default=None, help="the defect registry (default: testing/defects.json)")
    a = ap.parse_args()
    end = parse_end(a.week)
    start = end - datetime.timedelta(days=7)
    R = read(a.dirs, end)
    if a.defects: os.environ["JAH_DEFECTS_FILE"] = a.defects
    reg, why = ci_gate_check.gate_runlog.defects_load()
    if reg is None:
        print(f"(the defect registry is unusable: {why} — its section reads an empty registry)")
    DEFECTS.update(reg or {})
    print(f"GATE REPORT  week {start.isoformat(timespec='minutes')} .. {end.isoformat(timespec='minutes')}  "
          f"records read {len(R)} (to the week's end) from {', '.join(a.dirs)}")

    rows, G = gates(R)
    # a lane tool's own runs (tier `lane`, GATE-COST-2) are no gate: never in the gate wall
    GW = [g for g in G if g["start"] >= start and g["tier"] != "lane"]
    full = [g for g in GW if g["n"] >= FULL_ROWS]
    print("\n== 1. gates in the week (non-solo runs) ==")
    print(f"  runs {len(GW)}  full-size (>= {FULL_ROWS} rows) {len(full)}  their wall {h(sum(g['h'] for g in full) * 3600)}")
    by = collections.defaultdict(lambda: [0, 0.0])
    for g in GW:
        by[g["tier"]][0] += 1; by[g["tier"]][1] += g["h"]
    print(f"  summed gate wall (every non-solo run, rc tiers included): {sum(g['h'] for g in GW):.1f} h")
    print("  by kind: " + "  ".join(f"{t} {n} ({hh:.1f} h)" for t, (n, hh) in sorted(by.items(), key=lambda kv: -kv[1][0])))
    alone = [g for g in full if g["sib"] < ALONE_SIB]
    shared = [g for g in full if g["sib"] >= ALONE_SIB]
    med = lambda xs: sorted(xs)[len(xs) // 2] if xs else 0.0
    print(f"  full-size ALONE (< {ALONE_SIB} sibling ctests): {len(alone)}, {sum(g['h'] for g in alone):.1f} h "
          f"(median {med([g['h'] for g in alone]):.2f} h)   SHARED: {len(shared)}, {sum(g['h'] for g in shared):.1f} h "
          f"(median {med([g['h'] for g in shared]):.2f} h)")
    wk = [r for r in rows if r["_t"] >= start]
    solos = [r for r in wk if IS_SOLO(r)]
    print(f"\n== 2. solos ==\n  solo records {len(solos)}  {h(sum(r.get('seconds') or 0 for r in solos))}  rows "
          f"{len({r.get('suite') for r in solos})}  PASS {sum(1 for r in solos if r.get('verdict') == 'PASS')}")
    wk_all = [r for r in R if r.get("kind") != "verdict" and r["_t"] and r["_t"] >= start]
    na = [r for r in wk_all if r.get("verdict") == "NOADMIT"]
    hole, fixed = noadmit_pools(R)
    print("\n== 3. NOADMIT ==")
    print(f"  week: NOADMIT records {len(na)} (rows {sum(1 for r in na if r.get('arm') is None)}, arms "
          f"{sum(1 for r in na if r.get('arm'))})")
    print(f"  pools whose EVERY arm was NOADMIT recorded red (the hole U2 closes): {len(hole)}; recorded NOADMIT "
          f"(since U2): {len(fixed)}")
    for x in hole: print("    ", *x)
    to = [r for r in wk if r.get("verdict") == "TIMEOUT" and not IS_SOLO(r)]
    fam = collections.Counter((r.get("suite") or "?").split(".")[0] for r in to)
    print(f"\n== 4. TIMEOUT by family (week, gate rows) ==\n  {len(to)}: " +
          "  ".join(f"{k} {n}" for k, n in fam.most_common(12)))
    print("\n== 6b. the defect registry ==")
    if DEFECTS:
        for fld in ("kind", "found_by"):
            print(f"  by {fld}: " + "  ".join(f"{k} {n}" for k, n in collections.Counter(e[fld] for e in DEFECTS.values()).most_common()))
        print("  by state: " + "  ".join(f"{k} {n}" for k, n in collections.Counter(
            ci_gate_check.gate_runlog.defect_state(e) for e in DEFECTS.values()).most_common()))
    else:
        print("  (empty)")
    STAGE = ("stage", "stage-close")        # STAGE-CLOSE-1's batches are tier `stage-close`
    st = [g for g in GW if g["tier"] in STAGE]
    st_rows = [r for r in wk if r.get("tier") in STAGE and not IS_SOLO(r)]
    print(f"\n== 7. stage-close rows ==\n  stage runs {len(st)}  rows {len(st_rows)}  {sum(g['h'] for g in st):.1f} h  reds "
          f"{sum(1 for r in st_rows if r.get('verdict') != 'PASS')}")
    s2 = [r for r in wk_all if (r.get("schema") or 1) >= 2]
    slot, drain, hold = {}, {}, {}
    for r in s2:
        if r.get("slot_wait_s") is not None: slot[r.get("run")] = r["slot_wait_s"]
        if r.get("drain_s") is not None: drain[(r.get("run"), r["drain_s"])] = r["drain_s"]
        if r.get("hold_s") is not None:
            k = (r.get("run"), r.get("drain_s"))
            hold[k] = max(hold.get(k, 0.0), r["hold_s"])
    print(f"\n== 8. slot / drain / hold (schema-2 records: {len(s2)} of {len(wk_all)} in the week) ==")
    print(f"  slot wait {h(sum(slot.values()))} over {len(slot)} gate(s)   whole-card drain {h(sum(drain.values()))}   "
          f"hold {h(sum(hold.values()))}")
    q = [(r.get("box") or {}).get("queue_depth") for r in s2 if (r.get("box") or {}).get("queue_depth") is not None]
    mem = [((r.get("box") or {}).get("mem") or {}) for r in s2]
    psi = [m.get("psi10") for m in mem if m.get("psi10") is not None]
    print(f"  queue depth at row start: max {max(q) if q else '-'}   memory psi10 max {max(psi) if psi else '-'}   "
          f"rows started beside a build {sum(1 for m in mem if (m.get('builds') or 0) > 0)}")
    # THE FORCED EXITS (TESTING-CLEANUP-2 H8c; gate_runlog.forced_exit): rows whose app ended through the shutdown
    # watchdog's _Exit(86) instead of in order — green or red, each one a finding
    fx = [r for r in wk if r.get("forced_exit") is not None]
    print(f"\n== 9. forced exits (the shutdown watchdog, code 86; week, every row) ==\n  {len(fx)} record(s), "
          f"{sum(1 for r in fx if r.get('verdict') == 'PASS')} of them green: " +
          "  ".join(f"{k} {n}" for k, n in collections.Counter(r.get("suite") or "?" for r in fx).most_common(12)))
    print(f"\nTABLE gates {len(full)} | NOADMIT pools {len(hole)} | forced exits {len(fx)}")


if __name__ == "__main__":
    main()
