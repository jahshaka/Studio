#!/bin/bash
# gi.walk (PHOTON-7 stage 0; SPECS/PHOTON_7_SPEC.md L6: THE WALK IS THE PRODUCT) — THE WALKING ROW: what a walk through
# the world's light shows, per material class, and what each moving frame costs on both clocks.
# jahlab at Epic, 960x540, on two of its environments through the stillness door's moving dump (trail + dump) and its
# GI-freeze arm (freeze: the gather and the reflection keep their histories and take no sample, no GI cache works):
#   photon-court: the 15 m crossing, 0,3,18 -> 0,3,3 looking down the court, 60 frames;
#   photon-glass: the 2.4 m walk, -1.2,1.8,3.4 -> 1.2,1.8,1.0 looking at 0,0.5,-3 (the glossy panes, the mirror, the
#     pillars), 60 frames;
#   each from a hold of the FEWEST FRAMES THE LAWS NEED (PHOTON-7 stage 5, the lead's ruling: the row had lost its
#   margin at 59.5 s): L1 and L2 need none (the arms run the same deterministic frames); M1 and the frame law need the
#   GI's own rest at the walk's start, so the live walk's work is the walk's - measured at frame 80 in both scenes
#   (GiStatus::giAtRest, 20-frame steps) - plus the gather's hold (17): the court 120 (was 300); and a FROZEN walk needs
#   the readers HELD at its start (the stillness door refuses it otherwise: a frozen unconverged history makes M1 measure
#   the convergence) - the glass's reflection rests from ~frame 80 and holds K_max + 1 = 513 frames later: 600 (was 640).
#   The glass's M1 runs beside its second frozen walk (a frozen session reads no clock), its L1 after it.
# Each scene: a PRIME session (the walk's two poses, 3 frames each: every shader variant the walk draws compiled into the
# row's warm cache), then three sessions of the same script - LIVE, FROZEN and FROZEN again.
# M1, THE WALKING FLICKER (walk_m1.py): per pair of consecutive moving frames, the residual of this frame against the
# frame before reprojected (the gather's own distance and surface test), live less frozen per pixel - per class (plaster
# >= 0.8 roughness, rough, glossy < 0.4, mirror, foliage = cut-out, mover): mean / p99 / share over one code, each the
# mean over the 59 pairs. THE HITCH LAW over the live walk's 59 moving frames on the WALL clock (renderOneFrame) and the
# GPU clock (the record's busy span: span less the GPU's idle waits on a submission - THE one per-frame GPU time;
# frame_gpu_ms) - p50 / p95 / max and the frames over twice the median - printed with the frame clock's parts.
# THE WORK LAW'S DOOR (PHOTON-7 stage 2), printed after each live walk from `jahlab status` (WORK lines): per cascade the
# commits, the voxels their delta injections lit (the slab's + the changed direct set) against the cascade, the cells
# the direct was computed over and one pass covered, the lag; and the frames a commit waited for its shader.
# THE ROW ASSERTS ONLY ITS OWN LAWS (the bars are set by the stages that improve the numbers; the base is below):
#   (L1) FROZEN = FROZEN: the second frozen walk byte-equal to the first on every frame - the first with the gather's
#        history readback (its dump), the second without (dumprgb): the readback changes no picture;
#   (L2) THE ARMS START EQUAL: the converged hold before the walk byte-equal live vs frozen (one warm cache: AB G10);
#   (L3) THE CLASSES COVER THE COUNTED PIXELS: no pixel M1 counts is unclassified, and every pair has its class map
#        (--classes: an environment's map is the law, never vacuous - PHOTON-7 stage 0's read R5).
# THE BASE (PHOTON-7 stage 0, irisgl b0371442, Epic, 960x540, warm, clocks unlocked; M1 mean / p99 / share > 1 code;
# M1 is deterministic run to run, the clocks are not):
#   court  all +0.002 / 0.92 / 0.32 %; plaster -0.000 / 0.88 / 0.08 %; foliage +0.059 / 5.54 / 8.96 % (no rough,
#          glossy, mirror pixels); wall p50 / p95 / max 5.4 / 7.4 / 12.2 ms (1 frame over 2x the median); GPU busy
#          7.4 / 16.9 / 18.5 (4 over: the cascade commits, ifd.enter)
#   glass  all +0.036 / 2.55 / 2.12 %; plaster +0.002 / 0.89 / 0.17 %; glossy +0.093 / 4.50 / 5.37 %; mirror -0.000 /
#          0.28 / 0.13 %; mover +0.008 / 1.28 / 1.30 % (15 pairs; no rough pixels); wall 5.3 / 7.0 / 7.9 (0 over);
#          GPU busy 2.9 / 45.5 / 52.0 (28 over: vct.cascade0 36.4 ms, vct.cascade1 21.8, vct.light.settle 32-34,
#          ifd.enter / ifd.converge 4-10 a frame from the walk's 33rd frame on)
#   Bistro (gi.walk_bistro, the measure): the 4 m walk all +0.064 / 9.31 / 7.52 % (90 pairs), the fly +0.029 / 8.78 /
#          9.06 % (15); wall 8.4 / 10.2 / 13.1 (0 over), GPU busy 10.0 / 12.3 / 23.9 (1 over); the fly 8.3 / 9.3 / 9.3
#          and 9.2 / 10.9 / 10.9 (0 over)
# usage: walk.sh <jahlab> <work dir>
#        walk.sh <jahlab> <work dir> --rough   (gi.walk_rough: photon-rough's walk, the rough class's M1 - 7-3)
#        walk.sh <jahlab> <work dir> --bistro <model.fbx> <data root>   (gi.walk_bistro, the MEASURE row: its own
#        data root - the bake and the shader cache of Bistro, never the fixtures' - and no class map: M1 'all' only)
set -u
LAB=${1:?jahlab}; WD=${2:?work dir}; MODE=${3:-envs}
[ -n "${DISPLAY:-}" ] || { echo "FAIL: no DISPLAY (the rig's)"; exit 1; }
HERE=$(cd "$(dirname "$0")" && pwd)
rm -rf "$WD"; mkdir -p "$WD"
session() {   # name, the scene's source flags... (the script is $WD/<name>.lab)
    local name=$1; shift
    "$LAB" --headless --no-stdin --size 960x540 "$@" --script "$WD/$name.lab" > "$WD/$name.log" 2>&1
    rc=$?
    [ $rc -eq 0 ] || { echo "FAIL: jahlab ($name) exited $rc"; grep -E 'LAB (REFUSED|FAILED)|ERROR|stillness:' "$WD/$name.log" | head -5; tail -3 "$WD/$name.log"; exit 1; }
    grep -E '^LAB (FIRST FRAME|END|LOAD)' "$WD/$name.log" | cut -c1-160 | sed "s/^/  $name: /"
}
walk() {   # scene, source flags (one word), settle, start, target0, end, target1, frames, extra walk, trail, arms...
    local scene=$1 src=$2 settle=$3 s=$4 t0=$5 e=$6 t1=$7 n=$8 extra=$9 trail=${10}
    shift 10
    if [ "$PRIME" = 1 ]; then
        printf 'camera %s,%s\nframes 3\ncamera %s,%s\nframes 3\nquit\n' "$s" "$t0" "$e" "$t1" > "$WD/$scene-prime.lab"
        session "$scene-prime" $src
    fi
    for arm in "$@"; do
        local f=""; [ "${arm#frozen}" != "$arm" ] && f=" freeze"
        # (the second frozen walk is read for L1 alone - byte for byte against the first: its pictures alone, dumprgb,
        # with no history readback - so L1 also bars a readback that changes a picture, PHOTON-7 stage 5)
        local dk="dump"; [ "$arm" = frozen2 ] && dk="dumprgb"
        # (Bistro's arms boot on the SAME cache bytes: its data root's cache as the run found it, restored before each
        # arm - a session writes the variants it compiled at its exit, and a grown cache draws other variants, AB G10)
        if [ -n "${SNAP:-}" ]; then rm -rf "$ROOT/shadercache"; cp -a "$SNAP" "$ROOT/shadercache"; fi
        # (the window after the stop: 8 frames - the stop frame's history count, the door's mask, comes back a few
        # frames late; M1 and the hitch law read the trail, the moving frames, only)
        printf 'camera %s,%s\nframes %d\nstillness teleport:%s,%s/hold:1/walk:%s,%s:%d%s 8 trail=%d %s=%s-%s%s out=%s-%s\nstatus\nquit\n' \
            "$s" "$t0" "$settle" "$s" "$t0" "$e" "$t1" "$n" "$extra" "$trail" "$dk" "$scene" "$arm" "$f" "$scene" "$arm" \
            > "$WD/$scene-$arm.lab"
        session "$scene-$arm" $src
        [ "$arm" = live ] && work "$scene" "$WD/$scene-$arm.log"
    done
}
work() {   # scene, log - THE WORK LAW'S DOOR after the live walk (PHOTON-7 stage 2): per cascade, the commits, the voxels
           # their delta injections lit (slab + changed direct set) against the cascade, the direct's computed cells,
           # the voxels every delta MARCHED (an outer commit's inner cascades' re-marches included: stage 2's read D4),
           # the lag, and THE FEEDBACK (PHOTON-7 stage 4): the frames a cascade ran a pass and the bricks they ran - an
           # inner cascade's are its RESUMED passes after the outer commits; the commit frames' GPU time is the hitch
           # law's own (frame_gpu_ms, below)
    grep -m1 '^LAB STATUS ' "$2" | cut -c12- | python3 -I -c "
import json, sys
scene = sys.argv[1]
st = json.load(sys.stdin)
print('  WORK %s: the commits waited %d frame(s) for their shader (built in the background)'
      % (scene, st.get('gi', {}).get('commitShaderWaits', 0)))
for i, c in enumerate(st.get('gi', {}).get('cascades', [])):
    n = c['scrolls']; whole = c['res'] ** 3; bricks = whole // 512
    print('  FEEDBACK %s c%d: %d frames ran a pass, %d bricks run (%.1f a pass, of %d), %d stops, %d resumes, %d active; '
          'deltas marched %d voxels' % (scene, i, c['fbPasses'], c['fbBricksRan'], c['fbBricksRan'] / max(c['fbPasses'], 1),
                                       bricks, c['fbStops'], c['fbResumes'], c['fbActive'], c['marched']))
    if not n:
        print('  WORK %s c%d: no scroll committed' % (scene, i)); continue
    k = max(c['counted'], 1); lit = (c['litSlab'] + c['litBand']) / k
    print('  WORK %s c%d: %d commits (%d delta injections counted); lit a delta %.0f = slab %.0f + band %.0f of %d '
          '(%.2f %%); slab cells %.0f, direct computed %.0f (%.1f %%); lag last %d max %d frames'
          % (scene, i, n, c['counted'], lit, c['litSlab'] / k, c['litBand'] / k, whole, 100.0 * lit / whole,
             c['slabCells'] / n, c['injectCells'] / n, 100.0 * c['injectCells'] / n / whole, c['lagLast'], c['lagMax']))
print('  FEEDBACK %s: the chain came to rest %d time(s)' % (scene, st.get('gi', {}).get('feedbackRests', 0)))
" "$1"
}
PRIME=1
if [ "$MODE" = "--bistro" ]; then
    MODEL=${4:?model}; ROOT=${JAH_LAB_BISTRO_ROOT:-${5:?data root}}   # the run's own data root, else the lab's default
    PRIME=0   # its own data root's cache is Bistro's (no prime: a Bistro session is ~20 s); the arms boot on its bytes
    [ -f "$MODEL" ] || { echo "SKIP: the model $MODEL is not on this box (the owner's demo data)"; exit 77; }
    pgrep -x Jahshaka > /dev/null && { echo "SKIP: an app instance runs - Bistro is measured on a card nobody else uses"; exit 77; }
    # THE BAKE MUST BE CACHED (4 min of CPU otherwise): a bake-only probe that finds it returns at once
    timeout 30 "$LAB" --bake-only --data-root "$ROOT" "$MODEL" > "$WD/bake.log" 2>&1
    grep -q "CACHE HIT" "$WD/bake.log" || { echo "SKIP: $ROOT holds no bake of $MODEL - run jahlab --bake-only --data-root $ROOT $MODEL first, off the card"; exit 77; }
    # THE SHADER CACHE MUST BE WARM FOR THIS TREE (urgent #32): a row never compiles its scene's shaders inside its 60 s
    # (the walk's tier - jahlab's default, epic - and its two poses: a stamp at another tier or other cameras is cold)
    JAHLAB="$LAB" bash "$HERE/../../scripts/lab/warm.sh" --check --data-root "$ROOT" --tier epic \
        --camera -10.365,11.211,4.793,-4.461,9.705,-0.392 --camera -7.413,10.458,2.200,-4.461,9.705,-0.392 "$MODEL" ||
        { echo "FAIL: the Bistro shader cache is cold for this tree at the walk's tier and poses - warm it first (above)"; exit 1; }
    rm -rf "$ROOT/stillness/dump-bistro-"*
    SNAP="$WD/shadercache.snap"
    [ -d "$ROOT/shadercache" ] && cp -a "$ROOT/shadercache" "$SNAP" || SNAP=""
    # THE 4 m WALK (90 frames: 0.044 m a frame) THEN THE FLY BACK AT 0.25 m A FRAME (16 frames): one trail of 106 -
    # the hold, the walk's 90, the fly's first 15 (its 16th is the stop)
    # THE HOLD, THE FEWEST FRAMES ITS LAWS NEED (PHOTON-7 stage 5, the lead's ruling - measured once at the walk's start
    # pose: the GI's last row at frame 98, the gather held from 115, the reflection held from 611): a frozen walk needs
    # the readers held (the stillness door refuses it otherwise) - 620 (was 800)
    walk bistro "--data-root $ROOT $MODEL" 620 -10.365,11.211,4.793 -4.461,9.705,-0.392 -7.413,10.458,2.200 \
         -4.461,9.705,-0.392 90 "/walk:-10.365,11.211,4.793,-4.461,9.705,-0.392:16" 106 live frozen
    python3 -I "$HERE/walk_m1.py" "$ROOT/stillness" bistro bistro-live bistro-frozen --parts walk:1:90,fly:91:105 \
        --tsv "$ROOT/stillness/bistro-live.tsv"
    rc=$?
    rm -rf "$ROOT/stillness/dump-bistro-"*
    echo "gi.walk_bistro: $([ $rc -eq 0 ] && echo MEASURED || echo FAIL)"
    exit $rc
fi
analyse() {   # scene - its M1, its laws and its clocks (walk_m1.py), into $WD/<scene>-m1.out
    python3 -I "$HERE/walk_m1.py" "$WD/data/stillness" $1 $1-live $1-frozen $1-frozen2 \
        --tsv "$WD/data/stillness/$1-live.tsv" --classes > "$WD/$1-m1.out" 2>&1
}
if [ "$MODE" = "--rough" ]; then
    # gi.walk_rough (PHOTON-7 stage 7, 7-3): THE ROUGH SPECULAR'S WALK - photon-rough's four rough metal strips (0.45 -
    # 0.75), a 2 m walk along them, 0,1.6,2.4 -> 0,1.6,0.4 looking at 0,0,-3, 60 frames; the rough class's M1 is the
    # premise 7-3 is measured on (the cone with no history before it, the gather's probes after) - PRINTED, the laws
    # the walk's own (L2, L3); its hold the glass's (600: the 0.45 strip's ray reflection held before the frozen walk)
    walk rough "--data-root $WD/data --env photon-rough" 600 0,1.6,2.4 0,0,-3 0,1.6,0.4 0,0,-3 60 "" 60 live frozen
    python3 -I "$HERE/walk_m1.py" "$WD/data/stillness" rough rough-live rough-frozen \
        --tsv "$WD/data/stillness/rough-live.tsv" --classes
    rc=$?
    rm -rf "$WD/data/stillness/dump-"*
    echo "gi.walk_rough: $([ $rc -eq 0 ] && echo PASS || echo FAIL)"
    exit $rc
fi
SETTLE=120    # the court (the header: the GI's rest at 80 + the gather's 17-frame hold, rounded up)
GSETTLE=600   # the glass: its reflection HELD before a frozen walk (the door's precondition: rest from ~frame 80, + K_max + 1)
walk court "--data-root $WD/data --env photon-court" $SETTLE 0,3,18 0,3,-6 0,3,3 0,3,-21 60 "" 60 live frozen frozen2
walk glass "--data-root $WD/data --env photon-glass" $GSETTLE -1.2,1.8,3.4 0,0.5,-3 1.2,1.8,1.0 0,0.5,-3 60 "" 60 live
# (the analyses are CPU work: they run beside the glass's FROZEN sessions, which read no clock - never beside a live
# arm, whose wall and GPU clocks the hitch law reads; the glass's M1 beside its second frozen walk, its L1 after it)
analyse court & apid=$!
PRIME=0   # (primed above, with the glass's live arm)
walk glass "--data-root $WD/data --env photon-glass" $GSETTLE -1.2,1.8,3.4 0,0.5,-3 1.2,1.8,1.0 0,0.5,-3 60 "" 60 frozen
python3 -I "$HERE/walk_m1.py" "$WD/data/stillness" glass glass-live glass-frozen \
    --tsv "$WD/data/stillness/glass-live.tsv" --classes > "$WD/glass-m1.out" 2>&1 & gpid=$!
walk glass "--data-root $WD/data --env photon-glass" $GSETTLE -1.2,1.8,3.4 0,0.5,-3 1.2,1.8,1.0 0,0.5,-3 60 "" 60 frozen2
fails=0
wait $apid || fails=1
wait $gpid || fails=1
python3 -I "$HERE/walk_m1.py" "$WD/data/stillness" glass glass-live glass-frozen glass-frozen2 --l1-only \
    > "$WD/glass-l1.out" 2>&1 || fails=1
cat "$WD/court-m1.out" "$WD/glass-l1.out" "$WD/glass-m1.out"
rm -rf "$WD/data/stillness/dump-"*   # the dumps (~270 MB a walk) once their numbers are printed
echo "gi.walk: $([ $fails -eq 0 ] && echo PASS || echo FAIL)"
exit $fails
