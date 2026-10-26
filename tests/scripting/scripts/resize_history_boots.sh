#!/usr/bin/env bash
# scripting.e2e.resize_history.a / .b (PHOTON-7 stage 10-3) - A WINDOW's RESIZE HISTORY LEAVES NO TRACE IN THE PICTURE AT
# REST, AT ONE VIEWPORT (resize_history_boot.js.in says the law and the defect). Each row boots the app ONCE on a VIRGIN
# data root, the window through one history (700 x 520, then 1280 x 800) before fixture B: boot a keeps its shot and its
# viewport; boot b takes the window through the history again with the scene live, and requires boot a's viewport and
# its shot within one code of boot a's.
# usage: resize_history_boots.sh <app> <boot.js.in> <workdir> <a|b>
set -u
APP="$1"; SCRIPT_IN="$2"; WORK="$3"; K="$4"
case "$K" in
    a) rm -rf "$WORK"; mkdir -p "$WORK" || exit 1 ;;
    b) [ -s "$WORK/shot-a.png" ] || { echo "FAIL: boot a left no picture in $WORK (scripting.e2e.resize_history.a runs first)"; exit 1; } ;;
    *) echo "FAIL: boot '$K' - a or b"; exit 2 ;;
esac
dr="$WORK/dr-$K"; shot="$WORK/shot-$K.png"; js="$WORK/boot-$K.js"
rm -rf "$dr"; mkdir -p "$dr"
vpA=""
[ "$K" = b ] && vpA=$(grep -m1 -o 'boot a VIEWPORT [0-9]* x [0-9]*' "$WORK/boot-a.log" | cut -c17-)
sed -e "s#@SHOT@#$shot#g" -e "s#@SHOT_A@#$WORK/shot-a.png#g" -e "s#@MODE@#$K#g" -e "s#@VIEWPORT_A@#$vpA#g" "$SCRIPT_IN" > "$js"
( cd "$(dirname "$APP")" && env JAHSHAKA_NO_DITHER=1 timeout -k 10 300 "$APP" --data-root "$dr" --script "$js" \
    > "$WORK/boot-$K.log" 2>&1 )
rc=$?
grep -E '^resize_history:' "$WORK/boot-$K.log"
if [ $rc -ne 0 ] || [ ! -s "$shot" ]; then
    echo "FAIL: boot $K exited $rc (log: $WORK/boot-$K.log)"; grep -E 'Error: |RESIZE HISTORY' "$WORK/boot-$K.log" | tail -3; exit 1
fi
rm -rf "$dr"
[ "$K" = a ] && exit 0
echo "ok: a resize history left no trace"
rm -rf "$WORK"
