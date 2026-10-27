#!/usr/bin/env bash
# scripting.e2e.resize_history.a / .b (PHOTON-7 stage 10-3) - A WINDOW's RESIZE HISTORY LEAVES NO TRACE IN THE PICTURE AT
# REST, AT ONE VIEWPORT (resize_history_boot.js.in says the law and the defect). Each row boots the app ONCE on a VIRGIN
# data root, the window through one history (700 x 520, then 1280 x 800) before fixture B: boot a keeps its shot and its
# viewport; boot b takes the window through the history again with the scene live, and requires boot a's viewport and
# its shot within one code of boot a's.
# usage: resize_history_boots.sh <app> <boot.js.in> <workdir> <a|b>
set -u
APP="$1"; SCRIPT_IN="$2"; WORK="$3"; K="$4"
# THE SHADER CACHE COMES WARM (2026-10-27, the same form as a fixture's WARM: tests/support/fresh_home.sh --warm): the
# virgin data root links the tree's ONE shared app cache (<build>/tests/.shadercache, under its lock, shared; the cache
# writes by rename, so a link is never written through; its lock file never linked) - the row's law is the resize's, not
# a cold boot's; cold, each boot compiled ~300 shaders (40 s) and the rows sat at their 60 s cap.
seed_shaders() {   # seed_shaders <data root>
    local shared
    shared="$(cd "$(dirname "$APP")/.." && pwd)/tests/.shadercache"
    [ -d "$shared" ] || return 0
    mkdir -p "$1/shadercache"
    (
        flock -s -w 60 9 || exit 0
        ( cd "$shared" && find . -type f ! -name cache.lock -print ) | while IFS= read -r f; do
            mkdir -p "$1/shadercache/$(dirname "$f")"
            ln "$shared/$f" "$1/shadercache/$f" 2>/dev/null || cp -p "$shared/$f" "$1/shadercache/$f" 2>/dev/null || true
        done
    ) 9>"$shared.lock"
}
case "$K" in
    a) rm -rf "$WORK"; mkdir -p "$WORK" || exit 1 ;;
    b) [ -s "$WORK/shot-a.png" ] || { echo "FAIL: boot a left no picture in $WORK (scripting.e2e.resize_history.a runs first)"; exit 1; } ;;
    *) echo "FAIL: boot '$K' - a or b"; exit 2 ;;
esac
dr="$WORK/dr-$K"; shot="$WORK/shot-$K.png"; js="$WORK/boot-$K.js"
rm -rf "$dr"; mkdir -p "$dr"
seed_shaders "$dr"
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
