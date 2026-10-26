#!/usr/bin/env bash
# scripting.e2e.resize_rest.a / .b / .c (PHOTON-7 stage 10-3) - A WINDOW's RESIZE LEAVES THE SURFACE CACHE THE REST SET OF
# THE FRUSTUM IT ENDS AT (resize_rest_boot.js.in says the laws and their defects). Each row boots the app ONCE on a VIRGIN
# data root, the window taken to 700 x 520 and then 1280 x 800 in every boot: boot a before the open (the control: its
# shot, its resident set and its viewport kept), boot b inside the open's settle, boot c after the open's rest. Boots b
# and c each require boot a's viewport, a rest decision armed with both readers, boot a's set and a shot within one code.
# usage: resize_rest_boots.sh <app> <boot.js.in> <workdir> <a|b|c>
set -u
APP="$1"; SCRIPT_IN="$2"; WORK="$3"; K="$4"
case "$K" in
    a) rm -rf "$WORK"; mkdir -p "$WORK" || exit 1 ;;
    b|c) [ -s "$WORK/shot-a.png" ] && grep -q 'SET-A ' "$WORK/boot-a.log" ||
             { echo "FAIL: boot a left no picture and set in $WORK (scripting.e2e.resize_rest.a runs first)"; exit 1; } ;;
    *) echo "FAIL: boot '$K' - a, b or c"; exit 2 ;;
esac
setA=""; vpA=""
if [ "$K" != a ]; then
    setA=$(grep -m1 -o 'SET-A [0-9a-f]*' "$WORK/boot-a.log" | cut -c7-)
    vpA=$(grep -m1 -o 'VIEWPORT-A [0-9]* x [0-9]*' "$WORK/boot-a.log" | cut -c12-)
fi
dr="$WORK/dr-$K"; js="$WORK/boot-$K.js"
rm -rf "$dr"; mkdir -p "$dr"
sed -e "s#@WORK@#$WORK#g" -e "s#@MODE@#$K#g" -e "s#@SET_A@#$setA#g" -e "s#@VIEWPORT_A@#$vpA#g" "$SCRIPT_IN" > "$js"
( cd "$(dirname "$APP")" && env JAHSHAKA_NO_DITHER=1 timeout -k 10 300 "$APP" --data-root "$dr" --script "$js" \
    > "$WORK/boot-$K.log" 2>&1 )
rc=$?
grep -E '^resize_rest:' "$WORK/boot-$K.log"
if [ $rc -ne 0 ] || [ ! -s "$WORK/shot-$K.png" ]; then
    echo "FAIL: boot $K exited $rc (log: $WORK/boot-$K.log)"
    grep -E 'Error: |A RESIZE LEFT|ARMED WITHOUT|NO CARD RESIDENT|NOT THE FRUSTUM|LAYOUT DIFFERS' "$WORK/boot-$K.log" | tail -3
    exit 1
fi
rm -rf "$dr"
case "$K" in
    a) exit 0 ;;
    b) echo "ok: a resize inside the open's settle did not take the surface cache away" ;;
    c) echo "ok: a resize of a decided cache rests at the set and the picture of a boot opened at that size" ;;
esac
