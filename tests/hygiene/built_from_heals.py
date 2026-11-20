#!/usr/bin/env python3
"""tooling.built_from_heals (URGENT #187) - A SOURCE NINJA COULD NOT SEE IS HEALED, NEVER STAMPED AS BUILT: a toy CMake + ninja
project (a copy rule stands for a compile: ninja's mtime rule is the same) carrying the tree's own cmake/BuiltFrom.cmake and
cmake/built_from.py, with a toy irisgl and fork beside it. Each case is a law of the stamp:
  1 a clean tree: stale=0, no loud line, the stamp's content id stable across a no-op pass;
  2 a source restored with an OLD mtime (cp -p - tar xf does the same): ninja runs nothing, the stamp prints the one loud
    line, touches the file and stamps stale=1; the NEXT pass rebuilds it (the output is the new content) and stamps stale=0
    (red on the old stamp: the output stays old and the stamp says nothing - spikes/v2-lane-f/repro-187.sh);
  3 a FORK file restored with an old mtime: the line names build-ogre.sh, stale_fork=1, and it STAYS stale over a Studio
    pass until the install's BUILT_FROM is newer than the touch;
  4 an ordinary edit (a new mtime) rebuilds in one pass, stale=0.
No GPU, no display; seconds. usage: built_from_heals.py <source dir>"""
import os
import shutil
import subprocess
import sys
import tempfile
import time

src = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", ".."))
T = tempfile.mkdtemp(prefix="built-from-heals-")
fails = []


def want(name, cond, why=""):
    print(("ok   " if cond else "FAIL ") + name + ("" if cond else f" ({why})"), flush=True)
    if not cond:
        fails.append(name)


def git(d, *a):
    return subprocess.run(["git", "-c", "user.name=jahshaka", "-c", "user.email=jahshaka@gmail.com", "-C", d, *a],
                          capture_output=True, text=True, check=True).stdout


def build():
    r = subprocess.run(["cmake", "--build", B], capture_output=True, text=True)
    stamp = dict(l.split("=", 1) for l in open(os.path.join(B, "BUILT_FROM")).read().splitlines() if "=" in l)
    return r.returncode, r.stdout + r.stderr, stamp


def restore(path, text):
    """`text` into `path` the way an archive's restore does: the content new, the mtime an old one (2001)."""
    tmp = os.path.join(T, "restore.tmp")
    open(tmp, "w").write(text)
    os.utime(tmp, ns=(1_000_000_000 * 10 ** 9, 1_000_000_000 * 10 ** 9))
    shutil.copy2(tmp, path)


try:
    S, B = os.path.join(T, "src"), os.path.join(T, "src", "build")
    F = os.path.join(S, "irisgl", "thirdparty", "ogre-next")
    INST = os.path.join(S, "irisgl", "thirdparty", "ogre-next-install")
    os.makedirs(os.path.join(S, "cmake")); os.makedirs(os.path.join(F, "Media")); os.makedirs(INST)
    for f in ("BuiltFrom.cmake", "built_from.py"):
        shutil.copy2(os.path.join(src, "cmake", f), os.path.join(S, "cmake", f))
    open(os.path.join(S, "CMakeLists.txt"), "w").write(
        "cmake_minimum_required(VERSION 3.20)\nproject(toy NONE)\nfind_package(Python3 COMPONENTS Interpreter REQUIRED)\n"
        "list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)\n"
        "set(OGRE_NEXT_PREFIX ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/ogre-next-install)\n"
        "add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/app.txt COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/main.txt "
        "${CMAKE_BINARY_DIR}/app.txt DEPENDS ${CMAKE_SOURCE_DIR}/main.txt)\n"
        "add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/media.txt COMMAND ${CMAKE_COMMAND} -E copy "
        "${CMAKE_SOURCE_DIR}/irisgl/thirdparty/ogre-next/Media/piece.txt ${CMAKE_BINARY_DIR}/media.txt "
        "DEPENDS ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/ogre-next/Media/piece.txt)\n"
        "add_custom_target(app ALL DEPENDS ${CMAKE_BINARY_DIR}/app.txt ${CMAKE_BINARY_DIR}/media.txt)\ninclude(BuiltFrom)\n")
    open(os.path.join(S, ".gitignore"), "w").write("build/\nirisgl/\n")
    open(os.path.join(S, "main.txt"), "w").write("OLD\n")
    open(os.path.join(F, "Media", "piece.txt"), "w").write("piece OLD\n")
    open(os.path.join(INST, "BUILT_FROM"), "w").write("fork=x\n")
    git(S, "init", "-q"); git(S, "add", "-A"); git(S, "commit", "-qm", "toy")
    for d in (os.path.join(S, "irisgl"), F):
        git(d, "init", "-q")
    open(os.path.join(S, "irisgl", ".gitignore"), "w").write("thirdparty/ogre-next/\nthirdparty/ogre-next-install/\n")
    git(os.path.join(S, "irisgl"), "add", "-A"); git(os.path.join(S, "irisgl"), "commit", "-qm", "toy")
    git(F, "add", "-A"); git(F, "commit", "-qm", "toy")
    subprocess.run(["cmake", "-S", S, "-B", B, "-G", "Ninja"], capture_output=True, check=True)
    rc, out, st = build()
    want("1 the first pass seeds the map: rc 0, stale=0, a content id", rc == 0 and st.get("stale") == "0"
         and len(st.get("content", "")) == 16, f"rc {rc} {st}")
    t = time.perf_counter()
    rc, out, st2 = build()
    dt = time.perf_counter() - t
    want(f"1 a clean no-op pass: stale=0, no loud line, the same content id ({dt:.2f} s the whole pass)",
         rc == 0 and st2.get("stale") == "0" and "STALE" not in out and st2.get("content") == st.get("content"), f"{st2} {out}")

    restore(os.path.join(S, "main.txt"), "NEW\n")
    rc, out, st = build()
    app = open(os.path.join(B, "app.txt")).read().strip()
    want("2 a cp -p restore: ninja copies nothing (the output still OLD), the loud line names the file, stale=1",
         rc == 0 and app == "OLD" and "1 file(s) changed with an old mtime, touched: main.txt" in out
         and "this pass is STALE - build again" in out and st.get("stale") == "1" and st.get("stale_files") == "main.txt"
         and "build-ogre.sh" not in out, f"app {app} {st} {out}")
    rc, out, st = build()
    app = open(os.path.join(B, "app.txt")).read().strip()
    want("2 the next pass rebuilds it: the output NEW, stale=0, no loud line", rc == 0 and app == "NEW"
         and st.get("stale") == "0" and "STALE" not in out, f"app {app} {st} {out}")

    time.sleep(0.05)
    restore(os.path.join(F, "Media", "piece.txt"), "piece NEW\n")
    rc, out, st = build()
    want("3 a FORK file restored: the line names build-ogre.sh, stale=1, stale_fork=1",
         "irisgl/thirdparty/ogre-next/Media/piece.txt" in out and "build-ogre.sh" in out and st.get("stale") == "1"
         and st.get("stale_fork") == "1", f"{st} {out}")
    rc, out, st = build()
    want("3 a Studio pass without build-ogre.sh: still STALE, build-ogre.sh named", "STALE" in out and "build-ogre.sh" in out
         and st.get("stale_fork") == "1", f"{st} {out}")
    time.sleep(0.05)
    open(os.path.join(INST, "BUILT_FROM"), "w").write("fork=y\n")       # build-ogre.sh ran after the touch
    rc, out, st = build()
    want("3 after the install's stamp is newer than the touch: stale=0", st.get("stale") == "0" and "STALE" not in out
         and open(os.path.join(B, "media.txt")).read().strip() == "piece NEW", f"{st} {out}")

    time.sleep(0.05)
    open(os.path.join(S, "main.txt"), "w").write("EDITED\n")
    rc, out, st = build()
    want("4 an ordinary edit rebuilds in one pass, stale=0", open(os.path.join(B, "app.txt")).read().strip() == "EDITED"
         and st.get("stale") == "0" and "STALE" not in out, f"{st} {out}")
finally:
    shutil.rmtree(T, ignore_errors=True)
print("tooling.built_from_heals: " + ("PASS" if not fails else f"FAIL ({len(fails)})"), flush=True)
sys.exit(1 if fails else 0)
