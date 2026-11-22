#!/bin/bash
# scale-shells.sh <build-dir> [--jobs N] [--force] [<triangles>...] — THE SCALE SHELLS, BAKED ONCE A BOX AND A BAKE FORMAT
# (ATOM-TESTS-BACK-1 stage 5): a box-prep TOOL, never a row.
#
# WHY. The scale LONG rows (scale.levels, scale.residency: tests/scale/CMakeLists.txt) measure the REAL large assets -
# enginetest::proceduralShell at 250 k / 1 M / 5 M / 10 M triangles baked through the product's import door - or nothing:
# a row whose shell is missing or STALE (another bake producer's) FAILS and names this tool (scale_world.cpp
# requireShell). The bake is minutes and gigabytes, so no row ever does it under its 60-s cap; this tool does it once into
# the shared scale cache every tree on the box reads (tests/scale/CMakeLists.txt JAH_SCALE_ASSET_CACHE_ROOT, one
# sub-directory a bake format), re-baking a stale shell in place and skipping a current one (a no-op on a warm cache).
#
# WHAT IT DOES: builds the tool target tests/scale/scale_assets_gen (EXCLUDE_FROM_ALL: no ordinary build compiles it) in
# <build-dir> at nice 19 / ionice idle, then runs it at the same priority with the counts given (default: every shell a
# row reads). The tool prints one line a shell: its bake seconds, seconds per million triangles, the DAG stage, the blob,
# the levels and the process's peak memory.
#   --jobs N   the build's width (else $JAH_BUILD_JOBS, else nproc: the box's own answer)
#   --force    re-bake even a current shell
# EXIT: 0 every shell current; 1 a bake failed (the tool's FAIL line names it); 2 refused (no configured build-dir, usage).
set -euo pipefail

usage() {
    echo "usage: scripts/scale-shells.sh <build-dir> [--jobs N] [--force] [<triangles>...]" >&2
    exit 2
}

[[ $# -ge 1 ]] || usage
build=$1
shift
[[ -f "$build/CMakeCache.txt" ]] || { echo "REFUSED: $build is not a configured build tree (no CMakeCache.txt)" >&2; exit 2; }
jobs=${JAH_BUILD_JOBS:-$(nproc)}
args=()
while [[ $# -gt 0 ]]; do
    case $1 in
        --jobs) [[ $# -ge 2 && $2 =~ ^[0-9]+$ ]] || usage; jobs=$2; shift 2 ;;
        --force) args+=(--force); shift ;;
        *) [[ $1 =~ ^[0-9]+$ ]] || usage; args+=("$1"); shift ;;
    esac
done

nice -n 19 ionice -c 3 cmake --build "$build" --target scale_assets_gen -j "$jobs"
exe="$build/tests/scale/scale_assets_gen"
[[ -x "$exe" ]] || { echo "REFUSED: the build made no $exe" >&2; exit 2; }
QT_QPA_PLATFORM=offscreen nice -n 19 ionice -c 3 "$exe" "${args[@]}"
