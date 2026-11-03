#!/usr/bin/env python3
"""coverage_map.py - THE PER-ROW COVERAGE MAP (TEST-STREAMLINE-1 stage 2): which engine functions each row EXECUTED, read from a
JAH_COVERAGE build (CMakeLists.txt: JahshakaEngine instrumented, _Exit wrapped) whose rows each wrote their counters under
<build>/coverage/rows/<row>/ (GCOV_PREFIX, tests/CMakeLists.txt), and the warm-up's boots under <build>/coverage/boots/<boot>/.

  coverage_map.py collect <build> <map.json> [--tip <sha>]   read every row's (and boot's) .gcda through gcov --json-format and
                                                            write the map: {schema, tip, config, rows: {row: [[file, fn], ...]},
                                                            boots: {...}, no_profile: [rows that ran and left none]}
  coverage_map.py rows <map.json> <file> <function>...      the rows whose map holds (file, function) - a function by its
                                                            UNQUALIFIED name (gate_graph.enclosing_functions' form), so an
                                                            overload or a same-named static unions: an over-reach, never a miss

THE MAP NAMES NO TREE PATH (CODING_RULES §6, #154): a file is its path under the source root (irisgl/engine/src/OgreRayQuery.cpp);
a file outside it (a system or Ogre header) is not the map's. A function counts when its execution count is > 0."""
import glob
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

SCHEMA = 1
GCOV = shutil.which("gcov-15") or shutil.which("gcov") or "gcov"


def unqualified(demangled):
    """`ns::Cls::fn(int) const` -> `fn`; `ns::Cls::~Cls()` -> `~Cls`; `operator==` kept; a lambda -> its enclosing fn."""
    if "{lambda(" in demangled:          # a lambda's body is its enclosing function's (gate_graph.enclosing_functions' view)
        return unqualified(demangled[:demangled.index("{lambda(")].rstrip(":") + "()")
    s = demangled
    depth, cut = 0, len(s)
    for i, ch in enumerate(s):          # the argument list: the first '(' at template depth 0 that is not part of operator()
        if ch == "<":
            depth += 1
        elif ch == ">":
            depth -= 1
        elif ch == "(" and depth == 0 and not s[:i].endswith("operator"):
            cut = i
            break
    head = s[:cut]
    depth, parts, cur = 0, [], ""
    for ch in head:
        if ch == "<":
            depth += 1
        elif ch == ">":
            depth -= 1
        if depth == 0 and ch == ":" and cur.endswith(":"):
            parts.append(cur[:-1])
            cur = ""
            continue
        cur += ch if depth == 0 or ch in "<>" else ""
    parts.append(cur)
    name = re.sub(r"<.*$", "", parts[-1].strip()) if not parts[-1].strip().startswith("operator") else parts[-1].strip()
    name = name.split()[-1] if name and " " in name and not name.startswith("operator") else name
    return name or demangled


def read_prefix(prefix, root, build):
    """{(file, fn)} executed in one row's prefix: every .gcda under it, its .gcno found at the build path the prefix mirrors."""
    got = set()
    # os.walk, never glob's **: it skips hidden directories, and every worktree's objects sit under .claude/ (the real
    # build's first collect read 66 .gcda files as none)
    gcdas = [os.path.join(dp, f) for dp, _, fs in os.walk(prefix) for f in fs if f.endswith(".gcda")]
    if not gcdas:
        return None
    with tempfile.TemporaryDirectory() as t:
        for i, gcda in enumerate(gcdas):
            real = gcda[len(prefix):] if gcda.startswith(prefix) else gcda
            gcno = re.sub(r"\.gcda$", ".gcno", real)
            if not os.path.isfile(gcno):
                continue
            d = os.path.join(t, str(i))
            os.makedirs(d)
            base = os.path.basename(gcda)
            os.symlink(gcda, os.path.join(d, base))
            os.symlink(gcno, os.path.join(d, re.sub(r"\.gcda$", ".gcno", base)))
            p = subprocess.run([GCOV, "--json-format", "--stdout", base], cwd=d, capture_output=True, text=True)
            for line in p.stdout.splitlines():
                try:
                    doc = json.loads(line)
                except ValueError:
                    continue
                cwd = doc.get("current_working_directory") or build
                for f in doc.get("files", []):
                    path = os.path.realpath(os.path.join(cwd, f["file"]))
                    if not path.startswith(root + os.sep):
                        continue
                    rel = os.path.relpath(path, root)
                    for fn in f.get("functions", []):
                        if fn.get("execution_count", 0) > 0:
                            got.add((rel, unqualified(fn.get("demangled_name") or fn.get("name", ""))))
    return got


def collect(build, out, tip=None):
    build = os.path.abspath(build)
    # THE SOURCE ROOT the build was configured from (its CMakeCache's CMAKE_HOME_DIRECTORY; a coverage build lives beside
    # the tree, never inside it), else the build's parent
    root = os.path.realpath(os.path.join(build, ".."))
    try:
        for line in open(os.path.join(build, "CMakeCache.txt"), errors="replace"):
            if line.startswith("CMAKE_HOME_DIRECTORY:INTERNAL="):
                root = os.path.realpath(line.split("=", 1)[1].strip())
                break
    except OSError:
        pass
    cfg = ""
    try:
        cfg = hashlib.sha256(open(os.path.join(build, "CMakeCache.txt"), "rb").read()).hexdigest()[:16]
    except OSError:
        pass
    doc = {"schema": SCHEMA, "tip": tip, "config": cfg, "rows": {}, "boots": {}, "no_profile": []}
    for kind in ("rows", "boots"):
        for d in sorted(glob.glob(os.path.join(build, "coverage", kind, "*"))):
            got = read_prefix(os.path.realpath(d), root, build)
            if got is None:
                if kind == "rows":
                    doc["no_profile"].append(os.path.basename(d))
                continue
            doc[kind][os.path.basename(d)] = sorted([list(x) for x in got])
    with open(out, "w") as f:
        json.dump(doc, f, sort_keys=True)
    print(f"coverage_map: {len(doc['rows'])} row(s), {len(doc['boots'])} boot(s), {len(doc['no_profile'])} without a profile "
          f"-> {out}")
    return 0


def rows_of(doc, file, functions):
    want = {(file, f) for f in functions}
    return sorted(r for r, fns in doc.get("rows", {}).items() if want & {tuple(x) for x in fns})


def main(argv):
    if len(argv) >= 3 and argv[0] == "collect":
        tip = argv[argv.index("--tip") + 1] if "--tip" in argv else None
        return collect(argv[1], argv[2], tip)
    if len(argv) >= 4 and argv[0] == "rows":
        print("\n".join(rows_of(json.load(open(argv[1])), argv[2], argv[3:])))
        return 0
    print(__doc__.strip().splitlines()[2], file=sys.stderr)
    return 64


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
