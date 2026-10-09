#!/usr/bin/env python3
"""gate-report.py — THE WEEKLY TABLE OF THE RUN LOG (GATE-LOG-1, lane VERDICT-1; the end-of-sprint read).

The audit's and the preflight's queries (SPECS/audits/GATE_COST_2026-10-09.md; spikes/preflight-testing-1/
q_runlog.py + q_refine.py) as ONE command over the run log's directories (testing/runs, testing/runs-archive —
read recursively; a record later than the week's end is not read):

    scripts/gate-report.py <runs dir> [<runs dir> ...] [--week <date | date>T<hh:mm>] [--ref <integration ref>]
                           [--carried] [--contention <file>]

  --week   the seven days ENDING at that moment (a bare date: the end of that day; default: now)
  --ref    the integration line a tip "landed" on, for the carried reds (default d-build; the Studio repo
           this script lives in is asked, so a moved ref gives a later answer — name the sha to reproduce one)
  --carried  print every carried red (lane, tip, row), not only the per-lane counts

THE TABLE: gates by kind (tier) and the full-size ones (>= 400 rows) with their hours, alone vs shared (a full
gate with < 0.2 sibling ctests on average is alone); solos; NOADMIT (records, and the pools whose every arm was
NOADMIT — recorded FAIL before VERDICT-1 U2, NOADMIT since); TIMEOUT by family; the verdicts, their red classes
and what the verdict door (scripts/ci_gate_check.py door()) and the two preflight rules would refuse; the
carried reds (open reds left on a never-landed tip of a lane that merged — VERDICT-1 U4's report; forward, the
merge refusal carries them); the stage-close rows' runs; the slot / drain / hold hours (schema 2 records).
Section 1 and the per-week rows read the week; the verdicts, the NOADMIT pools and the carried reds read every
record up to the week's end (the log the judge reads)."""
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
import ci_gate_check  # noqa: E402  (the door)

GATE_TIERS = ("scoped", "scoped-fallback", "scoped-tier", "fork", "joint")
FULL_ROWS = 400
ALONE_SIB = 0.2
DISPLAY_DEATH = re.compile(r"Malformed resolution|display", re.I)      # rows killed by a dead display: not a red


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
        if not r.get("retry"): runs[r.get("run")].append(r)
    G = []
    for k, rs in runs.items():
        s = min(x["_t"] - datetime.timedelta(seconds=x.get("wallSeconds") or x.get("seconds") or 0) for x in rs)
        e = max(x["_t"] for x in rs)
        G.append(dict(run=k, tier=rs[0].get("tier"), lane=rs[0].get("lane"), n=len(rs), start=s, end=e,
                      h=(e - s).total_seconds() / 3600, sib=sum((x.get("box") or {}).get("other_ctests") or 0
                                                                for x in rs) / len(rs)))
    return rows, G


def preflight_verdicts(R, contention):
    """The preflight's rules over every verdict record (q_runlog Q5) + the door: {rule: [lines]}."""
    V = [r for r in R if r.get("kind") == "verdict" and r["_t"]]
    idx = collections.defaultdict(list)
    for r in R:
        if r.get("kind") != "verdict" and r["_t"]:
            idx[((r.get("tip") or {}).get("studio"), r.get("suite"), r.get("arm"))].append(r)
    DEFECT = re.compile(r"\b[A-Z][A-Z0-9]+(?:-[A-Z0-9]+)*-\d+[a-z]?\b")
    XID = re.compile(r"\bXid\b", re.I)
    kinds_c, out = collections.Counter(), collections.defaultdict(list)
    for v in V:
        t = (v.get("tip") or {}).get("studio")
        recs = sorted([r for r in idx[(t, v.get("suite"), v.get("arm"))] if r["_t"] <= v["_t"]], key=lambda r: r["_t"])
        reds = [r for r in recs if r.get("verdict") not in ("PASS", "NOADMIT", "NOTRUN")]
        lastred = max((r["_t"] for r in reds if not r.get("retry")), default=None)
        solos = [r for r in recs if r.get("retry") and (lastred is None or r["_t"] > lastred)]
        spass = sum(1 for r in solos if r.get("verdict") == "PASS")
        kinds = {r.get("verdict") for r in reds}
        text = v.get("text") or ""
        kinds_c[tuple(sorted(kinds)) or ("no red found",)] += 1
        tag = (v.get("suite"), v.get("arm"), sorted(kinds), text[:90])
        if solos and spass < 3:
            out["H2: solos below 3/3 cleared by text"].append(tag + ("%d/%d solo" % (spass, len(solos)),))
        hard = kinds & {"LOST", "OOM", "CRASH"}
        if hard and not (DEFECT.search(text) or XID.search(text)):
            out["H1: LOST/OOM/CRASH without a defect id or an Xid read"].append(tag)
        if "LOST" in kinds and not ci_gate_check._xid_window(text) and not ci_gate_check.DEFECT_ID.search(text):
            out["H1: a LOST verdict with no journal window (xid-read:) and no real:<id>"].append(tag)
        if reds:
            gate_reds = [r for r in reds if not r.get("retry")]
            last_gate = max((r["_t"] for r in gate_reds), default=None)
            all_solos = [r for r in idx[(t, v.get("suite"), v.get("arm"))]
                         if r.get("retry") and last_gate is not None and r["_t"] > last_gate]
            ok, why = ci_gate_check.door(text, reds, all_solos)
            if not ok:
                out["THE DOOR (VERDICT-1 U1, every rule incl. the class token)"].append(tag + (why[:70],))
    return V, kinds_c, out


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


def carried(R, repo, ref):
    """q_refine's query: open (unanswered) reds on tips that never landed on `ref`, of lanes that DID merge."""
    def git(*a):
        return subprocess.run(["git", "-C", repo] + list(a), capture_output=True, text=True)
    cache = {}

    def landed(t):
        if t not in cache: cache[t] = git("merge-base", "--is-ancestor", t, ref).returncode == 0
        return cache[t]
    idx = collections.defaultdict(list)
    vt = collections.defaultdict(list)
    tips_by_lane = collections.defaultdict(set)
    for r in R:
        t = (r.get("tip") or {}).get("studio")
        if r.get("kind") == "verdict":
            if r["_t"]: vt[(t, r.get("suite"), r.get("arm"))].append(r["_t"])
            continue
        idx[(t, r.get("suite"), r.get("arm"))].append(r)
        if t:
            for ln in _lanes(r): tips_by_lane[ln].add(t)
    merged = {ln for ln, ts in tips_by_lane.items() if any(landed(t) for t in ts)}
    out = collections.defaultdict(list)
    for (t, s, a), rs in idx.items():
        if not t or a: continue
        rs = [r for r in rs if r["_t"] and r.get("tier") in GATE_TIERS and r.get("gating", True)]
        reds = [r for r in rs if r.get("verdict") not in ("PASS", "NOADMIT", "NOTRUN") and not r.get("retry")
                and not DISPLAY_DEATH.search((r.get("failLine") or "") + (r.get("status") or ""))]
        if not reds: continue
        last = max(r["_t"] for r in reds)
        if any(x > last for x in vt.get((t, s, a), [])): continue
        solos = [r for r in rs if r.get("retry") and r["_t"] > last]
        if s in CONT and len(solos) >= 3 and all(r.get("verdict") == "PASS" for r in solos): continue
        lane = sorted(_lanes(reds[-1]) or {"?"})[0]
        if lane not in merged or landed(t): continue
        out[lane].append((t[:9], s, reds[-1].get("verdict"), (reds[-1].get("ts") or "")[:16]))
    return merged, out


def _lanes(r):
    v = r.get("lanes")
    if isinstance(v, list): return {x for x in v if x}
    return {r["lane"]} if r.get("lane") else set()


CONT = set()


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("dirs", nargs="+")
    ap.add_argument("--week", default=None)
    ap.add_argument("--ref", default="d-build")
    ap.add_argument("--carried", action="store_true")
    ap.add_argument("--contention", default=None, help="the contention list (default: <first dir>/../contention.json)")
    a = ap.parse_args()
    end = parse_end(a.week)
    start = end - datetime.timedelta(days=7)
    R = read(a.dirs, end)
    cfile = a.contention or os.path.join(os.path.dirname(os.path.abspath(a.dirs[0]).rstrip("/")), "contention.json")
    try:
        CONT.update(json.load(open(cfile)).get("suites", {}).keys())
    except (OSError, ValueError, AttributeError):
        print(f"(no contention list at {cfile}: the carried query reads no 3/3 solo clearance)")
    print(f"GATE REPORT  week {start.isoformat(timespec='minutes')} .. {end.isoformat(timespec='minutes')}  "
          f"records read {len(R)} (to the week's end) from {', '.join(a.dirs)}")

    rows, G = gates(R)
    GW = [g for g in G if g["start"] >= start]
    full = [g for g in GW if g["n"] >= FULL_ROWS]
    print("\n== 1. gates in the week (non-retry runs) ==")
    print(f"  runs {len(GW)}  full-size (>= {FULL_ROWS} rows) {len(full)}  their wall {h(sum(g['h'] for g in full) * 3600)}")
    by = collections.defaultdict(lambda: [0, 0.0])
    for g in GW:
        by[g["tier"]][0] += 1; by[g["tier"]][1] += g["h"]
    print("  by kind: " + "  ".join(f"{t} {n} ({hh:.1f} h)" for t, (n, hh) in sorted(by.items(), key=lambda kv: -kv[1][0])))
    alone = [g for g in full if g["sib"] < ALONE_SIB]
    shared = [g for g in full if g["sib"] >= ALONE_SIB]
    med = lambda xs: sorted(xs)[len(xs) // 2] if xs else 0.0
    print(f"  full-size ALONE (< {ALONE_SIB} sibling ctests): {len(alone)}, {sum(g['h'] for g in alone):.1f} h "
          f"(median {med([g['h'] for g in alone]):.2f} h)   SHARED: {len(shared)}, {sum(g['h'] for g in shared):.1f} h "
          f"(median {med([g['h'] for g in shared]):.2f} h)")
    wk = [r for r in rows if r["_t"] >= start]
    solos = [r for r in wk if r.get("retry")]
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
    to = [r for r in wk if r.get("verdict") == "TIMEOUT" and not r.get("retry")]
    fam = collections.Counter((r.get("suite") or "?").split(".")[0] for r in to)
    print(f"\n== 4. TIMEOUT by family (week, gate rows) ==\n  {len(to)}: " +
          "  ".join(f"{k} {n}" for k, n in fam.most_common(12)))
    V, kinds, ref = preflight_verdicts(R, CONT)
    print(f"\n== 5. verdicts ==\n  verdict records {len(V)}  red classes answered: "
          + "  ".join(f"{'/'.join(k)} {n}" for k, n in kinds.most_common()))
    for rule, xs in ref.items():
        print(f"  {rule}: {len(xs)}")
        if "DOOR" not in rule or len(xs) <= 8:
            for x in xs: print("      ", x)
    merged, out = carried(R, ci_gate_check.gate_runlog.ROOT, a.ref)
    tot = sum(len(v) for v in out.values())
    print(f"\n== 6. carried reds (open reds on never-landed tips of MERGED lanes; display deaths excluded; ref {a.ref}) ==")
    print(f"  merged lanes {len(merged)}  carried reds {tot}  on tips {len({x[0] for v in out.values() for x in v})}  "
          f"lanes {len(out)}")
    for ln, xs in sorted(out.items(), key=lambda kv: -len(kv[1])):
        print("    %-22s %3d red(s) on %d tip(s) %s" % (ln, len(xs), len({x[0] for x in xs}),
                                                     dict(collections.Counter(x[2] for x in xs))))
        if a.carried:
            for x in xs: print("        ", x)
    st = [g for g in GW if g["tier"] == "stage"]
    st_rows = [r for r in wk if r.get("tier") == "stage" and not r.get("retry")]
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
    print(f"\nTABLE gates {len(full)} | verdicts {len(V)} | refusable(H2 solos<3/3) "
          f"{len(ref.get('H2: solos below 3/3 cleared by text', []))} | door refuses "
          f"{len(ref.get('THE DOOR (VERDICT-1 U1, every rule incl. the class token)', []))} | NOADMIT pools {len(hole)} | "
          f"carried {tot}")


if __name__ == "__main__":
    main()
