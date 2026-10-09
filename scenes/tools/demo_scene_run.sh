#!/usr/bin/env bash
# demo_scene_run.sh <script.js> <bin dir> <data root> <source dir> <out dir> [display]
#
# DEMO-SCENES-1: runs one demo-scene build script (make_demo_*.js) into a data root.
# The scripts carry @SRC@ (the downloaded scene's directory, read as DATA) and @OUT@ (where the
# evidence screenshots and the timing JSON go); a --script run has no argv, so they are
# substituted here, the make_grand_showroom.js convention (and @NAME@ from DEMO_NAME, for
# demo_scene_open_check.js, which opens a saved project by name; pass any <source dir> to it). Any data root works, a fresh one included.
# Engine-up (the screenshots and the GI settle are real frames): it needs an X display (an Xvfb at
# 1920x1080x24) and runs through the box's VRAM queue at the lowest priority, holding
# DEMO_VRAM_TOKENS (default 4: these scenes carry hundreds of 2-4k textures, twice an app run's 2).
set -euo pipefail
JS=${1:?script}; BIN=${2:?bin dir}; ROOT=${3:?data root}; SRC=${4:?source dir}; OUT=${5:?out dir}
DISP=${6:-${DISPLAY:-}}
[ -n "$DISP" ] || { echo "no display: pass one (an Xvfb, never the owner's :0)" >&2; exit 2; }
[ "$DISP" = ":0" ] && { echo "refusing :0" >&2; exit 2; }
TREE=$(cd "$(dirname "$0")/../.." && pwd)
mkdir -p "$ROOT" "$OUT"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
sed -e "s|@SRC@|$SRC|g" -e "s|@OUT@|$OUT|g" -e "s|@NAME@|${DEMO_NAME:-}|g" "$JS" > "$TMP/run.js"
cd "$TMP"
# A busy box can hold the VRAM queue longer than gpu-admit's default 900 s wait; a scene build is
# worth waiting for, so the wait is two hours unless the caller says otherwise.
export JAH_VRAM_WAIT=${JAH_VRAM_WAIT:-7200}
DISPLAY=$DISP nice -n 19 ionice -c 3 "$TREE/scripts/gpu-admit.sh" "${DEMO_VRAM_TOKENS:-4}" -- \
    "$BIN/Jahshaka" --data-root "$ROOT" --script "$TMP/run.js"
