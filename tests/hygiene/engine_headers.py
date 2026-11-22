#!/usr/bin/env python3
"""source.engine_headers - THE ENGINE's PUBLIC HEADERS ARE ONE SUBJECT EACH, AND THEY ONLY GET SMALLER (ENGINE-HEADERS-1 stage 1,
the spec's call 13; plan U.1). The split cut Types.h (10,248 lines, 376 TUs) into a header per subject; this row holds the cut.

THE LAWS (over irisgl/engine/include/jahshaka/engine, the public headers; comments count as lines, never as includes):
  H1 Types.h IS GONE: no such header, and no file of the Studio tree or irisgl includes it.
  H2 Engine.h's OWN engine includes are exactly the BASE + Fwd.h (ENGINE_INCLUDES below): a subject header Engine.h would include
     makes every Engine.h user rebuild on that subject's edit - the cost the split removed. A type Engine.h only names goes
     through Fwd.h; one it holds by value is a BASE type.
  H3 EVERY PUBLIC HEADER HAS A LINE CEILING (engine_headers_ceilings.txt, `lines <header> <n>`), taken at the split's landing:
     over = RED; under = RED "lower the ceiling to M in the same commit" (a fall is recorded and cannot creep back); a header
     with no line = RED (a new subject is a new header, and its size is recorded when it is born).
  H4 EVERY PUBLIC HEADER HAS A FAN-OUT CEILING (`fanout <header> <n>`): the number of OTHER public headers whose include
     closure reaches it (the header graph; what an edit of it rebuilds beyond its own users). The law both ways, as H3.
     H4 GUARDS THE STRUCTURE; scripts/lead/fanout.sh MEASURES THE BUILD (the TUs an edit recompiles, from ninja's deps log):
     a per-TU count grows with every new test, so it is never held here (the lead's ruling, 2026-11-22).
  H5 THE HOUSE SPELLING OF AN irisgl INCLUDE (the lead, 2026-11-22: a finding of NOQT-CORE-1's final read): in the Studio tree
     (src, tests, lab, tools), a quoted include that resolves to a file under irisgl/ - and not to the includer's own directory
     or src/ - is spelled "irisgl/<dir>/..." (never a bare "core/...", "document/...", "import/..."). The engine's API path
     "jahshaka/engine/..." is its include root's, and is not this law's.
The self-test runs first, always: one synthetic violation per law on a toy tree must red, the law's both ways for H3 and H4.
Run: engine_headers.py <source-root> [--counts]   (--counts prints the ceilings file's lines as measured, never fails)
"""
import os
import re
import shutil
import sys
import tempfile

INC = "irisgl/engine/include/jahshaka/engine"
ENGINE_INCLUDES = ["Math.h", "Ids.h", "Config.h", "PostFx.h", "Camera.h", "Overlay.h", "Lights.h", "Particles.h", "GiModes.h", "Fwd.h"]
STUDIO = ("src", "tests", "lab", "tools")
CXX = (".h", ".hpp", ".cpp", ".mm", ".cc")
INCLUDE = re.compile(r'^[ \t]*#[ \t]*include[ \t]*"([^"]+)"', re.M)   # ([ \t], never \s: a \s* at ^ eats the newline before)
fails = 0


def check(ok, msg):
    global fails
    print(("  ok:   " if ok else "  FAIL: ") + msg, flush=True)
    if not ok:
        fails += 1


def headers(root):
    d = os.path.join(root, INC)
    return sorted(f for f in os.listdir(d) if f.endswith(".h")) if os.path.isdir(d) else []


def engine_includes(root, h):
    t = open(os.path.join(root, INC, h), errors="replace").read()
    return [m.group(1)[len("jahshaka/engine/"):] if m.group(1).startswith("jahshaka/engine/") else m.group(1)
            for m in INCLUDE.finditer(t) if m.group(1).startswith("jahshaka/engine/") or "/" not in m.group(1)]


def measure(root):
    """{header: (lines, fanout)}: its line count; how many OTHER public headers' include closure reaches it."""
    hs = headers(root)
    edges = {h: [x for x in engine_includes(root, h) if x in hs] for h in hs}
    reach = {}
    for h in hs:
        seen, stack = set(), [h]
        while stack:
            for x in edges.get(stack.pop(), ()):
                if x not in seen:
                    seen.add(x)
                    stack.append(x)
        reach[h] = seen
    return {h: (open(os.path.join(root, INC, h), errors="replace").read().count("\n"),
                sum(1 for o in hs if o != h and h in reach[o])) for h in hs}


def ceilings(root):
    out = {}
    p = os.path.join(root, "tests", "hygiene", "engine_headers_ceilings.txt")
    for line in open(p).read().splitlines() if os.path.isfile(p) else []:
        w = line.split("#", 1)[0].split()
        if len(w) == 3 and w[0] in ("lines", "fanout"):
            out[(w[0], w[1])] = int(w[2])
    return out


def studio_files(root):
    for top in STUDIO:
        for dp, dn, fn in os.walk(os.path.join(root, top)):
            dn[:] = [d for d in dn if d not in ("fixtures", "thirdparty")]
            for f in fn:
                if f.endswith(CXX):
                    yield os.path.join(dp, f)


def bare_irisgl(root):
    """[file:line spelled] - H5's violations."""
    bad = []
    for p in studio_files(root):
        t = open(p, errors="replace").read()
        for m in INCLUDE.finditer(t):
            inc = m.group(1)
            if "/" not in inc or inc.startswith(("irisgl/", "jahshaka/engine/")):
                continue
            mine = os.path.isfile(os.path.join(os.path.dirname(p), inc)) or os.path.isfile(os.path.join(root, "src", inc)) \
                or os.path.isfile(os.path.join(root, inc))
            if not mine and os.path.isfile(os.path.join(root, "irisgl", inc)):
                bad.append("%s:%d \"%s\" -> \"irisgl/%s\"" % (os.path.relpath(p, root), t.count("\n", 0, m.start()) + 1, inc, inc))
    return bad


def judge(root, say=True):
    """[the laws' faults] of the tree at root."""
    out = []
    if os.path.isfile(os.path.join(root, INC, "Types.h")):
        out.append("H1 %s/Types.h exists" % INC)
    users = []
    for p in list(studio_files(root)) + [os.path.join(dp, f) for top in ("irisgl/engine", "irisgl/document", "irisgl/mirror",
                                                                        "irisgl/import", "irisgl/core")
                                         for dp, dn, fn in os.walk(os.path.join(root, top)) if "/thirdparty" not in dp
                                         for f in fn if f.endswith(CXX)]:
        if re.search(r'^\s*#\s*include\s*"(jahshaka/engine/)?Types\.h"', open(p, errors="replace").read(), re.M):
            users.append(os.path.relpath(p, root))
    out += ["H1 %s includes Types.h" % u for u in users]
    if os.path.isfile(os.path.join(root, INC, "Engine.h")):
        got = engine_includes(root, "Engine.h")
        if sorted(got) != sorted(ENGINE_INCLUDES):
            out.append("H2 Engine.h includes %s; the law: %s (extra %s, missing %s)" % (
                got, ENGINE_INCLUDES, sorted(set(got) - set(ENGINE_INCLUDES)), sorted(set(ENGINE_INCLUDES) - set(got))))
    m, c = measure(root), ceilings(root)
    for h, (n, f) in sorted(m.items()):
        for kind, v, law in (("lines", n, "H3"), ("fanout", f, "H4")):
            cap = c.get((kind, h))
            if cap is None:
                out.append("%s %s has no `%s` ceiling: add `%s %s %d` to engine_headers_ceilings.txt" % (law, h, kind, kind, h, v))
            elif v > cap:
                out.append("%s %s %s %d, its ceiling %d: over" % (law, h, kind, v, cap))
            elif v < cap:
                out.append("%s %s %s %d under its ceiling %d: lower the ceiling to %d in the same commit" % (law, h, kind, v, cap, v))
    for (kind, h), cap in sorted(c.items()):
        if h not in m:
            out.append("%s a ceiling for %s, which is no public header: delete its line" % ("H3" if kind == "lines" else "H4", h))
    out += ["H5 " + b for b in bare_irisgl(root)]
    return out


def selftest():
    print("the laws catch (a toy tree):")
    t = tempfile.mkdtemp(prefix="engine-headers-toy-")
    try:
        d = os.path.join(t, INC)
        os.makedirs(d)
        os.makedirs(os.path.join(t, "tests", "hygiene"))
        os.makedirs(os.path.join(t, "irisgl", "core", "math"))
        os.makedirs(os.path.join(t, "src", "x"))
        for h in ENGINE_INCLUDES:
            open(os.path.join(d, h), "w").write("#pragma once\n")
        open(os.path.join(d, "Math.h"), "w").write('#pragma once\n#include "jahshaka/engine/Fwd.h"\n')
        open(os.path.join(d, "Engine.h"), "w").write("".join('#include "jahshaka/engine/%s"\n' % h for h in ENGINE_INCLUDES))
        open(os.path.join(t, "irisgl", "core", "math", "vec.h"), "w").write("#pragma once\n")
        open(os.path.join(t, "src", "x", "a.cpp"), "w").write('#include "irisgl/core/math/vec.h"\n')
        m = measure(t)
        open(os.path.join(t, "tests", "hygiene", "engine_headers_ceilings.txt"), "w").write(
            "".join("lines %s %d\nfanout %s %d\n" % (h, n, h, f) for h, (n, f) in sorted(m.items())))
        base = judge(t)
        check(not base and m["Fwd.h"][1] == 2 and m["Engine.h"][1] == 0,
              "the clean toy passes; Fwd.h's fan-out counts Math.h and Engine.h (%s %s)" % (base, m.get("Fwd.h")))

        def arm(name, do, undo, law):
            do()
            got = judge(t)
            undo()
            check(any(g.startswith(law) for g in got), "%s red: %s (%s)" % (law, name, got[:2]))

        p_eng = os.path.join(d, "Engine.h")
        eng = open(p_eng).read()
        p_t = os.path.join(d, "Types.h")
        arm("a Types.h restored", lambda: open(p_t, "w").write("#pragma once\n"), lambda: os.remove(p_t), "H1")
        p_u = os.path.join(t, "src", "x", "u.cpp")
        arm("a file includes Types.h", lambda: open(p_u, "w").write('#include "jahshaka/engine/Types.h"\n'),
            lambda: os.remove(p_u), "H1")
        open(os.path.join(d, "Stats.h"), "w").write("#pragma once\n")
        open(os.path.join(t, "tests", "hygiene", "engine_headers_ceilings.txt"), "a").write("lines Stats.h 1\nfanout Stats.h 0\n")
        arm("Engine.h includes a subject header", lambda: open(p_eng, "a").write('#include "jahshaka/engine/Stats.h"\n'),
            lambda: open(p_eng, "w").write(eng), "H2")
        p_s = os.path.join(d, "Stats.h")
        arm("a header over its line ceiling", lambda: open(p_s, "a").write("struct A {};\n"),
            lambda: open(p_s, "w").write("#pragma once\n"), "H3")
        arm("a header under its line ceiling", lambda: open(p_s, "w").write(""), lambda: open(p_s, "w").write("#pragma once\n"), "H3")
        p_n = os.path.join(d, "New.h")
        arm("a new header without its ceiling lines", lambda: open(p_n, "w").write("#pragma once\n"), lambda: os.remove(p_n), "H3")
        p_l = os.path.join(d, "Lights.h")
        arm("a header's fan-out over its ceiling (Lights.h now includes Stats.h)",
            lambda: open(p_l, "w").write('#pragma once\n#include "jahshaka/engine/Stats.h"\n'),
            lambda: open(p_l, "w").write("#pragma once\n"), "H4")
        p_m = os.path.join(d, "Math.h")
        arm("a header's fan-out under its ceiling (Math.h drops Fwd.h)", lambda: open(p_m, "w").write("#pragma once\n\n"),
            lambda: open(p_m, "w").write('#pragma once\n#include "jahshaka/engine/Fwd.h"\n'), "H4")
        p_a = os.path.join(t, "src", "x", "a.cpp")
        arm("a bare irisgl include (\"core/math/vec.h\")", lambda: open(p_a, "w").write('#include "core/math/vec.h"\n'),
            lambda: open(p_a, "w").write('#include "irisgl/core/math/vec.h"\n'), "H5")
        os.makedirs(os.path.join(t, "src", "core", "math"))
        open(os.path.join(t, "src", "core", "math", "vec.h"), "w").write("#pragma once\n")
        open(p_a, "w").write('#include "core/math/vec.h"\n')
        check(not [g for g in judge(t) if g.startswith("H5")], "H5 quiet when the bare path is the Studio tree's own (src/core/...)")
    finally:
        shutil.rmtree(t, ignore_errors=True)


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(2)
    root = os.path.realpath(sys.argv[1])
    if "--counts" in sys.argv:
        for h, (n, f) in sorted(measure(root).items()):
            print("lines %s %d\nfanout %s %d" % (h, n, h, f))
        sys.exit(0)
    selftest()
    print("the tree (%s):" % root)
    bad = judge(root)
    check(bool(headers(root)), "the public headers are read (%d)" % len(headers(root)))
    check(not bad, "H1-H5 hold%s" % "".join("\n         " + b for b in bad))
    print("source.engine_headers: %s" % ("PASSED" if not fails else "FAILED (%d)" % fails))
    sys.exit(1 if fails else 0)
