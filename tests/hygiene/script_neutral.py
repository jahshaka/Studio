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
  N4 THE LANGUAGE TABLE (the lead's S5): a language is ONE source file and ONE table line - every
     JAH_SCRIPT_LANGUAGE("<ext>", <maker>) line of irisgl/engine/src/script/LanguageTable.inc names a maker defined by exactly
     one source of src/script/ (the language's file), every maker a source defines has its one line, no extension or
     maker is listed twice, and no other source of src/script/ names a maker (a language reached around the table).
     Its negative controls (the self-test): a second, dummy language added as one file and one line PASSES; the file
     without its line, the line without its file, and a maker named in a command file each FAIL.

usage: script_neutral.py <source-root> <build dir>   (jahengine is <build dir>/bin/jahengine)
Its self-test runs first, on synthetic files: each rule red on a violation, green on the allowed file.
"""
import os
import re
import subprocess
import sys
import tempfile

FAILURES = []
SOURCE_EXTS = (".c", ".cc", ".cpp", ".cxx", ".h", ".hpp", ".inc")
ROOTS = ("src", "tests", "irisgl")
PRUNED = (os.path.join("irisgl", "thirdparty"),)
BINDING = re.compile(r"^irisgl/engine/src/script/js_[^/]+\.cpp$")
# the JS engine's functions and macros (JS_*), its types (JSContext, JSValue, JSRuntime: the final read's leftover - a
# forward-declared `struct JSContext;` passed N1) and its name; JSON is not it (JS + an upper-case letter + a lower-case one)
JS_NAME = re.compile(r"(?i:\bJS_[A-Za-z]|quickjs)|\bJS[A-Z][a-z]")
NEUTRAL = ("irisgl/engine/include/jahshaka/engine/Script.h", "irisgl/engine/src/script/registry.cpp",
           "irisgl/engine/src/script/host.cpp", "irisgl/engine/src/script/Language.h",
           "irisgl/engine/src/script/LanguageTable.inc")
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


SCRIPT_DIR = "irisgl/engine/src/script"
TABLE = SCRIPT_DIR + "/LanguageTable.inc"
TABLE_LINE = re.compile(r'^\s*JAH_SCRIPT_LANGUAGE\(\s*"(\.[A-Za-z0-9]+)"\s*,\s*(\w+)\s*\)', re.MULTILINE)
MAKER_DEF = re.compile(r'std::unique_ptr<Host::Language>\s+(?:detail::)?(\w+)\s*\(\s*Host\s*&\s*\w*\s*\)\s*\{')


def n4_bad(root):
    """THE LANGUAGE TABLE: [fault] - each table line's maker defined in EXACTLY ONE src/script/ source, every maker a
    source defines named by exactly one line, one line an extension, and no other source of src/script/ naming a maker
    (a language reached around the table)."""
    p = os.path.join(root, TABLE)
    if not os.path.isfile(p):
        return [TABLE + ": missing"]
    table = TABLE_LINE.findall(re.sub(r"//[^\n]*", "", open(p, errors="replace").read()))
    bad = []
    exts = [e for e, _ in table]
    makers = [m for _, m in table]
    bad += ["%s: the extension %s has %d lines" % (TABLE, e, exts.count(e)) for e in sorted(set(exts)) if exts.count(e) > 1]
    bad += ["%s: the maker %s has %d lines" % (TABLE, m, makers.count(m)) for m in sorted(set(makers)) if makers.count(m) > 1]
    defined = {}
    named = {}
    d = os.path.join(root, SCRIPT_DIR)
    for n in sorted(os.listdir(d)) if os.path.isdir(d) else []:
        if not n.endswith(SOURCE_EXTS):
            continue
        text = open(os.path.join(d, n), errors="replace").read()
        for m in MAKER_DEF.findall(text):
            defined.setdefault(m, []).append(n)
        for m in makers:
            if n not in ("Language.h", "host.cpp", "LanguageTable.inc") and re.search(r"\b%s\b" % m, text) and m not in MAKER_DEF.findall(text):
                named.setdefault(m, []).append(n)
    for m in makers:
        if len(defined.get(m, [])) != 1:
            bad.append("%s: the maker %s is defined by %d sources %s (exactly one: the language's file)"
                       % (TABLE, m, len(defined.get(m, [])), defined.get(m, [])))
    for m, files in sorted(defined.items()):
        if m not in makers:
            bad.append("%s defines the language maker %s that no table line names" % (files, m))
    for m, files in sorted(named.items()):
        bad.append("%s name the maker %s outside the table (a language reached around it)" % (files, m))
    if not table:
        bad.append(TABLE + ": no language")
    return bad


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
        write(t, "src/x/fwd.h", "struct JSContext;\nvoid f(JSContext *c); // JSON stays\n")
        check(n1_bad(t)[-1:] == ["src/x/fwd.h"] and not JS_NAME.search("JSON.stringify(x); // a JSONL file"),
              "N1 names a forward-declared JSContext (the engine's type names), never JSON")
    with tempfile.TemporaryDirectory() as t:
        SD = "irisgl/engine/src/script/"
        mk = "std::unique_ptr<Host::Language> detail::%s(Host &host) { return nullptr; }\n"
        write(t, SD + "LanguageTable.inc", '// a comment line naming JAH_SCRIPT_LANGUAGE(".x", makeX)\nJAH_SCRIPT_LANGUAGE(".js", makeJavaScript)\n')
        write(t, SD + "js_quickjs.cpp", mk % "makeJavaScript")
        write(t, SD + "Language.h", "#define JAH_SCRIPT_LANGUAGE(e, m) std::unique_ptr<Host::Language> m(Host &host);\n")
        write(t, SD + "host.cpp", "#define JAH_SCRIPT_LANGUAGE(e, m) addLanguage(e, &detail::m);\n")
        check(not n4_bad(t), "N4 the one language, its one file and its one line pass (%s)" % n4_bad(t))
        write(t, SD + "lua_dummy.cpp", mk % "makeLua")
        file_only = n4_bad(t)
        write(t, SD + "LanguageTable.inc", 'JAH_SCRIPT_LANGUAGE(".js", makeJavaScript)\nJAH_SCRIPT_LANGUAGE(".lua", makeLua)\n')
        check(len(file_only) == 1 and "makeLua that no table line names" in file_only[0] and not n4_bad(t),
              "N4 NEGATIVE CONTROL: a second (dummy) language as ONE file + ONE line passes; the file alone FAILS (%s)"
              % file_only)
        os.remove(os.path.join(t, SD + "lua_dummy.cpp"))
        line_only = n4_bad(t)
        write(t, SD + "lua_dummy.cpp", mk % "makeLua")
        write(t, SD + "commands_view.cpp", "auto l = detail::makeLua(host);\n")
        around = n4_bad(t)
        check(len(line_only) == 1 and "makeLua is defined by 0 sources" in line_only[0] and len(around) == 1
              and "outside the table" in around[0],
              "N4 a line with no file, and a maker named by a command file (around the table), each FAIL (%s %s)"
              % (line_only, around))
    dump = "Dynamic Section:\n  NEEDED               libOgreMain.so.4.0\n  NEEDED               libQt6Core.so.6\n" \
           "  NEEDED               libIrisGLd.so\n  NEEDED               libc.so.6\n"
    check(n3_bad(dump) == ["libQt6Core.so.6", "libIrisGLd.so"], "N3 names a Qt and an IrisGL library in a NEEDED list (%s)"
          % n3_bad(dump))


def main():
    if len(sys.argv) != 3:
        print("usage: script_neutral.py <source-root> <build dir>")
        print("source.script_neutral: FAILED (1)")
        return 1
    # the build dir, never the binary, on the row's command line: to the selector a row naming jahengine is one of ITS rows
    root, jahengine = sys.argv[1], os.path.join(sys.argv[2], "bin", "jahengine")
    self_test()
    print("the tree:")
    bad = n1_bad(root)
    check(not bad, "N1 the JS engine is named only by irisgl/engine/src/script/js_*.cpp (%d others: %s)" % (len(bad), bad[:8]))
    bad = n2_bad(root)
    check(not bad, "N2 Script.h, registry.cpp, host.cpp, Language.h and LanguageTable.inc include no Qt, Ogre or JS-engine "
                   "header (%s)" % bad[:8])
    bad = n4_bad(root)
    check(not bad, "N4 every language is one source file and one table line (%s)" % bad[:8])
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
