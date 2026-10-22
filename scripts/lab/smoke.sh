#!/bin/bash
# scripts/lab/smoke.sh — THE OWNER'S ONE LINE (LAB-1 stage 3; DOCS/LAB.md): jahlab on the CURRENT display with a model
# at the smoke CAM (14,5,22 looking at 2,0.5,4) — or an environment at its first dolly stop.
#
#   scripts/lab/smoke.sh ~/Downloads/demo-scenes/amazon-lumberyard-bistro/prepared/BistroExterior.fbx
#   scripts/lab/smoke.sh --env atom-street
#
# Any further arguments are jahlab's (--tier high, --data-root DIR, --script FILE ...; `jahlab --help`). It NEVER sets
# DISPLAY: the window opens where the shell that launched it lives (the owner's desktop, or an agent's Xvfb — an
# agent runs it under scripts/gpu-exclusive.sh with DISPLAY=:NN of its own rig, never :0). The first open of a model
# bakes it (minutes, once); every later open reads the bake. F1 in the window lists the keys.
set -u
here="$(cd "$(dirname "$0")" && pwd)"
lab="${JAHLAB:-$here/../../build-linux/bin/jahlab}"
[ -n "${DISPLAY:-}" ] || { echo "smoke.sh: no DISPLAY — run it from the desktop (or an agent's Xvfb)" >&2; exit 2; }
[ "$#" -gt 0 ] || { echo "usage: smoke.sh <model file | --env NAME> [jahlab flags...]" >&2; exit 64; }
[ -x "$lab" ] || { echo "smoke.sh: no jahlab at $lab (build the tree, or set JAHLAB)" >&2; exit 2; }
if [ "$1" = "--env" ]; then
    exec "$lab" "$@"
fi
file="$1"; shift
exec "$lab" --camera 14,5,22,2,0.5,4 "$@" "$file"
