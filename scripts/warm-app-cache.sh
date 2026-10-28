#!/bin/bash
# warm-app-cache.sh <build-dir> [--check] [--tests-dir <dir>] — THE TREE'S TWO SHARED SHADER CACHES, WARM AT THEIR KEY
# (URGENT #52, 2026-10-24; the engine rows' cache and the ray tier's SPIR-V in the key: PHOTON-7 11-2, 2026-10-28).
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
# jahshaka_engine_build_id.h: the engine's own sources, kCacheFormat among them), THE RAY TIER's BUILT-IN SPIR-V
# (<build>/engine/generated/rayquery/*_spv.h: compiled into the engine, not media, and outside the build id - an
# rq_reflect edit left both caches "warm" while every boot paid ~30 s building the reflection's two pipelines cold, and
# a row at the cap timed out and promoted nothing: gi.rough_reference 60 s, 2026-10-28), the fork commit the engine was compiled
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
#   --app <exe>      the app the boot runs (default <build>/bin/Jahshaka) - the guard row's fake, which compiles nothing.
#   --only app|engine  ONE cache alone (URGENT #102's tool fix, 2026-10-28): a cold tree's two boots measured 382 s + 332 s
#                    = 714 s with the driver cache off (#100) - past a builder's 10-min foreground call, which the harness
#                    then moved to the background. Each boot fits one call: `--only app`, then `--only engine`. With
#                    --check it answers for that cache alone.
# THE STAMP IS WRITTEN ONLY WHEN THE BOOT'S CACHE IS THE SHARED ONE: promoted (fresh_home.sh: "REPLACED", or its rule (2)
# silently), or nothing to promote (the boot compiled nothing the shared cache lacked). A promotion fresh_home SKIPPED -
# its rule (3), "NOT replaced" (another home promoted since this one was seeded), or its lock not had ("no promotion") -
# leaves the stamp unwritten: the cache stays cold at the key and the next call warms again (exit 0: the boot passed).
# THE BOOTS, one a cache, each only where its cache is cold:
#   app     <tests-dir>/.shadercache (the app rows', fresh_home --warm), stamp .shadercache.warm-key: `Jahshaka
#           --engine-selftest` - the app's own startup warm-up gate (the born World's set: Epic, every feature) PLUS the
#           self-test's six poses (the default scene, fixture B with the rays on and off, the motion pose, the mover) - a
#           superset of the warm-up path, and the set of the row the cap broke first (app.engine_selftest_validation).
#   engine  <tests-dir>/.shadercache-engine (the CLASS engine rows', fresh_home --warm --cache engine, tests/CMakeLists.txt
#           jah_gpu_row), stamp .shadercache-engine.warm-key: jahlab on photon-rough at Epic in Both, a few frames - the
#           ray tier's every pipeline (the gather, the reflection and its compact pass, the resolve, the temporal, the
#           filter) and the photon envs' Hlms set the lab rows share (--lab <exe>, default <build>/bin/jahlab).
# A row booting a test tier with features off (JAHSHAKA_TEST_NEEDS) compiles its own variants once more; what it adds is
# promoted by its own green run. ONE answer for both: "warm at <key>" when both are, else "cold at <key>: <the cold
# ones' whys>" (exit 3 under --check; else their boots, exit 0 when every boot passed).
set -u
BUILD=""; CHECK=0; TESTS=""; APP=""; LAB=""; ONLY=""
while [ $# -gt 0 ]; do
    case "$1" in
        --check) CHECK=1; shift ;;
        --tests-dir) TESTS="${2:?--tests-dir needs a directory}"; shift 2 ;;
        --app) APP="${2:?--app needs an executable}"; shift 2 ;;
        --lab) LAB="${2:?--lab needs an executable}"; shift 2 ;;
        --only) ONLY="${2:?--only needs app or engine}"; shift 2
                case "$ONLY" in app|engine) ;; *) echo "warm-app-cache: --only app|engine, not $ONLY" >&2; exit 64 ;; esac ;;
        -*) echo "warm-app-cache: unknown flag $1" >&2; exit 64 ;;
        *) BUILD="$1"; shift ;;
    esac
done
[ -n "$BUILD" ] && [ -d "$BUILD" ] || { echo "usage: warm-app-cache.sh <build-dir> [--check] [--tests-dir <dir>] [--app <exe>] [--lab <exe>] [--only app|engine]" >&2; exit 64; }
BUILD=$(cd "$BUILD" && pwd)
HERE=$(cd "$(dirname "$0")" && pwd)
SRC=$(cd "$HERE/.." && pwd)
TESTS=${TESTS:-$BUILD/tests}
mkdir -p "$TESTS"

# ---- THE KEY ------------------------------------------------------------------------------------------------------
key_inputs() {
    local id="$BUILD/engine/generated/jahshaka_engine_build_id.h"
    [ -f "$id" ] || { echo "warm-app-cache: no engine build id at $id (build the tree first)" >&2; return 1; }
    echo "build-id $(sha256sum < "$id" | cut -c1-64)"
    echo "rq-spirv $(cd "$BUILD/engine/generated" 2>/dev/null && find rayquery -name '*_spv.h' -print0 2>/dev/null | LC_ALL=C sort -z | xargs -0 -r sha256sum | sha256sum | cut -c1-64)"
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
cold_why() {   # <shared> <stamp>: empty when warm at the key, else why not
    if [ ! -f "$2" ]; then echo "no warm-up recorded"
    elif [ "$(sed -n 1p "$2")" != "$KEY" ]; then echo "the key moved (warmed at $(sed -n 1p "$2"))"
    elif [ ! -f "$1/cache-manifest.txt" ]; then echo "the shared cache is gone"
    elif [ "$(sed -n 2p "$2")" != "$(layer_keys "$1")" ]; then echo "the shared cache was replaced at another key since"
    fi
}
APP_SHARED="$TESTS/.shadercache"; APP_STAMP="$TESTS/.shadercache.warm-key"
ENG_SHARED="$TESTS/.shadercache-engine"; ENG_STAMP="$TESTS/.shadercache-engine.warm-key"
app_why=$(cold_why "$APP_SHARED" "$APP_STAMP")
eng_why=$(cold_why "$ENG_SHARED" "$ENG_STAMP")
# --only: the other cache is not this call's (its state is not read as cold, its boot never runs)
[ "$ONLY" = engine ] && app_why=""
[ "$ONLY" = app ] && eng_why=""
if [ -z "$app_why" ] && [ -z "$eng_why" ]; then
    echo "warm at $KEY"
    exit 0
fi
why=""
[ -n "$app_why" ] && why="$app_why"
[ -n "$eng_why" ] && why="${why:+$why; }the engine rows' cache: $eng_why"
echo "cold at $KEY: $why"
[ "$CHECK" = 1 ] && exit 3

# ---- THE BOOTS (once each, uncapped: not a test) -------------------------------------------------------------------
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
# boot <name> <shared> <stamp> <home> <log> <command...>: the boot through fresh_home (its --warm args in FH_ARGS), the
# stamp only on a promotion; 0 passed (stamped or not), 1 failed
boot() {
    local name=$1 shared=$2 stamp=$3 home=$4 log=$5; shift 5
    echo "warm-app-cache: booting the $name cache: $1 on $DISPLAY (home $home)"
    local t0 rc wall skipped
    t0=$(date +%s)
    ( cd "$BUILD/bin" && HOME="$home" JAHSHAKA_DATA_ROOT="$home/.local/share/Jahshaka" QT_QPA_PLATFORM=xcb \
          nice -n 10 "$SRC/scripts/gpu-exclusive.sh" --label "warm-app-cache ($name)" \
          sh "$SRC/tests/support/fresh_home.sh" $FH_ARGS "$home" -- "$@" ) > "$log" 2>&1
    rc=$?
    wall=$(( $(date +%s) - t0 ))
    grep -E 'fresh_home.sh|^engine-selftest: .*-> (PASS|FAIL)|^LAB (END|FAILED)' "$log" | head -5
    if [ $rc -ne 0 ]; then
        echo "warm-app-cache: the $name boot FAILED (exit $rc) after ${wall} s - its log $log; the cache is not warm"
        return 1
    fi
    if [ ! -f "$shared/cache-manifest.txt" ]; then
        echo "warm-app-cache: the $name boot passed but no shared cache was promoted ($shared) - not warm"
        return 1
    fi
    skipped=$(grep -m1 -E 'fresh_home.sh: the shared cache NOT replaced|fresh_home.sh: .*no promotion' "$log")
    if [ -n "$skipped" ]; then
        echo "warm-app-cache: the $name boot passed in ${wall} s but its cache was NOT promoted (${skipped#fresh_home.sh: }) -" \
             "not stamped: the cache stays cold at $KEY and the next call warms again"
        return 0
    fi
    printf '%s\n%s\n' "$KEY" "$(layer_keys "$shared")" > "$stamp"
    echo "warm-app-cache: the $name cache warm at $KEY - the boot ${wall} s, $(du -sh "$shared" | cut -f1)" \
         "($(sed -n 's/^shaders //p' "$shared/cache-manifest.txt") shaders)"
    return 0
}
fails=0
if [ -n "$app_why" ]; then
    BIN="${APP:-$BUILD/bin/Jahshaka}"
    [ -x "$BIN" ] || { echo "warm-app-cache: no app at $BIN" >&2; exit 2; }
    H="$TESTS/warm-app-cache-home"
    FH_ARGS="--warm"
    # THE PERMUTATIONS THE BOOT GENERATES, DUMPED (URGENT #89): every Hlms vertex and pixel shader of the warm-up and the
    # self-test's poses, written by the engine (JAHSHAKA_HLMS_DEBUG_DIR, OgreEngine.cpp) into <tests>/.hlms-dump and stamped
    # with this key after a passed boot - source.shaders_spirv_valid validates them (tests/hygiene/shaders_spirv_valid.py);
    # the dump is the boot's at this key, promoted or not: the shaders are the build's
    DUMP="$TESTS/.hlms-dump"
    rm -rf "$DUMP"; mkdir -p "$DUMP"
    if boot app "$APP_SHARED" "$APP_STAMP" "$H" "$TESTS/warm-app-cache.log" \
            env "JAHSHAKA_HLMS_DEBUG_DIR=$DUMP/" "$BIN" --engine-selftest "$H/run/warm.png"; then
        echo "$KEY" > "$DUMP/.key"
    else
        fails=1
    fi
fi
if [ -n "$eng_why" ]; then
    LABX="${LAB:-$BUILD/bin/jahlab}"
    [ -x "$LABX" ] || { echo "warm-app-cache: no lab at $LABX" >&2; exit 2; }
    H="$TESTS/warm-engine-cache-home"
    FH_ARGS="--warm --cache engine"
    mkdir -p "$TESTS"
    printf 'reflections both\nframes 4\nquit\n' > "$TESTS/warm-engine-cache.lab"
    boot engine "$ENG_SHARED" "$ENG_STAMP" "$H" "$TESTS/warm-engine-cache.log" \
         env "JAHSHAKA_SHADER_CACHE_DIR=$H/.local/share/Jahshaka/shadercache" JAH_TEXTURE_CACHE=0 \
         "$LABX" --headless --no-stdin --size 960x540 --data-root "$H/run/lab" --env photon-rough --tier epic \
         --script "$TESTS/warm-engine-cache.lab" || fails=1
fi
[ $fails = 0 ] || exit 1
if [ -z "$(cold_why "$APP_SHARED" "$APP_STAMP")" ] && [ -z "$(cold_why "$ENG_SHARED" "$ENG_STAMP")" ]; then
    echo "warm-app-cache: warm at $KEY - both caches"
fi
exit 0
