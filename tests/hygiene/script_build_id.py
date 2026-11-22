#!/usr/bin/env python3
"""source.script_build_id — THE SCRIPT HOST IS OUTSIDE THE ENGINE BUILD ID, AND IT IS A HOST (SCRIPT-IN-ENGINE-1 2a a3).

JAHSHAKA_ENGINE_BUILD_ID (irisgl/cmake/EngineBuildId.cmake) is the hash of the engine's own sources: the texture cache's key,
the warm-up's top-up signal and a term of the app's startup-pass id. The script host's sources (irisgl/engine/src/script/,
JAHSHAKA_ENGINE_SCRIPT_SOURCES) are compiled into the engine library but are not its inputs: a command calls only the
public engine API, so no edit of one can change what a shader permutation means - and hashing them re-keyed those caches
at every command edit. jahengine/ is its own executable, outside the list by construction.

  B1 THE CONFIGURED INPUTS HOLD NO SCRIPT SOURCE: the build's own command line for EngineBuildId.cmake (build.ninja's
     -DJAH_BUILD_ID_SOURCES) names no file under irisgl/engine/src/script/ or irisgl/engine/jahengine/. Its control: the
     list holds the engine (src/OgreEngine.cpp in it, over 50 files).
  B2 THE SCRIPT HOST CALLS ONLY THE PUBLIC API: every file under irisgl/engine/src/script/ and irisgl/engine/jahengine/
     includes only jahshaka/engine/*.h (an existing public header), script/*, the standard library (<name>, no extension),
     and - in src/script/js_*.cpp only - the JS engine (quickjs.h). Its control: the files scanned and their includes > 0.
  B3 THE ID IS THE BUILD's COMPUTATION, AND A SCRIPT EDIT LEAVES IT: EngineBuildId.cmake is run with the build's own
     arguments, its source list remapped onto a scratch copy of every input plus every script source: (a) the copy's ID
     equals the build's generated header (the build is current: this IS its computation); (b) a comment appended to the
     copy of src/script/commands_node.cpp leaves the ID unchanged; (c) THE CONTROL: a comment appended to the copy of
     src/OgreEngine.cpp (an input) moves it.

usage: script_build_id.py <source-root> <build dir> <cmake>
Its self-test runs first, on synthetic text: each rule red on a violation, green on the allowed form.
"""
import os
import re
import shlex
import shutil
import subprocess
import sys
import tempfile

FAILURES = []
SCRIPT_DIRS = ("irisgl/engine/src/script", "irisgl/engine/jahengine")
PUBLIC = "irisgl/engine/include/jahshaka/engine"
INCLUDE = re.compile(r'^\s*#\s*include\s*([<"])([^>"]+)[>"]', re.MULTILINE)
STD = re.compile(r"^[a-z_]+$")


def check(ok, what):
    print(("  ok: " if ok else "  FAIL: ") + what)
    if not ok:
        FAILURES.append(what)


def id_command(ninja_text):
    """{-D name: value, "-P": script, "cmake": exe} of the build's EngineBuildId.cmake command, or None."""
    for line in ninja_text.splitlines():
        if "EngineBuildId.cmake" in line and "-DJAH_BUILD_ID_SOURCES=" in line:
            cmd = line.split("COMMAND = ", 1)[-1].split(" && ", 1)[-1]
            argv = shlex.split(cmd)
            out = {"cmake": argv[0]}
            for i, a in enumerate(argv):
                if a.startswith("-D") and "=" in a:
                    k, v = a[2:].split("=", 1)
                    out[k] = v
                elif a == "-P" and i + 1 < len(argv):
                    out["-P"] = argv[i + 1]
            return out
    return None


def b1_bad(sources, root):
    """[the listed files under a script directory]."""
    rels = [os.path.relpath(os.path.realpath(s), os.path.realpath(root)).replace(os.sep, "/") for s in sources]
    return [r for r in rels if any(r == d or r.startswith(d + "/") for d in SCRIPT_DIRS)]


def b2_bad(root):
    """([`file: include`] of every include outside the host's allowance, files scanned, includes read)."""
    bad, files, n = [], 0, 0
    for d in SCRIPT_DIRS:
        for dd, _, names in os.walk(os.path.join(root, d)):
            for name in sorted(names):
                if not name.endswith((".cpp", ".h", ".inc")):
                    continue
                files += 1
                rel = os.path.relpath(os.path.join(dd, name), root).replace(os.sep, "/")
                for m in INCLUDE.finditer(open(os.path.join(dd, name), errors="replace").read()):
                    n += 1
                    kind, inc = m.group(1), m.group(2).strip()
                    if kind == "<":
                        ok = bool(STD.match(inc))
                    elif inc.startswith("jahshaka/engine/"):
                        ok = os.path.isfile(os.path.join(root, PUBLIC, inc[len("jahshaka/engine/"):]))
                    elif inc.startswith("script/"):
                        ok = True
                    else:
                        ok = inc == "quickjs.h" and re.match(r"^irisgl/engine/src/script/js_[^/]+\.cpp$", rel) is not None
                    if not ok:
                        bad.append("%s: %s%s" % (rel, kind, inc))
    return bad, files, n


def run_id(cmd, sources, out):
    """The ID EngineBuildId.cmake writes for `sources` (the build's other arguments as they are), or None."""
    args = [cmd["cmake"], "-DJAH_BUILD_ID_SOURCES=" + "|".join(sources)]
    for k in ("JAH_BUILD_ID_CACHE_SOURCES", "JAH_COMPILE_PATH_LIST", "JAH_COMPILE_PATH_ROOT"):
        args.append("-D%s=%s" % (k, cmd.get(k, "")))
    args += ["-DJAH_BUILD_ID_OUT=" + out, "-P", cmd.get("-P", "")]
    p = subprocess.run(args, capture_output=True, text=True)
    return header_id(open(out).read()) if p.returncode == 0 and os.path.isfile(out) else None


def header_id(text):
    m = re.search(r'^#define JAHSHAKA_ENGINE_BUILD_ID "([0-9a-f]+)"', text, re.MULTILINE)
    return m.group(1) if m else None


def write(root, rel, text):
    p = os.path.join(root, rel)
    os.makedirs(os.path.dirname(p), exist_ok=True)
    with open(p, "w") as f:
        f.write(text)


def self_test():
    print("the rules catch (synthetic):")
    ninja = ('build x: CUSTOM_COMMAND\n  COMMAND = cd /b/engine && /usr/bin/cmake "-DJAH_BUILD_ID_SOURCES=/s/a.cpp|/s/b.h" '
             '"-DJAH_BUILD_ID_CACHE_SOURCES=/s/c.cpp" -DJAH_COMPILE_PATH_LIST=/s/l.txt -DJAH_BUILD_ID_OUT=/b/id.h '
             '-P /s/irisgl/cmake/EngineBuildId.cmake\n')
    c = id_command(ninja)
    check(c is not None and c["JAH_BUILD_ID_SOURCES"] == "/s/a.cpp|/s/b.h" and c["-P"].endswith("EngineBuildId.cmake")
          and c["cmake"] == "/usr/bin/cmake" and id_command("build y: phony\n") is None,
          "B1 reads the build's own EngineBuildId.cmake command line (its list, its script, its cmake)")
    check(b1_bad(["/r/irisgl/engine/src/OgreEngine.cpp", "/r/irisgl/engine/src/script/registry.cpp",
                  "/r/irisgl/engine/jahengine/main.cpp", "/r/irisgl/engine/src/scripted.cpp"], "/r") ==
          ["irisgl/engine/src/script/registry.cpp", "irisgl/engine/jahengine/main.cpp"],
          "B1 names a script source and jahengine in the list, never an engine file that merely starts with 'script'")
    with tempfile.TemporaryDirectory() as t:
        write(t, PUBLIC + "/Engine.h", "")
        write(t, "irisgl/engine/src/script/commands_x.cpp",
              '#include "script/commands.h"\n#include "jahshaka/engine/Engine.h"\n#include <cmath>\n')
        write(t, "irisgl/engine/src/script/js_x.cpp", '#include "quickjs.h"\n#include <memory>\n')
        write(t, "irisgl/engine/jahengine/main.cpp", '#include "jahshaka/engine/Engine.h"\n#include <cstdio>\n')
        clean = b2_bad(t)
        write(t, "irisgl/engine/src/script/commands_y.cpp",
              '#include "EnginePrivate.h"\n#include "jahshaka/engine/Missing.h"\n#include <OgreRoot.h>\n'
              '#include "quickjs.h"\n#include <QString>\n')
        dirty = b2_bad(t)
        check(clean == ([], 3, 7) and len(dirty[0]) == 5 and all(x.startswith("irisgl/engine/src/script/commands_y.cpp")
                                                                   for x in dirty[0]),
              "B2: the public headers, script/, the standard library and quickjs.h in js_*.cpp pass; EnginePrivate.h, a "
              "public name that is no header, Ogre, the JS engine in a command file and Qt each FAIL (%s %s)"
              % (clean, dirty[0]))
    check(header_id('#pragma once\n#define JAHSHAKA_ENGINE_BUILD_ID "00ff"\n#define JAHSHAKA_SHADER_CACHE_CODE_ID "11"\n')
          == "00ff", "B3 reads the generated header's engine build id, not its other ids")


def main():
    if len(sys.argv) != 4:
        print("usage: script_build_id.py <source-root> <build dir> <cmake>")
        print("source.script_build_id: FAILED (1)")
        return 1
    root, build, cmake = sys.argv[1:]
    self_test()
    print("the tree:")
    ninja = os.path.join(build, "build.ninja")
    cmd = id_command(open(ninja, errors="replace").read()) if os.path.isfile(ninja) else None
    check(cmd is not None, "B1 the build's EngineBuildId.cmake command is in %s" % ninja)
    if cmd is None:
        print("source.script_build_id: FAILED (%d)" % len(FAILURES))
        return 1
    cmd["cmake"] = cmake
    sources = [s for s in cmd["JAH_BUILD_ID_SOURCES"].split("|") if s]
    bad = b1_bad(sources, root)
    engine = [s for s in sources if s.replace(os.sep, "/").endswith("irisgl/engine/src/OgreEngine.cpp")]
    check(not bad and engine and len(sources) > 50,
          "B1 the build ID's %d inputs (OgreEngine.cpp among them: %s) hold no script source (%s)"
          % (len(sources), bool(engine), bad[:6]))
    bad, files, n = b2_bad(root)
    check(not bad and files > 0 and n > 0,
          "B2 the %d script-host files (%d includes) include only the public API, script/, the standard library and "
          "(js_*.cpp) the JS engine (%s)" % (files, n, bad[:6]))
    built = header_id(open(cmd.get("JAH_BUILD_ID_OUT", ""), errors="replace").read()) \
        if os.path.isfile(cmd.get("JAH_BUILD_ID_OUT", "")) else None
    with tempfile.TemporaryDirectory() as t:
        real = os.path.realpath(root)
        copy = {}
        for s in sources + [os.path.join(root, d, n) for d in SCRIPT_DIRS
                            for n in sorted(os.listdir(os.path.join(root, d)))]:
            rel = os.path.relpath(os.path.realpath(s), real)
            dest = os.path.join(t, "src", rel)
            os.makedirs(os.path.dirname(dest), exist_ok=True)
            if os.path.isfile(s):
                shutil.copyfile(s, dest)
            copy[rel.replace(os.sep, "/")] = dest
        listed = [copy[os.path.relpath(os.path.realpath(s), real).replace(os.sep, "/")] for s in sources]
        out = os.path.join(t, "id.h")
        first = run_id(cmd, listed, out)
        with open(copy["irisgl/engine/src/script/commands_node.cpp"], "a") as f:
            f.write("// a comment: source.script_build_id's edit of a command\n")
        script_edit = run_id(cmd, listed, out)
        with open(copy["irisgl/engine/src/OgreEngine.cpp"], "a") as f:
            f.write("// a comment: source.script_build_id's edit of an input (the control)\n")
        engine_edit = run_id(cmd, listed, out)
    check(first is not None and first == built,
          "B3 (a) EngineBuildId.cmake on the copy gives the build's own ID (%s; the header: %s)" % (first, built))
    check(first is not None and script_edit == first,
          "B3 (b) an edit of src/script/commands_node.cpp leaves the ID (%s -> %s)" % (first, script_edit))
    check(first is not None and engine_edit is not None and engine_edit != first,
          "B3 (c) THE CONTROL: an edit of src/OgreEngine.cpp, an input, moves it (%s -> %s)" % (script_edit, engine_edit))
    if FAILURES:
        print("source.script_build_id: FAILED (%d)" % len(FAILURES))
        return 1
    print("source.script_build_id: PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
