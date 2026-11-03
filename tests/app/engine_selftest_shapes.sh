#!/usr/bin/env bash
#
# app.engine_selftest_shapes.{alone,after_default} — FIXTURE B IS A FUNCTION OF ITS OWN SCENE, NEVER OF WHAT THE PROCESS
# DREW BEFORE IT (URGENT #157). By physics a second scene renders exactly as if it were opened first: the self-test's
# fixture half (pose pair B and the mover) must hash the same bytes whether it runs alone (JAHSHAKA_SELFTEST_POSES=
# fixture) or after the default scene's three poses, their resizes and a teardown (the full six-pose run). It did not:
# the mover was 122cb3a8 after the default half and 2d88c4ee alone, because the window's resize history left the editor
# view 748 px wide in one and 733 in the other, and the editor view's frames are the scene's path (fixed by stating the
# view's size: selftestrunner.cpp, "the view pinned"). Any state that outlives a scene and reaches a trace, an
# allocation or a rest - an engine member, a cache, a layout - reds this pair.
#
#   alone          the fixture half alone: its B1 / B2 / mover pictures kept in <out>/selftest-shapes/alone (the setup
#                  of the pair: FIXTURES_SETUP selftest_shapes)
#   after_default  the full six-pose run; its B1 / B2 / mover must be BYTE-EQUAL to the alone run's
#                  (FIXTURES_REQUIRED selftest_shapes)
#
# Both without the validation layer (the same configuration on both sides; the layer's API guard is the
# app.engine_selftest_validation.* rows'), both on the short run (JAHSHAKA_SELFTEST_SHORT: the frames are the law's, not
# the shipped hashes'). The bar is equality, exact: a picture is a function of the scene, the eye and that scene's own path.
# usage: engine_selftest_shapes.sh <jahshaka-binary> <out-dir> alone|after_default    (cwd: the binary's directory)
set -u
BIN="$1"
OUT="$2/selftest-shapes"
ARM="${3:?alone|after_default}"
POSES="B1 B2 mover"
case "$ARM" in
    alone)         dir="$OUT/alone";  export JAHSHAKA_SELFTEST_POSES=fixture ;;
    after_default) dir="$OUT/after";  unset JAHSHAKA_SELFTEST_POSES ;;
    *) echo "engine_selftest_shapes: the arm '$ARM' (alone|after_default)"; exit 1 ;;
esac
rm -rf "$dir"
mkdir -p "$dir"
if [ "$ARM" = after_default ]; then
    for p in $POSES; do
        if [ ! -s "$OUT/alone/s.$p.png" ]; then
            echo "engine_selftest_shapes: no alone run's $p picture ($OUT/alone/s.$p.png) - the setup row did not pass"
            exit 1
        fi
    done
fi
"$BIN" --engine-selftest "$dir/s.png" > "$dir/run.log" 2>&1
rc=$?
if [ "$rc" -ne 0 ]; then
    echo "engine_selftest_shapes: the $ARM self-test exited $rc"
    tail -20 "$dir/run.log"
    exit "$rc"
fi
grep -m1 'engine-selftest: fixture B renders at' "$dir/run.log"
bad=0
for p in $POSES; do
    if [ ! -s "$dir/s.$p.png" ]; then
        echo "engine_selftest_shapes: the $ARM run wrote no $p picture"
        exit 1
    fi
    h=$(sha256sum "$dir/s.$p.png" | cut -c1-16)
    if [ "$ARM" = alone ]; then
        echo "engine_selftest_shapes: alone $p $h"
        continue
    fi
    a=$(sha256sum "$OUT/alone/s.$p.png" | cut -c1-16)
    if cmp -s "$OUT/alone/s.$p.png" "$dir/s.$p.png"; then
        echo "engine_selftest_shapes: $p equal ($h) after the default half and alone"
    else
        echo "engine_selftest_shapes: $p DIFFERS - after the default half $h, alone $a: what the process drew before the fixture reached its picture"
        bad=1
    fi
done
exit "$bad"
