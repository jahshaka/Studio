#!/usr/bin/env python3
"""source.script_neutral — THE ENGINE'S SCRIPT HOST IS LANGUAGE-NEUTRAL (SCRIPT-IN-ENGINE-1 stage 1; the spec's neutrality law:
the value type and the registry know nothing of JavaScript, and a second language is one more file).

  N1 THE JS ENGINE IS NAMED IN ONE PLACE. No C/C++ source or header under src/, tests/ or irisgl/ (irisgl/thirdparty/ - the
     vendored trees - excluded) names the JS engine (`JS_<name>`, `quickjs`, any case) except irisgl/engine/src/script/js_*.cpp,
     the language's binding. (The build files name the library they build: they are not sources.)
  N2 THE HOST's NEUTRAL FILES INCLUDE NOTHING BUT THE STANDARD LIBRARY AND THEMSELVES: jahshaka/engine/Script.h and
     src/script/{registry.cpp, host.cpp, Language.h} include no Qt, Ogre or JS-engine header.
  N3 jahengine LINKS THE ENGINE AND NO QT: its NEEDED list (`objdump -p`) names no Qt library and no IrisGL (the document
     library that carries Qt today); the JS engine and the engine are static inside it.

usage: script_neutral.py <source-root> <jahengine binary>
Its self-test runs first, on synthetic files: each rule red on a violation, green on the allowed file.
"""
import os
import re
import subprocess
import sys
import tempfile

FAILURES = []
SOURCE_EXTS = (".c", ".cc", ".cpp", ".cxx", ".h", ".hpp")
ROOTS = ("src", "tests", "irisgl")
PRUNED = (os.path.join("irisgl", "thirdparty"),)
BINDING = re.compile(r"^irisgl/engine/src/script/js_[^/]+\.cpp$")
JS_NAME = re.compile(r"\bJS_[A-Za-z]|quickjs", re.IGNORECASE)
NEUTRAL = ("irisgl/engine/include/jahshaka/engine/Script.h", "irisgl/engine/src/script/registry.cpp",
           "irisgl/engine/src/script/host.cpp", "irisgl/engine/src/script/Language.h")
FOREIGN_INCLUDE = re.compile(r'^\s*#\s*include\s*[<"]\s*(Q[A-Za-z]|qt|Ogre|quickjs|[^>"]*/(Q[A-Za-z]|Ogre))', re.MULTILINE)


def check(ok, what):
    print(("  ok: " if ok else "  FAIL: ") + what)
    if not ok:
        FAILURES.append(what)


def n1_bad(root):
    """[relative path] of every C/C++ file under the roots, but the binding, that names the JS engine."""
    bad = []
    for top in ROOTS:
        for d, dirs, names in os.walk(os.path.join(root, top)):
            rel_d = os.path.relpath(d, root)
            dirs[:] = [x for x in dirs if os.path.join(rel_d, x) not in PRUNED and x != ".git"]
            for n in names:
                if not n.endswith(SOURCE_EXTS):
                    continue
                rel = os.path.join(rel_d, n).replace(os.sep, "/")
                if BINDING.match(rel):
                    continue
                if JS_NAME.search(open(os.path.join(d, n), errors="replace").read()):
                    bad.append(rel)
    return sorted(bad)


def n2_bad(root):
    """[`file: include`] of every foreign include in the neutral files (a missing neutral file is itself a FAIL)."""
    bad = []
    for rel in NEUTRAL:
        p = os.path.join(root, rel)
        if not os.path.isfile(p):
            bad.append(rel + ": missing")
            continue
        bad += ["%s: %s" % (rel, m.group(0).strip()) for m in FOREIGN_INCLUDE.finditer(open(p, errors="replace").read())]
    return bad


def n3_bad(objdump_text):
    """[library] of the NEEDED entries that are Qt's or IrisGL's, read from `objdump -p` text."""
    needed = re.findall(r"^\s*NEEDED\s+(\S+)", objdump_text, re.MULTILINE)
    return [n for n in needed if re.match(r"libQt|libIrisGL", n)]


def write(root, rel, text):
    p = os.path.join(root, rel)
    os.makedirs(os.path.dirname(p), exist_ok=True)
    with open(p, "w") as f:
        f.write(text)


def self_test():
    print("the rules catch (synthetic):")
    with tempfile.TemporaryDirectory() as t:
        for rel in NEUTRAL:
            write(t, rel, "#include <string>\n#include \"jahshaka/engine/Script.h\"\n")
        write(t, "irisgl/engine/src/script/js_quickjs.cpp", "#include \"quickjs.h\"\nJS_NewRuntime();\n")
        write(t, "irisgl/thirdparty/quickjs-ng/quickjs.c", "JS_NewRuntime\n")
        check(not n1_bad(t) and not n2_bad(t), "the binding and the vendored tree may name the JS engine; clean neutral files pass")
        write(t, "irisgl/engine/src/script/commands_engine.cpp", "JSValue v = JS_NewObject(ctx);\n")
        write(t, "src/scripting/x.h", "// built on QuickJS\n")
        check(n1_bad(t) == ["irisgl/engine/src/script/commands_engine.cpp", "src/scripting/x.h"],
              "N1 names a command file calling the JS engine and a Studio header naming it (%s)" % n1_bad(t))
        write(t, NEUTRAL[0], "#include <QString>\n#include \"quickjs.h\"\n#include <OgreRoot.h>\n")
        write(t, NEUTRAL[1], "#include <QtCore/QVariant>\n")
        check(len(n2_bad(t)) == 4, "N2 names a Qt, a JS-engine and an Ogre include in the neutral files (%s)" % n2_bad(t))
    dump = "Dynamic Section:\n  NEEDED               libOgreMain.so.4.0\n  NEEDED               libQt6Core.so.6\n" \
           "  NEEDED               libIrisGLd.so\n  NEEDED               libc.so.6\n"
    check(n3_bad(dump) == ["libQt6Core.so.6", "libIrisGLd.so"], "N3 names a Qt and an IrisGL library in a NEEDED list (%s)"
          % n3_bad(dump))


def main():
    if len(sys.argv) != 3:
        print("usage: script_neutral.py <source-root> <jahengine binary>")
        print("source.script_neutral: FAILED (1)")
        return 1
    root, jahengine = sys.argv[1], sys.argv[2]
    self_test()
    print("the tree:")
    bad = n1_bad(root)
    check(not bad, "N1 the JS engine is named only by irisgl/engine/src/script/js_*.cpp (%d others: %s)" % (len(bad), bad[:8]))
    bad = n2_bad(root)
    check(not bad, "N2 Script.h, registry.cpp, host.cpp and Language.h include no Qt, Ogre or JS-engine header (%s)" % bad[:8])
    dump = subprocess.run(["objdump", "-p", jahengine], capture_output=True, text=True)
    needed = re.findall(r"^\s*NEEDED\s+(\S+)", dump.stdout, re.MULTILINE)
    check(dump.returncode == 0 and needed and not n3_bad(dump.stdout),
          "N3 jahengine's NEEDED list holds no Qt and no IrisGL (%d libraries: %s)" % (len(needed), " ".join(needed)))
    if FAILURES:
        print("source.script_neutral: FAILED (%d)" % len(FAILURES))
        return 1
    print("source.script_neutral: PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
