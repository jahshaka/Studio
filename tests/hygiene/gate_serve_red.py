#!/usr/bin/env python3
"""gate.serve_red (URGENT #54) - A RED AT THE SAME INPUT IS SERVED-RED, NEVER RE-RUN: ci_gate_check.py serve's decision
(classify / red_stands) on synthetic records. Re-running a red at the same input is a quiet retry that could flip it green
by chance, so a red RESULT (FAIL, TIMEOUT, CRASH) recorded at THIS tree with the same build configuration, declaration and
data is served-red (reported with its record, neither served nor run); a red of an ANCESTOR's tree runs (the diff may have
fixed it); a PASS serves as before; NOTRUN / NOADMIT are not results. No build, no git, no display.
usage: gate_serve_red.py <source dir>"""
import os
import sys

src = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "..")
sys.path.insert(0, os.path.join(src, "scripts"))
import ci_gate_check as cgc  # noqa: E402


class Reach:
    """A Reach of the test's making: whether a record's tree is this one, and what A..C reaches."""
    def __init__(self, same, reaches=(), fork=()):
        self.same, self.reaches, self.fork, self.tip = same, set(reaches), set(fork), "c" * 40

    def same_tree(self, a):
        return self.same

    def of(self, a):
        return self.reaches


def rec(verdict, cfg="cfg", decl="d", data="h"):
    return {"verdict": verdict, "failLine": "FAIL: the law", "tip": {"studio": "a" * 40}, "config": {"hash": cfg},
            "declaration": {"hash": decl}, "data_hash": data, "run": "run-1", "seconds": 3.0, "tier": "lane"}


FACT = {"declaration": {"hash": "d"}, "data_hash": "h", "labels": ["gi"]}
CFG = {"hash": "cfg", "build_type": "Debug"}
fails = []


def want(name, got, kind, why_has=None):
    ok = got[0] == kind and (why_has is None or (got[1] or "").find(why_has) >= 0)
    print(("ok   " if ok else "FAIL ") + "%s: %s%s" % (name, got[0], (" (%s)" % got[1]) if got[1] else ""))
    if not ok:
        fails.append(name)


C = lambda r, reach, fact=FACT, cfg=CFG: cgc.classify(r, fact, "gi.row", reach, "fk", "fk", cfg)
for v in ("FAIL", "TIMEOUT", "CRASH"):
    want("a %s at THIS tree, the same configuration, declaration and data: SERVED-RED" % v, C(rec(v), Reach(True)), "red")
want("a FAIL at an ANCESTOR's tree runs (the diff may have fixed it)", C(rec("FAIL"), Reach(False)), "run", "ancestor")
want("a FAIL at this tree, its build configuration changed: runs", C(rec("FAIL", cfg="other"), Reach(True)), "run",
     "configuration")
want("a FAIL at this tree, its declaration changed: runs", C(rec("FAIL", decl="d2"), Reach(True)), "run", "declaration")
want("a FAIL at this tree, its data changed: runs", C(rec("FAIL", data="h2"), Reach(True)), "run", "data")
want("a FAIL at this tree of a row out of the inventory: runs", C(rec("FAIL"), Reach(True), fact=None), "run")
want("a PASS at this tree serves as before", C(rec("PASS"), Reach(True)), "served")
want("a PASS at an ancestor the range does not reach serves as before", C(rec("PASS"), Reach(False)), "served")
want("a PASS at an ancestor the range reaches runs", C(rec("PASS"), Reach(False, reaches={"gi.row"})), "run", "reaches")
for v in ("NOTRUN", "NOADMIT"):
    want("a %s is not a result: never served-red (it runs)" % v, C(rec(v), Reach(True)), "run")
print("gate.serve_red:", "FAIL (%d)" % len(fails) if fails else "PASS")
sys.exit(1 if fails else 0)
