#!/usr/bin/env python3
"""source.row_budget — A ROW RENDERS SECONDS (TESTING-SPEED-A item 7; SPECS/TESTING_SPEED_SPEC.md §2 A8).

The merge runs what a change reaches, in minutes — so a KEPT row is a seconds-long guard of a named behaviour: its
measured price (scripts/gate-times.txt, the quiet median) is held against a BUDGET — the row's own (a KEEP:rewrite line of
tests/hygiene/row_budgets.txt, the lead's ROWS table) or its class's: a fixture (CLASS engine|selftest, a CPU row) 10 s, an
app row 60 s, a pool 60 s OF ARMS (the sum of its <pool>::<arm> prices, the boot on top) (the class read from the row's admission label `<row>:<class>`, a pool's `pool` label). A row over
its budget is A RED OF THE ROW — cut its frames, split it, or move it to the stage close — never a widened budget.

RED BY DESIGN ON THE BASE (0beeac4d3) until TESTING-SPEED-B lands Part B: the 45 KEEP:rewrite rows of the ROWS table are
over their budgets by definition (that is why they are rewritten); the row prints them and names the lane that owes them,
so the door reads a known red (the lead registers it at TESTING-SPEED-A's land). Rows whose fate is MOVE:stage-close,
THROW, SETUP or TOOLING are Part B's (moved, deleted, following their consumer, the tools' own); a stage-close / target /
hygiene / tooling labelled row is not a merge row at all. Fed a violation first (a lint that cannot fail guards nothing).

Run: row_budget.py <source-dir> <build-dir>
"""
import json
import os
import re
import subprocess
import sys

BUDGET = {"fixture": 10.0, "other": 10.0, "app": 60.0}
NOT_KEPT = ("MOVE:stage-close", "THROW", "SETUP", "TOOLING")
NOT_MERGE_LABELS = {"stage-close", "photon-target", "scale-target", "hygiene", "tooling", "quiet-box"}
OWED_BY = "TESTING-SPEED-B"
FAILURES = []


def check(ok, what):
    print(("  ok: " if ok else "  FAIL: ") + what)
    if not ok:
        FAILURES.append(what)


def parse_budgets(text):
    out = {}
    for line in text.splitlines():
        line = line.split("#", 1)[0].split()
        if len(line) == 4:
            row, klass, fate, b = line
            out[row] = (klass, fate, None if b == "-" else float(b))
    return out


def parse_prices(text):
    out = {}
    for line in text.splitlines():
        if line.startswith("#") or not line.strip():
            continue
        p = line.split()
        if len(p) >= 2:
            try:
                out[p[0]] = float(p[1])
            except ValueError:
                pass
    return out


def klass_of(t):
    """fixture | app | other, from the row's command and labels."""
    props = {p["name"]: p["value"] for p in t.get("properties", [])}
    labels = props.get("LABELS") or []
    labels = [labels] if isinstance(labels, str) else labels
    cmd = " ".join(t.get("command") or [])
    m = re.search(r"--label \S+:(\w+)", cmd)
    if "pool" in labels or (m and m.group(1) in ("app", "vr")) or "bin/Jahshaka" in cmd:
        return "app"
    if m:
        return "fixture"
    return "other"


def over(rows, budgets, prices):
    """[(row, price, budget, fate)] of the kept merge rows over their budgets. `rows`: [(name, klass, labels)]."""
    out = []
    for name, klass, labels in rows:
        if set(labels) & NOT_MERGE_LABELS or any(l.startswith("lane:") for l in labels):
            continue                   # a lane's own row (tests/lanes/<LANE>/) goes at the lane's land — not a kept row
        kb, fate, own = budgets.get(name, (klass, "KEEP", None))
        if fate in NOT_KEPT:
            continue
        b = own if own is not None else BUDGET.get(klass, 10.0)
        if "pool" in labels:
            # THE POOL RULE (the lead, batch 2's read): a pool's budget is 60 s OF ARMS — the sum of its `<pool>::<arm>`
            # price lines, the boot on top; never the whole row (pool.capture: 71.8 s whole, 51 s of arms: green). A pool
            # with no arm price is not judged here (its arms are priced by --record-times)
            arms = [v for k, v in prices.items() if k.startswith(name + "::")]
            p = round(sum(arms), 2) if arms else None
        else:
            p = prices.get(name)
        if p is not None and p > b:
            out.append((name, p, b, fate))
    return sorted(out)


def main(source, build):
    print("the rule catches (synthetic):")
    b = parse_budgets("a fixture KEEP -\nr app KEEP:rewrite 20\nm app MOVE:stage-close -\n")
    rows = [("a", "fixture", []), ("r", "app", []), ("m", "app", []), ("n", "app", []), ("s", "fixture", ["stage-close"])]
    got = over(rows, b, {"a": 12, "r": 25, "m": 900, "n": 61, "s": 99})
    check([x[0] for x in got] == ["a", "n", "r"], "a fixture over 10 s, an app row over 60 s and a rewrite row over its "
          "own 20 s are red; a row Part B moves and a stage-close row are not (%s)" % got)
    check(not over([("r", "app", [])], b, {"r": 19}), "a rewrite row inside its own budget is green")
    pr = {"pool.p": 71.8, "pool.p::a": 30.0, "pool.p::b": 21.0, "pool.q": 40.0, "pool.q::a": 45.0, "pool.q::b": 20.0}
    got = over([("pool.p", "app", ["pool"]), ("pool.q", "app", ["pool"])], {}, pr)
    check([x[0] for x in got] == ["pool.q"] and got[0][1] == 65.0,
          "THE POOL RULE: a pool is judged by the SUM OF ITS ARMS against 60 s, never the whole row — pool.p (71.8 s whole, "
          "51 s of arms) green, pool.q (40 s whole, 65 s of arms) red (%s)" % got)
    sys.path.insert(0, os.path.join(source, "scripts"))
    import gate_graph
    raw, rc = gate_graph.ctest_inventory(build)
    tests = json.loads(raw or "{}").get("tests", []) if rc == 0 else []
    check(bool(tests), "the build registers rows (%d)" % len(tests))
    rows = []
    for t in tests:
        props = {p["name"]: p["value"] for p in t.get("properties", [])}
        labels = props.get("LABELS") or []
        rows.append((t["name"], klass_of(t), [labels] if isinstance(labels, str) else labels))
    budgets = parse_budgets(open(os.path.join(source, "tests", "hygiene", "row_budgets.txt")).read())
    prices = parse_prices(open(os.path.join(source, "scripts", "gate-times.txt")).read())
    bad = over(rows, budgets, prices)
    rewrite = [x for x in bad if x[3] == "KEEP:rewrite"]
    other = [x for x in bad if x[3] != "KEEP:rewrite"]
    print("the tree: %d kept merge row(s) over budget — %d KEEP:rewrite (owed by %s), %d other"
          % (len(bad), len(rewrite), OWED_BY, len(other)))
    for name, p, b_, fate in bad:
        print("  OVER  %-48s %7.1f s > %5.1f s  (%s%s)" % (name, p, b_, fate,
                                                         ", owed by " + OWED_BY if fate == "KEEP:rewrite" else ""))
    check(not bad, "every kept merge row is inside its budget (%d over: %d KEEP:rewrite rows — KNOWN RED BY DESIGN until "
          "%s lands Part B — and %d other)" % (len(bad), len(rewrite), OWED_BY, len(other)))
    if FAILURES:
        print("source.row_budget: FAILED (%d)" % len(FAILURES))
        return 1
    print("source.row_budget: PASSED")
    return 0


BUDGET_RX = re.compile(r"(?:<=|\u2264|\bbudget)\s*(\d+(?:\.\d+)?)\s*s\b")
ROW_RX = re.compile(r"\|\s*([A-Za-z0-9_.:\-]+)\s*\|\s*(\w+)\s*\|\s*([\d.]+|-)\s*\|\s*([A-Za-z:\-]+)\s*\|(.*)\|\s*$")


def generate(rows_md):
    """row_budgets.txt FROM THE ROWS TABLE (the lead, batch 2's read: the table is the one source of budgets; this file is
    generated, never hand-kept): `<row> <class> <fate> <budget s|->` per table row, the budget the LAST `<= N s` /
    `budget N s` of a KEEP or KEEP:rewrite row's reason, `-` otherwise (its class's)."""
    out, seen = [], set()
    for line in open(rows_md, errors="replace"):
        m = ROW_RX.match(line)
        if not m or m.group(1) in seen or m.group(1) in ("row", "---"):
            continue
        name, klass, secs, fate, why = m.groups()
        seen.add(name)
        if secs == "-":
            # a row the table prices "-" (a NEW row, no record yet): KEPT in the file, and SAID — never dropped silently
            sys.stderr.write(f"row_budgets: WARN {name} has no seconds in the table (\"-\"): kept with its fate and budget\n")
        b = "-"
        if fate.startswith("KEEP"):
            nums = BUDGET_RX.findall(why)
            if nums:
                b = ("%g" % float(nums[-1]))
        out.append(f"{name} {klass} {fate} {b}")
    return out


if __name__ == "__main__":
    sys.stdout.reconfigure(line_buffering=True)
    if sys.argv[1:2] == ["--generate"]:
        # row_budget.py --generate <SPECS/TESTING_SPEED_ROWS.md> <tests/hygiene/row_budgets.txt>: the header kept
        dst = sys.argv[3]
        head = [l.rstrip("\n") for l in open(dst) if l.startswith("#")] if os.path.exists(dst) else []
        body = generate(sys.argv[2])
        open(dst, "w").write("\n".join(head + body) + "\n")
        print("row_budgets: %d rows from %s (%d with their own budget)" % (len(body), sys.argv[2],
                                                                          sum(1 for l in body if not l.endswith(" -"))))
        sys.exit(0)
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(2)
    sys.exit(main(os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2])))
