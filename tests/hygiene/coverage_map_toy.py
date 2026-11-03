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
  (4) `rows <map> src/lib.cpp other` answers B alone.
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
    s, b = os.path.join(t, "src"), os.path.join(t, "build")
    os.makedirs(s)
    os.makedirs(b)
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

    def run(exe, row, *args):
        env = dict(os.environ, GCOV_PREFIX=os.path.join(b, "coverage", "rows", row), GCOV_PREFIX_STRIP="0")
        os.makedirs(env["GCOV_PREFIX"], exist_ok=True)
        return subprocess.run([os.path.join(b, exe)] + list(args), cwd=b, env=env).returncode
    rc = [run("nowrap", "row.nowrap"), run("row", "row.A"), run("row", "row.B", "B")]
    law(rc == [0, 0, 0], "three toy rows ran green (%s)" % rc)
    m = os.path.join(t, "map.json")
    p = subprocess.run([sys.executable, "-I", CM, "collect", b, m, "--tip", "toy"], capture_output=True, text=True)
    doc = json.load(open(m)) if p.returncode == 0 and os.path.isfile(m) else {}
    rows = doc.get("rows", {})
    law("row.nowrap" in doc.get("no_profile", []) and "row.nowrap" not in rows,
        "(1) a row ending in _Exit WITHOUT the wrap leaves no profile - named under no_profile (%s)" % doc.get("no_profile"))
    a = {tuple(x) for x in rows.get("row.A", [])}
    bb = {tuple(x) for x in rows.get("row.B", [])}
    law(a == {("src/lib.cpp", "work"), ("src/lib.cpp", "spin")} and bb == {("src/lib.cpp", "other")},
        "(2) each row's map holds the functions it ran and only those - A %s, B %s" % (sorted(a), sorted(bb)))
    law(json.dumps(doc).find(t) < 0, "(3) the map names no tree path (files under the source root)")
    q = subprocess.run([sys.executable, "-I", CM, "rows", m, "src/lib.cpp", "other"], capture_output=True, text=True)
    law(q.stdout.split() == ["row.B"], "(4) `rows <map> src/lib.cpp other` answers row.B alone (%s)" % q.stdout.split())
print("tooling.coverage_map: %s" % ("FAIL" if fails else "PASS"))
sys.exit(1 if fails else 0)
