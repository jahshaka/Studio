#!/bin/bash
# warm-app-cache.sh <build-dir> [--check] [--only <boot>[,...]] [--budget <s>] [--tests-dir <dir>] — THE TREE'S TWO SHARED
# SHADER CACHES, AND THE DRIVER CACHES OF THE EXES THAT USE NEITHER, WARM AT THEIR KEY
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
# load()), read from the build without booting - the shader cache's CODE id (<build>/engine/generated/
# jahshaka_engine_build_id.h's JAHSHAKA_SHADER_CACHE_CODE_ID: the container's own source, kCacheFormat in it; NOT every
# engine source since TEST-STREAMLINE-1 1a: an Hlms entry is keyed by its merged properties + pieces after every C++ hook,
# so an engine edit adds entries, never makes one wrong - irisgl/cmake/EngineBuildId.cmake), THE RAY TIER's BUILT-IN SPIR-V
# (<build>/engine/generated/rayquery/*_spv.h: compiled into the engine, not media, and outside the build id - an
# rq_reflect edit left both caches "warm" while every boot paid ~30 s building the reflection's two pipelines cold, and
# a row at the cap timed out and promoted nothing: gi.rough_reference 60 s, 2026-10-28), the fork commit the engine was compiled
# against (compile_commands.json's JAHSHAKA_OGRE_FORK_COMMIT), the staged Hlms and Photon media (<build>/bin/media, the
# directories the engine hashes), glslang's build_info.h, the build type, and the GPU (name and driver). sha256 of
# those, its first 16 hex digits.
# THE TOP-UP (TEST-STREAMLINE-1 1a (c)): the engine's FULL build id (JAHSHAKA_ENGINE_BUILD_ID) is not in the key but is
# read: an engine edit that sets a NEW property makes permutations the warm cache lacks, which the first rows would compile
# under their cap. So an -off boot warm at the key whose <shared>.engine-id names another full id runs once more as a
# TOP-UP - a warm boot on valid caches (every layer served, only the new permutations compile); its -on boot and the cold
# rows are NOT re-run (their stamps hold the key).
# WARM AT THE KEY = for an -off boot, its stamp (<shared>.warm-key, written after the boot was promoted) names this key
# AND the shared cache's manifest still carries the layer keys it recorded then (a later promotion at the same key only
# grows the union; one at another key replaced it) AND the cache passes the engine's own load check - every file its
# manifest names there at its length and hash (tests/support/shadercache_check.py; URGENT #117: a torn shared cache
# read "warm" here by its keys while every engine row seeded from it booted cold, eight timed out in turn; the -off
# boot on it boots cold and its promotion replaces it, fresh_home's rule 2); for an -on boot, its stamp (<shared>.driver-key) names this key and
# the exe as it is now (the two-boot law, below).
#
#   (no flag)        warm: prints "warm at <key>", exits 0 at once. Cold: prints "cold at <key>: <why>", then runs the cold
#                    boots named (--only; all four by default) in order, each once through fresh_home.sh --warm (the
#                    shared cache seeded in, a green boot's grown cache promoted by fresh_home's own rules), each under
#                    scripts/gpu-exclusive.sh for itself (re-entrant under a holder), on the caller's DISPLAY or its own
#                    Xvfb (60-99, 1920x1080x24, killed by its recorded pid); prints each boot's wall and the cache's size;
#                    exits 0 when every boot passed (a failed boot's log kept under <tests-dir>/.evidence by fresh_home).
#   --check          the answer alone, never a boot: exit 0 warm, 3 cold.
#   --inherit <b>    A FRESH TREE TAKES ITS PARENT's WARM CACHES AT AN EQUAL KEY (TEST-STREAMLINE-1 1a; `spec.sh new` calls it
#                    after the build): <b> is the parent tree's build dir. Its key, computed by THIS script, must equal this
#                    tree's (else exit 3, nothing taken); then each shared cache the parent holds stamped at the key, passing
#                    its own manifest (#117) and not warm here already, is taken under the parent's shared lock - by hard links
#                    (the files are never written in place: a promotion renames a new directory over the old), with its
#                    stamps and its top-up stamp; the -off boot's dump at the key; each cold row the parent primed at the key
#                    for the same registration (paths aside) and the same media by content, with its private driver cache
#                    (a *CACHE*PATH* value under the parent's build: COPIED - the driver writes it in place) and its startup seed (a directory beside its working dir
#                    holding tree-key.txt - its own freshness check still judges it). Prints what it took and what is still
#                    to warm here; boots nothing, takes no card. Exit 0 (something or nothing taken), 3 (keys differ).
#   --key            THE TREE's SHADER KEY alone (below), printed, exit 0 - its one definition, read by a row that keeps
#                    a cache of its own (shader.async_after_open.seed's stamp, URGENT #125), never a second formula.
#   --tests-dir <d>  the shared cache's directory (default <build>/tests) - the guard row's own scratch.
#   --app <exe>      the app the boot runs (default <build>/bin/Jahshaka) - the guard row's fake, which compiles nothing.
#   --cold-rows <f>  the cold rows' list cold-on runs (default <build>/tests/test-cold-rows.txt) - the guard
#                    row's fake.
#   --only <boots>   THE BOOTS THIS CALL MAY RUN, one or a comma list (URGENT #103 + #106): app-off, app-on, engine-off,
#                    engine-on, cold-on; `app` = app-off,app-on and `engine` = engine-off,engine-on (the pairs, for a hand
#                    run that accepts both waits in one call); none = all five, in that order. ONE BOOT A CALL is the form a caller
#                    with a foreground limit uses (the lead's util.warm_app_cache: four calls): a cold tree's boots measured
#                    382 s + 332 s with the driver cache off (#103), and `--only app` (both of the app's boots) ran 9 m 37 s,
#                    226 s of it the card's wait, 25 s short of the harness's 10-minute call (#106). With --check it answers
#                    for the named boots alone.
#   --budget <s>     the call's whole bound (default 1170: a 1200-s call window less 30 s — RUN IT IN THE BACKGROUND; the card is
#                    shared and one hold runs up to 10 min, so a boot's wait must cover a full hold, URGENT #130). THE CARD's WAIT IS
#                    NEVER INSIDE A BOOT's BUDGET: each boot takes the lock for itself (scripts/gpu-exclusive.sh) and waits at
#                    most the budget left less the boot's own estimate (its last measured wall, <tests-dir>/.warm-walls; the
#                    first warm of a tree the cold walls measured below), never under a tenth of the budget (at most 60 s)
#                    for the call's first boot (the smallest call there is); the card held past it = exit 75, nothing
#                    more booted - call again (once; a second 75 is a finding). A later boot that no longer fits the budget left is not started: exit 75 with
#                    the boots left named.
# EXITS: 0 warm, or every boot asked for passed; 1 a boot failed (its log named); 2 an unbuilt tree; 3 cold under --check,
# or an `-on` boot asked for without its `-off` boot's cache warm (nothing booted); 64 a bad flag; 75 the card stayed held
# or the budget ran out (call again).
# THE STAMP IS WRITTEN ONLY WHEN THE BOOT'S CACHE IS THE SHARED ONE: promoted (fresh_home.sh: "REPLACED", or its rule (2)
# silently), or nothing to promote (the boot compiled nothing the shared cache lacked). A promotion fresh_home SKIPPED -
# its rule (3), "NOT replaced" (another home promoted since this one was seeded), or its lock not had ("no promotion") -
# leaves the stamp unwritten: the cache stays cold at the key and the next call warms again (exit 0: the boot passed).
# THE TWO-BOOT LAW (URGENT #100 + #102b, 2026-10-28; each boot its own call since #106): a cache is warmed by TWO boots of
# the SAME exe, in order, each only where it is cold:
#   <cache>-off  the driver's disk cache OFF (__GL_SHADER_DISK_CACHE=0): every pipeline is compiled by the driver INTO the
#                engine's VkPipelineCache, the one every exe loads - a pipeline the NVIDIA driver served from its own disk
#                cache was never written into it, and the first engine row after a "warm" verdict compiled 67 s of ray
#                pipelines and timed out (#100). Stamp <shared>.warm-key: this key and the shared cache's layer keys.
#   <cache>-on   the driver's disk cache ON, the engine's cache warm now: the driver keeps its cache PER EXECUTABLE
#                (GLCache/<...>/<exe key>.bin) and the -off boot stores nothing in it, so after a shader change the first
#                rows of this exe compiled every pipeline in the driver from cold - past the 60-s cap AT BOOT, and a row
#                killed at the cap stores nothing: the loop (#102b, 8 app rows on urgent-102). This boot stores the
#                driver's compile DURING the run (measured: written mid-run; the exit-only teardown loses nothing). Stamp
#                <shared>.driver-key: this key and the exe's NAME - the driver keys its per-exe cache on the name, not
#                the path or the build: measured (HARNESS-1, 2026-10-29), this tree's new Jahshaka (another path,
#                another ELF build id) grew the same GLCache .bin another tree's Jahshaka had written 30 min before, so
#                a relink at the same key needs no -on boot; NVIDIA documents nothing (the rest of its key unverified).
#                THE RESIDUE: every other exe (each test fixture) keeps a cold driver cache after a shader change and
#                pays that compile ONCE on its first run - stored during the run, never awaited at its exit
#                (tests/support/enginetesthelpers.h).
# THE COLD ROWS (cold-on; URGENT #119 + #122, HARNESS-4): a row declared COLD makes its own cache, so no boot above
#   reaches it. An ENGINE row so declared (its fixture states its own cache: frame.streaming_picture's shaderCacheOff,
#   frame.monitor_pop's own dir) boots on NO shared pipeline cache, so every pipeline it creates is the NVIDIA driver's,
#   from that exe's own driver cache alone (keyed per executable): after a shader change rq_reflect's ahead build took
#   117-121 s in nvvm (BUGS-8's gdb: the teardown joins it), the 60-s cap killed the row, nothing was stored, red at
#   every run. An APP row so declared keeps a cache keyed on the shaders (shader.async_after_open.seed: its startup
#   seed, a 64-s boot of the epic World Mode whenever its stamp is stale, timed out at 60.04 s on 8B's check, the row
#   behind it NOT RUN). The cold-on boot runs every command of <build>/tests/test-cold-rows.txt (written at generate
#   time from the COLD registrations with each row's test tier, tests/CMakeLists.txt _jah_cold_row) once, UNCAPPED (the
#   call's end, never the 60-s cap), the driver's cache ON, ONE ROW AT A TIME, EACH ITS OWN HOLD (tests/support/
#   warm_cold_rows.sh; SHIP-CUT-2 0b, URGENT #155: run all at once, a prime died out of GPU memory and was stamped).
#   A STAMP PER ROW, written only for a run the runner judged PRIMED (it reached its own end; a seed fresh after it):
#   <tests>/.cold-rows/<row>.driver-key - this key, the row's line digest (its command: a new or changed COLD row reads
#   cold) and the BUILT digest, the size and time of every staged media file: what a row's own cache stamp keys on beyond
#   this key (the seed's stamp is this key, all of bin/media by content and the fork's BUILT_FROM; never a binary's bytes -
#   URGENT #125). Only the rows not primed at the key run; the prime's cost is printed per row. A row on the
#   shared cache needs none of it - measured, with an EMPTY driver cache rtreflect.sphere_glint built all 13 ray
#   pipelines through the shared VkPipelineCache in 1.04 s. A fixture that states its own cache while registered warm is
#   refused at its start (enginetesthelpers.h), so this list cannot miss one of those.
# THE CACHES:
#   app     <tests-dir>/.shadercache (the app rows', fresh_home --warm): `Jahshaka --engine-selftest` - the app's own
#           startup warm-up gate (the born World's set: Epic, every feature) PLUS the self-test's six poses (the default
#           scene, fixture B with the rays on and off, the motion pose, the mover) - a superset of the warm-up path, and
#           the set of the row the cap broke first (app.engine_selftest_validation) - THEN, in the same home, one
#           `--script tests/support/warm_tier.js` boot per test tier the rows declare (<build>/tests/test-boot-sets.txt,
#           written at configure; tests/support/warm_app_boot.sh): a row's tier draws its own permutations of the born
#           World (HARNESS-1, measured: log.engine at epic + photon built 3 pbs permutations and 256,658 bytes of
#           pipelines on every run, and the 31 rows a WARM fixture seeds never promote). The stamp's third line is the
#           sets file's digest: a new tier reads cold.
#   engine  <tests-dir>/.shadercache-engine (the CLASS engine rows', fresh_home --warm --cache engine, tests/CMakeLists.txt
#           jah_gpu_row): jahlab on photon-rough at Epic in Both, a few frames - the ray tier's every pipeline (the
#           gather, the reflection and its compact pass, the resolve, the temporal, the filter) and the photon envs' Hlms
#           set the lab rows share (--lab <exe>, default <build>/bin/jahlab).
# What a row draws beyond its tier's born World (its own scene, a sample, an engine fixture's content) it compiles once
# more: a row wrapped by fresh_home.sh --warm promotes it on its own green run (every engine row; GPU rows run one at a
# time, so the generation is its seed's); a row seeded by a WARM fixture (fresh_home.sh --setup) never promotes - its
# content beyond the born World compiles on every run (the residue named, HARNESS-1). ONE answer for the boots named: "warm at <key>" when every one is, else "cold at <key>:
# <each cold boot's why>" (exit 3 under --check; else their boots, in order).
set -u
BUILD=""; INHERIT=""; CHECK=0; KEYONLY=0; TESTS=""; APP=""; LAB=""; ONLY=""; BUDGET=1170; COLDROWS=""
while [ $# -gt 0 ]; do
    case "$1" in
        --check) CHECK=1; shift ;;
        --key) KEYONLY=1; shift ;;
        --tests-dir) TESTS="${2:?--tests-dir needs a directory}"; shift 2 ;;
        --app) APP="${2:?--app needs an executable}"; shift 2 ;;
        --lab) LAB="${2:?--lab needs an executable}"; shift 2 ;;
        --cold-rows) COLDROWS="${2:?--cold-rows needs a file}"; shift 2 ;;
        --only) ONLY="${2:?--only needs a boot}"; shift 2 ;;
        --inherit) INHERIT="${2:?--inherit needs the parent build dir}"; shift 2 ;;
        --budget) BUDGET="${2:?--budget needs seconds}"; shift 2
                  case "$BUDGET" in ""|*[!0-9]*) echo "warm-app-cache: --budget <seconds>, not $BUDGET" >&2; exit 64 ;; esac ;;
        -*) echo "warm-app-cache: unknown flag $1" >&2; exit 64 ;;
        *) BUILD="$1"; shift ;;
    esac
done
ALL="app-off app-on engine-off engine-on cold-on"
SEL=""
for o in $(echo "${ONLY:-$ALL}" | tr ',' ' '); do
    case "$o" in
        app) SEL="$SEL app-off app-on" ;;
        engine) SEL="$SEL engine-off engine-on" ;;
        app-off|app-on|engine-off|engine-on|cold-on) SEL="$SEL $o" ;;
        *) echo "warm-app-cache: --only takes app-off, app-on, engine-off, engine-on, cold-on (or the pairs app, engine), not $o" >&2
           exit 64 ;;
    esac
done
SEL=$(for b in $ALL; do case " $SEL " in *" $b "*) printf '%s ' "$b" ;; esac; done)
USAGE="usage: warm-app-cache.sh <build-dir> [--check] [--only <boot>[,<boot>...]] [--budget <s>] [--tests-dir <dir>] [--app <exe>] [--lab <exe>] [--cold-rows <file>]"
[ -n "$BUILD" ] && [ -d "$BUILD" ] || { echo "$USAGE" >&2; exit 64; }
BUILD=$(cd "$BUILD" && pwd)
HERE=$(cd "$(dirname "$0")" && pwd)
SRC=$(cd "$HERE/.." && pwd)
# THE WARM-UP's OWN BOOTS run on the caches they are warming: fresh_home.sh --warm's stamp check (URGENT #140) is not theirs
export JAH_WARM_CHECK=0
# THE SHIPPED SEED OFF (URGENT #143): the warm-up compiles the build's own pipelines into the shared caches every row links,
# never serves a tree's bin/shadercache-seed - the rows boot with JAHSHAKA_SHADER_SEED=none (tests/cmake/vram_rows.cmake)
export JAHSHAKA_SHADER_SEED=none
TESTS=${TESTS:-$BUILD/tests}
mkdir -p "$TESTS"

# ---- THE KEY ------------------------------------------------------------------------------------------------------
key_inputs() {
    local id="$BUILD/engine/generated/jahshaka_engine_build_id.h" code
    [ -f "$id" ] || { echo "warm-app-cache: no engine build id at $id (build the tree first)" >&2; return 1; }
    code=$(sed -n 's/^#define JAHSHAKA_SHADER_CACHE_CODE_ID "\([0-9a-f]*\)"$/\1/p' "$id")
    [ -n "$code" ] || { echo "warm-app-cache: no JAHSHAKA_SHADER_CACHE_CODE_ID in $id (build the tree first)" >&2; return 1; }
    echo "cache-code $code"
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
[ "$KEYONLY" = 1 ] && { echo "$KEY"; exit 0; }
# the engine's FULL build id, its first 16 hex digits: the top-up's signal (the header)
ENGINE_ID=$(sed -n 's/^#define JAHSHAKA_ENGINE_BUILD_ID "\([0-9a-f]\{16\}\).*"$/\1/p' "$BUILD/engine/generated/jahshaka_engine_build_id.h")
layer_keys() {   # the shared cache's per-layer keys (its manifest's last column), sorted, one line
    [ -f "$1/cache-manifest.txt" ] && awk '$1 == "file" { print $NF }' "$1/cache-manifest.txt" | LC_ALL=C sort -u | tr '\n' ' '
}
cold_why() {   # <shared> <stamp> [<sets digest>]: empty when warm at the key, else why not
    if [ ! -f "$2" ]; then echo "no warm-up recorded"
    elif [ "$(sed -n 1p "$2")" != "$KEY" ]; then echo "the key moved (warmed at $(sed -n 1p "$2"))"
    elif [ ! -f "$1/cache-manifest.txt" ]; then echo "the shared cache is gone"
    elif [ "$(sed -n 2p "$2")" != "$(layer_keys "$1")" ]; then echo "the shared cache was replaced at another key since"
    elif ! torn=$(python3 -I "$SRC/tests/support/shadercache_check.py" "$1"); then   # its stderr (#132's fallback line) passes
        echo "the shared cache fails its own manifest ($torn) - the engine would boot every row seeded from it cold"
    elif [ $# -ge 3 ] && [ "$(sed -n 3p "$2")" != "$3" ]; then echo "the rows' test tiers changed since (test-boot-sets.txt)"
    elif [ "${TOPUP_CHECK:-1}" = 1 ] && [ "$(cat "$1.engine-id" 2>/dev/null)" != "$ENGINE_ID" ]; then
        echo "the engine's code moved since ($(cat "$1.engine-id" 2>/dev/null || echo none) -> $ENGINE_ID; the key holds: a TOP-UP," \
             "a warm boot that adds the new permutations)"
    fi
}
# THE ROWS' TEST TIERS (tests/CMakeLists.txt writes them at configure): the app-off boot boots each once after the self-test
SETS="$BUILD/tests/test-boot-sets.txt"
SETS_DIGEST="sets $( [ -f "$SETS" ] && sha256sum < "$SETS" | cut -c1-16 || echo none)"
APP_SHARED="$TESTS/.shadercache"; APP_STAMP="$TESTS/.shadercache.warm-key"; APP_DSTAMP="$TESTS/.shadercache.driver-key"
ENG_SHARED="$TESTS/.shadercache-engine"; ENG_STAMP="$TESTS/.shadercache-engine.warm-key"
ENG_DSTAMP="$TESTS/.shadercache-engine.driver-key"
APPX="${APP:-$BUILD/bin/Jahshaka}"; LABX="${LAB:-$BUILD/bin/jahlab}"
COLDROWS="${COLDROWS:-$BUILD/tests/test-cold-rows.txt}"; COLD_SDIR="$TESTS/.cold-rows"
built_digest() {   # [<build>]: every staged media file's CONTENT, by its path under bin/media - what a cold row's own cache may
    # key on beyond the key (the seed's stamp hashes all of bin/media by content). NOT an exe's or the app's bytes: a relink
    # changes no shader (URGENT #125: keyed on the app's size and time, the seed and this boot re-ran ~160 s at every commit).
    # NOT the files' times or the tree's path (CODING_RULES §6, #154's rule: two trees at one key read 6 of 6 cold stamps
    # apart - a fresh tree's media staged at another time read "rebuilt since"): ~4 MB, ~30 ms
    ( cd "${1:-$BUILD}/bin/media" 2>/dev/null && find . -type f -print0 | LC_ALL=C sort -z | xargs -0 -r sha256sum )
}
built_key() { echo "built $(built_digest "${1:-}" | sha256sum | cut -c1-16)"; }   # [<build>]
row_digest() {   # <the row's line> [<build> <src>]: its command, its tier, its dir - the tree's own paths written <build> and
    # <src> first (CODING_RULES §6: the line carries the tree's spelling, test-cold-rows.txt; a tree path in a stamp made
    # every other tree's stamp read "its registration changed")
    # EVERY SPELLING of the tree (the stage-1 audit's finding 11: ~/Developer is a symlink to /mnt/work/Developer - a call made
    # through the other spelling than the configure's left the lines unstripped and every stamp stale): the logical and the
    # physical path of each, and the spellings its CMakeCache records (CMAKE_CACHEFILE_DIR / CMAKE_HOME_DIRECTORY)
    local b=${2:-$BUILD} s=${3:-$SRC} l=$1 x
    for x in "$b" "$(cd "$b" 2>/dev/null && pwd -P)" "$(sed -n 's/^CMAKE_CACHEFILE_DIR:INTERNAL=//p' "$b/CMakeCache.txt" 2>/dev/null)"; do
        [ -n "$x" ] && l=${l//"$x"/<build>}
    done
    for x in "$s" "$(cd "$s" 2>/dev/null && pwd -P)" "$(sed -n 's/^CMAKE_HOME_DIRECTORY:INTERNAL=//p' "$b/CMakeCache.txt" 2>/dev/null)"; do
        [ -n "$x" ] && l=${l//"$x"/<src>}
    done
    echo "row $(printf '%s' "$l" | sha256sum | cut -c1-16)"
}
row_stamp_why() {   # <row> <line>: empty when this row's prime ran at this key, for this command, on this build
    local st="$COLD_SDIR/$1.driver-key"
    if [ ! -f "$st" ]; then echo "no prime recorded"
    elif [ "$(sed -n 1p "$st")" != "$KEY" ]; then echo "the key moved (primed at $(sed -n 1p "$st"))"
    elif [ "$(sed -n 2p "$st")" != "$(row_digest "$2")" ]; then echo "its registration changed"
    elif [ "$(sed -n 3p "$st")" != "$BUILT_KEY" ]; then echo "the staged media rebuilt since"
    fi
}
stale_seeds() {   # the seed rows whose own freshness check says stale (URGENT #139: a seed keys on the Studio build too)
    bash "$SRC/tests/support/warm_cold_rows.sh" --stale-seeds "$COLDROWS" | tr '\n' ' ' | sed 's/ $//'
}
coldrows_left() {   # "<row> TAB <why>" for every cold row to prime: its stamp not this key's, or a startup seed stale (#139: one
    # re-seed per Studio commit, by the warm-up - uncapped - never inside a capped row: SHIP-CUT-2 0d)
    [ -s "$COLDROWS" ] || return 0
    BUILT_KEY=${BUILT_KEY:-$(built_key)}
    local line row w s
    s=" $(stale_seeds) "
    while IFS= read -r line; do
        [ -n "$line" ] || continue
        row=${line%%$'\t'*}; w=$(row_stamp_why "$row" "$line")
        [ -z "$w" ] && case "$s" in *" $row "*) w="its startup seed is stale (a new Studio build)" ;; esac
        [ -z "$w" ] || printf '%s\t%s\n' "$row" "$w"
    done < "$COLDROWS"
}
coldrows_why() {   # the cold rows: empty when every one is primed at this key and every startup seed among them is fresh
    local l; l=$(coldrows_left)
    [ -n "$l" ] || return 0
    echo "$(printf '%s\n' "$l" | grep -c .) of $(grep -c . "$COLDROWS") to prime: $(printf '%s\n' "$l" | sed 's/\t/ (/; s/$/)/' | tr '\n' ';' | sed 's/;$//; s/;/; /g')"
}
exe_id() {   # <exe>: the name the driver keys its per-exe cache on - its file name (measured below)
    basename "$1"
}
driver_why() {   # <stamp> <exe>: empty when this exe's driver cache was stored at the key, else why not
    if [ ! -f "$1" ]; then echo "no driver-cache boot recorded"
    elif [ "$(sed -n 1p "$1")" != "$KEY" ]; then echo "the key moved (stored at $(sed -n 1p "$1"))"
    elif [ "$(sed -n 2p "$1")" != "$(exe_id "$2")" ]; then echo "stored for another exe ($(sed -n 2p "$1"))"
    fi
}
why_of() {   # <boot>: empty when warm
    case "$1" in
        app-off) cold_why "$APP_SHARED" "$APP_STAMP" "$SETS_DIGEST" ;;
        app-on) driver_why "$APP_DSTAMP" "$APPX" ;;
        engine-off) cold_why "$ENG_SHARED" "$ENG_STAMP" ;;
        engine-on) driver_why "$ENG_DSTAMP" "$LABX" ;;
        cold-on) coldrows_why ;;
    esac
}
what_of() {
    case "$1" in
        app-off) echo "the app rows' cache" ;; app-on) echo "the app exe's driver cache" ;;
        engine-off) echo "the engine rows' cache" ;; engine-on) echo "the lab exe's driver cache" ;;
        cold-on) echo "the cold rows' own caches" ;;
    esac
}
# ---- THE INHERITANCE (TEST-STREAMLINE-1 1a): a fresh tree takes its PARENT's warm caches at an EQUAL key ----------------
if [ -n "$INHERIT" ]; then
    PB=$(cd "$INHERIT" 2>/dev/null && pwd) || { echo "warm-app-cache: --inherit: no build dir $INHERIT" >&2; exit 64; }
    [ "$PB" != "$BUILD" ] || { echo "warm-app-cache: --inherit: the parent is this build" >&2; exit 64; }
    PSRC=$(cd "$PB/.." && pwd); PT="$PB/tests"
    # THE PARENT's KEY BY THIS SCRIPT's FORMULA (a stamp is comparable only at one formula): equal, or nothing moves
    PKEY=$(bash "$0" "$PB" --key 2>/dev/null)
    if [ -z "$PKEY" ] || [ "$PKEY" != "$KEY" ]; then
        echo "warm-app-cache: inherited nothing - the parent's key ${PKEY:-unreadable} is not this tree's $KEY ($PB)"
        exit 3
    fi
    got=""
    # take <src> <dst> [copy]: a cache directory into a sibling first, then renamed over ours under OUR lock - BY HARD LINKS
    # only where every writer of its files REPLACES them by rename (the engine's caches: cachefile::writeAtomic; a promotion
    # renames a new directory over the old, fresh_home.sh), else COPIED (`copy`): the NVIDIA driver writes its cache files
    # IN PLACE (it grows a GLCache .bin mid-run), so a linked driver cache would let a child write its parent's file (the
    # stage-1 audit's finding 1); the cold rows' seeds likewise (their stamps are written by a test, not by rename)
    take() {
        local nxt="$2.inherit.$$"
        mkdir -p "$(dirname "$2")"; rm -rf "$nxt"
        if [ "${3:-}" = copy ]; then cp -a "$1" "$nxt" || { rm -rf "$nxt"; return 1; }
        else cp -al "$1" "$nxt" 2>/dev/null || { rm -rf "$nxt"; cp -a "$1" "$nxt"; } || { rm -rf "$nxt"; return 1; }
        fi
        ( flock -w 60 9 || exit 1; rm -rf "$2.old.$$"; [ -e "$2" ] && mv "$2" "$2.old.$$"; mv "$nxt" "$2"; rm -rf "$2.old.$$" ) \
            9>"$2.lock"
    }
    # the two shared caches: the parent's stamped at this key and passing its own manifest, ours not warm at it
    for c in app engine; do
        if [ $c = app ]; then sh_=.shadercache; st=.warm-key; dk=.driver-key; else sh_=.shadercache-engine; st=.warm-key; dk=.driver-key; fi
        P="$PT/$sh_"; M="$TESTS/$sh_"
        [ -f "$P$st" ] && [ "$(sed -n 1p "$P$st")" = "$KEY" ] && [ "$(sed -n 2p "$P$st")" = "$(layer_keys "$P")" ] || continue
        if [ -f "$M$st" ] && [ -z "$(TOPUP_CHECK=0 cold_why "$M" "$M$st")" ]; then continue; fi
        ( flock -s -w "${JAH_INHERIT_LOCK_WAIT:-60}" 9 || exit 1   # (the knob: tooling.warm_inherit's held lock, seconds)
          python3 -I "$SRC/tests/support/shadercache_check.py" "$P" > /dev/null || exit 1
          take "$P" "$M" ) 9>"$P.lock" || { echo "warm-app-cache: the parent's $sh_ not taken (its lock, or its manifest)"; continue; }
        cp "$P$st" "$M$st"
        [ -f "$P$dk" ] && [ "$(sed -n 1p "$P$dk")" = "$KEY" ] && cp "$P$dk" "$M$dk"
        [ -f "$P.engine-id" ] && cp "$P.engine-id" "$M.engine-id"
        got="$got $sh_ ($(du -sh "$M" | cut -f1))"
    done
    # the -off boot's permutation dump (source.shaders_spirv_valid), stamped with the key
    if [ "$(cat "$PT/.hlms-dump/.key" 2>/dev/null)" = "$KEY" ] && [ "$(cat "$TESTS/.hlms-dump/.key" 2>/dev/null)" != "$KEY" ]; then
        rm -rf "$TESTS/.hlms-dump"; cp -a "$PT/.hlms-dump" "$TESTS/.hlms-dump" && got="$got .hlms-dump"
    fi
    [ -f "$TESTS/.warm-walls" ] || { [ -f "$PT/.warm-walls" ] && cp "$PT/.warm-walls" "$TESTS/.warm-walls"; }
    # the seeded library template (TEST-STREAMLINE-1 3a item 5), COPIED - a home writes its library in place - when the
    # parent's is at this tree's seed key (its own, not the warm key: tests/support/seed_home.sh)
    bk=$(sh "$SRC/tests/support/seed_home.sh" key "$TESTS")
    if [ -n "$bk" ] && [ "$(head -1 "$PT/.seedhome/seed-key" 2>/dev/null)" = "$bk" ] \
           && [ "$(head -1 "$TESTS/.seedhome/seed-key" 2>/dev/null)" != "$bk" ]; then
        take "$PT/.seedhome" "$TESTS/.seedhome" copy && got="$got .seedhome"
    fi
    # THE COLD ROWS: each row the parent primed at this key, for the same registration (paths aside) and the same staged
    # media by content, takes the parent's stamp and the row's own state - its private driver cache (a *CACHE*PATH* value
    # under the parent's build) and its startup seed (a directory beside its working dir holding tree-key.txt: the seed's own
    # freshness check still judges it, #139)
    case "$COLDROWS" in "$BUILD"/*) PROWS="$PB/${COLDROWS#"$BUILD"/}" ;; *) PROWS="$PB/tests/test-cold-rows.txt" ;; esac; n=0   # (the parent's own)
    if [ -s "$COLDROWS" ] && [ -s "$PROWS" ]; then
        BUILT_KEY=$(built_key); PBUILT=$(built_key "$PB")
        mkdir -p "$COLD_SDIR"
        while IFS= read -r line; do
            [ -n "$line" ] || continue
            row=${line%%$'\t'*}
            [ -z "$(row_stamp_why "$row" "$line")" ] && continue   # ours primed already
            pline=$(awk -F'\t' -v r="$row" '$1 == r' "$PROWS" | head -1); pst="$PT/.cold-rows/$row.driver-key"
            [ -n "$pline" ] && [ -f "$pst" ] || continue
            [ "$(sed -n 1p "$pst")" = "$KEY" ] && [ "$(sed -n 2p "$pst")" = "$(row_digest "$pline" "$PB" "$PSRC")" ] &&
                [ "$(sed -n 3p "$pst")" = "$PBUILT" ] && [ "$PBUILT" = "$BUILT_KEY" ] &&
                [ "$(row_digest "$pline" "$PB" "$PSRC")" = "$(row_digest "$line")" ] || continue
            readarray -t -d $'\t' f < <(printf '%s' "$pline")
            wd=${f[1]}
            for a in "${f[@]:2}"; do
                for v in $(printf '%s' "$a" | tr '\037' '\n'); do
                    case "$v" in *CACHE*PATH*=*) d=${v#*=}; case "$d" in "$PB"/*) [ -d "$d" ] && take "$d" "$BUILD/${d#"$PB"/}" copy ;; esac ;; esac
                done
            done
            for sd in "$wd"/*/; do
                rel=${sd%/}; rel=${rel#"$PB"/}
                [ -f "$sd/tree-key.txt" ] && case "$sd" in "$PB"/*) take "${sd%/}" "$BUILD/$rel" copy ;; esac
            done
            printf '%s\n%s\n%s\n' "$KEY" "$(row_digest "$line")" "$BUILT_KEY" > "$COLD_SDIR/$row.driver-key"; n=$((n + 1))
        done < "$COLDROWS"
    fi
    [ "$n" -gt 0 ] && got="$got $n cold row(s)"
    echo "warm-app-cache: inherited at $KEY from $PB:${got:- nothing (the parent holds none at this key, or this tree is warm)}"
    TOPUP_CHECK=1
    left=""; for b in $ALL; do [ -n "$(why_of "$b")" ] && left="$left $b"; done
    echo "warm-app-cache: still to warm here:${left:- nothing}"
    exit 0
fi
# THE SEEDED LIBRARY TEMPLATE (TEST-STREAMLINE-1 3a item 5; tests/support/seed_home.sh): the 17 shipped meshes' library,
# made ONCE per seed key (seed_home.sh key: the bake producer, the assimp pin, the 17 sources...) by one NULL headless boot (no card, ~3 s) when the template is missing or at another key, so every
# --warm home is COPIED a seeded library and its boot seeds nothing (0.93 s a boot). With the app boots asked for, never
# under --check; a tests dir with no seed key inputs (a toy's, the guard rows') makes none and says nothing.
if [ "$CHECK" = 0 ] && [ -f "$TESTS/seed-key-inputs.txt" ] && case " $SEL " in *" app-off "*) true ;; *) false ;; esac; then
    sh "$SRC/tests/support/seed_home.sh" make "$APPX" "$TESTS" || true
fi
RUN=""; why=""
for b in $SEL; do
    w=$(why_of "$b")
    [ -n "$w" ] || continue
    RUN="$RUN $b"
    why="${why:+$why; }$(what_of "$b") ($b): $w"
done
RUN=${RUN# }
if [ -z "$RUN" ]; then
    echo "warm at $KEY"
    exit 0
fi
echo "cold at $KEY: $why"
[ "$CHECK" = 1 ] && exit 3
# AN -on BOOT RUNS ON ITS -off BOOT's CACHE: asked for alone while that cache is cold, nothing boots
for b in $RUN; do
    case "$b" in *-on) off="${b%-on}-off"
        if [ -n "$(TOPUP_CHECK=0 why_of "$off")" ] && ! case " $RUN " in *" $off "*) true ;; *) false ;; esac; then
            echo "warm-app-cache: $b boots on $(what_of "$off"), cold - call --only $off first; nothing booted"
            exit 3
        fi ;;
    esac
done

# ---- THE BOOTS (once each, uncapped: not a test) -------------------------------------------------------------------
# THIS CALL's LOGS, ITS OWN DIRECTORY (SHIP-CUT-2 0c): every boot's log and each cold row's went to one fixed path under
# <tests>, overwritten by the next call - ORPHAN-REDS-1's cold-on per-row logs, the only record of why a prime failed, were
# gone by the time anyone read them. Now <tests>/warm-logs/<time>-<pid>-<boots>/ (JAH_WARM_LOG_DIR names another), the
# newest 40 calls kept; the call prints the directory first.
WLOGS="$TESTS/warm-logs"
LOGS="${JAH_WARM_LOG_DIR:-$WLOGS/$(date +%Y%m%dT%H%M%S)-$$-$(echo "$RUN" | tr ' ' '+')}"
mkdir -p "$LOGS"
ls -1dt "$WLOGS"/*/ 2>/dev/null | tail -n +41 | xargs -r rm -rf
echo "warm-app-cache: this call's logs: $LOGS"
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
# A BOOT's ESTIMATE: its last measured wall on this tree, the card's wait apart (<tests-dir>/.warm-walls), else a cold
# tree's: the -off boots 382 s / 332 s (#103, the driver cache off); measured on HARNESS-1's fresh tree (2026-10-29, a
# loaded box, the card's wait apart): app-off 282 s, app-on 29 s, engine-off 204 s, engine-on 2 s (the lab's driver cache
# already held by its name) - the defaults keep #103's colder numbers and a minute or two for an -on boot
WALLS="$TESTS/.warm-walls"
est_of() {
    local w
    w=$(awk -v b="$1" '$1 == b { w = $2 } END { print w }' "$WALLS" 2>/dev/null)
    [ -n "$w" ] && { echo "$w"; return; }
    case "$1" in app-off) echo 400 ;; engine-off) echo 340 ;; app-on) echo 120 ;; engine-on) echo 60 ;; cold-on) echo 330 ;; esac   # cold-on: 306 s measured, three rows, loaded (#122)
}
waited_of() {   # <log>: the card's wait in whole seconds (the lock prints tenths since #95: "waited 12.3 s"), 0 when none
    local w; w=$(sed -n 's/^gpu-lock: waited \([0-9]*\)\(\.[0-9]*\)\{0,1\} s$/\1/p' "$1" | head -1); echo "${w:-0}"
}
note_wall() {
    { grep -v "^$1 " "$WALLS" 2>/dev/null; echo "$1 $2"; } > "$WALLS.tmp" && mv "$WALLS.tmp" "$WALLS"
}
T0=$(date +%s); RAN=0; WAIT=0
FLOOR=$(( BUDGET / 10 )); [ "$FLOOR" -le 60 ] || FLOOR=60   # the first boot's least wait: 60 s (the default budget 1170 leaves a boot ~830 s of wait, #130)
# room <boot> <boots left>: the card's wait this boot may take (WAIT), or exit 75 when the boot no longer fits the call
room() {
    local est left
    est=$(est_of "$1"); left=$(( BUDGET - ($(date +%s) - T0) ))
    WAIT=$(( left - est ))
    if [ "$RAN" = 0 ]; then
        [ "$WAIT" -ge "$FLOOR" ] || WAIT=$FLOOR
    elif [ "$WAIT" -lt $(( FLOOR / 2 )) ]; then
        echo "warm-app-cache: the $1 boot (est. ${est} s) does not fit this call's budget (${left} s of ${BUDGET} s left) -" \
             "call again: --only $(echo "$1 $2" | tr -s ' ' ',' | sed 's/,$//')"
        exit 75
    fi
}
# held <log> <boot> <boots left>: the card stayed held past the boot's wait - exit 75, nothing more booted
held() {
    local line
    line=$(grep -m1 '^gpu-lock: the card stayed held' "$1") || return 0
    echo "warm-app-cache: ${line#gpu-lock: } for the $2 boot - call again: --only $(echo "$3" | tr -s ' ' ',' | sed 's/,$//')"
    exit 75
}
# boot <name> <shared> <stamp> <home> <log> <boots left> <command...>: the -off boot through fresh_home (its --warm args
# in FH_ARGS), the stamp only on a promotion; 0 passed (stamped or not), 1 failed
boot() {
    local name=$1 shared=$2 stamp=$3 home=$4 log=$5 rest=$6; shift 6
    room "$name" "$rest"
    echo "warm-app-cache: booting $(what_of "$name") ($name): $(basename "$EXE") on $DISPLAY (home $home;" \
         "the card's wait at most ${WAIT} s)"
    local t0 rc wall skipped waited
    t0=$(date +%s)
    # THE DRIVER's CACHE OFF for this boot (URGENT #100): every pipeline is compiled into the engine's VkPipelineCache,
    # which every exe loads - a driver-cache hit would leave it out of it
    ( cd "$BUILD/bin" && HOME="$home" JAHSHAKA_DATA_ROOT="$home/.local/share/Jahshaka" QT_QPA_PLATFORM=xcb \
          __GL_SHADER_DISK_CACHE=0 nice -n 10 "$SRC/scripts/gpu-exclusive.sh" --wait-max "$WAIT" \
          --label "warm-app-cache ($name)" env $(cov_env "$name") sh "$SRC/tests/support/fresh_home.sh" $FH_ARGS "$home" -- "$@" ) > "$log" 2>&1
    rc=$?
    [ $rc = 75 ] && held "$log" "$name" "$name $rest"
    wall=$(( $(date +%s) - t0 ))
    grep -E 'fresh_home.sh|^gpu-lock: waited|^engine-selftest: .*-> (PASS|FAIL)|^warm_app_boot: |^LAB (END|FAILED)' "$log" |
        head -12
    RAN=1
    if [ $rc -ne 0 ]; then
        echo "warm-app-cache: the $name boot FAILED (exit $rc) after ${wall} s - its log $log; the cache is not warm"
        return 1
    fi
    waited=$(waited_of "$log"); wall=$(( wall - waited ))   # THE BOOT's OWN WALL: the card's wait is not the boot's (block A)
    note_wall "$name" "$wall"
    if [ ! -f "$shared/cache-manifest.txt" ]; then
        echo "warm-app-cache: the $name boot passed but no shared cache was promoted ($shared) - not warm"
        return 1
    fi
    skipped=$(grep -m1 -E 'fresh_home.sh: the shared cache NOT replaced|fresh_home.sh: .*no promotion' "$log")
    if [ -n "$skipped" ]; then
        echo "warm-app-cache: the $name boot passed in ${wall} s (the card's wait ${waited} s) but its cache was NOT promoted (${skipped#fresh_home.sh: }) -" \
             "not stamped: the cache stays cold at $KEY and the next call warms again"
        return 0
    fi
    if [ "$name" = app-off ]; then
        printf '%s\n%s\n%s\n' "$KEY" "$(layer_keys "$shared")" "$SETS_DIGEST" > "$stamp"
    else
        printf '%s\n%s\n' "$KEY" "$(layer_keys "$shared")" > "$stamp"
    fi
    echo "$ENGINE_ID" > "$shared.engine-id"   # the top-up's stamp (the header)
    echo "warm-app-cache: $(what_of "$name") warm at $KEY - the boot ${wall} s (the card's wait ${waited} s), $(du -sh "$shared" | cut -f1)" \
         "($(sed -n 's/^shaders //p' "$shared/cache-manifest.txt") shaders)"
    return 0
}
# driver_boot <name> <dstamp> <exe> <home> <log> <boots left> <command...>: the -on boot (the driver's cache on, the
# engine's warm), stamped with the key and the exe when it passed
driver_boot() {
    local name=$1 dstamp=$2 exe=$3 home=$4 log=$5 rest=$6; shift 6
    room "$name" "$rest"
    echo "warm-app-cache: booting $(what_of "$name") ($name): $(basename "$EXE") on $DISPLAY (the card's wait at most ${WAIT} s)"
    local t0 rc wall b0 b1 dpath waited
    dpath="${__GL_SHADER_DISK_CACHE_PATH:-/home/jahshaka/.cache/nvidia}"
    b0=$(du -sb "$dpath" 2>/dev/null | cut -f1)
    t0=$(date +%s)
    ( cd "$BUILD/bin" && HOME="$home" JAHSHAKA_DATA_ROOT="$home/.local/share/Jahshaka" QT_QPA_PLATFORM=xcb \
          env -u __GL_SHADER_DISK_CACHE nice -n 10 "$SRC/scripts/gpu-exclusive.sh" --wait-max "$WAIT" \
          --label "warm-app-cache ($name)" env $(cov_env "$name") sh "$SRC/tests/support/fresh_home.sh" $FH_ARGS "$home" -- "$@" ) > "$log" 2>&1
    rc=$?
    [ $rc = 75 ] && held "$log" "$name" "$name $rest"
    wall=$(( $(date +%s) - t0 ))
    b1=$(du -sb "$dpath" 2>/dev/null | cut -f1)
    grep -E '^gpu-lock: waited' "$log" | head -1
    RAN=1
    if [ $rc -ne 0 ]; then
        echo "warm-app-cache: the $name boot FAILED (exit $rc) after ${wall} s - its log $log"
        return 1
    fi
    waited=$(waited_of "$log"); wall=$(( wall - waited ))   # THE BOOT's OWN WALL: the card's wait is not the boot's (block A)
    note_wall "$name" "$wall"
    printf '%s\n%s\n' "$KEY" "$(exe_id "$exe")" > "$dstamp"
    echo "warm-app-cache: $(what_of "$name") stored at $KEY - the boot ${wall} s (the card's wait ${waited} s), the driver cache" \
         "$(( (${b1:-0} - ${b0:-0}) / 1048576 )) MB grown"
    return 0
}
# cold_boot <boots left>: the cold rows not primed at the key, each once, uncapped, the driver's cache on, ONE AT A TIME,
# each its own hold (warm_cold_rows.sh); each row the runner judged PRIMED gets its stamp, the rest are named
cold_boot() {
    local rest=$1 t0 rc wall log="$LOGS/cold-on.log" deadline todo names line row n
    BUILT_KEY=$(built_key)
    todo=$(coldrows_left); names=$(printf '%s\n' "$todo" | cut -f1 | tr '\n' ' ' | sed 's/ *$//')
    room cold-on "$rest"
    # THE EXES' BOUND IS THE CALL's END, read after each row's card wait (the runner subtracts the clock); the end's own
    # 15-s margin comes off the first wait too (#122's proof: the seed finished 1 s inside the bound after a 248-s wait)
    deadline=$(( T0 + BUDGET - 15 ))
    WAIT=$(( WAIT - 15 )); [ "$WAIT" -ge 1 ] || WAIT=1
    echo "warm-app-cache: booting $(what_of cold-on) (cold-on): $(printf '%s\n' "$todo" | grep -c .) of $(grep -c . "$COLDROWS")" \
         "row(s) on $DISPLAY, one at a time, each its own hold, uncapped (to the call's end, $(( deadline - $(date +%s) )) s from" \
         "now; each row's card wait at most ${WAIT} s): $names"
    t0=$(date +%s)
    ( H="$TESTS/warm-cold-home"; rm -rf "$H"; mkdir -p "$H"
      HOME="$H" JAH_COLD_ONLY="$names" JAH_COLD_WAIT="$WAIT" env -u __GL_SHADER_DISK_CACHE $(cov_env cold-on) nice -n 10 \
          bash "$SRC/tests/support/warm_cold_rows.sh" "$COLDROWS" "$deadline" "$LOGS/cold-on"; rc=$?; rm -rf "$H"; exit $rc ) > "$log" 2>&1
    rc=$?
    wall=$(( $(date +%s) - t0 ))
    grep -E '^warm_cold_rows: ' "$log" | grep -v ': skipped (primed' | head -40
    RAN=1
    mkdir -p "$COLD_SDIR"; n=0
    while IFS= read -r line; do
        [ -n "$line" ] || continue
        row=${line%%$'\t'*}
        if grep -qF "warm_cold_rows: $row: PRIMED" "$log"; then
            printf '%s\n%s\n%s\n' "$KEY" "$(row_digest "$line")" "$BUILT_KEY" > "$COLD_SDIR/$row.driver-key"; n=$((n + 1))
        fi
    done < "$COLDROWS"
    note_wall cold-on "$(grep -oE 'PRIMED - its run [0-9]+ s' "$log" | awk '{ s += $(NF - 1) } END { print s + 0 }')"
    if [ $rc = 1 ]; then
        echo "warm-app-cache: the cold-on boot FAILED after ${wall} s - A PRIME CRASHED, NOT STAMPED: $(grep -E '^warm_cold_rows: .*: NOT PRIMED - (its run CRASHED|the seed is not fresh)' "$log" |
             sed -E 's/^warm_cold_rows: ([^:]*): NOT PRIMED - (its run CRASHED|the seed is not fresh after its run) \(([^)]*)\).*/\1 (\2: \3)/' | tr '\n' ';' | sed 's/;$//; s/;/; /g')" \
             "- ${n} other row(s) primed and stamped; each row's log in $LOGS/cold-on"
        return 1
    fi
    if [ $rc = 75 ]; then
        echo "warm-app-cache: the cold-on boot primed ${n} row(s) in ${wall} s; the rest wait (the card stayed held past a row's" \
             "wait, or the call's end came) - call again: --only $(echo "cold-on $rest" | tr -s ' ' ',' | sed 's/,$//')"
        exit 75
    fi
    if [ $rc -ne 0 ]; then
        echo "warm-app-cache: the cold-on boot FAILED (exit $rc) after ${wall} s - its log $log"
        return 1
    fi
    echo "warm-app-cache: $(what_of cold-on) primed at $KEY - ${n} row(s), the boot ${wall} s"
    return 0
}
# A TOP-UP REPLACES THE UNION (the stage-1 audit's finding 4, the lead's ruling): an -off boot that runs ONLY because the
# engine's code moved boots without the shared hlms layer (fresh_home.sh --drop-hlms: the microcode and pipelines warm), so
# its promotion replaces the union with what this engine uses - the dead permutations of earlier engine versions never pile up
topup_args() {   # <boot>: " --drop-hlms" when its only why is the top-up
    case "$(why_of "$1")" in *"a TOP-UP"*) [ -z "$(TOPUP_CHECK=0 why_of "$1")" ] && echo " --drop-hlms" ;; esac
}
# THE WARM-UP's BOOTS WRITE THEIR COVERAGE (TEST-STREAMLINE-1 stage 2, the 2a audit's finding 1, the lead's ruling): in a
# JAH_COVERAGE build each boot's counters go under <build>/coverage/boots/<boot> (scripts/coverage_map.py reads them as the
# producers of the cached artefacts its class's rows load); in every other build nothing
COVERAGE=$(sed -n 's/^JAH_COVERAGE:BOOL=//p' "$BUILD/CMakeCache.txt" 2>/dev/null)
cov_env() {   # <boot>: the GCOV_PREFIX assignments for env, or nothing
    case "$COVERAGE" in ON|on|1|TRUE|true) mkdir -p "$BUILD/coverage/boots/$1"
        echo "GCOV_PREFIX=$BUILD/coverage/boots/$1 GCOV_PREFIX_STRIP=0" ;; esac
}
DUMP="$TESTS/.hlms-dump"
LABSCRIPT="$TESTS/warm-engine-cache.lab"
left="$RUN"
for b in $RUN; do
    left=${left#"$b"}; left=${left# }
    # an -on boot after its -off boot in this call: only on the cache that boot promoted
    case "$b" in *-on) off="${b%-on}-off"
        if [ -n "$(TOPUP_CHECK=0 why_of "$off")" ]; then
            echo "warm-app-cache: $b waits for $(what_of "$off") (not promoted by this call's $off boot) - the next call warms both"
            continue
        fi ;;
    esac
    case "$b" in
        app-off|app-on)
            [ -x "$APPX" ] || { echo "warm-app-cache: no app at $APPX" >&2; exit 2; }
            H="$TESTS/warm-app-cache-home"; FH_ARGS="--warm$(topup_args app-off)"; EXE="$APPX"
            if [ "$b" = app-off ]; then
                # THE PERMUTATIONS THE BOOT GENERATES, DUMPED (URGENT #89): every Hlms vertex and pixel shader of the
                # warm-up and the self-test's poses, written by the engine (JAHSHAKA_HLMS_DEBUG_DIR, OgreEngine.cpp) into
                # <tests>/.hlms-dump and stamped with this key after a passed boot - source.shaders_spirv_valid validates
                # them (tests/hygiene/shaders_spirv_valid.py); the dump is the boot's at this key, promoted or not
                rm -rf "$DUMP"; mkdir -p "$DUMP"
                boot app-off "$APP_SHARED" "$APP_STAMP" "$H" "$LOGS/app-off.log" "$left" \
                    bash "$SRC/tests/support/warm_app_boot.sh" "$APPX" "$H/run/warm.png" "$SETS" "$DUMP" || exit 1
                echo "$KEY" > "$DUMP/.key"
            else
                driver_boot app-on "$APP_DSTAMP" "$APPX" "$H" "$LOGS/app-on.log" "$left" \
                    "$APPX" --engine-selftest "$H/run/warm.png" || exit 1
            fi ;;
        engine-off|engine-on)
            [ -x "$LABX" ] || { echo "warm-app-cache: no lab at $LABX" >&2; exit 2; }
            H="$TESTS/warm-engine-cache-home"; FH_ARGS="--warm --cache engine$(topup_args engine-off)"; EXE="$LABX"
            printf 'reflections both\nframes 4\nquit\n' > "$LABSCRIPT"
            LABCMD=(env "JAHSHAKA_SHADER_CACHE_DIR=$H/.local/share/Jahshaka/shadercache" JAH_TEXTURE_CACHE=0
                    "$LABX" --headless --no-stdin --size 960x540 --data-root "$H/run/lab" --env photon-rough --tier epic
                    --script "$LABSCRIPT")
            if [ "$b" = engine-off ]; then
                boot engine-off "$ENG_SHARED" "$ENG_STAMP" "$H" "$LOGS/engine-off.log" "$left" "${LABCMD[@]}" || exit 1
            else
                driver_boot engine-on "$ENG_DSTAMP" "$LABX" "$H" "$LOGS/engine-on.log" "$left" \
                    "${LABCMD[@]}" || exit 1
            fi ;;
        cold-on)
            cold_boot "$left" || exit 1 ;;
    esac
done
still=""
for b in $SEL; do [ -z "$(why_of "$b")" ] || still="$still $b"; done
if [ -z "$still" ]; then
    echo "warm-app-cache: warm at $KEY - $(echo $SEL | tr ' ' ',')"
fi
exit 0
