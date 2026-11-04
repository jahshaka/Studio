#!/usr/bin/env python3
"""tooling.coverage_map - THE PER-ROW MAP IS EACH ROW's OWN, AND A PROCESS ENDING IN _Exit STILL WRITES IT (TEST-STREAMLINE-1
stage 2, the reader's V1-V3; no card): a toy tree (<t>/src: a static library of three functions, one a lambda's host; <t>/build)
compiled --coverage, linked with the tree's own cmake/coverage/gcov_exit_wrap.c and -Wl,--wrap=_Exit as the JAH_COVERAGE build
links; two toy "rows" end as the engine fixtures do (on_exit -> std::_Exit), each with its own GCOV_PREFIX under
<build>/coverage/rows/<row>; scripts/coverage_map.py collect reads them:
  (1) without the wrap a row leaves NO profile (the reason the wrap exists) - the map names it under no_profile;
  (2) with it, row A's map holds exactly the functions A ran (work, and spin for its lambda), row B's exactly B's (other) - a
      row's map is its own;
  (3) the map's files are paths under the source root (src/lib.cpp), never the tree's absolute spelling (CODING_RULES §6);
  (4) `rows <map> src/lib.cpp other` answers B alone;
  (10) the under-reach audit's judgement (coverage_map.under_reach, pure: the map is the audit's, never the selector's).
usage: coverage_map_toy.py <source dir>"""
import json
import os
import shutil
import subprocess
import sys
import tempfile

src = sys.argv[1]
CM = os.path.join(src, "scripts", "coverage_map.py")
WRAP = os.path.join(src, "cmake", "coverage", "gcov_exit_wrap.c")
fails = []


def law(ok, text):
    print(("  ok   " if ok else "  FAIL ") + text)
    if not ok:
        fails.append(text)


if not shutil.which("g++") or not (shutil.which("gcov-15") or shutil.which("gcov")):
    print("tooling.coverage_map: FAIL - no g++ / gcov on this box")
    sys.exit(1)
with tempfile.TemporaryDirectory() as t:
    s, b = os.path.join(t, "src"), os.path.join(t, ".hidden", "build")   # (a hidden dir on the path, as every worktree's .claude/)
    os.makedirs(s)
    os.makedirs(b)
    s = os.path.join(t, ".hidden", "src"); os.makedirs(s)   # the source root is the build's parent (coverage_map's rule)
    open(os.path.join(s, "lib.cpp"), "w").write(
        "#include <thread>\n"
        "int work(int n) { int k = 0; for (int i = 0; i < n; ++i) k += i; return k; }\n"
        "int other() { return 7; }\n"
        "void spin() { std::thread([] { volatile int x = 0; x = x + 1; }).join(); }\n")
    open(os.path.join(s, "main.cpp"), "w").write(
        "#include <cstdlib>\n#include <cstring>\nint work(int); int other(); void spin();\n"
        "static void quit(int, void *) { std::_Exit(0); }\n"
        "int main(int argc, char **argv) { on_exit(quit, nullptr);\n"
        "  if (argc > 1 && !std::strcmp(argv[1], \"B\")) return other() == 7 ? 0 : 1;\n"
        "  spin(); return work(3) == 3 ? 0 : 1; }\n")

    def sh(*a):
        return subprocess.run(list(a), cwd=b, capture_output=True, text=True)
    r = [sh("g++", "-O0", "--coverage", "-fprofile-update=atomic", "-c", os.path.join(s, "lib.cpp"), "-o", "lib.o"),
         sh("ar", "rcs", "liblib.a", "lib.o"),
         sh("g++", "-O0", "-c", os.path.join(s, "main.cpp"), "-o", "main.o"),
         sh("gcc", "-c", WRAP, "-o", "wrap.o"),
         sh("g++", "--coverage", "main.o", "liblib.a", "-o", "nowrap", "-pthread"),
         sh("g++", "--coverage", "main.o", "liblib.a", "wrap.o", "-Wl,--wrap=_Exit", "-o", "row", "-pthread")]
    law(all(x.returncode == 0 for x in r), "the toy builds with the tree's wrapper (%s)" % [x.stderr[-200:] for x in r if x.returncode])

    def run(exe, row, *args, kind="rows"):
        env = dict(os.environ, GCOV_PREFIX=os.path.join(b, "coverage", kind, row), GCOV_PREFIX_STRIP="0")
        os.makedirs(env["GCOV_PREFIX"], exist_ok=True)
        return subprocess.run([os.path.join(b, exe)] + list(args), cwd=b, env=env).returncode
    rc = [run("nowrap", "row.nowrap"), run("row", "row.A"), run("row", "row.B", "B"), run("row", "row.F"), run("row", "row.C"),
          run("row", "row.S"), run("row", "app-off", kind="boots"), run("row", "row.D"), run("row", "row.E")]
    law(rc == [0] * 9, "nine toy runs (seven rows, a setup, a boot) ran (%s)" % rc)
    # row.C's profile torn (a gcov error); the pass's records (row.F FAILED); row.B requires row.S's fixture
    for dp, _, fs in os.walk(os.path.join(b, "coverage", "rows", "row.C")):
        for f in fs:
            if f.endswith(".gcda"):
                open(os.path.join(dp, f), "wb").write(b"torn")
    recs = os.path.join(t, "pass.jsonl")
    with open(recs, "w") as fh:
        for r, v in (("row.nowrap", "PASS"), ("row.A", "PASS"), ("row.B", "PASS"), ("row.F", "FAIL"), ("row.C", "PASS"), ("row.S", "PASS"),
                     ("row.D", "PASS"), ("row.E", "PASS"), ("row.W", "PASS"), ("row.Wf", "FAIL")):
            fh.write(json.dumps({"suite": r, "verdict": v, "ts": "2026-11-03T00:00:00+01:00"}) + "\n")
    inv = os.path.join(t, "inv.json")
    json.dump({"tests": [{"name": "row.S", "properties": [{"name": "FIXTURES_SETUP", "value": ["fx"]}]},
                         {"name": "row.B", "properties": [{"name": "FIXTURES_REQUIRED", "value": ["fx"]}]},
                         # row.W: a home WIPE (a fresh_home setup: runs no instrumented code, writes no .gcda) that PASSED;
                         # row.Wf: one that FAILED - each the setup of a row that ran the engine
                         {"name": "row.W", "properties": [{"name": "FIXTURES_SETUP", "value": ["fxw"]}]},
                         {"name": "row.D", "properties": [{"name": "FIXTURES_REQUIRED", "value": ["fxw"]}]},
                         {"name": "row.Wf", "properties": [{"name": "FIXTURES_SETUP", "value": ["fxf"]}]},
                         {"name": "row.E", "properties": [{"name": "FIXTURES_REQUIRED", "value": ["fxf"]}]}]}, open(inv, "w"))
    m = os.path.join(t, "map.json")
    p = subprocess.run([sys.executable, "-I", CM, "collect", b, m, "--tip", "toy", "--records", recs, "--inventory", inv],
                       capture_output=True, text=True)
    doc = json.load(open(m)) if p.returncode == 0 and os.path.isfile(m) else {}
    rows = doc.get("rows", {})
    law("row.nowrap" in doc.get("no_profile", []) and "row.nowrap" not in rows,
        "(1) a row ending in _Exit WITHOUT the wrap leaves no profile - named under no_profile (%s)" % doc.get("no_profile"))
    a = {tuple(x) for x in rows.get("row.A", [])}
    bb = {tuple(x) for x in rows.get("row.B", [])}
    law(a == {("src/lib.cpp", "work"), ("src/lib.cpp", "spin")} and bb == {("src/lib.cpp", "other"), ("src/lib.cpp", "work"), ("src/lib.cpp", "spin")},
        "(2) each row's map holds the functions it ran - A %s - and a row its required setup's too - B %s (other + row.S's)"
        % (sorted(a), sorted(bb)))
    np_ = set(doc.get("no_profile", []))
    law({"row.F", "row.C"} <= np_ and "row.F" not in rows and "row.C" not in rows,
        "(5) a FAILED row (its pass record) and a row whose .gcda gcov cannot read are no_profile, never 'ran nothing' (%s)"
        % {k: doc.get("why", {}).get(k) for k in ("row.F", "row.C")})
    law({tuple(x) for x in rows.get("row.D", [])} == {("src/lib.cpp", "work"), ("src/lib.cpp", "spin")} and "row.E" in np_
        and "row.W" not in rows,
        "(9) a setup that PASSED and wrote no profile (a home wipe: it ran no instrumented code) is whole and EMPTY - its "
        "consumer row.D mapped %s; a FAILED one leaves its consumer row.E no_profile (%s); the wipe itself is not mapped "
        "(TE8, 2026-11-04: 35 rows of block E's map lost to their fresh_home setups)"
        % (sorted({tuple(x) for x in rows.get("row.D", [])}), doc.get("why", {}).get("row.E")))
    law({tuple(x) for x in doc.get("boots", {}).get("app-off", [])} == {("src/lib.cpp", "work"), ("src/lib.cpp", "spin")},
        "(6) a warm-up boot's profile (coverage/boots/<boot>) collected as its boots entry, by the real collector")
    law(json.dumps(doc).find(t) < 0, "(3) the map names no tree path (files under the source root)")
    q = subprocess.run([sys.executable, "-I", CM, "rows", m, "src/lib.cpp", "other"], capture_output=True, text=True)
    law(q.stdout.split() == ["row.B"], "(4) `rows <map> src/lib.cpp other` answers row.B alone (%s)" % q.stdout.split())
    nr = subprocess.run([sys.executable, "-I", CM, "collect", b, m], capture_output=True, text=True)
    law(nr.returncode == 64, "(7) collect without the pass's records is refused (only PASS rows are mapped)")
    subprocess.run([sys.executable, "-I", CM, "reset", b], capture_output=True)
    law(not os.path.exists(os.path.join(b, "coverage", "rows")), "(8) reset empties the prefixes before a pass (gcov merges)")
# (10) THE UNDER-REACH AUDIT's JUDGEMENT (the landing audit's F3: the map is the audit's, never the selector's) - pure, on a
#      toy map: a row that ran a changed function and the selection lacks is MISSED (with the function); a selected one is
#      not; a warm-up boot that ran it names its class's rows; a namespace-scope line, a file the map never saw and a
#      function no row ran are UNANSWERED (the table's alone); a row with no profile is UNMAPPED; the whole tier misses nothing
import importlib.util  # noqa: E402
_sp = importlib.util.spec_from_file_location("coverage_map_under_test", CM)
_cm = importlib.util.module_from_spec(_sp)
_sp.loader.exec_module(_cm)
toy = {"rows": {"e.ran": [["e/A.cpp", "f"]], "e.sel": [["e/A.cpp", "f"]], "e.other": [["e/A.cpp", "g"]], "a.app": []},
       "boots": {"app-off": [["e/A.cpp", "h"]]}}
cls = {"e.ran": "engine", "e.sel": "engine", "e.other": "engine", "a.app": "app", "a.noprof": "app"}
ur = getattr(_cm, "under_reach", None)
got = ur(toy, {"e/A.cpp": {"f", "h", "nobody", None}, "e/B.cpp": {"k"}}, {"e.sel"}, cls) if ur else {}
law(ur is not None and got["missed"] == {"e.ran": [("e/A.cpp", "f")], "a.app": [("e/A.cpp", "h")],
                                         "a.noprof": [("e/A.cpp", "h")]}
    and ("e/A.cpp", "a namespace-scope line") in got["unanswered"]
    and ("e/A.cpp", "nobody(): no row or boot of the map ran it") in got["unanswered"]
    and ("e/B.cpp", "the map holds nothing of this file") in got["unanswered"] and got["unmapped"] == ["a.noprof"]
    and not ur(toy, {"e/A.cpp": {"f"}}, None, cls)["missed"],
    "(10) the under-reach audit: e.ran missed through f(), e.sel selected, the app boot's h() names the app rows, the "
    "namespace line / unseen file / unrun function unanswered, a.noprof unmapped, the whole tier misses nothing (%s)"
    % ({k: got.get(k) for k in ("missed", "unmapped")} if got else "no under_reach"))
print("tooling.coverage_map: %s" % ("FAIL" if fails else "PASS"))
sys.exit(1 if fails else 0)
