#!/usr/bin/env python3
"""coverage_map.py - THE PER-ROW COVERAGE MAP (TEST-STREAMLINE-1 stage 2): which engine functions each row EXECUTED, read from a
JAH_COVERAGE build (CMakeLists.txt: JahshakaEngine instrumented, _Exit wrapped) whose rows each wrote their counters under
<build>/coverage/rows/<row>/ (GCOV_PREFIX, tests/CMakeLists.txt), and the warm-up's boots under <build>/coverage/boots/<boot>/.

  coverage_map.py reset <build>                              empty <build>/coverage/{rows,boots} before a coverage pass (gcov
                                                            MERGES into an existing .gcda: an earlier pass's counters mix in)
  coverage_map.py collect <build> <map.json> --records <jsonl>... [--tip <sha>] [--inventory <ctest.json>]
                                                            read every row's (and boot's) .gcda through gcov --json-format and
                                                            write the map: {schema, tip, config, rows: {row: [[file, fn], ...]},
                                                            boots: {...}, no_profile: [rows that ran and left none]}
  coverage_map.py rows <map.json> <file> <function>...      the rows whose map holds (file, function) - a function by its
                                                            UNQUALIFIED name (gate_graph.enclosing_functions' form), so an
                                                            overload or a same-named static unions: an over-reach, never a miss

THE MAP HOLDS ONLY WHOLE RUNS (the 2a audit's findings 4, 5, 7): a row is mapped only when the coverage pass's newest record
of it is PASS (any other verdict - a run that stopped early or lost a process's dump - is no_profile: the table for it), and only
when every .gcda of it read cleanly (a non-zero gcov or a .gcda with no .gcno: no_profile); its map is its own UNION its
FIXTURES_REQUIRED setups' (a function only the setup ran reaches the row that reads its product), no_profile when a setup is
not whole. The ASan copies are not instrumented: every *_asan row is no_profile (the table), said in `why`.
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
    got, why = set(), None
    # os.walk, never glob's **: it skips hidden directories, and every worktree's objects sit under .claude/ (the real
    # build's first collect read 66 .gcda files as none)
    gcdas = [os.path.join(dp, f) for dp, _, fs in os.walk(prefix) for f in fs if f.endswith(".gcda")]
    if not gcdas:
        return None, "no .gcda"
    with tempfile.TemporaryDirectory() as t:
        for i, gcda in enumerate(gcdas):
            real = gcda[len(prefix):] if gcda.startswith(prefix) else gcda
            gcno = re.sub(r"\.gcda$", ".gcno", real)
            if not os.path.isfile(gcno):
                return None, f"{os.path.basename(gcda)} has no .gcno at {gcno}"
            d = os.path.join(t, str(i))
            os.makedirs(d)
            base = os.path.basename(gcda)
            os.symlink(gcda, os.path.join(d, base))
            os.symlink(gcno, os.path.join(d, re.sub(r"\.gcda$", ".gcno", base)))
            p = subprocess.run([GCOV, "--json-format", "--stdout", base], cwd=d, capture_output=True, text=True)
            if p.returncode != 0 or not p.stdout.strip():
                return None, f"gcov failed on {base} (rc {p.returncode}: {p.stderr.strip()[-160:]})"
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
    return got, None


def newest_verdicts(paths):
    """{row: verdict} of the newest record of each row in the coverage pass's record files (gate_runlog's jsonl)."""
    newest = {}
    for p in paths:
        for line in open(p, errors="replace"):
            try:
                r = json.loads(line)
            except ValueError:
                continue
            if r.get("kind") == "hash" or not r.get("suite") or r.get("arm"):
                continue
            ts = r.get("ts") or ""
            if r["suite"] not in newest or ts >= newest[r["suite"]][0]:
                newest[r["suite"]] = (ts, r.get("verdict"))
    return {k: v for k, (_, v) in newest.items()}


def setups_of(tests):
    """{row: [its FIXTURES_REQUIRED setups' rows]} of a ctest json-v1 list."""
    lst = lambda v: [] if v is None else [v] if isinstance(v, str) else list(v)  # noqa: E731
    P = {t["name"]: {x["name"]: x["value"] for x in t.get("properties", [])} for t in tests}
    by_fx = {}
    for n, p in P.items():
        for f in lst(p.get("FIXTURES_SETUP")):
            by_fx.setdefault(f, []).append(n)
    return {n: sorted({x for f in lst(p.get("FIXTURES_REQUIRED")) for x in by_fx.get(f, [])} - {n}) for n, p in P.items()}


def reset(build):
    for kind in ("rows", "boots"):
        shutil.rmtree(os.path.join(build, "coverage", kind), ignore_errors=True)
    print(f"coverage_map: {build}/coverage emptied (rows, boots) - the pass writes fresh counters")
    return 0


def collect(build, out, tip=None, records=(), inventory=None):
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
    tree = None   # THE MAP RECORDS ITS TIP AND ITS TREE (the 2a audit's finding 8): the git tree of the tip it was collected at
    if tip:
        r = subprocess.run(["git", "-C", root, "rev-parse", f"{tip}^{{tree}}"], capture_output=True, text=True)
        tree = r.stdout.strip() or None
    doc = {"schema": SCHEMA, "tip": tip, "tree": tree, "config": cfg, "rows": {}, "boots": {}, "no_profile": [], "why": {}}
    verdicts = newest_verdicts(records)
    try:
        if inventory:
            tests = json.load(open(inventory)).get("tests", [])
        else:
            sys.path.insert(0, os.path.join(root, "scripts"))
            import gate_graph
            tests = json.loads(gate_graph.ctest_inventory(build)[0] or "{}").get("tests", [])
    except Exception:
        tests = []
    setups = setups_of(tests)
    own, boots = {}, {}
    for kind in ("rows", "boots"):
        for d in sorted(glob.glob(os.path.join(build, "coverage", kind, "*"))):
            name = os.path.basename(d)
            got, why = read_prefix(os.path.realpath(d), root, build)
            if kind == "rows" and got is not None and verdicts.get(name) != "PASS":
                got, why = None, f"its coverage-pass verdict is {verdicts.get(name) or 'unrecorded'} - a partial run"
            if kind == "rows" and got is not None and "_asan" in name:
                got, why = None, "the ASan copy is not instrumented"
            if got is None:
                if kind == "rows":
                    doc["no_profile"].append(name)
                    doc["why"][name] = why
                continue
            (own if kind == "rows" else boots)[name] = got
    # A SETUP THAT PASSED AND WROTE NO PROFILE IS WHOLE AND EMPTY (TE8, 2026-11-04): a home wipe (a fresh_home setup) or a seed
    # its own stamp served runs no instrumented code, so it leaves no .gcda - it reads as a setup with no whole profile, and
    # block E's first map lost 35 rows to their fresh_home setups. Its consumer's map is its own; a setup that did not PASS
    # still leaves it no_profile. The setup itself stays unmapped (the table).
    empty_ok = {x for x, v in verdicts.items() if v == "PASS" and x not in own
                and doc["why"].get(x, "no .gcda") == "no .gcda"}
    for name, got in own.items():
        need = setups.get(name, [])
        if any(x not in own and x not in empty_ok for x in need
               if x in verdicts or os.path.isdir(os.path.join(build, "coverage", "rows", x))):
            doc["no_profile"].append(name)
            doc["why"][name] = "a fixture setup it requires has no whole profile"
            continue
        u = set(got)
        for x in need:
            u |= own.get(x, set())
        doc["rows"][name] = sorted([list(x) for x in u])
    doc["boots"] = {k: sorted([list(x) for x in v]) for k, v in boots.items()}
    with open(out, "w") as f:
        json.dump(doc, f, sort_keys=True)
    print(f"coverage_map: {len(doc['rows'])} row(s), {len(doc['boots'])} boot(s), {len(doc['no_profile'])} without a profile "
          f"-> {out}")
    return 0


def rows_of(doc, file, functions):
    want = {(file, f) for f in functions}
    return sorted(r for r, fns in doc.get("rows", {}).items() if want & {tuple(x) for x in fns})


def main(argv):
    if len(argv) == 2 and argv[0] == "reset":
        return reset(argv[1])
    if len(argv) >= 3 and argv[0] == "collect":
        opts, recs, i = {}, [], 3
        while i < len(argv):
            if argv[i] in ("--tip", "--inventory"):
                opts[argv[i]] = argv[i + 1]; i += 2
            elif argv[i] == "--records":
                i += 1
                while i < len(argv) and not argv[i].startswith("--"):
                    recs.append(argv[i]); i += 1
            else:
                i += 1
        if not recs:
            print("coverage_map collect: --records <the coverage pass's jsonl>... is required (only PASS rows are mapped)",
                  file=sys.stderr)
            return 64
        return collect(argv[1], argv[2], opts.get("--tip"), recs, opts.get("--inventory"))
    if len(argv) >= 4 and argv[0] == "rows":
        print("\n".join(rows_of(json.load(open(argv[1])), argv[2], argv[3:])))
        return 0
    print(__doc__.strip().splitlines()[2], file=sys.stderr)
    return 64


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
