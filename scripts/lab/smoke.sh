#!/bin/bash
# scripts/lab/smoke.sh — THE OWNER'S ONE LINE (LAB-1 stage 3; DOCS/LAB.md): jahlab on the CURRENT display with a model
# at the smoke CAM (14,5,22 looking at 2,0.5,4) — or an environment at its first dolly stop.
#
#   scripts/lab/smoke.sh ~/Downloads/demo-scenes/amazon-lumberyard-bistro/prepared/BistroExterior.fbx
#   scripts/lab/smoke.sh --env atom-street
#
#   scripts/lab/smoke.sh --desktop <model | --env NAME>   (the OWNER's desktop: a DISPLAY that is not a rig, :0)
#
# Any further arguments are jahlab's (--tier high, --data-root DIR, --script FILE ...; `jahlab --help`). It NEVER sets
# DISPLAY: the window opens where the shell that launched it lives. A RIG display (:60-:99, an agent's Xvfb, under
# scripts/gpu-exclusive.sh) needs nothing more; ANY OTHER - the owner's :0 - needs --desktop FIRST, else it is REFUSED
# (exit 2): the owner's display is opened only on purpose, by the lead at his word, and the flag makes the purpose visible
# in the command (the lead's ruling, 2026-11-21, URGENT #195; scripts/display-guard.sh). A model is baked first
# (jahlab --bake-only into the same data root: minutes the first time, a hash after - jahlab itself never bakes in a
# run, the urgent list's #30); every later open reads the bake. F1 in the window lists the keys.
set -u
here="$(cd "$(dirname "$0")" && pwd)"
lab="${JAHLAB:-$here/../../build-linux/bin/jahlab}"
desktop=0; [ "${1:-}" = --desktop ] && { desktop=1; shift; }
[ -n "${DISPLAY:-}" ] || { echo "smoke.sh: no DISPLAY — run it from the desktop with --desktop (or on an agent's Xvfb)" >&2; exit 2; }
. "$here/../display-guard.sh"
if ! jah_rig_display "$DISPLAY" && [ "$desktop" != 1 ]; then
    echo "smoke.sh: REFUSED (exit 2): DISPLAY=$DISPLAY is not a rig display (60-99) - the OWNER's display opens only on purpose:" \
         "$here/smoke.sh --desktop $*" >&2
    exit 2
fi
[ "$#" -gt 0 ] || { echo "usage: smoke.sh [--desktop] <model file | --env NAME> [jahlab flags...]" >&2; exit 64; }
[ -x "$lab" ] || { echo "smoke.sh: no jahlab at $lab (build the tree, or set JAHLAB)" >&2; exit 2; }
if [ "$1" = "--env" ]; then
    exec "$lab" "$@"
fi
file="$1"; shift
# THE BAKE first, into the data root the run reads (its --data-root, else jahlab's default)
root=()
prev=""
for a in "$@"; do
    [ "$prev" = "--data-root" ] && root=(--data-root "$a")
    prev="$a"
done
"$lab" --bake-only "$file" "${root[@]}" || { echo "smoke.sh: the bake of $file failed" >&2; exit 4; }
exec "$lab" --camera 14,5,22,2,0.5,4 "$@" "$file"
