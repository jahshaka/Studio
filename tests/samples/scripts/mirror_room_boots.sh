#!/usr/bin/env bash
# samples.mirror_room_boots — REFLECT-STATE-1: the Mirror Room picture is ONE picture.
#
# ATOM-DECODE-CLASS-1 recorded the sample flipping between TWO stable pictures across runs
# (29,118 px on the reflective sphere, torus and mirror; base and new binaries alike). This
# row boots the app N times (default 10), each on a VIRGIN data root, opens the sample, settles
# 600 fixed-clock frames and takes the scene's offscreen picture; every picture must be
# byte-identical. Measured on d7-engine-fixes-1 (2026-09-28): 10 of 10 equal here and 6 of 6
# through the on-screen viewport (the ADC1 grab) — the bimodality did not reproduce after
# ATOM-S3-CARDCAP; this row keeps it from coming back.
# usage: mirror_room_boots.sh <app> <boot.js.in> <workdir> [boots]
set -u
APP="$1"; SCRIPT_IN="$2"; WORK="$3"; N="${4:-10}"
rm -rf "$WORK"; mkdir -p "$WORK" || exit 1
declare -A seen
for k in $(seq 1 "$N"); do
    dr="$WORK/dr-$k"; shot="$WORK/shot-$k.png"; js="$WORK/boot-$k.js"
    mkdir -p "$dr"
    sed "s#@SHOT@#$shot#g" "$SCRIPT_IN" > "$js"
    ( cd "$(dirname "$APP")" && env JAHSHAKA_NO_DITHER=1 timeout -k 10 300 "$APP" --data-root "$dr" --script "$js" \
        > "$WORK/boot-$k.log" 2>&1 )
    rc=$?
    if [ $rc -ne 0 ] || [ ! -s "$shot" ]; then
        echo "FAIL: boot $k exited $rc with no picture (log: $WORK/boot-$k.log)"; tail -5 "$WORK/boot-$k.log"; exit 1
    fi
    sha=$(sha256sum "$shot" | cut -c1-16)
    seen[$sha]=$(( ${seen[$sha]:-0} + 1 ))
    echo "boot $k: $sha"
    rm -rf "$dr"
done
if [ ${#seen[@]} -ne 1 ]; then
    echo "FAIL: $N boots gave ${#seen[@]} different pictures:"; for s in "${!seen[@]}"; do echo "  $s x ${seen[$s]}"; done
    exit 1
fi
echo "ok: $N boots, one picture (${!seen[@]})"
rm -rf "$WORK"
