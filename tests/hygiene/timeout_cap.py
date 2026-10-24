#!/usr/bin/env python3
"""source.timeout_cap — EVERY TEST IS CAPPED AT 60 s (the owner's law, 2026-10-20; URGENT #48, 2026-10-24).

A row longer than a minute is rebuilt to the fewest frames its law needs or archived - never registered, never widened.
Its TIMEOUT is that cap made mechanical, so this row refuses, on the REGISTERED set (ctest's own json listing - whatever
helper or variable computed the value):
  (1) a row with no TIMEOUT (ctest's default is 1,500 s: no cap at all);
  (2) a row whose TIMEOUT is over 60 s;
  (3) a row whose command bounds its run past 60 s (gpu-exclusive.sh --run-timeout <s>);
and, on the TEXT of tests/**/CMakeLists.txt (code, not comments), a literal `TIMEOUT <n>` / `RUN_TIMEOUT <n>` or a
`set(_<..>timeout<..> <n>)` over 60 - a value no row reads today is the next row's. Fed violations first (a lint that
cannot fail guards nothing).

Run: timeout_cap.py <source-dir> <build-dir>
"""
import glob
import json
import os
import re
import sys

CAP = 60.0
FAILURES = []
CODE_RE = re.compile(r'\b((?:RUN_)?TIMEOUT)\s+(\d+)\b')
VAR_RE = re.compile(r'^\s*set\((_\w*timeout\w*|_cap_timing_\w+)\s+(\d+)\)', re.I)


def check(ok, what):
    print(("  ok: " if ok else "  FAIL: ") + what)
    if not ok:
        FAILURES.append(what)


def rows_over(tests):
    """[(row, why)] of the registered rows past the cap. `tests`: ctest's json-v1 `tests`."""
    out = []
    for t in tests:
        p = {x["name"]: x["value"] for x in t.get("properties", [])}
        to = p.get("TIMEOUT")
        if to is None:
            out.append((t["name"], "no TIMEOUT (ctest's default: uncapped)"))
        elif float(to) > CAP:
            out.append((t["name"], "TIMEOUT %g" % float(to)))
        cmd = [str(c) for c in (t.get("command") or [])]
        for i, c in enumerate(cmd[:-1]):
            if c == "--run-timeout" and cmd[i + 1].replace(".", "", 1).isdigit() and float(cmd[i + 1]) > CAP:
                out.append((t["name"], "--run-timeout %s" % cmd[i + 1]))
    return out


def text_over(lines):
    """[(line number, text)] of the code lines that write a timeout over the cap."""
    out = []
    for n, ln in enumerate(lines, 1):
        code = ln.split("#", 1)[0]
        hit = any(int(m.group(2)) > CAP for m in CODE_RE.finditer(code))
        m = VAR_RE.match(code)
        if hit or (m and int(m.group(2)) > CAP):
            out.append((n, ln.strip()))
    return out


def main(source, build):
    T = lambda n, cmd=(), **p: {"name": n, "command": list(cmd),
                                "properties": [{"name": k, "value": v} for k, v in p.items()]}
    synth = [T("a.ok", TIMEOUT=60), T("b.long", TIMEOUT=61), T("c.none"), T("d.ok", TIMEOUT=10),
             T("e.run", ("/s/gpu-exclusive.sh", "--run-timeout", "300", "/b/x"), TIMEOUT=60),
             T("f.run", ("/s/gpu-exclusive.sh", "--run-timeout", "40", "/b/x"), TIMEOUT=60)]
    got = sorted({n for n, _ in rows_over(synth)})
    print("the rule catches (synthetic rows):")
    check(got == ["b.long", "c.none", "e.run"], "a TIMEOUT over 60, a missing TIMEOUT and a run bound over 60 are red; "
          "60, 10 and a run bound of 40 are not (%s)" % got)
    txt = ["set_tests_properties(x PROPERTIES TIMEOUT 120 LABELS \"a\")", "set_tests_properties(y PROPERTIES TIMEOUT 60)",
           "# TIMEOUT 430 = the old budget (a comment)", "jah_gpu_exclusive_test(NAME z RUN_TIMEOUT 300 COMMAND z)",
           "set(_soak_timeout_primitives 230)", "set(_reopen_timeout_main 60)"]
    print("the rule catches (synthetic text):")
    check([n for n, _ in text_over(txt)] == [1, 4, 5], "a literal TIMEOUT 120, a RUN_TIMEOUT 300 and a timeout "
          "variable of 230 are red; 60, a variable of 60 and a comment are not (%s)" % [n for n, _ in text_over(txt)])
    sys.path.insert(0, os.path.join(source, "scripts"))
    import gate_graph            # a listing from a copy of the CTestTestfile tree (never truncates a running gate's log)
    tests = json.loads(gate_graph.ctest_inventory(build)[0] or "{}").get("tests", [])
    check(len(tests) > 0, "the build registers rows (%d)" % len(tests))
    bad = rows_over(tests)
    print("the registered rows:")
    for n, why in bad:
        print("  OVER  %-56s %s" % (n, why))
    check(not bad, "every registered row has a TIMEOUT within 60 s and no run bound past it (%d over)" % len(bad))
    print("the text (tests/**/CMakeLists.txt):")
    nText = 0
    for f in sorted(glob.glob(os.path.join(source, "tests", "**", "CMakeLists.txt"), recursive=True)):
        for n, ln in text_over(open(f, errors="replace").read().split("\n")):
            print("  OVER  %s:%d  %s" % (os.path.relpath(f, source), n, ln[:120]))
            nText += 1
    check(nText == 0, "no code line writes a timeout over 60 (%d)" % nText)
    print("source.timeout_cap: %s" % ("FAILED (%d)" % len(FAILURES) if FAILURES else "PASSED"))
    return 1 if FAILURES else 0


if __name__ == "__main__":
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(2)
    sys.exit(main(os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2])))
