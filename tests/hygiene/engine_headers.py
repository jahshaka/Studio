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
  H6 THE BUILD's WIDTH PER FILE (the lead, 2026-11-23: the split's final read, its defect 1 - 222 includes of FrameInput.h
     nothing needed made ~54 more source files reach six heavy headers, and H4 could not see it): for each header of REACH,
     the number of SOURCE FILES (.cpp/.mm/.cc of the Studio tree and irisgl, no thirdparty) whose include closure reaches it
     has a ceiling (`reach <header> <n>`), the law both ways, as H3 (a new source file that reaches one of them raises its
     line in the same commit: every widening is a reviewed line). THE CONTROL: each is reached by at least one file.
     THE WALK is static (no build): every `#include` line, quoted from the includer's directory then the include roots
     (ROOTS: the build's own -I directories of our trees), angle-bracket from the roots; a quoted include they do not resolve
     is followed when exactly one file of ours ends with its path (a target's own -I, e.g. tests/atom from tests/compute).
     ITS BLIND SPOTS, each counted on every run: (a) `#if` is not evaluated - every include is taken, so the walk can only
     over-count (an Apple-only include counts on Linux), never hide a rise; (b) the build's GENERATED files are not walked
     (moc's mocs_compilation.cpp, the SPIR-V arrays, the build-id and provenance headers: none includes an engine header);
     (c) a quoted include that two files of ours could answer, and a computed `#include MACRO`, could hide a path: each is
     RED until spelled from an include root. Measured against ninja's deps log at its landing: the same files but the 12
     sources no target compiles and one target never built (walk +13) and mocs_compilation.cpp (walk -1).
  WHO GUARDS WHAT: H4 the STRUCTURE (header to header); H6 the BUILD's WIDTH per source file (static, any tree, no build);
     scripts/lead/fanout.sh MEASURES the build (the objects an edit recompiles, twins and per-target copies included, from
     ninja's deps log, with their CPU-s).
  H5 THE HOUSE SPELLING OF AN irisgl INCLUDE (the lead, 2026-11-22: a finding of NOQT-CORE-1's final read): in the Studio tree
     (src, tests, lab, tools), a quoted include that resolves to a file under irisgl/ - and not to the includer's own directory
     or src/ - is spelled "irisgl/<dir>/..." (never a bare "core/...", "document/...", "import/..."). The engine's API path
     "jahshaka/engine/..." is its include root's, and is not this law's.
The self-test runs first, always: one synthetic violation per law on a toy tree must red, the law's both ways for H3, H4 and H6.
Run: engine_headers.py <source-root> [--counts | --write | --reach <header>]   (--counts prints the ceilings file's lines as
     measured; --write rewrites engine_headers_ceilings.txt to them, its comment lines kept, and prints every line it moved;
     --reach the source files that reach a header of REACH; none of them fails)
THE CEILINGS ARE GENERATED, NEVER HAND-MERGED (the lead, 2026-11-23: the split's re-read, finding (d)). After a rebase, a merge
or a replay over the split - and whenever engine_headers_ceilings.txt conflicts - take either side of the file, run
`engine_headers.py <tree> --write`, and commit THAT diff as its own commit, naming for each moved line the landing or the file
that moved it (a header's edit for `lines`, a new or deleted source for `reach`). A ceilings conflict is resolved by
regenerating, the diff shown; never a number merged by hand.
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
REACH = ("GiParams.h", "FrameInput.h", "Monitor.h", "Sky.h", "Fog.h", "Planar.h")   # H6: the heavy headers the split widened
TUS = (".cpp", ".mm", ".cc")
WALKED = TUS + (".h", ".hpp", ".inl", ".inc")
ROOTS = (".", "src", "irisgl", "irisgl/engine/include", "irisgl/engine/src", "irisgl/engine", "irisgl/core", "src/lab", "tests",
         "tests/support", "extras/downloader")
ANY_INCLUDE = re.compile(r'^[ \t]*#[ \t]*include[ \t]*(?:"([^"]+)"|<([^>]+)>|([A-Za-z_]\w*))', re.M)
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


def reach(root):
    """({header of REACH: the source files whose include closure reaches it}, {what the walk followed and its blind spots})."""
    files = {}
    for top in STUDIO + ("extras", "irisgl"):
        for dp, dn, fn in os.walk(os.path.join(root, top)):
            dn[:] = [d for d in dn if d not in ("fixtures", "thirdparty") and not d.startswith(".")]
            for f in fn:
                if f.endswith(WALKED):
                    p = os.path.join(dp, f)
                    files[os.path.relpath(p, root)] = p
    tails = {}
    for rel in files:
        parts = rel.split("/")
        for i in range(len(parts)):
            tails.setdefault("/".join(parts[i:]), []).append(rel)
    walk = {"files": len(files), "sources": sum(1 for r in files if r.endswith(TUS)), "roots": 0, "angle": 0, "tail": 0,
            "not ours": 0, "blind": []}
    users = {}
    for rel, p in files.items():
        for m in ANY_INCLUDE.finditer(open(p, errors="replace").read()):
            q, a, macro = m.groups()
            if macro:
                walk["blind"].append("%s: a computed #include %s" % (rel, macro))
                continue
            inc = os.path.normpath(q or a)
            got = os.path.normpath(os.path.join(os.path.dirname(rel), inc)) if q else None
            if got not in files:
                got = next((os.path.normpath(os.path.join(r, inc)) for r in ROOTS
                            if os.path.normpath(os.path.join(r, inc)) in files), None)
                if got:
                    walk["roots" if q else "angle"] += 1
                elif q and len(tails.get(inc, ())) == 1:
                    got = tails[inc][0]
                    walk["tail"] += 1
                elif q and tails.get(inc):
                    walk["blind"].append("%s: \"%s\" is %s" % (rel, q, " or ".join(sorted(tails[inc]))))
                    continue
                else:
                    walk["not ours"] += 1
                    continue
            users.setdefault(got, []).append(rel)
    out = {}
    for h in REACH:
        seen, stack = set(), [os.path.join(INC, h)]
        while stack:
            for u in users.get(stack.pop(), ()):
                if u not in seen:
                    seen.add(u)
                    stack.append(u)
        if os.path.join(INC, h) in files:
            out[h] = sorted(u for u in seen if u.endswith(TUS))
    return out, walk


def ceilings(root):
    out = {}
    p = os.path.join(root, "tests", "hygiene", "engine_headers_ceilings.txt")
    for line in open(p).read().splitlines() if os.path.isfile(p) else []:
        w = line.split("#", 1)[0].split()
        if len(w) == 3 and w[0] in ("lines", "fanout", "reach"):
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


def judge(root, say=False):
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
        if kind != "reach" and h not in m:
            out.append("%s a ceiling for %s, which is no public header: delete its line" % ("H3" if kind == "lines" else "H4", h))
    out += ["H5 " + b for b in bare_irisgl(root)]
    r, walk = reach(root)
    if say:
        print("  H6 walk: %d files, %d sources; includes followed %d by the include roots, %d angle-bracket, %d by a unique path"
              " tail; %d not ours (std, Qt, Ogre, generated); %d blind" % (walk["files"], walk["sources"], walk["roots"],
                                                                            walk["angle"], walk["tail"], walk["not ours"],
                                                                            len(walk["blind"])), flush=True)
    out += ["H6 the walk cannot follow %s: spell it from an include root" % b for b in walk["blind"]]
    for h in REACH:
        if h not in r:
            out.append("H6 %s is no public header: REACH (engine_headers.py) names it" % h)
            continue
        n, cap = len(r[h]), c.get(("reach", h))
        if not n:
            out.append("H6 %s is reached by no source file: the walk resolved nothing (the control)" % h)
        if cap is None:
            out.append("H6 %s has no `reach` ceiling: add `reach %s %d` to engine_headers_ceilings.txt" % (h, h, n))
        elif n > cap:
            out.append("H6 %s reached by %d source files, its ceiling %d: over (a header now includes what it only names? "
                       "engine_headers.py --reach %s lists them)" % (h, n, cap, h))
        elif n < cap:
            out.append("H6 %s reached by %d source files under its ceiling %d: lower the ceiling to %d in the same commit"
                       % (h, n, cap, n))
    out += ["H6 a reach ceiling for %s, which REACH does not name: delete its line" % h
            for (kind, h) in sorted(c) if kind == "reach" and h not in REACH]
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
        # H6's toy: FrameInput.h includes the other five, as the tree's does; three routes reach them - a root
        # (src/x/r.cpp), the includer's directory then an angle bracket (src/y/b.cpp -> b.h), a unique path tail
        # (tests/compute/c.cpp -> "helper.h" = tests/atom/helper.h)
        for h in REACH:
            open(os.path.join(d, h), "w").write("#pragma once\n" + ("".join('#include "jahshaka/engine/%s"\n' % o for o in REACH
                                                                            if o != h) if h == "FrameInput.h" else ""))
        for f, text in (("src/x/r.cpp", '#include "jahshaka/engine/FrameInput.h"\n'), ("src/y/b.cpp", '#include "b.h"\n'),
                        ("src/y/b.h", "#pragma once\n#include <jahshaka/engine/Sky.h>\n"),
                        ("tests/atom/helper.h", '#pragma once\n#include "jahshaka/engine/GiParams.h"\n'),
                        ("tests/compute/c.cpp", '#include "helper.h"\n')):
            os.makedirs(os.path.dirname(os.path.join(t, f)), exist_ok=True)
            open(os.path.join(t, f), "w").write(text)
        m, r = measure(t), reach(t)[0]
        p_c = os.path.join(t, "tests", "hygiene", "engine_headers_ceilings.txt")
        open(p_c, "w").write("".join("lines %s %d\nfanout %s %d\n" % (h, n, h, f) for h, (n, f) in sorted(m.items())) +
                             "".join("reach %s %d\n" % (h, len(us)) for h, us in sorted(r.items())))
        base = judge(t)
        want = {h: 2 if h in ("GiParams.h", "Sky.h") else 1 for h in REACH}
        check(not base and m["Fwd.h"][1] == 2 and m["Engine.h"][1] == 0 and {h: len(us) for h, us in r.items()} == want,
              "the clean toy passes; Fwd.h's fan-out counts Math.h and Engine.h; H6 reaches by the three routes (%s %s %s)"
              % (base, m.get("Fwd.h"), {h: len(us) for h, us in r.items()}))

        def arm(name, do, undo, law, word=""):
            do()
            got = judge(t)
            undo()
            check(any(g.startswith(law) and word in g for g in got), "%s red: %s (%s)" % (law, name, got[:2]))

        def put(path, text):
            return lambda: open(path, "w").write(text)

        p_b, p_cc = os.path.join(t, "src", "y", "b.h"), os.path.join(t, "tests", "compute", "c.cpp")
        p_r = os.path.join(t, "src", "x", "r.cpp")
        b, cc, rr, cs = open(p_b).read(), open(p_cc).read(), open(p_r).read(), open(p_c).read()
        arm("a header that named a type now includes FrameInput.h (the split's first form)",
            put(p_b, b + '#include "jahshaka/engine/FrameInput.h"\n'), put(p_b, b), "H6",
            "GiParams.h reached by 3 source files, its ceiling 2: over")
        arm("a source file drops its way to GiParams.h", put(p_cc, ""), put(p_cc, cc), "H6", "lower the ceiling to 1")
        arm("a header of REACH without its reach line", put(p_c, cs.replace("reach Fog.h 1\n", "")), put(p_c, cs), "H6",
            "add `reach Fog.h")
        arm("the control: a walk that reaches nothing reads 0 under ceilings of 0",
            lambda: [put(p, "")() for p in (p_b, p_cc, p_r)] + [put(p_c, re.sub(r"(reach \S+) \d+", r"\1 0", cs))()],
            lambda: [put(p, x)() for p, x in ((p_b, b), (p_cc, cc), (p_r, rr), (p_c, cs))], "H6", "reached by no source file")
        p_o = os.path.join(t, "tests", "other", "helper.h")
        os.makedirs(os.path.dirname(p_o))
        arm("a quoted include two files of ours answer", put(p_o, "#pragma once\n"), lambda: os.remove(p_o), "H6",
            "cannot follow tests/compute/c.cpp")
        arm("a computed include", put(p_cc, cc + "#include JAH_SOME_HEADER\n"), put(p_cc, cc), "H6", "a computed #include")

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
        for h, us in sorted(reach(root)[0].items()):
            print("reach %s %d" % (h, len(us)))
        sys.exit(0)
    if "--write" in sys.argv:
        p = os.path.join(root, "tests", "hygiene", "engine_headers_ceilings.txt")
        was = open(p).read().splitlines()
        got = ["lines %s %d\nfanout %s %d" % (h, n, h, f) for h, (n, f) in sorted(measure(root).items())]
        got = "\n".join(got + ["reach %s %d" % (h, len(us)) for h, us in sorted(reach(root)[0].items())]).split("\n")
        open(p, "w").write("\n".join([l for l in was if l.startswith("#")] + got) + "\n")
        old = {tuple(l.split()[:2]): l for l in was if not l.startswith("#") and l.split()}
        new = {tuple(l.split()[:2]): l for l in got}
        for k in sorted(set(old) | set(new)):
            if old.get(k) != new.get(k):
                print("%-40s -> %s" % (old.get(k, "(none)"), new.get(k, "(deleted)")))
        sys.exit(0)
    if "--reach" in sys.argv:
        print("\n".join(reach(root)[0].get(sys.argv[sys.argv.index("--reach") + 1], [])))
        sys.exit(0)
    selftest()
    print("the tree (%s):" % root)
    bad = judge(root, say=True)
    check(bool(headers(root)), "the public headers are read (%d)" % len(headers(root)))
    check(not bad, "H1-H6 hold%s" % "".join("\n         " + b for b in bad))
    print("source.engine_headers: %s" % ("PASSED" if not fails else "FAILED (%d)" % fails))
    sys.exit(1 if fails else 0)
