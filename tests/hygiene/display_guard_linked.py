#!/usr/bin/env python3
"""source.display_guard_linked - EVERY TEST BINARY CARRIES THE DISPLAY GUARD (DEV_ERRORS 37 + 38, 2026-11-22;
tests/support/display_guard.cpp, linked by tests/cmake/display_guard.cmake's walk). CPU only, no display.
  (A) every ELF executable a ctest row runs from inside the build (ctest's own listing, --show-only=json-v1: never a walk of
      the build tree) carries the guard's section `jah_display_guard` (readelf -S: the headers only) - except the app and
      jahlab (not test targets: they run through rig.sh or the owner's launch) and the ONE negative control,
      display_guard_probe_unguarded, which must NOT carry it (source.display_guard_toy's control); red with every binary named;
  (B) THE WALK REACHES A TARGET ADDED LATER: a scratch project that includes the module and defines executables in a NEW
      subdirectory and a sub-subdirectory, with no line naming the guard, builds them guarded - their section present, and
      run with DISPLAY=:0 and WAYLAND_DISPLAY set (a main that only prints its environment: it opens nothing) they see both
      unset, QT_QPA_PLATFORM=offscreen.
usage: display_guard_linked.py <source dir> <build dir>"""
import json, os, re, shutil, subprocess, sys, tempfile

SRC, BUILD = os.path.realpath(sys.argv[1]), os.path.realpath(sys.argv[2])
NOT_TESTS = {"Jahshaka", "jahlab"}
CONTROL = "display_guard_probe_unguarded"
fails = 0


def want(ok, what):
    global fails
    print(("  ok   " if ok else "  FAIL ") + what, flush=True)
    fails += 0 if ok else 1


def guarded(exe):
    out = subprocess.run(["readelf", "-S", "-W", exe], capture_output=True, text=True).stdout
    return re.search(r"\]\s+jah_display_guard\s", out) is not None


# (A)
env = {k: v for k, v in os.environ.items() if k not in ("DISPLAY", "WAYLAND_DISPLAY")}
listing = json.loads(subprocess.run(["ctest", "--show-only=json-v1"], cwd=BUILD, env=env, capture_output=True,
                                    text=True, check=True).stdout)
exes = {}
for t in listing["tests"]:
    for w in t.get("command", []):
        if not (os.path.isabs(w) and os.path.isfile(w)):
            continue
        r = os.path.realpath(w)
        if not r.startswith(BUILD + "/") or os.path.basename(r) in NOT_TESTS:
            continue
        with open(r, "rb") as f:
            if f.read(4) != b"\x7fELF":
                continue
        exes.setdefault(r, t["name"])
bare = sorted(e for e in exes if os.path.basename(e) != CONTROL and not guarded(e))
control = [e for e in exes if os.path.basename(e) == CONTROL]
want(len(exes) > 0 and not bare, "(A) %d test binaries ctest runs, every one guarded%s"
     % (len(exes), "" if not bare else " - %d are NOT (binary: a row that runs it):\n%s" % (len(bare), "\n".join(
         "         %s: %s" % (os.path.relpath(e, BUILD), exes[e]) for e in bare))))
want(len(control) == 1 and not guarded(control[0]), "(A) the negative control %s is registered once and is NOT guarded"
     % CONTROL)

# (B)
cache = open(os.path.join(BUILD, "CMakeCache.txt")).read()
cxx = re.search(r"^CMAKE_CXX_COMPILER:\w+=(.*)$", cache, re.M)
with tempfile.TemporaryDirectory(prefix="display-guard-linked.") as t:
    os.makedirs(os.path.join(t, "src", "late", "deeper"))
    with open(os.path.join(t, "src", "CMakeLists.txt"), "w") as f:
        f.write("cmake_minimum_required(VERSION 3.16)\nproject(display_guard_late CXX)\n"
                "include(\"%s/tests/cmake/display_guard.cmake\")\njah_display_guard_library()\n"
                "add_subdirectory(late)\njah_display_guard_all(\"${CMAKE_CURRENT_SOURCE_DIR}\")\n" % SRC)
    main = ('#include <cstdio>\n#include <cstdlib>\nint main() { const char *n[] = {"DISPLAY", '
            '"WAYLAND_DISPLAY", "QT_QPA_PLATFORM"}; for (const char *k : n) { const char *v = std::getenv(k); '
            'std::printf("%s=%s\\n", k, v ? v : "<unset>"); } return 0; }\n')
    for d, name in (("late", "late_target"), (os.path.join("late", "deeper"), "deeper_target")):
        with open(os.path.join(t, "src", d, "main.cpp"), "w") as f:
            f.write(main)
        with open(os.path.join(t, "src", d, "CMakeLists.txt"), "w") as f:
            f.write("add_executable(%s main.cpp)\n%s" % (name, "add_subdirectory(deeper)\n" if d == "late" else ""))
    cfg = ["cmake", "-S", os.path.join(t, "src"), "-B", os.path.join(t, "b")]
    if shutil.which("ninja"):
        cfg += ["-G", "Ninja"]
    if cxx:
        cfg.append("-DCMAKE_CXX_COMPILER=" + cxx.group(1))
    c = subprocess.run(cfg, capture_output=True, text=True)
    b = c.returncode == 0 and subprocess.run(["cmake", "--build", os.path.join(t, "b")], capture_output=True,
                                             text=True).returncode == 0
    want(b, "(B) the scratch project with a new subdirectory configures and builds%s"
         % ("" if b else ": " + (c.stderr.strip().splitlines() or ["?"])[-1][:200]))
    for rel in ("late/late_target", "late/deeper/deeper_target"):
        exe = os.path.join(t, "b", rel)
        if not b or not os.path.exists(exe):
            want(False, "(B) %s built" % rel)
            continue
        r = subprocess.run([exe], env={"PATH": "/usr/bin:/bin", "DISPLAY": ":0", "WAYLAND_DISPLAY": "wayland-0"},
                           capture_output=True, text=True)
        want(guarded(exe) and "DISPLAY=<unset>" in r.stdout and "WAYLAND_DISPLAY=<unset>" in r.stdout
             and "QT_QPA_PLATFORM=offscreen" in r.stdout,
             "(B) %s (no line names the guard): guarded, DISPLAY=:0 and WAYLAND_DISPLAY unset, offscreen - "
             "got %r" % (rel, r.stdout.split()))

print("source.display_guard_linked: PASSED" if not fails else "source.display_guard_linked: FAILED (%d)" % fails, flush=True)
sys.exit(1 if fails else 0)
