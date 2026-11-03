#!/usr/bin/env python3
"""source.shader_cache_key - THE HLMS LAYER's KEY HOLDS ONLY WHILE NO ENGINE C++ WRITES SHADER SOURCE TEXT (TEST-STREAMLINE-1 1a).

The shader cache's hlms layer keys on JAHSHAKA_SHADER_CACHE_CODE_ID - the C++ that WRITES its bytes (irisgl/engine/CMakeLists.txt
JAHSHAKA_SHADER_CACHE_CODE_SOURCES: OgreShaderCache.cpp) - and no longer on every engine source. That is sound because an Ogre
Hlms disk-cache entry is found by its MERGED properties and pieces, taken after every C++ hook (OgreHlms.cpp
createShaderCacheEntry), the root layout is made fresh at every compile and the PSOs at runtime (irisgl/cmake/EngineBuildId.cmake
says it with the anchors): engine C++ reaches a cached Hlms source ONLY through property and piece values, which are the key.
The read of 2026-11-03 found no engine file that builds source text into an Hlms entry. This row keeps that true: a file under
irisgl/engine/src that calls a SOURCE-BUILDING API reds, unless it is
  - in the cache-code list (its bytes key the hlms layer), or
  - in LOW_LEVEL below: it builds a NON-Hlms program, whose bytes the MICROCODE layer keys on its source + preamble (defines and
    root layout, the fork's OgreVulkanProgram.cpp getNameForMicrocodeCache) - never the hlms layer.
The fix for a red: the file goes into JAHSHAKA_SHADER_CACHE_CODE_SOURCES (an edit of it then re-keys the hlms layer), or, for a
low-level program, into LOW_LEVEL with its reason. Its negative control: --self-test seeds each API into a toy tree and requires a red.
usage: shader_cache_key.py <studio source dir> | --self-test
"""
import os
import re
import sys
import tempfile

# an API that puts TEXT into a shader's source from C++ (Ogre-Next's: the Hlms piece and custom-piece doors, a program made or
# re-sourced from a C++ string, the disk cache's own calls - applyTo/copyFrom/saveTo/loadFrom: a member DECLARED elsewhere,
# EnginePrivate.h's ShaderCache, calls nothing)
SOURCE_APIS = re.compile(r"\bsetPiece\s*\(|\bsetCustomPieceCodeFromMemory\b|\b_addDatablockCustomPieceFile\b|"
                         r"\bcreateProgram(Detached)?\s*\(|\bsetSource\s*\(|(\.|->)\s*(applyTo|copyFrom|saveTo|loadFrom)\s*\(")
# THE LOW-LEVEL CALL SITES, BY FUNCTION (the stage-1 audit's finding 8: the whole 4,780-line file was exempt, so a later
# setPiece anywhere in it would have passed): (file relative to irisgl/engine/src, its top-level function) -> why its
# source-building is outside the hlms layer. A call in any other function of the file reds.
LOW_LEVEL = {
    ("OgreChain.cpp", "multiviewMaterial"): "the stereo chain's multiview twin of a LOW-LEVEL fragment program: "
                                            "microcode-keyed on its source + preamble, never in an Hlms entry",
}


def cache_code_sources(src):
    """The files JAHSHAKA_SHADER_CACHE_CODE_SOURCES names, relative to irisgl/engine/src (the one list)."""
    text = open(os.path.join(src, "irisgl", "engine", "CMakeLists.txt")).read()
    m = re.search(r"set\(JAHSHAKA_SHADER_CACHE_CODE_SOURCES\s+([^)]*)\)", text)
    if not m:
        return None
    return {f.replace("${CMAKE_CURRENT_SOURCE_DIR}/src/", "") for f in m.group(1).split()}


def top_functions(text):
    """[name or None] per line of `text` (comments stripped): the top-level function whose body holds the line - a `namespace`
    or `extern "C"` block is transparent; a class body is its own scope (its inline members by their own names)."""
    out, depth, cand, stack, cur = [], 0, None, [], None
    for line in text.split("\n"):
        line_fn = cur
        for m in re.finditer(r"[A-Za-z_~][A-Za-z0-9_]*\s*\(|\{|\}|;", line):
            t = m.group(0)
            if t == "{":
                head = line[:m.start()]
                if re.search(r"\bnamespace\b|extern\s+\"C\"", head) and cur is None:
                    stack.append("ns")
                    continue
                stack.append("fn" if cur is None and cand else "blk")
                if cur is None and cand:
                    cur = cand
                    line_fn = cur
            elif t == "}":
                kind = stack.pop() if stack else "blk"
                if kind == "fn":
                    cur = None
            elif t == ";":
                if cur is None:
                    cand = None
            elif cur is None:
                cand = re.sub(r"\s*\($", "", t).split("::")[-1]
        out.append(line_fn if line_fn else cur)
    return out


def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", lambda m: "\n" * m.group(0).count("\n"), text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def offenders(src):
    listed = cache_code_sources(src)
    if listed is None:
        return None, ["irisgl/engine/CMakeLists.txt has no set(JAHSHAKA_SHADER_CACHE_CODE_SOURCES ...)"]
    root = os.path.join(src, "irisgl", "engine", "src")
    bad, seen = [], 0
    for d, _, files in os.walk(root):
        for f in sorted(files):
            if not f.endswith((".cpp", ".h", ".hpp", ".inl")):
                continue
            rel = os.path.relpath(os.path.join(d, f), root)
            seen += 1
            if rel in listed:
                continue
            text = strip_comments(open(os.path.join(d, f), errors="replace").read())
            fns = top_functions(text)
            for n, line in enumerate(text.splitlines(), 1):
                if SOURCE_APIS.search(line) and (rel, fns[n - 1]) not in LOW_LEVEL:
                    bad.append(f"{rel}:{n} (in {fns[n - 1] or 'namespace scope'}): {line.strip()[:140]}")
    for rel in listed:
        if not os.path.isfile(os.path.join(root, rel)):
            bad.append(f"the cache-code list names {rel}, which is not under irisgl/engine/src")
    for rel, fn in LOW_LEVEL:
        if not os.path.isfile(os.path.join(root, rel)):
            bad.append(f"LOW_LEVEL names {rel}, which is not under irisgl/engine/src (delete its line)")
        elif fn not in top_functions(strip_comments(open(os.path.join(root, rel), errors="replace").read())):
            bad.append(f"LOW_LEVEL names {rel}'s function {fn}, which it no longer defines (delete or rename its line)")
    return seen, bad


def check(src):
    seen, bad = offenders(src)
    if bad:
        print("source.shader_cache_key: FAIL - engine C++ that builds shader SOURCE text outside the hlms layer's key "
              "(add the file to JAHSHAKA_SHADER_CACHE_CODE_SOURCES in irisgl/engine/CMakeLists.txt, or - a low-level "
              "program - to LOW_LEVEL here with its reason):")
        for b in bad:
            print("  " + b)
        return 1
    print(f"source.shader_cache_key: PASS - {seen} engine files read; source text is built only in the cache-code list "
          f"({', '.join(sorted(cache_code_sources(src)))}) and the low-level call sites "
          f"({', '.join(f + ' ' + fn for f, fn in sorted(LOW_LEVEL))})")
    return 0


def self_test():
    """The negative control: a toy tree whose one engine file calls each API reds; the same file listed passes."""
    fails = 0
    with tempfile.TemporaryDirectory() as t:
        os.makedirs(os.path.join(t, "irisgl", "engine", "src"))
        open(os.path.join(t, "irisgl", "engine", "src", "OgreShaderCache.cpp"), "w").write("// setPiece( in a comment is not a call\n")
        chain = os.path.join(t, "irisgl", "engine", "src", "OgreChain.cpp")
        open(chain, "w").write("namespace chain {\nvoid multiviewMaterial(int x) {\n    pm.createProgram(a, b, c, d);\n}\n}\n")
        cm = os.path.join(t, "irisgl", "engine", "CMakeLists.txt")
        open(cm, "w").write("set(JAHSHAKA_SHADER_CACHE_CODE_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/OgreShaderCache.cpp)\n")
        seen, bad = offenders(t)
        if bad:
            print(f"  FAIL a clean toy reads red: {bad}"); fails += 1
        for call in ("job->setPiece(name, text);", "pm.createProgram(n, g, s, t);", "prog->setSource(text);",
                     "cache->applyTo(hlms, 4);", "hlms->_addDatablockCustomPieceFile(f, g, h);"):
            p = os.path.join(t, "irisgl", "engine", "src", "OgreNew.cpp")
            open(p, "w").write("void f() {\n    " + call + "\n}\n")
            seen, bad = offenders(t)
            if not (len(bad) == 1 and bad[0].startswith("OgreNew.cpp:2 ")):
                print(f"  FAIL '{call}' not caught: {bad}"); fails += 1
            open(cm, "w").write("set(JAHSHAKA_SHADER_CACHE_CODE_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/OgreShaderCache.cpp\n"
                                "    ${CMAKE_CURRENT_SOURCE_DIR}/src/OgreNew.cpp)\n")
            seen, bad = offenders(t)
            if bad:
                print(f"  FAIL '{call}' in a LISTED file still red: {bad}"); fails += 1
            open(cm, "w").write("set(JAHSHAKA_SHADER_CACHE_CODE_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/OgreShaderCache.cpp)\n")
            os.remove(p)
        # BY CALL SITE (finding 8): the allowed function's call passes; the same call in another function of that file reds
        open(chain, "a").write("namespace chain {\nstatic void other(int y) {\n    if (y) { prog->setSource(text); }\n}\n}\n")
        seen, bad = offenders(t)
        if not (len(bad) == 1 and bad[0].startswith("OgreChain.cpp:8 (in other)")):
            print(f"  FAIL a source call in another function of OgreChain.cpp not caught by call site: {bad}"); fails += 1
    print("source.shader_cache_key --self-test: " + ("PASS" if fails == 0 else f"FAIL ({fails})"))
    return 1 if fails else 0


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__.strip().splitlines()[-1])
    if sys.argv[1] == "--self-test":
        sys.exit(self_test())
    rc = self_test()
    sys.exit(check(sys.argv[1]) or rc)
