#!/bin/bash
# row-env.sh <build> <row> [--run [--scratch <dir>]] - A ROW RUN BY HAND CARRIES ITS REGISTRATION (URGENT #147, 2026-11-02:
# gi.hold_after_stop.sh run bare lost its row's `fresh_home.sh --warm --cache engine` and JAHSHAKA_SHADER_CACHE_DIR, booted
# jahlab on a virgin shader cache and compiled cold under the card for 2.8 min - no price, a sibling's call queued behind it).
# A row is run by hand through `ctest -R '^<row>$'` in your own build (its ENVIRONMENT and its fixtures), or through this:
#   (no flag)        PRINTS the row as ctest registered it (ctest --show-only=json-v1): its command, its working directory,
#                    ENVIRONMENT and ENVIRONMENT_MODIFICATION, TIMEOUT, class and the fixtures it requires.
#   --run            RUNS it as ctest would: the registration's environment applied (ENVIRONMENT, then each
#                    ENVIRONMENT_MODIFICATION op in order), in its working directory, at nice 10; a GPU row (JAH_CLASS)
#                    under this tree's scripts/gpu-exclusive.sh (at once inside a holder) and on an explicit DISPLAY
#                    (never :0). A row that requires fixtures is REFUSED (their setups are ctest's: run `ctest -R`).
#   --scratch <dir>  ...and WRITES NOTHING UNDER <build>: every path under <build>/tests in the command, the working
#                    directory and the environment is mapped to <dir>/tests; a file the row names there is copied in (an
#                    input), a directory starts empty (its outputs land in <dir>); a fresh_home.sh --warm row's shared cache
#                    (<build>/tests/.shadercache[-<key>]) is COPIED with its stamps and the build's CMakeCache.txt, so the
#                    home is warm exactly as the row's is - after this script checks the build's own warm stamp against
#                    the build's key (URGENT #140's refusal, read at the real build: a cold cache is refused, never run).
# exit: the row's own code; 2 refused (no such row, fixtures, no DISPLAY, a cold cache); 64 usage.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
[ "$#" -ge 2 ] || { echo "usage: row-env.sh <build> <row> [--run [--scratch <dir>]]" >&2; exit 64; }
BUILD=$(cd "$1" && /bin/pwd -P) || exit 64; ROW=$2; shift 2
run=0 scratch=""
while [ "$#" -gt 0 ]; do
    case "$1" in
        --run) run=1; shift ;;
        --scratch) scratch="${2:?--scratch needs a directory}"; shift 2 ;;
        *) echo "row-env.sh: unknown argument $1" >&2; exit 64 ;;
    esac
done
[ -z "$scratch" ] || [ "$run" = 1 ] || { echo "row-env.sh: --scratch goes with --run" >&2; exit 64; }
js=$(ctest --test-dir "$BUILD" --show-only=json-v1 -R "^$(printf '%s' "$ROW" | sed 's/[][\.*^$+?(){}|]/\\&/g')\$" 2>/dev/null) \
    || { echo "row-env.sh: ctest --show-only failed in $BUILD" >&2; exit 2; }
exec python3 -I - "$BUILD" "$ROW" "$run" "$scratch" "$HERE" "$js" <<'PY'
import json, os, shutil, subprocess, sys
build, row, run, scratch, here, js = sys.argv[1], sys.argv[2], sys.argv[3] == "1", sys.argv[4], sys.argv[5], sys.argv[6]
tests = [t for t in json.loads(js or "{}").get("tests", []) if t["name"] == row]
if not tests:
    print("row-env.sh: no registered row %s in %s" % (row, build), file=sys.stderr); sys.exit(2)
t = tests[0]
P = {p["name"]: p["value"] for p in t.get("properties", [])}
L = lambda v: [v] if isinstance(v, str) else list(v or [])
cmd, wd = list(t.get("command") or []), P.get("WORKING_DIRECTORY") or build
env_set, env_mod, fixtures = L(P.get("ENVIRONMENT")), L(P.get("ENVIRONMENT_MODIFICATION")), L(P.get("FIXTURES_REQUIRED"))
gpu = bool(P.get("JAH_CLASS"))
print("row %s (class %s, TIMEOUT %s)" % (row, P.get("JAH_CLASS") or "-", P.get("TIMEOUT", "-")))
print("  working directory: %s" % wd)
for e in env_set: print("  ENVIRONMENT %s" % e)
for e in env_mod: print("  ENVIRONMENT_MODIFICATION %s" % e)
print("  fixtures required: %s" % (" ".join(fixtures) or "none"))
print("  command: %s" % " ".join(cmd))
sys.stdout.flush()
if not run:
    sys.exit(0)
if fixtures:
    print("row-env.sh: REFUSED - %s requires the fixtures %s, whose setups only ctest runs: `ctest --test-dir %s -R '^%s$'`"
          % (row, " ".join(fixtures), build, row), file=sys.stderr); sys.exit(2)
tdir = os.path.join(build, "tests") + os.sep
def mapped(s):
    return s.replace(tdir, os.path.join(scratch, "tests") + os.sep) if scratch else s
if scratch:
    scratch = os.path.abspath(scratch)
    st = os.path.join(scratch, "tests")
    os.makedirs(st, exist_ok=True)
    shutil.copy2(os.path.join(build, "CMakeCache.txt"), os.path.join(scratch, "CMakeCache.txt"))
    words = [str(w) for w in cmd]
    if any(os.path.basename(w) == "fresh_home.sh" for w in words) and "--warm" in words:
        key = words[words.index("--cache") + 1] if "--cache" in words else ""
        # THE BUILD's OWN WARM STAMP AGAINST ITS KEY (URGENT #140, read at the real build)
        k = subprocess.run(["bash", os.path.join(here, "warm-app-cache.sh"), build, "--key"], capture_output=True, text=True)
        cur = k.stdout.strip().splitlines()[0] if k.returncode == 0 and k.stdout.strip() else ""
        stamp = os.path.join(build, "tests", ".shadercache%s.warm-key" % ("-" + key if key else ""))
        have = open(stamp).readline().strip() if os.path.isfile(stamp) else ""
        if cur and have != cur:
            print("row-env.sh: REFUSED - this build's %s cache is not warmed (%s, the build's key %s): run `spec.sh test` or "
                  "`scripts/warm-app-cache.sh %s --only %s-off` first" % (key or "app", have or "no warm-up recorded", cur,
                  build, key or "app"), file=sys.stderr); sys.exit(2)
        name = ".shadercache%s" % ("-" + key if key else "")
        src = os.path.join(build, "tests", name)
        if os.path.isdir(src):
            shutil.copytree(src, os.path.join(st, name), symlinks=True, dirs_exist_ok=True)
        for f in os.listdir(os.path.join(build, "tests")):
            if f.startswith(name + ".") and f.endswith(("-key", ".lock")):
                shutil.copy2(os.path.join(build, "tests", f), os.path.join(st, f))
    import re
    for w in words + [wd] + env_set + env_mod:   # every path under <build>/tests a word names, inside a shell string too
        for piece in re.findall(re.escape(tdir) + r"""[^\s"'()`;|&<>:=$]*""", w):
            dst = mapped(piece)
            if os.path.isfile(piece) and not os.path.exists(dst):
                os.makedirs(os.path.dirname(dst), exist_ok=True); shutil.copy2(piece, dst)
    cmd, wd = [mapped(str(w)) for w in cmd], mapped(wd)
    env_set, env_mod = [mapped(e) for e in env_set], [mapped(e) for e in env_mod]
    os.makedirs(wd, exist_ok=True)
env = dict(os.environ)
if scratch:
    env["JAH_WARM_CHECK"] = "0"   # checked above at the real build; the scratch copy carries no key of its own
for e in env_set:
    k, _, v = e.partition("="); env[k] = v
for e in env_mod:
    k, _, rest = e.partition("="); op, _, v = rest.partition(":")
    cur = env.get(k, "")
    if op == "reset": env[k] = os.environ.get(k, "") if k in os.environ else env.pop(k, None) or ""
    elif op == "set": env[k] = v
    elif op == "unset": env.pop(k, None)
    elif op == "string_append": env[k] = cur + v
    elif op == "string_prepend": env[k] = v + cur
    elif op == "path_list_append": env[k] = cur + (os.pathsep if cur else "") + v
    elif op == "path_list_prepend": env[k] = v + (os.pathsep if cur else "") + cur
    elif op == "cmake_list_append": env[k] = cur + (";" if cur else "") + v
    elif op == "cmake_list_prepend": env[k] = v + (";" if cur else "") + cur
    else:
        print("row-env.sh: unknown ENVIRONMENT_MODIFICATION op %s" % e, file=sys.stderr); sys.exit(2)
if gpu:
    d = env.get("DISPLAY", "")
    if not d or d in (":0", ":0.0"):
        print("row-env.sh: REFUSED - a GPU row runs on an explicit DISPLAY=:NN of your own Xvfb, never :0 (got '%s')" % d,
              file=sys.stderr); sys.exit(2)
    if env.get("JAH_GPU_LOCK_HELD") != "1":
        cmd = [os.path.join(here, "gpu-exclusive.sh"), "--label", "row-env " + row] + cmd
cmd = ["nice", "-n", "10"] + cmd
print("row-env.sh: running in %s%s" % (wd, " (scratch: nothing under %s written)" % build if scratch else ""))
sys.stdout.flush()
os.chdir(wd)
os.execvpe(cmd[0], cmd, env)
PY
