#!/usr/bin/env python3
"""source.row_budget — A ROW IS A SECONDS-LONG GUARD (DOCS/PROCESS.md, Testing; TESTING-SIMPLE).

ONE RULE: every kept guard — every registered row but a `measure` row (it runs at the sprint's ship only) — is held
against its class's budget at its price in scripts/gate-times.txt: a fixture or a CPU row 10 s, an app row (CLASS
app|vr|selftest, a pool) 60 s. A guard over its budget is RED: rewritten to its budget, split (a pool by its arms),
or labelled `measure` (tests/CMakeLists.txt's JAH_MEASURE_ROWS: a guard of a named behaviour is never deleted for its
price) — never excused, there is no "by design". An unpriced guard is source.testing_rules' R2. Fed a violation first (a lint that cannot
fail guards nothing).

Run: row_budget.py <source-dir> <build-dir>
"""
import json
import os
import sys

BUDGET = {"app": 60.0, "vr": 60.0, "selftest": 60.0}       # every other class (engine, a CPU row): 10 s
FAILURES = []


def check(ok, what):
    print(("  ok: " if ok else "  FAIL: ") + what)
    if not ok:
        FAILURES.append(what)


def budget_of(name, props, cmd=()):
    """An app row: CLASS app|vr|selftest, a pool, or a row whose command starts the app (a harness or a headless app row
    registered by plain add_test); every other row 10 s."""
    if name.startswith("pool.") or any(str(c).endswith("/bin/Jahshaka") for c in cmd):
        return 60.0
    return BUDGET.get(props.get("JAH_CLASS") or "", 10.0)


def over(tests, prices):
    """[(row, price, budget)] of the guards over their budgets. `tests`: ctest's json-v1 `tests`."""
    out = []
    for t in tests:
        p = {x["name"]: x["value"] for x in t.get("properties", [])}
        labels = p.get("LABELS") or []
        labels = [labels] if isinstance(labels, str) else labels
        if "measure" in labels or t["name"] not in prices:
            continue
        b = budget_of(t["name"], p, t.get("command") or ())
        if prices[t["name"]] > b:
            out.append((t["name"], prices[t["name"]], b))
    return sorted(out, key=lambda x: -x[1])


def prices_of(path):
    out = {}
    for line in open(path, errors="replace"):
        p = line.split()
        if len(p) >= 2 and not line.startswith("#"):
            try:
                out[p[0]] = float(p[1])
            except ValueError:
                pass
    return out


def main(source, build):
    T = lambda n, **p: {"name": n, "properties": [{"name": k, "value": v} for k, v in p.items()]}
    synth = [T("f.slow", JAH_CLASS="engine"), T("a.slow", JAH_CLASS="app"), T("a.ok", JAH_CLASS="app"),
             T("m.long", JAH_CLASS="engine", LABELS=["measure"]), T("pool.p", JAH_CLASS="app"), T("lint")]
    got = [n for n, _, _ in over(synth, {"f.slow": 12, "a.slow": 61, "a.ok": 59, "m.long": 500, "pool.p": 59,
                                         "lint": 11})]
    print("the rule catches (synthetic):")
    check(sorted(got) == ["a.slow", "f.slow", "lint"], "a fixture over 10 s, an app row over 60 s and a CPU row over "
          "10 s are red; an app row under 60 s, a pool under 60 s and a measure row are not (%s)" % got)
    sys.path.insert(0, os.path.join(source, "scripts"))
    import gate_graph            # a listing from a copy of the CTestTestfile tree (never truncates a running gate's log)
    tests = json.loads(gate_graph.ctest_inventory(build)[0] or "{}").get("tests", [])
    check(len(tests) > 0, "the build registers rows (%d)" % len(tests))
    bad = over(tests, prices_of(os.path.join(source, "scripts", "gate-times.txt")))
    print("the tree:")
    for n, p, b in bad:
        print("  OVER  %-48s %7.1f s > %5.1f s" % (n, p, b))
    check(not bad, "every kept guard is within its budget (%d over)" % len(bad))
    print("source.row_budget: %s" % ("FAILED (%d)" % len(FAILURES) if FAILURES else "PASSED"))
    return 1 if FAILURES else 0


if __name__ == "__main__":
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(2)
    sys.exit(main(os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2])))
