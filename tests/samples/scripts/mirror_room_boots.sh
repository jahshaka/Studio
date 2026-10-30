#!/usr/bin/env bash
# samples.mirror_room_boots.boot1 / .boot2 — REFLECT-STATE-1: the Mirror Room picture is ONE picture.
#
# ATOM-DECODE-CLASS-1 recorded the sample flipping between TWO stable pictures across runs
# (29,118 px on the reflective sphere, torus and mirror; base and new binaries alike). Each row
# boots the app ONCE on a VIRGIN data root (the shader cache WARM: below), opens the sample, settles 600 fixed-clock frames and
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
# THE DATA ROOT VIRGIN, THE SHADER CACHE WARM (PHOTON-7 stage 11, the lead's ruling): the law is ONE picture across two
# boots of a virgin data root - settings, library, store - and the shader cache is not part of it. A picture that hung on
# whether a shader came from the cache or a compile would be the COMPILE-TIMED class (COLD-BOOT-2: a GI decision's frame
# never depends on a compile's timing), which has its own row with the cache cold by design. ATOM-DECODE-CLASS-1's bistable
# picture was the tile capture's decode state and the boot pair's later cause the view-creation race, neither the cache.
# Cold, each boot compiled ~300 shaders (49.6 s of its 60 under load) and timed out once a fork bump grew every PBS
# permutation. Each boot's home is fresh_home.sh --warm's (the tree's shared cache hard-linked in; wiped after a green run).
home="$WORK/home-$K"; dr="$home/.local/share/Jahshaka"; shot="$WORK/shot-$K.png"; js="$WORK/boot-$K.js"
sed "s#@SHOT@#$shot#g" "$SCRIPT_IN" > "$js"
HERE=$(cd "$(dirname "$0")" && pwd)
FRESH="$HERE/../../support/fresh_home.sh"
APPDIR=$(dirname "$APP")
sh "$FRESH" --warm "$home" -- sh -c 'cd "$1" && exec env HOME="$2" JAHSHAKA_NO_DITHER=1 timeout -k 10 300 "$3" --data-root "$4" --script "$5"' \
    sh "$APPDIR" "$home" "$APP" "$dr" "$js" > "$WORK/boot-$K.log" 2>&1
rc=$?
if [ $rc -ne 0 ] || [ ! -s "$shot" ]; then
    echo "FAIL: boot $K exited $rc with no picture (log: $WORK/boot-$K.log)"; tail -5 "$WORK/boot-$K.log"; exit 1
fi
sha=$(sha256sum "$shot" | cut -c1-16)
echo "boot $K: $sha"
grep -E '^mirror_room_boot: the shot settled' "$WORK/boot-$K.log" | head -1
# MEASURING ARM (DETERMINISM-1 stage 0): the boot's own logs kept beside the shot, the work dir kept after a green boot 2
for f in $(find "$home" -name '*.log'); do cp "$f" "$WORK/boot-$K.$(basename "$f")"; done
rm -rf "$home"
[ "$K" = 1 ] && exit 0
first=$(sha256sum "$WORK/shot-1.png" | cut -c1-16)
if [ "$sha" != "$first" ]; then
    echo "FAIL: 2 boots gave 2 different pictures: boot 1 $first, boot 2 $sha (kept: $WORK)"; exit 1
fi
echo "ok: 2 boots, one picture ($sha)"
