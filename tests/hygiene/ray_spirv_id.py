#!/usr/bin/env python3
"""source.ray_spirv_id - THE RAY SPIR-V IS IN THE ENGINE's IDENTITY (SHADER-SYSTEM-1 stage 6a; WARM-SHARE-1 call 4).

Before it the ray tier's `.comp` shaders were in no engine id: JAHSHAKA_ENGINE_BUILD_ID hashes the engine's C++ and four
headers, so a changed ray shader moved neither the startup pass's key nor the key the warm-up trusts, and its new pipelines
compiled in the editor's frames. irisgl/cmake/EngineBuildId.cmake writes JAHSHAKA_RAY_SPIRV_ID over the generated SPIR-V
headers (irisgl/engine/CMakeLists.txt's JAH_RAY_SPIRV_HEADERS, the list JahshakaRayQuerySpv builds) and
cmake/startup_pass_id.py folds it into JAH_STARTUP_PASS_CODE_ID beside the engine's build id.

THE LAWS (CPU; cmake -P on toy files, the build's own build.ninja and header; no card, no display):
  1  one byte of one toy SPIR-V header moves JAHSHAKA_RAY_SPIRV_ID, and ONLY it (the five other ids unmoved)
  2  the CONTROL: the same bytes under another directory give the same ids (content, never a path: #154)
  3  no ray tier (an empty list) writes "none"
  4  the startup pass's key folds it: engine_terms of two headers that differ only in the ray id differ (equal ones equal)
  5  the build's wiring: every generated *_spv.h of the build is in the build-id command's list AND an input of its edge
     (ninja re-runs it when one moves), and the build's header holds the id of exactly those bytes (not "none")
usage: ray_spirv_id.py <source dir> <build dir> <cmake>"""
import hashlib
import os
import re
import shutil
import subprocess
import sys
import tempfile

src, build, cmake = os.path.realpath(sys.argv[1]), sys.argv[2], sys.argv[3]
script = os.path.join(src, "irisgl", "cmake", "EngineBuildId.cmake")
fails = []


def law(ok, text):
    print(("  ok   " if ok else "  FAIL ") + text, flush=True)
    if not ok:
        fails.append(text)


def ids_of(header_text):
    return dict(re.findall(r'#define (\w+) "([^"]*)"', header_text))


def run_script(root, ray):
    """EngineBuildId.cmake over a toy tree under `root`: two C++ sources, one cache source, a compile-path list with its
    three groups, and the ray headers `ray` (paths); returns the header's ids."""
    files = {}
    for rel, text in (("eng/a.cpp", "int a;\n"), ("eng/b.h", "int b;\n"), ("eng/cache.cpp", "int c;\n"),
                      ("fork/m.cpp", "m\n"), ("fork/p.cpp", "p\n"), ("fork/c.cpp", "c\n")):
        p = os.path.join(root, rel)
        os.makedirs(os.path.dirname(p), exist_ok=True)
        open(p, "w").write(text)
        files[rel] = p
    lst = os.path.join(root, "ShaderCompilePath.txt")
    open(lst, "w").write("[microcode]\nm.cpp\n[preprocessor]\np.cpp\n[compute]\nc.cpp\n")
    out = os.path.join(root, "out", "id.h")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    r = subprocess.run([cmake, "-DJAH_BUILD_ID_SOURCES=" + files["eng/a.cpp"] + "|" + files["eng/b.h"],
                        "-DJAH_BUILD_ID_CACHE_SOURCES=" + files["eng/cache.cpp"],
                        "-DJAH_RAY_SPIRV=" + "|".join(ray),
                        "-DJAH_COMPILE_PATH_LIST=" + lst, "-DJAH_COMPILE_PATH_ROOT=" + os.path.join(root, "fork"),
                        "-DJAH_BUILD_ID_OUT=" + out, "-P", script], capture_output=True, text=True)
    if r.returncode != 0:
        sys.exit("ray_spirv_id.py: EngineBuildId.cmake failed: " + r.stderr[-400:])
    return ids_of(open(out).read())


def toy_spirv(root, name, word):
    p = os.path.join(root, "rq", name)
    os.makedirs(os.path.dirname(p), exist_ok=True)
    open(p, "w").write("\t// 1116.2.0\n\t #pragma once\nconst uint32_t k%s[] = {\n\t0x07230203,0x%08x,0x0008000b\n};\n"
                       % (name.split(".")[0], word))
    return p


tmp = tempfile.mkdtemp(prefix="ray_spirv_id.")
try:
    a = os.path.join(tmp, "a")
    base = run_script(a, [toy_spirv(a, "rq_one_spv.h", 0x00010400), toy_spirv(a, "rq_two_spv.h", 0x00010500)])
    law(base.get("JAHSHAKA_RAY_SPIRV_ID", "none") not in ("", "none"),
        "1  the header carries JAHSHAKA_RAY_SPIRV_ID over a two-header list (%s)" % base.get("JAHSHAKA_RAY_SPIRV_ID"))
    moved = run_script(a, [toy_spirv(a, "rq_one_spv.h", 0x00010400), toy_spirv(a, "rq_two_spv.h", 0x00010501)])
    law(moved.get("JAHSHAKA_RAY_SPIRV_ID") != base.get("JAHSHAKA_RAY_SPIRV_ID"),
        "1  one byte of rq_two_spv.h moves JAHSHAKA_RAY_SPIRV_ID (%s -> %s)"
        % (base.get("JAHSHAKA_RAY_SPIRV_ID"), moved.get("JAHSHAKA_RAY_SPIRV_ID")))
    others = sorted(k for k in base if k != "JAHSHAKA_RAY_SPIRV_ID")
    law(len(others) == 5 and all(moved.get(k) == base[k] for k in others),
        "1  ...and only it: %s unmoved" % ", ".join(others))
    b = os.path.join(tmp, "elsewhere", "b")
    again = run_script(b, [toy_spirv(b, "rq_one_spv.h", 0x00010400), toy_spirv(b, "rq_two_spv.h", 0x00010500)])
    law(again == base, "2  CONTROL: the same bytes under another directory give every id unmoved")
    none = run_script(os.path.join(tmp, "c"), [])
    law(none.get("JAHSHAKA_RAY_SPIRV_ID") == "none", "3  no ray tier writes \"none\" (%s)" % none.get("JAHSHAKA_RAY_SPIRV_ID"))

    # 4: the startup pass's key folds it (cmake/startup_pass_id.py's own function, not a copy)
    code = open(os.path.join(src, "cmake", "startup_pass_id.py")).read()
    ns = {"__name__": "spid"}
    argv = sys.argv
    sys.argv = ["x", src, build, "/dev/null", "/dev/null", "/dev/null", "/dev/null"]   # (it reads its arguments at import)
    try:
        exec(compile(code[:code.index("\ndeps = compiler_deps()")], "startup_pass_id.py", "exec"), ns)
    finally:
        sys.argv = argv

    def header(path, ray):
        open(path, "w").write('#define JAHSHAKA_ENGINE_BUILD_ID "e1"\n#define JAHSHAKA_RAY_SPIRV_ID "%s"\n' % ray)
        return path
    h1 = ns["engine_terms"](header(os.path.join(tmp, "h1.h"), "r1"))
    h2 = ns["engine_terms"](header(os.path.join(tmp, "h2.h"), "r2"))
    h3 = ns["engine_terms"](header(os.path.join(tmp, "h3.h"), "r1"))
    law(h1 != h2 and h1 == h3, "4  JAH_STARTUP_PASS_CODE_ID's engine terms move with the ray id alone, equal at an equal one")
finally:
    shutil.rmtree(tmp, ignore_errors=True)

# 5: the build's own wiring
gen = os.path.join(build, "engine", "generated")
rq = os.path.join(gen, "rayquery")
spv = sorted(f for f in os.listdir(rq) if f.endswith("_spv.h")) if os.path.isdir(rq) else []
ninja = open(os.path.join(build, "build.ninja"), errors="replace").read()
m = re.search(r"^build [^\n]*jahshaka_engine_build_id\.h[^\n]*: CUSTOM_COMMAND ([^\n]*)\n(?:  [^\n]*\n)*?  COMMAND = ([^\n]*)",
              ninja, re.M)
if not m:
    law(False, "5  build.ninja has the build id's edge")
else:
    inputs, command = m.group(1), m.group(2)
    arg = re.search(r"-DJAH_RAY_SPIRV=([^ \"]*)", command.replace('"', ""))
    listed = arg.group(1).split("|") if arg and arg.group(1) else []
    names = [os.path.basename(p) for p in listed]
    law(bool(spv) == bool(listed) and sorted(names) == spv,
        "5  the build-id command's list is every generated *_spv.h of the build (%d listed, %d generated%s)"
        % (len(names), len(spv), "" if sorted(names) == spv else ": missing " + ",".join(sorted(set(spv) - set(names)))))
    law(all(("rayquery/" + n) in inputs for n in spv), "5  ...each an input of the build id's edge (ninja re-hashes on a move)")
    have = ids_of(open(os.path.join(gen, "jahshaka_engine_build_id.h")).read()).get("JAHSHAKA_RAY_SPIRV_ID")
    blob = "".join(hashlib.sha256(open(p, "rb").read()).hexdigest() + ";" for p in listed)
    want = hashlib.sha256(blob.encode()).hexdigest() if listed else "none"
    law(have == want and (have != "none") == bool(spv),
        "5  the build's header holds the id of those bytes (%s; recomputed %s)" % (have, want))

print("source.ray_spirv_id: " + ("PASSED" if not fails else "FAILED (%d)" % len(fails)))
sys.exit(1 if fails else 0)
