#!/bin/bash
# warm-app-cache.sh <build-dir> [--check] [--tests-dir <dir>] — THE TREE'S SHARED APP SHADER CACHE, WARM AT ITS KEY
# (URGENT #52, 2026-10-24).
#
# WHY. Every app row boots on the tree's ONE shared app cache (<build>/tests/.shadercache; tests/support/fresh_home.sh
# --warm) and a GREEN run promotes what it compiled. Under the 60 s cap (URGENT #48) a row whose boot compiles cold past
# the cap times out, and a timed-out run promotes nothing: after an input of the cache's key moves (a fork bump, an
# engine rebuild, a media edit, a driver), no row can warm the cache for itself (the photon-7r merge's master check:
# app.engine_selftest_validation and avatar.responsive timed out cold, app.test_needs_boot ran 56 s of its 60).
# This is not a test: it boots the app ONCE, UNCAPPED, when the key moved, and leaves the shared cache warm for the rows.
#
# THE KEY: what the engine's shader-cache layer keys are made of (irisgl/engine/src/OgreShaderCache.cpp configure() and
# load()), read from the build without booting - the engine build id (<build>/engine/generated/
# jahshaka_engine_build_id.h: the engine's own sources, kCacheFormat among them), the fork commit the engine was compiled
# against (compile_commands.json's JAHSHAKA_OGRE_FORK_COMMIT), the staged Hlms and Photon media (<build>/bin/media, the
# directories the engine hashes), glslang's build_info.h, the build type, and the GPU (name and driver). sha256 of
# those, its first 16 hex digits.
# WARM AT THE KEY = the stamp <tests-dir>/.shadercache.warm-key, written after this script's own boot was promoted,
# names this key AND the shared cache's manifest still carries the layer keys it recorded then (a later promotion at
# the same key only grows the union; one at another key replaced it).
#
#   (no flag)        warm: prints "warm at <key>", exits 0 at once. Cold: prints "cold at <key>: <why>", then boots the
#                    app once through fresh_home.sh --warm (<tests-dir>/warm-app-cache-home: the shared cache seeded
#                    in, a green boot's grown cache promoted by fresh_home's own rules), under scripts/gpu-exclusive.sh
#                    (re-entrant under a holder), on the caller's DISPLAY or its own Xvfb (60-99, 1920x1080x24, killed by
#                    its recorded pid); prints its wall and the cache's size; exits 0 when the boot passed and was
#                    promoted, nonzero on a failed boot (its log kept under <tests-dir>/.evidence by fresh_home).
#   --check          the answer alone, never a boot: exit 0 warm, 3 cold.
#   --tests-dir <d>  the shared cache's directory (default <build>/tests) - the guard row's own scratch.
# THE BOOT: `Jahshaka --engine-selftest` - the app's own startup warm-up gate (the born World's set: Epic, every
# feature) PLUS the self-test's six poses (the default scene, fixture B with the rays on and off, the motion pose, the
# mover) - a superset of the warm-up path, and the set of the row the cap broke first (app.engine_selftest_validation).
# A row booting a test tier with features off (JAHSHAKA_TEST_NEEDS) compiles its own variants once more; what it adds is
# promoted by its own green run.
set -u
BUILD=""; CHECK=0; TESTS=""
while [ $# -gt 0 ]; do
    case "$1" in
        --check) CHECK=1; shift ;;
        --tests-dir) TESTS="${2:?--tests-dir needs a directory}"; shift 2 ;;
        -*) echo "warm-app-cache: unknown flag $1" >&2; exit 64 ;;
        *) BUILD="$1"; shift ;;
    esac
done
[ -n "$BUILD" ] && [ -d "$BUILD" ] || { echo "usage: warm-app-cache.sh <build-dir> [--check] [--tests-dir <dir>]" >&2; exit 64; }
BUILD=$(cd "$BUILD" && pwd)
HERE=$(cd "$(dirname "$0")" && pwd)
SRC=$(cd "$HERE/.." && pwd)
TESTS=${TESTS:-$BUILD/tests}
mkdir -p "$TESTS"
SHARED="$TESTS/.shadercache"
STAMP="$TESTS/.shadercache.warm-key"

# ---- THE KEY ------------------------------------------------------------------------------------------------------
key_inputs() {
    local id="$BUILD/engine/generated/jahshaka_engine_build_id.h"
    [ -f "$id" ] || { echo "warm-app-cache: no engine build id at $id (build the tree first)" >&2; return 1; }
    echo "build-id $(sha256sum < "$id" | cut -c1-64)"
    echo "fork $(grep -o -m1 'JAHSHAKA_OGRE_FORK_COMMIT=[^ ]*' "$BUILD/compile_commands.json" 2>/dev/null | grep -oE '[0-9a-f]{40}' | head -1)"
    local d
    for d in Hlms Photon; do
        [ -d "$BUILD/bin/media/$d" ] || { echo "warm-app-cache: no staged media $BUILD/bin/media/$d" >&2; return 1; }
        echo "media-$d $(cd "$BUILD/bin/media/$d" && find . -type f -print0 | LC_ALL=C sort -z | xargs -0 sha256sum | sha256sum | cut -c1-64)"
    done
    echo "glslang $( [ -f /usr/include/glslang/build_info.h ] && sha256sum < /usr/include/glslang/build_info.h | cut -c1-64 || echo none)"
    echo "type $(sed -n 's/^CMAKE_BUILD_TYPE:STRING=//p' "$BUILD/CMakeCache.txt" | head -1)"
    local gpu
    gpu=$(nvidia-smi --query-gpu=name,driver_version --format=csv,noheader 2>/dev/null | head -1)
    echo "gpu ${gpu:-unknown}"
}
INPUTS=$(key_inputs) || exit 2
KEY=$(printf '%s\n' "$INPUTS" | sha256sum | cut -c1-16)
layer_keys() {   # the shared cache's per-layer keys (its manifest's last column), sorted, one line
    [ -f "$1/cache-manifest.txt" ] && awk '$1 == "file" { print $NF }' "$1/cache-manifest.txt" | LC_ALL=C sort -u | tr '\n' ' '
}
why=""
if [ ! -f "$STAMP" ]; then why="no warm-up recorded"
elif [ "$(sed -n 1p "$STAMP")" != "$KEY" ]; then why="the key moved (warmed at $(sed -n 1p "$STAMP"))"
elif [ ! -f "$SHARED/cache-manifest.txt" ]; then why="the shared cache is gone"
elif [ "$(sed -n 2p "$STAMP")" != "$(layer_keys "$SHARED")" ]; then why="the shared cache was replaced at another key since"
fi
if [ -z "$why" ]; then
    echo "warm at $KEY"
    exit 0
fi
echo "cold at $KEY: $why"
[ "$CHECK" = 1 ] && exit 3

# ---- THE BOOT (once, uncapped: not a test) -------------------------------------------------------------------------
BIN="$BUILD/bin/Jahshaka"
[ -x "$BIN" ] || { echo "warm-app-cache: no app at $BIN" >&2; exit 2; }
HOME_DIR="$TESTS/warm-app-cache-home"
XPID=""
cleanup() { [ -n "$XPID" ] && kill "$XPID" 2>/dev/null; }
trap cleanup EXIT
if [ -z "${DISPLAY:-}" ]; then
    for n in $(seq 60 99); do
        [ -e "/tmp/.X$n-lock" ] || [ -e "/tmp/.X11-unix/X$n" ] && continue
        Xvfb ":$n" -screen 0 1920x1080x24 -nolisten tcp > /dev/null 2>&1 &
        XPID=$!
        for _ in $(seq 50); do [ -e "/tmp/.X11-unix/X$n" ] && break; sleep 0.1; done
        if [ -e "/tmp/.X11-unix/X$n" ] && kill -0 "$XPID" 2>/dev/null; then export DISPLAY=":$n"; break; fi
        kill "$XPID" 2>/dev/null; XPID=""
    done
    [ -n "${DISPLAY:-}" ] || { echo "warm-app-cache: no free display in 60-99" >&2; exit 2; }
fi
echo "warm-app-cache: booting $BIN --engine-selftest on $DISPLAY (home $HOME_DIR)"
t0=$(date +%s)
LOG="$TESTS/warm-app-cache.log"
( cd "$BUILD/bin" && HOME="$HOME_DIR" JAHSHAKA_DATA_ROOT="$HOME_DIR/.local/share/Jahshaka" QT_QPA_PLATFORM=xcb \
      nice -n 19 "$SRC/scripts/gpu-exclusive.sh" --label warm-app-cache \
      sh "$SRC/tests/support/fresh_home.sh" --warm "$HOME_DIR" -- \
      "$BIN" --engine-selftest "$HOME_DIR/run/warm.png" ) > "$LOG" 2>&1
rc=$?
wall=$(( $(date +%s) - t0 ))
grep -E 'fresh_home.sh|^engine-selftest: .*-> (PASS|FAIL)' "$LOG" | head -5
if [ $rc -ne 0 ]; then
    echo "warm-app-cache: the boot FAILED (exit $rc) after ${wall} s - its log $LOG; the cache is not warm"
    exit 1
fi
if [ ! -f "$SHARED/cache-manifest.txt" ]; then
    echo "warm-app-cache: the boot passed but no shared cache was promoted ($SHARED) - not warm"
    exit 1
fi
printf '%s\n%s\n' "$KEY" "$(layer_keys "$SHARED")" > "$STAMP"
echo "warm-app-cache: warm at $KEY - the boot ${wall} s, the shared cache $(du -sh "$SHARED" | cut -f1)" \
     "($(sed -n 's/^shaders //p' "$SHARED/cache-manifest.txt") shaders)"
exit 0
