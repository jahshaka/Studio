#!/usr/bin/env bash
# scripting.e2e.resize_rest.a / .b (PHOTON-7 stage 10-3) - A WINDOW's RESIZE DOES NOT TAKE THE SURFACE CACHE AWAY
# (resize_rest_boot.js.in says the law and the defect). Each row boots the app ONCE on a VIRGIN data root: boot a opens the
# Mirror Room in a 1280 x 800 window, rests and keeps its shot; boot b opens it in the window's own size, takes the window
# to 700 x 520 and then 1280 x 800 at once, rests, and requires the rest decision armed with both readers, cards resident
# and its shot within one code of boot a's.
# usage: resize_rest_boots.sh <app> <boot.js.in> <workdir> <a|b>
set -u
APP="$1"; SCRIPT_IN="$2"; WORK="$3"; K="$4"
case "$K" in
    a) rm -rf "$WORK"; mkdir -p "$WORK" || exit 1 ;;
    b) [ -s "$WORK/shot-a.png" ] || { echo "FAIL: boot a left no picture in $WORK (scripting.e2e.resize_rest.a runs first)"; exit 1; } ;;
    *) echo "FAIL: boot '$K' - a or b"; exit 2 ;;
esac
dr="$WORK/dr-$K"; js="$WORK/boot-$K.js"
rm -rf "$dr"; mkdir -p "$dr"
sed -e "s#@WORK@#$WORK#g" -e "s#@MODE@#$K#g" "$SCRIPT_IN" > "$js"
( cd "$(dirname "$APP")" && env JAHSHAKA_NO_DITHER=1 timeout -k 10 300 "$APP" --data-root "$dr" --script "$js" \
    > "$WORK/boot-$K.log" 2>&1 )
rc=$?
grep -E '^resize_rest:' "$WORK/boot-$K.log"
if [ $rc -ne 0 ] || [ ! -s "$WORK/shot-$K.png" ]; then
    echo "FAIL: boot $K exited $rc (log: $WORK/boot-$K.log)"
    grep -E 'Error: |A RESIZE LEFT|ARMED WITHOUT|NO CARD RESIDENT' "$WORK/boot-$K.log" | tail -3; exit 1
fi
rm -rf "$dr"
[ "$K" = a ] && exit 0
echo "ok: a resize did not take the surface cache away"
rm -rf "$WORK"
