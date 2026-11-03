#!/usr/bin/env python3
"""startup_pass_id.py - JAH_STARTUP_PASS_CODE_ID FROM THE PASS's REAL DEPENDENCY CLOSURE (TEST-STREAMLINE-1, the stage-1 audit's
finding 5; replaces cmake/StartupPassId.cmake's one-level #include walk, which missed the gizmo's overlay material).

  startup_pass_id.py <src> <build> <engine-id header> <out header> <depfile> <list file> [<extra source>...]

THE CLOSURE: every project header the COMPILER reads for src/app/shaderbuildgate.cpp (its compile_commands.json command with
-MM: the same set ninja's deps log holds for its object), each with the source that defines it (<header>.cpp beside it), plus
the named sources the pass calls into that no header of it names (the extras, CMakeLists.txt: the gizmo's meshes and its other
handles, whose overlay materials the pass draws). The id: sha256 over every file's content in sorted path order, then the
engine's full build id. Writes the header only when the value moves, the depfile (ninja re-runs this when any file of the
closure changes) and the list (<build>/startup-pass-files.txt, read by shadercache.startup_pass_key). No tree path is hashed."""
import hashlib
import json
import os
import re
import shlex
import subprocess
import sys

src, build, idh, out, depfile, listfile = sys.argv[1:7]
extras = sys.argv[7:]
src = os.path.realpath(src)
TU = os.path.join(src, "src", "app", "shaderbuildgate.cpp")


def project(path):
    rel = os.path.relpath(os.path.realpath(path), src)
    return rel if (rel.startswith("src/") or rel.startswith("irisgl/")) and not rel.startswith("irisgl/thirdparty/") else None


def compiler_deps():
    """The project files the compiler reads for the TU (its own -MM), relative to the source root."""
    cc = json.load(open(os.path.join(build, "compile_commands.json")))
    e = next(x for x in cc if os.path.realpath(x["file"]) == os.path.realpath(TU))
    args = e.get("arguments") or shlex.split(e["command"])
    cmd, skip = [], False
    for a in args:
        if skip:
            skip = False
            continue
        if a in ("-o", "-MF", "-MT", "-MQ"):
            skip = True
            continue
        if a in ("-c", "-MD", "-MMD"):
            continue
        cmd.append(a)
    if cmd and os.path.basename(cmd[0]) == "env":            # (the tree's launcher: env PWD=<dir> ccache c++ ...)
        while cmd and ("=" in cmd[0] or os.path.basename(cmd[0]) in ("env", "ccache")):
            cmd = cmd[1:]
    elif cmd and os.path.basename(cmd[0]) == "ccache":
        cmd = cmd[1:]
    r = subprocess.run(cmd + ["-MM", "-MG"], cwd=e["directory"], capture_output=True, text=True)
    if r.returncode != 0:
        sys.exit("startup_pass_id.py: the compiler's -MM failed: " + r.stderr[-400:])
    words = r.stdout.replace("\\\n", " ").split()[1:]
    return sorted({p for p in (project(os.path.join(e["directory"], w)) for w in words) if p})


deps = compiler_deps()
files = set(deps)
for h in deps:
    if h.endswith((".h", ".hpp")):
        cpp = re.sub(r"\.(h|hpp)$", ".cpp", h)
        if os.path.isfile(os.path.join(src, cpp)):
            files.add(cpp)
for x in extras:
    rel = project(x)
    if rel and os.path.isfile(os.path.join(src, rel)):
        files.add(rel)
files = sorted(files)
h = hashlib.sha256()
for f in files:
    h.update(f.encode() + b"\0" + hashlib.sha256(open(os.path.join(src, f), "rb").read()).digest())
eng = next((l for l in open(idh) if l.startswith("#define JAHSHAKA_ENGINE_BUILD_ID ")), None)
if not eng:
    sys.exit(f"startup_pass_id.py: no JAHSHAKA_ENGINE_BUILD_ID in {idh}")
h.update(b"engine " + eng.strip().encode())
text = ('// GENERATED - do not edit, do not commit. See cmake/startup_pass_id.py.\n#pragma once\n'
        f'#define JAH_STARTUP_PASS_CODE_ID "{h.hexdigest()}"\n')
if not os.path.isfile(out) or open(out).read() != text:
    open(out, "w").write(text)
with open(depfile, "w") as d:
    d.write(out.replace(" ", "\\ ") + ": " + " ".join(os.path.join(src, f).replace(" ", "\\ ") for f in files) + " " + idh + "\n")
with open(listfile, "w") as l:
    l.write("".join(f + "\n" for f in files))
print(f"startup_pass_id.py: {len(files)} files ({len(deps)} the compiler reads, their sources, {len(extras)} named)")
