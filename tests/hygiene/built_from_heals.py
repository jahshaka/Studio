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
THE TARGETS IT CLAIMS (ENGINE-HEADERS-1 stage 0, build by reach: `targets=`, cmake/BuiltFrom.cmake), two out-of-`all` targets A
and B that both depend on one "library" L (lib.txt), each built by a `--target` pass and stamped by jah_built_reach:
  5 after a commit, a by-reach pass of A claims A alone (the whole build's `all` was another state's); a second pass of B at the
    same state ADDS B (A,B: the last --code call's targets stay servable);
  6 a commit, then a pass of B: B alone - A, built at the old tip, is never claimed at the new one;
  7 L's source EDITED between two passes at one tip (A not rebuilt, its output on disk from the old L): the pass of B claims B
    alone - A's claim falls with the content; L only TOUCHED (its bytes equal): A stays claimed (its output IS from these bytes);
  8 a whole build at an equal state keeps the reach's targets beside `all`; 9 jah_built_reach without JAH_BUILT_REACH refuses;
  11 a claim never outlives a build the stamp did not see: A built by hand at an edited state, the edit reverted - the next
    pass does not claim A (its output is from the edit); a by-reach pass claims it again;
  12 `all` the same way: a DEFAULT target built by hand at an edited state, the edit reverted - a by-reach pass does not carry the
    old `all`; a whole pass claims it again (and says nothing about its own rebuilds);
  10 THE RECORDS (scripts/gate_runlog.py coverage + stale_build, scripts/row_targets.py): after 6 (A built at the old tip, B at
    this one) a row needing A is `built.uncovered` [A] - its record is never its tip's - and a row needing B is covered.
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


def reach(*targets):
    """A BY-REACH PASS as util.build makes one: `--target <T>`, then - only on its exit 0 - jah_built_reach in its own call."""
    r = subprocess.run(["cmake", "--build", B, "--target", *targets], capture_output=True, text=True)
    if r.returncode:
        return r.returncode, r.stdout + r.stderr, {}
    env = dict(os.environ, JAH_BUILT_REACH=",".join(targets))
    r2 = subprocess.run(["cmake", "--build", B, "--target", "jah_built_reach"], capture_output=True, text=True, env=env)
    stamp = dict(l.split("=", 1) for l in open(os.path.join(B, "BUILT_FROM")).read().splitlines() if "=" in l)
    return r2.returncode, r.stdout + r2.stdout + r2.stderr, stamp


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
        "add_custom_target(app ALL DEPENDS ${CMAKE_BINARY_DIR}/app.txt ${CMAKE_BINARY_DIR}/media.txt)\n"
        # A and B: out of `all`, each "linking" L (a copy of lib.txt) - the shape of two test executables and their library
        "add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/L.txt COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/lib.txt "
        "${CMAKE_BINARY_DIR}/L.txt DEPENDS ${CMAKE_SOURCE_DIR}/lib.txt)\n"
        "add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/A.txt COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/L.txt "
        "${CMAKE_BINARY_DIR}/A.txt DEPENDS ${CMAKE_BINARY_DIR}/L.txt)\n"
        "add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/B.txt COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/L.txt "
        "${CMAKE_BINARY_DIR}/B.txt DEPENDS ${CMAKE_BINARY_DIR}/L.txt)\n"
        "add_custom_target(A DEPENDS ${CMAKE_BINARY_DIR}/A.txt)\nadd_custom_target(B DEPENDS ${CMAKE_BINARY_DIR}/B.txt)\n"
        "set_target_properties(A PROPERTIES JAH_ARTEFACT ${CMAKE_BINARY_DIR}/A.txt)\n"
        "set_target_properties(B PROPERTIES JAH_ARTEFACT ${CMAKE_BINARY_DIR}/B.txt)\n"
        "set_target_properties(app PROPERTIES JAH_ARTEFACT ${CMAKE_BINARY_DIR}/app.txt)\n"
        "enable_testing()\nadd_test(NAME ra COMMAND sh -c true)\nadd_test(NAME rb COMMAND sh -c true)\n"
        "set_tests_properties(ra PROPERTIES JAH_BUILD_TARGETS A)\nset_tests_properties(rb PROPERTIES JAH_BUILD_TARGETS B)\n"
        "include(BuiltFrom)\n")
    open(os.path.join(S, ".gitignore"), "w").write("build/\nirisgl/\n")
    open(os.path.join(S, "main.txt"), "w").write("OLD\n")
    open(os.path.join(S, "lib.txt"), "w").write("L1\n")
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
    want("4 a whole build claims `all`", st.get("targets") == "all", f"{st}")

    git(S, "commit", "-qam", "edit")
    rc, out, st = reach("A")
    want("5 after a commit, a by-reach pass of A claims A alone", rc == 0 and st.get("targets") == "A"
         and st.get("stale") == "0", f"rc {rc} {st} {out}")
    rc, out, st = reach("B")
    want("5 a second pass at the same state ADDS its target: A,B", rc == 0 and st.get("targets") == "A,B", f"rc {rc} {st}")

    open(os.path.join(S, "main.txt"), "w").write("COMMITTED\n")
    git(S, "commit", "-qam", "move")
    rc, out, st = reach("B")
    want("6 a commit, then a pass of B: B alone (A was built at the old tip)", rc == 0 and st.get("targets") == "B", f"rc {rc} {st}")
    sys.path.insert(0, os.path.join(src, "scripts"))
    import gate_runlog
    cov = gate_runlog.coverage(B)
    ua, ub = cov("ra"), cov("rb")
    tip = {"studio": st["studio"], "irisgl": st["irisgl"], "built": dict(gate_runlog.built_from(B), uncovered=ua)}
    why = gate_runlog.stale_build(tip)
    want("10 a row needing A (built at the old tip) is uncovered [A] and its record is not its tip's; a row needing B is covered",
         ua == ["A"] and ub == [] and why is not None and "did not build this row's targets (A)" in why, f"ra {ua} rb {ub} why {why}")

    open(os.path.join(S, "main.txt"), "w").write("DIRTY\n")        # dirty already: only the content can tell the two apart
    rc, out, st = reach("A")
    time.sleep(0.05)
    open(os.path.join(S, "lib.txt"), "w").write("L2\n")             # the library's source edited, A not rebuilt
    rc, out, st = reach("B")
    a = open(os.path.join(B, "A.txt")).read().strip()
    want("7 L edited between two passes at one tip: the pass of B claims B alone (A's output still from L1)",
         rc == 0 and st.get("targets") == "B" and a == "L1" and open(os.path.join(B, "B.txt")).read().strip() == "L2",
         f"rc {rc} {st} A.txt {a}")
    rc, out, st = reach("A")
    time.sleep(0.05)
    os.utime(os.path.join(S, "lib.txt"))                              # touched: the same bytes, a new mtime
    rc, out, st = reach("B")
    want("7 L only touched (its bytes equal): A stays claimed beside B", rc == 0 and st.get("targets") == "A,B", f"rc {rc} {st}")

    rc, out, st = build()
    want("8 a whole build at an equal state keeps the reach's targets beside `all`", rc == 0 and st.get("targets") == "A,B,all",
         f"rc {rc} {st}")
    r = subprocess.run(["cmake", "--build", B, "--target", "jah_built_reach"], capture_output=True, text=True,
                       env={k: v for k, v in os.environ.items() if k != "JAH_BUILT_REACH"})
    want("9 jah_built_reach without JAH_BUILT_REACH refuses (no stamp claims an unnamed pass)",
         r.returncode != 0 and "JAH_BUILT_REACH" in r.stdout + r.stderr, f"rc {r.returncode}")

    # 11 (the final read's L2) A CLAIM NEVER OUTLIVES A BUILD THE STAMP DID NOT SEE: A claimed at this state; L edited, A built BY
    # HAND (`--target A`, no stamp), L reverted to its bytes (the state equal again): the next pass must not claim A (its output
    # is from the edit); a by-reach pass of A at once after claims it again, and a pass after that keeps it
    rc, out, st = reach("A")
    want("11 A claimed at this state", rc == 0 and "A" in st.get("targets", "").split(","), f"rc {rc} {st}")
    time.sleep(0.05)
    lib = open(os.path.join(S, "lib.txt")).read()
    open(os.path.join(S, "lib.txt"), "w").write("HAND\n")
    subprocess.run(["cmake", "--build", B, "--target", "A"], capture_output=True, text=True)
    open(os.path.join(S, "lib.txt"), "w").write(lib)
    rc, out, st = build()
    a = open(os.path.join(B, "A.txt")).read().strip()
    want("11 after a hand build of A at an edited state, reverted: the next pass does NOT claim A (A.txt is from the edit)",
         rc == 0 and a == "HAND" and "A" not in st.get("targets", "").split(",") and "all" in st.get("targets", "").split(","),
         f"rc {rc} {st} A.txt {a}")
    rc, out, st = reach("A")
    rc2, out2, st2 = build()
    want("11 a by-reach pass of A claims it again, and the next whole pass keeps it (the stamp touched after A was built)",
         rc == 0 and open(os.path.join(B, "A.txt")).read().strip() == lib.strip() and "A" in st2.get("targets", "").split(","),
         f"rc {rc} {st} then {st2}")

    # 12 (the re-read's L5) `all` THE SAME WAY: a whole pass claims `all`; main.txt edited, the default target `app` built BY HAND,
    # the edit reverted: a by-reach pass at this state must not carry the old `all` (app.txt is from the edit); a whole pass
    # rebuilds it and claims `all` again
    rc, out, st = build()
    want("12 a whole pass claims `all`", rc == 0 and "all" in st.get("targets", "").split(","), f"rc {rc} {st}")
    time.sleep(0.05)
    main = open(os.path.join(S, "main.txt")).read()
    open(os.path.join(S, "main.txt"), "w").write("HAND\n")
    subprocess.run(["cmake", "--build", B, "--target", "app"], capture_output=True, text=True)
    open(os.path.join(S, "main.txt"), "w").write(main)
    rc, out, st = reach("B")
    app = open(os.path.join(B, "app.txt")).read().strip()
    want("12 after a hand build of a DEFAULT target at an edited state, reverted: a by-reach pass does NOT carry `all`",
         rc == 0 and app == "HAND" and "all" not in st.get("targets", "").split(",") and "B" in st.get("targets", "").split(","),
         f"rc {rc} {st} app.txt {app}")
    rc, out, st = build()
    want("12 a whole pass rebuilds it and claims `all` again", rc == 0 and open(os.path.join(B, "app.txt")).read() == main
         and "all" in st.get("targets", "").split(",") and "no longer claimed" not in out, f"rc {rc} {st}")
finally:
    shutil.rmtree(T, ignore_errors=True)
print("tooling.built_from_heals: " + ("PASS" if not fails else f"FAIL ({len(fails)})"), flush=True)
sys.exit(1 if fails else 0)
