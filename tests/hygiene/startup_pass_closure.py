#!/usr/bin/env python3
"""source.startup_pass_closure - THE STARTUP PASS's KEY HASHES ITS WHOLE CLOSURE (TEST-STREAMLINE-1, the stage-1 audit's finding 5):
<build>/startup-pass-files.txt (cmake/startup_pass_id.py writes it at build time) must hold every project file the COMPILER
reads for src/app/shaderbuildgate.cpp (re-asked here, the same -MM), each header's defining source, and the named sources the
pass calls into (CMakeLists.txt JAH_STARTUP_PASS_EXTRA_SOURCES: the gizmo's meshes and handles). Red when one is missing (the
one-level #include walk it replaced missed gizmo.cpp's siblings: a gizmo material change compiled LIVE). Its negative control
first: a list with one dependency taken out reds.
usage: startup_pass_closure.py <source dir> <build dir>"""
import importlib.util
import os
import re
import sys

src, build = os.path.realpath(sys.argv[1]), sys.argv[2]
fails = []


def law(ok, text):
    print(("  ok   " if ok else "  FAIL ") + text)
    if not ok:
        fails.append(text)


sp = importlib.util.spec_from_file_location("spid", os.path.join(src, "cmake", "startup_pass_id.py"))
spid = importlib.util.module_from_spec(sp)
sys_argv = sys.argv
sys.argv = ["x", src, build, "/dev/null", "/dev/null", "/dev/null", "/dev/null"]
try:
    spid.__name__ = "spid"
    code = open(os.path.join(src, "cmake", "startup_pass_id.py")).read()
    code = code[:code.index("\ndeps = compiler_deps()")]          # the module's functions, not its run
    exec(compile(code, "startup_pass_id.py", "exec"), spid.__dict__)
finally:
    sys.argv = sys_argv
deps = spid.compiler_deps()
want = set(deps)
for h in deps:
    cpp = re.sub(r"\.(h|hpp)$", ".cpp", h)
    if h != cpp and os.path.isfile(os.path.join(src, cpp)):
        want.add(cpp)
cm = open(os.path.join(src, "CMakeLists.txt")).read()
m = re.search(r"set\(JAH_STARTUP_PASS_EXTRA_SOURCES(.*?)\)", cm, re.S)
extras = re.findall(r"\$\{CMAKE_SOURCE_DIR\}/([^\"\s]+)", m.group(1)) if m else []
want |= set(extras)


def missing(listed):
    return sorted(want - listed)


law(len(deps) > 10 and extras, "the compiler reads %d project file(s) for shaderbuildgate.cpp; %d named extra(s)" % (len(deps), len(extras)))
try:
    listed = set(l.strip() for l in open(os.path.join(build, "startup-pass-files.txt")) if l.strip())
except OSError:
    listed = set()
some = sorted(want)[len(want) // 2] if want else None
law(bool(some) and missing(listed - {some}) == [some], "the negative control: the list less one dependency (%s) reds" % some)
law(listed and not missing(listed), "the build's list holds the whole closure (%d files; missing %s)" % (len(listed), missing(listed)[:6]))
print("source.startup_pass_closure: %s" % ("FAIL" if fails else "PASS"))
sys.exit(1 if fails else 0)
