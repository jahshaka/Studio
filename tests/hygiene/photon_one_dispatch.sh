#!/usr/bin/env bash
#
# source.photon_one_dispatch — EVERY PHOTON COMPUTE DISPATCH GOES THROUGH THE ONE DOOR
# (ENGINE-UPGRADE-2B stage 3; photon/PhotonDispatch.h).
#
# photonDispatch makes a Photon dispatch with its barriers and notes it for the GI region's
# witness (PhotonDispatchStatus: in / outside the region), so the ASYNC bracket that wraps the
# region sees every one. A raw `->dispatch(` / `.dispatch(` on an HlmsCompute under
# src/photon or in OgreSurfaceCache.cpp (the cards' relight) is a dispatch the witness never
# sees. The door itself (PhotonComponent.cpp's photonDispatch) is the one allowed call.
#
# $1 = the repo root
set -u

ROOT="${1:?usage: photon_one_dispatch.sh <source-root>}"
SRC="$ROOT/irisgl/engine/src"
[ -d "$SRC/photon" ] || { echo "source.photon_one_dispatch: FAIL — no $SRC/photon"; exit 1; }
[ -f "$SRC/OgreSurfaceCache.cpp" ] || { echo "source.photon_one_dispatch: FAIL — no $SRC/OgreSurfaceCache.cpp"; exit 1; }
# Code lines only (a line whose first non-blank is // is a comment).
hits=$(grep -rnE '(->|\.)dispatch[[:space:]]*\(' "$SRC/photon" "$SRC/OgreSurfaceCache.cpp" \
           --include='*.cpp' --include='*.h' | grep -vE '^[^:]+:[0-9]+:[[:space:]]*//')
door=$(printf '%s\n' "$hits" | grep -cE '/photon/PhotonComponent\.cpp:[0-9]+:[[:space:]]*hc->dispatch\(job, 0, 0\);')
raw=$(printf '%s\n' "$hits" | grep -vE '/photon/PhotonComponent\.cpp:[0-9]+:[[:space:]]*hc->dispatch\(job, 0, 0\);' | grep -c .)
if [ "$door" != 1 ]; then
    echo "source.photon_one_dispatch: FAIL — the door (PhotonComponent.cpp photonDispatch's hc->dispatch) found $door time(s), want 1"
    exit 1
fi
if [ "$raw" = 0 ]; then
    echo "source.photon_one_dispatch: ok — every Photon compute dispatch goes through photonDispatch"
    exit 0
fi
printf '%s\n' "$hits" | grep -vE '/photon/PhotonComponent\.cpp:[0-9]+:[[:space:]]*hc->dispatch\(job, 0, 0\);'
echo "source.photon_one_dispatch: FAIL — $raw raw dispatch(es) above: route each through"
echo "    jahshaka::engine::detail::photonDispatch (photon/PhotonDispatch.h)"
exit 1
