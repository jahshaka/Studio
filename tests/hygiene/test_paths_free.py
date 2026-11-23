#!/usr/bin/env python3
"""source.test_paths_free - NO TREE PATH ON A TEST's COMPILE LINE (the build-speed audit D1, 2026-11-23).

ccache's base_dir rewrites path ARGUMENTS (-I, the source, -o) but never the value of a -D: a -D naming this tree made each
TU that carried it miss the cache in every other tree - 219 of 1,879 compile lines, 172 misses in every fresh tree (55 % of
the box's misses by count; spikes/build-speed-audit/AUDIT.md B1, d1/). tests/CMakeLists.txt's jah_scope_path_defines now
puts such a macro on its sources as jahtest::path("NAME") and the values into ONE generated TU (jah_test_paths).

THE LAW, read from the build's own compile_commands.json: no compile line of a TEST target (its object under <build>/tests/)
carries the source tree's or the build tree's path in a -D value. THE CONTROLS (a pass over nothing is no pass): the test
lines read > 0, and the lines that carry the path-free jahtest::path form > 0 (the mechanism in use).
usage: test_paths_free.py <source tree> <build dir>
"""
import json
import os
import shlex
import sys


def main():
    if len(sys.argv) != 3:
        print("usage: test_paths_free.py <source tree> <build dir>")
        return 2
    src, build = sys.argv[1].rstrip("/"), sys.argv[2].rstrip("/")
    # (both spellings of each: the one given and the real one - a tree reached through a link is the same tree)
    spells = {src, build, os.path.realpath(src), os.path.realpath(build)}
    try:
        doc = json.load(open(os.path.join(build, "compile_commands.json")))
    except (OSError, ValueError) as e:
        print(f"FAIL no compile_commands.json in {build} ({e}) - configure the tree")
        print("source.test_paths_free: FAILED (1)")
        return 1
    tests = tuple(os.path.join(b, "tests") + os.sep for b in (build, os.path.realpath(build)))
    seen, lookup, bad = 0, 0, []
    for e in doc:
        out = e.get("output") or ""
        out = os.path.normpath(os.path.join(e.get("directory") or build, out)) if out else ""
        if not out.startswith(tests):
            continue
        seen += 1
        args = e.get("arguments") or shlex.split(e.get("command") or "")
        for a in args:
            if not a.startswith("-D"):
                continue
            if "jahtest::path(" in a:
                lookup += 1
            if any(s in a for s in spells):
                bad.append((os.path.relpath(os.path.join(e.get("directory") or "", e.get("file") or ""), src), a[:120]))
    fails = []
    if seen == 0:
        fails.append("no test target's compile line read (the control): compile_commands.json names no object under tests/")
    if lookup == 0:
        fails.append("no test compile line carries the path-free jahtest::path form (the control): the mechanism is not in use")
    if bad:
        fails.append(f"{len(bad)} -D value(s) on test compile lines carry the tree's path - each a ccache miss in every other "
                     "tree: give the macro to jah_scope_path_defines (tests/CMakeLists.txt) instead")
        for f, a in bad[:12]:
            print(f"  {f}: {a}")
    print(f"the test compile lines: {seen} read, {lookup} path macros read at run time (jahtest::path), {len(bad)} with the "
          f"tree's path in a -D", flush=True)
    for f in fails:
        print("FAIL " + f)
    print("source.test_paths_free: FAILED (%d)" % len(fails) if fails else "source.test_paths_free: PASSED")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
