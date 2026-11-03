#!/usr/bin/env python3
"""source.code_rows - THE `code` LABEL MEANS "TAKES NO CARD" (TEST-STREAMLINE-1 1b; CODING_RULES §5, the stage loop).

`spec.sh test <spec> --code` runs the rows labelled `code` that a stage's diff reaches, ctest -j12, with NO card lock and NO
warm-up. So a `code` row must take no card, and nothing ctest pulls in beside it may either. tests/CMakeLists.txt
(jah_code_label_pass) derives the label; this row holds the listing to the same rule, both ways:
  - a `code` row has no card slots (RESOURCE_GROUPS), no JAH_CLASS, no `measure` label, no JAH_BOOTS (a warm-up's product:
    source.shaders_spirv_valid reads the app-off boot's dump - it runs with the six at a stage's end);
  - every setup its FIXTURES_REQUIRED names is `code` (ctest runs a required setup outside -L: a GPU setup would run
    unlocked), and it is no setup or cleanup of a fixture a non-code row requires (a GPU row's fixture half);
  - and every row that meets those is labelled (a CPU row is a code test at once - none left out).
Fed a violation first (a lint that cannot fail guards nothing). Prints the count.
usage: code_rows.py <source-dir> <build-dir>
"""
import json
import os
import sys

FAILURES = []


def check(ok, what):
    print(("  ok: " if ok else "  FAIL: ") + what)
    if not ok:
        FAILURES.append(what)


def _list(v):
    return [] if v is None else [v] if isinstance(v, str) else list(v)


def judge(tests):
    """(labelled, wrongly labelled [(row, why)], left out [row]) of a json-v1 `tests` list."""
    P = {t["name"]: {x["name"]: x["value"] for x in t.get("properties", [])} for t in tests}
    takes = {n for n, p in P.items()
             if p.get("RESOURCE_GROUPS") or p.get("JAH_CLASS") or "measure" in _list(p.get("LABELS")) or p.get("JAH_BOOTS")}
    code = set(P) - takes
    setups, users = {}, {}
    for n, p in P.items():
        for f in _list(p.get("FIXTURES_SETUP")) + _list(p.get("FIXTURES_CLEANUP")):
            setups.setdefault(f, set()).add(n)
        for f in _list(p.get("FIXTURES_REQUIRED")):
            users.setdefault(f, set()).add(n)
    moved = True
    while moved:
        moved = False
        for n in sorted(code):
            p = P[n]
            half = any(u not in code for f in _list(p.get("FIXTURES_SETUP")) + _list(p.get("FIXTURES_CLEANUP"))
                       for u in users.get(f, ()))
            pulls = any(s not in code for f in _list(p.get("FIXTURES_REQUIRED")) for s in setups.get(f, ()))
            if half or pulls:
                code.discard(n)
                moved = True
    labelled = {n for n, p in P.items() if "code" in _list(p.get("LABELS"))}
    wrong = []
    for n in sorted(labelled - code):
        p = P[n]
        why = ("card slots" if p.get("RESOURCE_GROUPS") else "a JAH_CLASS" if p.get("JAH_CLASS") else
               "a measure row" if "measure" in _list(p.get("LABELS")) else "a warm-up's product (JAH_BOOTS)"
               if p.get("JAH_BOOTS") else "a card row's fixture half, or it requires a setup that takes the card")
        wrong.append((n, why))
    return labelled, wrong, sorted(code - labelled)


def T(name, **props):
    return {"name": name, "properties": [{"name": k, "value": v} for k, v in props.items()]}


def main(source, build):
    print("the rule catches (synthetic):")
    synth = [T("c.ok", LABELS=["hygiene", "code"]), T("c.slots", LABELS=["code"], RESOURCE_GROUPS=[{"vram": 8}]),
             T("c.class", LABELS=["code"], JAH_CLASS="app"), T("c.measure", LABELS=["measure", "code"]),
             T("c.boots", LABELS=["code"], JAH_BOOTS="app-off"),
             T("g.row", JAH_CLASS="engine", FIXTURES_REQUIRED=["fxg"]), T("c.half", LABELS=["code"], FIXTURES_SETUP=["fxg"]),
             T("g.setup", JAH_CLASS="app", FIXTURES_SETUP=["fxs"]), T("c.pulls", LABELS=["code"], FIXTURES_REQUIRED=["fxs"]),
             T("c.setup_ok", LABELS=["code"], FIXTURES_SETUP=["fxc"]), T("c.user_ok", LABELS=["code"], FIXTURES_REQUIRED=["fxc"]),
             T("c.left_out", LABELS=["hygiene"])]
    _, wrong, left = judge(synth)
    check(sorted(n for n, _ in wrong) == ["c.boots", "c.class", "c.half", "c.measure", "c.pulls", "c.slots"] and left == ["c.left_out"],
          "card slots, a class, a measure row, a warm-up product, a GPU row's fixture half and a row pulling a GPU setup are "
          "refused the label; a CPU setup of a CPU row keeps it; an unlabelled CPU row is named (%s / %s)" % (wrong, left))
    sys.path.insert(0, os.path.join(source, "scripts"))
    import gate_graph            # a listing from a copy of the CTestTestfile tree (never truncates a running gate's log)
    tests = json.loads(gate_graph.ctest_inventory(build)[0] or "{}").get("tests", [])
    check(len(tests) > 0, "the build registers rows (%d)" % len(tests))
    labelled, wrong, left = judge(tests)
    print("the tree: %d rows labelled code of %d" % (len(labelled), len(tests)))
    for n, why in wrong:
        print("  WRONGLY code  %-48s %s" % (n, why))
    for n in left:
        print("  NOT LABELLED  %s" % n)
    check(not wrong and not left, "the `code` label is exactly the rows that take no card (%d wrong, %d left out)" % (len(wrong), len(left)))
    print("source.code_rows: %s" % ("FAILED (%d)" % len(FAILURES) if FAILURES else "PASSED"))
    return 1 if FAILURES else 0


if __name__ == "__main__":
    if len(sys.argv) < 3:
        sys.exit(__doc__.strip().splitlines()[-1])
    sys.exit(main(sys.argv[1], sys.argv[2]))
