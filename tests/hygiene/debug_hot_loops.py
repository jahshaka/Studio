#!/usr/bin/env python3
"""source.debug_hot_loops - THE VENDORED HOT LOOPS ARE OPTIMISED IN DEBUG (TEST-STREAMLINE-1 5c; irisgl/CMakeLists.txt).

Debug is the daily driver, so the vendored libraries whose loops ARE the work take -O2 in a Debug build, set from our
CMake (never the vendored tree's): Bullet's four targets (the physics step), meshoptimizer (the bake's simplifier) and the
zip library (a share file's deflate). A target that silently loses its option costs seconds in every Debug export, bake
and physics row and no test would say why. This reads the build's compile_commands.json: in a Debug build every compile
command of each family's sources carries -O2 (and each family has sources); in another configuration it says so and
passes. Fed a violation of each law first.

usage: debug_hot_loops.py <source-dir> <build-dir>"""
import json
import os
import sys

# the families: a path prefix under the source tree -> the rule's name
FAMILIES = {
    "irisgl/thirdparty/zip/": "zip",
    "irisgl/thirdparty/meshoptimizer/src/": "meshoptimizer",
    "irisgl/thirdparty/bullet3/src/BulletDynamics/": "BulletDynamics",
    "irisgl/thirdparty/bullet3/src/BulletCollision/": "BulletCollision",
    "irisgl/thirdparty/bullet3/src/LinearMath/": "LinearMath",
    "irisgl/thirdparty/bullet3/src/Bullet3Common/": "Bullet3Common",
}
FAILS = []


def check(ok, what):
    print(("ok:   " if ok else "FAIL: ") + what)
    if not ok:
        FAILS.append(what)


def bad(entries, source):
    """{family: [files without -O2]} and {family: count} of a compile_commands list."""
    missing, seen = {}, {f: 0 for f in FAMILIES.values()}
    root = os.path.realpath(source) + os.sep
    for e in entries:
        f = os.path.realpath(os.path.join(e.get("directory", ""), e["file"]))
        rel = f[len(root):] if f.startswith(root) else None
        if rel is None:
            continue
        fam = next((n for p, n in FAMILIES.items() if rel.startswith(p)), None)
        if fam is None or not rel.endswith((".c", ".cc", ".cpp", ".cxx")):
            continue
        seen[fam] += 1
        args = e.get("arguments") or e.get("command", "").split()
        opt = [a for a in args if a.startswith("-O")]
        if not opt or opt[-1] != "-O2":       # the LAST -O wins
            missing.setdefault(fam, []).append(rel)
    return missing, seen


def self_test():
    src = "/s"
    cc = lambda f, flags: {"directory": "/b", "file": src + "/" + f, "command": "cc " + flags + " -c " + src + "/" + f}
    good = [cc("irisgl/thirdparty/zip/src/zip.c", "-g -O2"), cc("irisgl/thirdparty/meshoptimizer/src/a.cpp", "-O2 -g")]
    good += [cc("irisgl/thirdparty/bullet3/src/%s/x.cpp" % d, "-O2") for d in
             ("BulletDynamics", "BulletCollision", "LinearMath", "Bullet3Common")]
    m, s = bad(good, src)
    check(not m and all(s.values()), "self-test: every family at -O2 holds (%s)" % m)
    m, _ = bad(good + [cc("irisgl/thirdparty/zip/src/zip2.c", "-g")], src)
    check(m == {"zip": ["irisgl/thirdparty/zip/src/zip2.c"]}, "self-test: a zip source with no -O2 is named (%s)" % m)
    m, _ = bad(good[1:] + [cc("irisgl/thirdparty/zip/src/zip.c", "-O2 -g -O0")], src)
    check(m == {"zip": ["irisgl/thirdparty/zip/src/zip.c"]}, "self-test: a later -O0 overrides the -O2 (%s)" % m)
    _, s = bad(good[1:], src)
    check(s["zip"] == 0, "self-test: a family with no source is counted 0 (the main check fails it)")


def main(source, build):
    self_test()
    cache = open(os.path.join(build, "CMakeCache.txt"), errors="replace").read()
    btype = next((l.split("=", 1)[1].strip() for l in cache.splitlines() if l.startswith("CMAKE_BUILD_TYPE:")), "")
    cc_path = os.path.join(build, "compile_commands.json")
    if btype != "Debug":
        print("source.debug_hot_loops: a %s build - the rule is Debug's; nothing to read" % (btype or "untyped"))
    elif not os.path.exists(cc_path):
        check(False, "the build exports compile_commands.json (configure with -DCMAKE_EXPORT_COMPILE_COMMANDS=ON)")
    else:
        missing, seen = bad(json.load(open(cc_path)), source)
        check(all(seen.values()), "every family has sources in this build (%s)" % seen)
        check(not missing, "every source of zip, meshoptimizer and Bullet's four targets compiles at -O2 in Debug "
              "(%d without: %s)" % (sum(map(len, missing.values())),
                                    {f: v[:3] for f, v in missing.items()}))
    print("source.debug_hot_loops: " + ("PASSED" if not FAILS else "FAILED (%d)" % len(FAILS)))
    return 1 if FAILS else 0


if __name__ == "__main__":
    if len(sys.argv) != 3:
        print(__doc__)
        sys.exit(2)
    sys.exit(main(os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2])))
