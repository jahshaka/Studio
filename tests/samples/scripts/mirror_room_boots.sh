#!/usr/bin/env bash
# samples.mirror_room_boots.boot1 / .boot2 — REFLECT-STATE-1: the Mirror Room picture is ONE picture.
#
# ATOM-DECODE-CLASS-1 recorded the sample flipping between TWO stable pictures across runs
# (29,118 px on the reflective sphere, torus and mirror; base and new binaries alike). Each row
# boots the app ONCE on a VIRGIN data root, opens the sample, settles 600 fixed-clock frames and
# takes the scene's offscreen picture; boot 2 requires its picture byte-identical to boot 1's.
# TWO BOOTS, TWO ROWS (TEST-CUT-1; CLAUDE.md Testing: every row at most 60 s): one picture is an
# EQUALITY, and an equality needs two pictures — boot 1 (a fixture setup, its picture kept) and
# boot 2 (it requires boot 1, compares, and cleans up). Ten boots in one row were 322 s.
# usage: mirror_room_boots.sh <app> <boot.js.in> <workdir> <1|2>
set -u
APP="$1"; SCRIPT_IN="$2"; WORK="$3"; K="$4"
case "$K" in
    1) rm -rf "$WORK"; mkdir -p "$WORK" || exit 1 ;;
    2) [ -s "$WORK/shot-1.png" ] || { echo "FAIL: boot 1 left no picture in $WORK (samples.mirror_room_boots.boot1 runs first)"; exit 1; } ;;
    *) echo "FAIL: boot '$K' — 1 or 2"; exit 2 ;;
esac
dr="$WORK/dr-$K"; shot="$WORK/shot-$K.png"; js="$WORK/boot-$K.js"
rm -rf "$dr"; mkdir -p "$dr"
sed "s#@SHOT@#$shot#g" "$SCRIPT_IN" > "$js"
( cd "$(dirname "$APP")" && env JAHSHAKA_NO_DITHER=1 timeout -k 10 300 "$APP" --data-root "$dr" --script "$js" \
    > "$WORK/boot-$K.log" 2>&1 )
rc=$?
if [ $rc -ne 0 ] || [ ! -s "$shot" ]; then
    echo "FAIL: boot $K exited $rc with no picture (log: $WORK/boot-$K.log)"; tail -5 "$WORK/boot-$K.log"; exit 1
fi
sha=$(sha256sum "$shot" | cut -c1-16)
echo "boot $K: $sha"
rm -rf "$dr"
[ "$K" = 1 ] && exit 0
first=$(sha256sum "$WORK/shot-1.png" | cut -c1-16)
if [ "$sha" != "$first" ]; then
    echo "FAIL: 2 boots gave 2 different pictures: boot 1 $first, boot 2 $sha (kept: $WORK)"; exit 1
fi
echo "ok: 2 boots, one picture ($sha)"
rm -rf "$WORK"
