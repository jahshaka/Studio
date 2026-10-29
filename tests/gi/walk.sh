#!/bin/bash
# gi.walk_court / gi.walk_glass (PHOTON-7 stage 0; SPECS/PHOTON_7_SPEC.md L6: THE WALK IS THE PRODUCT) — THE WALKING
# ROWS: what a walk through the world's light shows, per material class, and what each moving frame costs on both clocks.
# ONE ROW PER PROOF (the lead's ruling, 2026-10-28): the two scenes were one row, gi.walk, whose frame counts are already
# the fewest its laws need and whose wall reached 70-87 s on a loaded box (its 60 s cap); each scene is its own row now,
# the same sessions, the same laws, the same analyses (--court, --glass).
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
#   the convergence) - the glass: `rest 1200 held` (HARNESS-3 (f): the GI at rest AND every reader HELD, the door's freeze
#   precondition, by the engine's own predicate; it was 600 frames, sized to the old K_max + 1 = 513 after the rest's
#   ~frame 80 - K_max is deleted (no frame count holds a rest), and since 11-2 (8) the glass holds about 1,000 rest frames
#   in: the 600 read "not converged at the walk's start").
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
# usage: walk.sh <jahlab> <work dir> --court   (gi.walk_court: the court's prime + live + frozen + frozen2, its M1, laws, clocks)
#        walk.sh <jahlab> <work dir> --glass-live|--glass-frozen|--glass-frozen2   (gi.walk_glass.live / .frozen /
#            .frozen2, SPLIT BY PROOF in HARNESS-3 (f) - a fixture chain on one work dir: .live the prime + the live walk
#            (its clocks); .frozen the frozen walk + M1 (live against frozen); .frozen2 the second frozen walk + L1)
#        walk.sh <jahlab> <work dir> --area    (gi.walk_area: photon-area's penumbra walk, M1 and the live residual printed)
#        walk.sh <jahlab> <work dir> --bistro <model.fbx> <data root>   (gi.walk_bistro, the MEASURE row: its own
#        data root - the bake and the shader cache of Bistro, never the fixtures' - and no class map: M1 'all' only)
set -u
LAB=${1:?jahlab}; WD=${2:?work dir}; MODE=${3:?--court|--glass-live|--glass-frozen|--glass-frozen2|--area|--bistro}
[ -n "${DISPLAY:-}" ] || { echo "FAIL: no DISPLAY (the rig's)"; exit 1; }
HERE=$(cd "$(dirname "$0")" && pwd)
case "$MODE" in --glass-frozen|--glass-frozen2) mkdir -p "$WD" ;;   # (the chain's later parts read the live part's dir)
                *) rm -rf "$WD"; mkdir -p "$WD" ;; esac
session() {   # name, the scene's source flags... (the script is $WD/<name>.lab)
    local name=$1; shift
    "$LAB" --headless --no-stdin --size 960x540 "$@" --script "$WD/$name.lab" > "$WD/$name.log" 2>&1
    rc=$?
    [ $rc -eq 0 ] || { echo "FAIL: jahlab ($name) exited $rc"; grep -E 'LAB (REFUSED|FAILED)|ERROR|stillness:' "$WD/$name.log" | head -5; tail -3 "$WD/$name.log"; exit 1; }
    grep -E '^LAB (FIRST FRAME|END|LOAD|REST)' "$WD/$name.log" | cut -c1-160 | sed "s/^/  $name: /"
}
walk() {   # scene, source flags (one word), settle (frames, or a lab line: `rest ...`), start, target0, end, target1, frames, extra walk, trail, arms...
    local scene=$1 src=$2 settle=$3 s=$4 t0=$5 e=$6 t1=$7 n=$8 extra=$9 trail=${10}
    shift 10
    case "$settle" in *[!0-9]*) ;; *) settle="frames $settle" ;; esac
    if [ "$PRIME" = 1 ]; then
        printf 'camera %s,%s\nframes 3\ncamera %s,%s\nframes 3\nquit\n' "$s" "$t0" "$e" "$t1" > "$WD/$scene-prime.lab"
        session "$scene-prime" $src
        # (the glass chain's parts boot on ONE cache's bytes - AB G10: a grown cache draws other variants; each part is
        # its own row with its own home - so the live part keeps the primed cache for the frozen parts to restore)
        if [ -n "${SNAP_SAVE:-}" ] && [ -d "${JAHSHAKA_SHADER_CACHE_DIR:-}" ]; then
            rm -rf "$SNAP_SAVE"; cp -a "$JAHSHAKA_SHADER_CACHE_DIR" "$SNAP_SAVE"
        fi
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
        printf 'camera %s,%s\n%s\nstillness teleport:%s,%s/hold:1/walk:%s,%s:%d%s 8 trail=%d %s=%s-%s%s out=%s-%s\nstatus\nquit\n' \
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
if [ "$MODE" = "--area" ]; then
    # gi.walk_area (PHOTON-7 stage 7: the walk 7-A owed, run at the stage's master check) - THE AREA LIGHT'S PENUMBRA
    # WALK: photon-area's plate throwing a 3 m penumbra across the floor and a glossy metal strip through it; a 2.5 m walk
    # sideways under the plate's height, 2,1.4,2.5 -> -0.5,1.4,2.5 looking at 0,0,-1.5, 60 frames; the hold the glass's
    # (600: the strip's ray reflection held before the frozen walk). PRINTED: M1 (live less frozen: the area
    # visibility's history - one stratum a texel a frame, the ratio's two sums - stepping while the camera moves) and
    # THE LIVE RESIDUAL (the live arm's own frame-to-frame residual), per class: the floor's plaster and the strip's
    # glossy. The walk's own laws (L2, L3) asserted - the area visibility's bars are gi.card_lamp_shadow_area's.
    walk area "--data-root $WD/data --env photon-area" 600 2,1.4,2.5 0,0,-1.5 -0.5,1.4,2.5 0,0,-1.5 60 "" 60 live frozen
    python3 -I "$HERE/walk_m1.py" "$WD/data/stillness" area area-live area-frozen \
        --tsv "$WD/data/stillness/area-live.tsv" --classes > "$WD/area-m1.out" 2>&1
    rc=$?
    cat "$WD/area-m1.out"
    rm -rf "$WD/data/stillness/dump-"*
    echo "gi.walk_area: $([ $rc -eq 0 ] && echo PASS || echo FAIL)"
    exit $rc
fi
SETTLE=160    # the court: its cards' last relight at frame 127 + the gather's 17-frame hold, rounded up (PHOTON-7 10-2: the
              # card-to-card re-listing's rounds at 4 cards a frame, the RGBA16F store's quantum - was the GI's rest at 80)
# the glass: every reader HELD before a walk (the door's freeze precondition; the header says why). THE THREE ARMS START
# ON ONE FRAME (L2: their first frames byte-equal): the predicate is read back with a lag, so a wait on it ends a frame
# apart between sessions (measured: live 828, frozen 827 - 2,712 px apart). So the LIVE arm waits on it, `rest 1200
# held`, then 4 frames more (the read-back's bound: ScreenProbeGather.h kReaderStaleFrames, the 3 frames in flight + the
# cache's one frame late), and records n + 4; the frozen arms replay exactly that count - held there by construction.
GSETTLE="rest 1200 held
frames 4"
if [ "$MODE" = "--court" ]; then
    walk court "--data-root $WD/data --env photon-court" $SETTLE 0,3,18 0,3,-6 0,3,3 0,3,-21 60 "" 60 live frozen frozen2
    # (the analysis is CPU work, after the court's last session: no live arm's clock beside it)
    fails=0
    analyse court || fails=1
    cat "$WD/court-m1.out"
    rm -rf "$WD/data/stillness/dump-"*   # the dumps (~270 MB a walk) once their numbers are printed
    echo "gi.walk_court: $([ $fails -eq 0 ] && echo PASS || echo FAIL)"
    exit $fails
fi
glass_walk() {   # arm - the glass's walk (the rows run this script with sh: no arrays)
    walk glass "--data-root $WD/data --env photon-glass" "$GSETTLE" -1.2,1.8,3.4 0,0.5,-3 1.2,1.8,1.0 0,0.5,-3 60 "" 60 "$1"
}
restore_cache() {   # the frozen parts: the live part's primed cache bytes in this row's cache dir
    [ -n "${JAHSHAKA_SHADER_CACHE_DIR:-}" ] || { echo "  (no JAHSHAKA_SHADER_CACHE_DIR: a hand run - the cache as found)"; return 0; }
    [ -d "$WD/cache.snap" ] || { echo "FAIL: the live part's cache snapshot is missing ($WD/cache.snap)"; exit 1; }
    rm -rf "$JAHSHAKA_SHADER_CACHE_DIR"; cp -a "$WD/cache.snap" "$JAHSHAKA_SHADER_CACHE_DIR"
}
if [ "$MODE" = "--glass-live" ]; then   # gi.walk_glass.live: the prime and the live walk (the hitch law's clocks, alone)
    SNAP_SAVE="$WD/cache.snap" glass_walk live
    n=$(sed -n 's/^LAB REST after \([0-9]*\) frames.*/\1/p' "$WD/glass-live.log" | head -1)
    [ -n "$n" ] || { echo "FAIL: the live arm's rest count is missing"; exit 1; }
    echo $((n + 4)) > "$WD/glass.settle"
    echo "gi.walk_glass.live: the arms' settle $((n + 4)) frames (rest $n + 4)"
    echo "gi.walk_glass.live: PASS"
    exit 0
fi
if [ "$MODE" = "--glass-frozen" ]; then   # gi.walk_glass.frozen: the frozen walk, then M1 (live against frozen)
    [ -f "$WD/data/stillness/glass-live.tsv" ] && [ -f "$WD/glass.settle" ] || { echo "FAIL: no live walk in $WD (gi.walk_glass.live writes it)"; exit 1; }
    PRIME=0; GSETTLE=$(cat "$WD/glass.settle")
    restore_cache
    glass_walk frozen
    python3 -I "$HERE/walk_m1.py" "$WD/data/stillness" glass glass-live glass-frozen \
        --tsv "$WD/data/stillness/glass-live.tsv" --classes > "$WD/glass-m1.out" 2>&1
    rc=$?
    cat "$WD/glass-m1.out"
    echo "gi.walk_glass.frozen: $([ $rc -eq 0 ] && echo PASS || echo FAIL)"
    exit $rc
fi
if [ "$MODE" = "--glass-frozen2" ]; then   # gi.walk_glass.frozen2: the second frozen walk, then L1 (byte for byte)
    [ -f "$WD/data/stillness/glass-frozen.tsv" ] || { echo "FAIL: no frozen walk in $WD (gi.walk_glass.frozen writes it)"; exit 1; }
    PRIME=0; GSETTLE=$(cat "$WD/glass.settle")
    restore_cache
    glass_walk frozen2
    python3 -I "$HERE/walk_m1.py" "$WD/data/stillness" glass glass-live glass-frozen glass-frozen2 --l1-only \
        > "$WD/glass-l1.out" 2>&1
    rc=$?
    cat "$WD/glass-l1.out"
    rm -rf "$WD/data/stillness/dump-"* "$WD/cache.snap"   # the three walks' dumps (~270 MB a walk) and the snapshot, once printed
    echo "gi.walk_glass.frozen2: $([ $rc -eq 0 ] && echo PASS || echo FAIL)"
    exit $rc
fi
echo "FAIL: walk.sh: unknown mode '$MODE' (--court, --glass, --area, --bistro)"
exit 1
